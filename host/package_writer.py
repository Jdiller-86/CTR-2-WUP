"""Original bounded encrypted Wii U homebrew writer; console acceptance pending.

Four nonhashed contents: FST, code, content, meta. Signatures are explicitly
absent. User-local certificates/keys are required; no donor template is used.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import struct
import tempfile
import xml.etree.ElementTree as ET

from .atomic import publish_directory
from .build import _validate_rpx
from .errors import HostError
from .package_crypto import PackagingInputs, cipher, fail
from .package_validator import CONTENT_START, SIGNATURE_TYPE, ValidatorError, _title_id

BLOCK = 0x8000
MAX_CONTENT = 1024 * 1024 * 1024
MAX_NODES = 100000
MAX_FST = 16 * 1024 * 1024
CHUNK = 1024 * 1024
FOLDERS = ("code", "content", "meta")


def _align(value, unit):
    return (value + unit - 1) // unit * unit


def xml_document(path, root):
    from .package_crypto import local_bytes
    data = local_bytes(path, 1024 * 1024)
    if b"<!DOCTYPE" in data.upper() or b"<!ENTITY" in data.upper():
        fail("invalid_title_metadata", "Title XML must not declare entities or document types")
    try:
        document = ET.fromstring(data)
    except ET.ParseError:
        fail("invalid_title_metadata", "Title XML could not be parsed")
    if document.tag != root or len({item.tag for item in document}) != len(document):
        fail("invalid_title_metadata", "Title XML root or field uniqueness is invalid")
    return {item.tag: item.text or "" for item in document}


def _metadata(source):
    app = xml_document(source / "code/app.xml", "app")
    meta = xml_document(source / "meta/meta.xml", "menu")
    cos = xml_document(source / "code/cos.xml", "app")
    try:
        title = _title_id(app.get("title_id"))
        version = int(app["title_version"], 16)
        group = int(app["group_id"], 16)
        os_version = int(app["os_version"], 16)
        app_type = int(app["app_type"], 16)
        agrees = (meta["title_id"].lower() == title and int(meta["title_version"], 10) == version)
    except (KeyError, ValueError, TypeError, ValidatorError):
        fail("invalid_title_metadata", "app.xml and meta.xml require matching application IDs and valid version/group/OS/type fields")
    if not agrees or not 0 <= version <= 65535 or not 0 <= group <= 65535 or not 0 <= os_version < 2**64 or app_type != 0x80000000:
        fail("invalid_title_metadata", "Title metadata exceeds the supported homebrew application profile")
    name = cos.get("argstr", "")
    if not name.endswith(".rpx") or "/" in name or "\\" in name or name in (".", ".."):
        fail("invalid_title_metadata", "COS argstr must name one staged RPX without arguments")
    _validate_rpx(source / "code" / name)
    return title, version, group, os_version, app_type


def _inventory(source):
    nodes = [{"path": "", "directory": True, "parent": 0, "end": 0, "content": 0}]
    groups = [[], [], [], []]
    sizes = [0, 0, 0, 0]

    def visit(path, parent, content, depth):
        if depth > 64 or len(nodes) >= MAX_NODES:
            fail("staging_limit", "Staging exceeds the supported depth or entry count")
        info = path.lstat()
        if path.is_symlink() or not (stat.S_ISDIR(info.st_mode) or stat.S_ISREG(info.st_mode)):
            fail("unsafe_staging", "Staging entries must be regular files or directories")
        name = path.name
        if (name in (".", "..") or name.endswith((".", " ")) or "\\" in name
                or any(ord(c) < 32 or c in ':<>"|?*' for c in name)
                or len(name.encode("utf-8")) > 255):
            fail("unsafe_staging", "Staging names must be bounded portable path components")
        node = {"path": path.relative_to(source).as_posix(), "directory": path.is_dir(),
                "parent": parent, "end": 0, "content": content, "size": info.st_size,
                "snapshot": (info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns)}
        index = len(nodes)
        nodes.append(node)
        if node["directory"]:
            children = sorted(path.iterdir(), key=lambda p: p.name.encode("utf-8"))
            if len({p.name.casefold() for p in children}) != len(children):
                fail("unsafe_staging", "Staging contains case-insensitive name collisions")
            for child in children:
                visit(child, index, content, depth + 1)
            node["end"] = len(nodes)
        else:
            node["offset"] = sizes[content]
            sizes[content] += _align(info.st_size, 32)
            if sizes[content] > MAX_CONTENT:
                fail("staging_limit", "Each staged content group is limited to 1 GiB")
            digest = hashlib.sha256()
            with path.open("rb") as handle:
                for data in iter(lambda: handle.read(CHUNK), b""):
                    digest.update(data)
            node["sha256"] = digest.hexdigest()
            groups[content].append(node)

    for number, name in enumerate(FOLDERS, 1):
        folder = source / name
        if not folder.is_dir() or folder.is_symlink():
            fail("unsafe_staging", "Staging requires regular code, content and meta directories")
        visit(folder, 0, number, 0)
    nodes[0]["end"] = len(nodes)
    return nodes, groups, [0] + [max(BLOCK, _align(n, BLOCK)) for n in sizes[1:]]


def _fst(nodes, sizes, title, group, *, plaintext=False):
    strings = bytearray(b"\0")
    entries = bytearray()
    for number, node in enumerate(nodes):
        offset = len(strings) if number else 0
        if number:
            strings += node["path"].rsplit("/", 1)[-1].encode("utf-8") + b"\0"
        if len(strings) > 0xFFFFFF:
            fail("staging_limit", "FST name table exceeds 24-bit addressing")
        first = node["parent"] if node["directory"] else node["offset"] // 32
        second = node["end"] if node["directory"] else node["size"]
        entries += struct.pack(">IIIHH", offset | (int(node["directory"]) << 24), first, second,
                               0x666 if plaintext and number else 0, node["content"])
    length = _align(0x20 + 4 * 0x20 + len(entries) + len(strings), BLOCK)
    if length > MAX_FST:
        fail("staging_limit", "FST exceeds the supported 16 MiB bound")
    sizes[0] = length
    header = bytearray(struct.pack(">4sII20x", b"FST\0", 32, 4))
    if plaintext:
        header[12] = 1  # No hash tree: original nonhashed homebrew contents.
    # Logical cluster placement follows the documented research profile. Actual
    # filesystem/installer acceptance remains a physical-console check.
    header += struct.pack(">IIQI B11x", 0, 0 if plaintext else length // BLOCK, 0, 0, 0)
    sector = max(2, length // BLOCK) if plaintext else 2 + (length // BLOCK if length > BLOCK else 0)
    for number in range(1, 4):
        owner = int(title, 16) if number == 2 else 0
        gid = group if number == 2 else 0x400 if number == 3 else 0
        header += struct.pack(">IIQI B11x", sector, sizes[number] // BLOCK, owner, gid, 1)
        sector += sizes[number] // BLOCK
    result = header + entries + strings
    return bytes(result) + bytes(length - len(result))


def _encrypt_group(source, files, size, output, key, index, *, fst=None, plaintext=False):
    engine = None if plaintext else cipher(key, index.to_bytes(2, "big") + bytes(14)).encryptor()
    digest = hashlib.sha1()
    position = 0
    with output.open("xb") as target:
        def write(data):
            nonlocal position
            digest.update(data)
            target.write(engine.update(data) if engine is not None else data)
            position += len(data)

        if fst is not None:
            write(fst)
        else:
            for node in files:
                if position != node["offset"]:
                    fail("packing_internal", "Staged file offset differs from its content plan")
                digest_file = hashlib.sha256()
                read = 0
                path = source / node["path"]
                if path.is_symlink():
                    fail("input_changed", "Staging changed during packaging")
                with path.open("rb") as handle:
                    for data in iter(lambda: handle.read(CHUNK), b""):
                        read += len(data)
                        if read > node["size"]:
                            fail("input_changed", "Staging changed during packaging")
                        digest_file.update(data)
                        write(data)
                info = path.stat()
                snapshot = (info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns)
                if read != node["size"] or snapshot != node["snapshot"] or digest_file.hexdigest() != node["sha256"]:
                    fail("input_changed", "Staging changed during packaging")
                write(bytes((-position) % 32))
        while position < size:
            write(bytes(min(CHUNK, size - position)))
        if engine is not None:
            target.write(engine.finalize())
    return digest.digest() + bytes(12)


def _tmd(title, version, group, os_version, app_type, sizes, hashes, issuer):
    data = bytearray(CONTENT_START + 4 * 0x30)
    struct.pack_into(">I", data, 0, SIGNATURE_TYPE)
    data[0x140:0x140 + len(issuer)] = issuer.encode("ascii")
    data[0x180] = 1
    struct.pack_into(">QQIHI", data, 0x184, os_version, int(title, 16), 0x100, group, app_type)
    struct.pack_into(">I", data, 0x1A2, 0x019A0000)
    struct.pack_into(">HHH", data, 0x1DC, version, 4, 0)
    for number, (size, digest) in enumerate(zip(sizes, hashes)):
        offset = CONTENT_START + number * 0x30
        struct.pack_into(">IHHQ", data, offset, number, number, 0x2001, size)
        data[offset + 16:offset + 48] = digest
    struct.pack_into(">HH", data, 0x204, 0, 4)
    data[0x208:0x228] = hashlib.sha256(data[CONTENT_START:]).digest()
    data[0x1E4:0x204] = hashlib.sha256(data[0x204:CONTENT_START]).digest()
    return data


def _ticket(title, version, key, common, issuer):
    data = bytearray(0x350)
    struct.pack_into(">I", data, 0, SIGNATURE_TYPE)
    data[0x140:0x140 + len(issuer)] = issuer.encode("ascii")
    data[0x1BC] = 1
    title_bytes = bytes.fromhex(title)
    engine = cipher(common, title_bytes + bytes(8)).encryptor()
    data[0x1BF:0x1CF] = engine.update(key) + engine.finalize()
    data[0x1D0:0x1D8] = os.urandom(8)
    data[0x1DC:0x1E4] = title_bytes
    struct.pack_into(">H", data, 0x1E6, version)
    data[0x1F1] = 0  # scoped retail common-key index, not a bundled key
    struct.pack_into(">H", data, 0x1F2, 5)
    # Version-1 rights header, one content-access section with 1024-bit mask.
    struct.pack_into(">HHIIHHI", data, 0x2A4, 1, 0x14, 0xAC, 0x14, 1, 0x14, 0)
    struct.pack_into(">IIIIHH", data, 0x2B8, 0x28, 1, 0x84, 0x84, 3, 0)
    for index in range(4):
        data[0x2D0 + index // 8] |= 1 << (index % 8)
    return data


def pack_title(source, output, config):
    """Publish only after independent decryption, FST and source-file checks."""
    source = Path(source).absolute()
    output = Path(output).absolute()
    if source.is_symlink() or not source.is_dir():
        fail("unsafe_staging", "Staging input must be a regular directory")
    if output.exists() or output.is_symlink():
        fail("output_exists", "Package output already exists; it was preserved")
    if output.resolve().is_relative_to(source.resolve()):
        fail("unsafe_output", "Package output must be outside the staging tree")
    inputs = PackagingInputs(config)
    for path in (inputs.key_path, inputs.cert_path, Path(config).absolute()):
        if any(path.resolve().is_relative_to((source / name).resolve()) for name in FOLDERS):
            fail("unsafe_staging", "Local packaging credentials must be outside staged folders")
    identity = _metadata(source)
    nodes, groups, sizes = _inventory(source)
    fst = _fst(nodes, sizes, identity[0], identity[2])
    output.parent.mkdir(parents=True, exist_ok=True)
    if shutil.disk_usage(output.parent).free < sum(sizes) + 64 * 1024 * 1024:
        fail("insufficient_disk", "Packaging needs space for encrypted contents and 64 MiB working margin")
    stage = Path(tempfile.mkdtemp(prefix=".ctr2wup-package-", dir=output.parent))
    try:
        title_key = os.urandom(16)
        hashes = [_encrypt_group(source, groups[n], sizes[n], stage / f"{n:08x}.app",
                                 title_key, n, fst=fst if n == 0 else None) for n in range(4)]
        (stage / "title.tmd").write_bytes(_tmd(*identity, sizes, hashes, inputs.issuers[0]))
        (stage / "title.tik").write_bytes(_ticket(identity[0], identity[1], title_key, inputs.key, inputs.issuers[1]))
        (stage / "title.cert").write_bytes(inputs.certificates)
        from .package_integrity import verify_package
        report = verify_package(stage, inputs.key_path, expected_staging=source)
        report.update(schema_version=1, stage="pack-title", signature_policy="homebrew_unsigned",
                      installable_wup="awaiting_installer_and_console_validation", console_verified=False,
                      credentials="read locally; values and input paths omitted", title_version=identity[1])
        (stage / "pack-report.json").write_text(json.dumps(report, indent=2, ensure_ascii=True) + "\n")
        publish_directory(stage, output)
        return {**report, "output": str(output)}
    finally:
        if stage.exists():
            shutil.rmtree(stage)
