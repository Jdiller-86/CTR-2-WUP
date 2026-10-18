"""Original plaintext WUP candidate for signature-patched homebrew environments.

Format facts: makefst e363897e29109e6f1125d60376822c3768d3c2d4.
No common key, production certificate, donor title or private signing key is
loaded or distributed. Generated test signatures do not authenticate Nintendo
content. Installation and Menu execution remain physical acceptance checks.
"""
from __future__ import annotations

import json
from pathlib import Path
import shutil
import struct
import tempfile

from .atomic import publish_directory
from .package_crypto import fail
from .package_writer import (_metadata, _inventory, _fst, _encrypt_group,
                             _tmd, _ticket, _align, BLOCK)


def _file_contents(nodes, title, group):
    """One independently readable plaintext stream per file, as in makefst.

    Keep tree order and all staged bytes. An empty top-level folder retains an
    empty stream with its own ownership; it must not inherit the next folder's.
    """
    groups, sizes, owners = [[]], [0], [(0, 0)]
    for top in ('code', 'content', 'meta'):
        entries = [n for n in nodes[1:] if n['path'] == top or n['path'].startswith(top + '/')]
        files = [n for n in entries if not n['directory']]
        first = len(groups)
        owner = (int(title, 16), group) if top == 'content' else (0, 0x400 if top == 'meta' else 0)
        for node in files or [None]:
            if len(groups) >= 1024:
                fail('staging_limit', 'Plaintext homebrew supports at most 1024 contents including the FST')
            if node is not None:
                node['content'], node['offset'] = len(groups), 0
            groups.append([node] if node is not None else [])
            sizes.append(max(BLOCK, _align(node['size'], BLOCK)) if node is not None else BLOCK)
            owners.append(owner)
        following = first
        for node in reversed(entries):
            if node['directory']:
                node['content'] = following
            else:
                following = node['content']
    return groups, sizes, owners


def pack_homebrew(source, output, *, require_menu=False, require_startup=False, content_id_base=0):
    source, output = Path(source).absolute(), Path(output).absolute()
    if source.is_symlink() or not source.is_dir():
        fail('unsafe_staging', 'Homebrew staging must be a regular directory')
    if output.exists() or output.is_symlink():
        fail('output_exists', 'Existing package output was preserved')
    if output.resolve().is_relative_to(source.resolve()):
        fail('unsafe_output', 'Package output must be outside the staging tree')
    identity = _metadata(source)
    if type(content_id_base) is not int or content_id_base not in (0, identity[1] << 16):
        fail('invalid_content_identity', 'Content IDs must use the legacy base or the staged title revision')
    nodes, _, _ = _inventory(source)
    groups, sizes, owners = _file_contents(nodes, identity[0], identity[2])
    fst = _fst(nodes, sizes, identity[0], identity[2], plaintext=True, owners=owners)
    output.parent.mkdir(parents=True, exist_ok=True)
    if shutil.disk_usage(output.parent).free < sum(sizes) + 64*1024*1024:
        fail('insufficient_disk', 'Packaging needs content space and a 64 MiB working margin')
    stage = Path(tempfile.mkdtemp(prefix='.ctr2wup-homebrew-', dir=output.parent))
    try:
        from .package_test_signing import generate_chain, sign_document
        certificates, cp_key, xs_key = generate_chain()
        hashes = [_encrypt_group(source, groups[n], sizes[n], stage/f'{content_id_base+n:08x}.app',
                                 None, n, fst=fst if n == 0 else None, plaintext=True)
                  for n in range(len(groups))]
        tmd = _tmd(*identity, sizes, hashes, 'Root-CA00000003-CP0000000b')
        for n in range(len(groups)):
            from .package_validator import CONTENT_START
            struct.pack_into('>I', tmd, CONTENT_START+n*48, content_id_base+n)
            struct.pack_into('>H', tmd, CONTENT_START+n*48+6, 0x2000)
        # IDs name revisions; indices still select FST streams/ticket rights.
        # Recompute authenticated table hashes after changing IDs and flags.
        import hashlib
        tmd[0x208:0x228] = hashlib.sha256(tmd[CONTENT_START:]).digest()
        tmd[0x1E4:0x204] = hashlib.sha256(tmd[0x204:CONTENT_START]).digest()
        ticket = _ticket(identity[0], identity[1], bytes(16), bytes(16),
                         'Root-CA00000003-XS0000000c', all_content_rights=True)
        ticket[0x1BF:0x1CF] = bytes(16)  # Unused for unencrypted contents.
        (stage/'title.tmd').write_bytes(sign_document(tmd, cp_key, kind='tmd'))
        (stage/'title.tik').write_bytes(sign_document(ticket, xs_key, kind='ticket'))
        (stage/'title.cert').write_bytes(certificates)
        from .package_integrity import verify_package
        report = verify_package(stage, None, expected_staging=source, require_menu=require_menu,require_startup=require_startup)
        report.update(schema_version=1, stage='pack-homebrew', profile='wiiu_plaintext_homebrew',
                      content_layout='one_file_per_content',
                      signature_policy='original_self_signed', requires_signature_patches=True,
                      common_key_required=False, production_certificates_required=False,
                      install_test_ready=True, installable_wup='physical_installer_validation_pending',
                      title_version=identity[1], console_verified=False)
        (stage/'pack-report.json').write_text(json.dumps(report, indent=2)+'\n')
        publish_directory(stage, output)
        return {**report, 'output': str(output)}
    finally:
        if stage.exists():
            shutil.rmtree(stage)
