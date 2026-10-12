"""Local CIA/NCSD/NCCH -> checked decrypted main CXI; no key acquisition."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import tempfile

from . import __version__
from .atomic import publish_directory
from .cia import read_cia, signed_body
from .containers import Reader, check_overlaps, check_range, inspect, u32, u64
from .errors import HostError
from .rom_keys import RomKeys, fail, scramble

CHUNK = 1024 * 1024
MAX_MAIN_BYTES = 8 * 1024 ** 3
METHOD_SLOTS = {0: "2c", 1: "25", 0xA: "18", 0xB: "1b"}


def cipher(key, iv, *, ctr=False):
    try:
        from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    except ImportError:
        fail("crypto_unavailable", "Install ctr2wup[decryption] for encrypted input preparation")
    return Cipher(algorithms.AES(key), modes.CTR(iv) if ctr else modes.CBC(iv))


def locate(reader):
    header = reader.read(0, 0x200, "container header")
    magic = header[0x100:0x104]
    if magic == b"NCCH":
        return {"format": "NCCH", "offset": 0, "size": reader.size, "outer_encrypted": False}
    if magic == b"NCSD":
        if u32(header, 0x104) * 512 < reader.size:
            fail("invalid_bounds", "NCSD media capacity is smaller than the input")
        extents = [(0, 512, "NCSD header")]; main = None
        for index in range(8):
            offset, length = struct.unpack_from("<II", header, 0x120 + index * 8)
            offset *= 512; length *= 512
            if bool(offset) != bool(length):
                fail("invalid_bounds", "NCSD partition offset/size disagree")
            if length:
                check_range(offset, length, reader.size, "NCSD partition")
                if header[0x110 + index]:
                    fail("unsupported_partition", "NAND/non-NCCH partitions are unsupported")
                extents.append((offset, length, "NCSD partition " + str(index)))
                if index == 0: main = (offset, length)
        check_overlaps(extents)
        if main is None: fail("missing_main_partition", "NCSD has no main partition 0")
        return {"format": "NCSD", "offset": main[0], "size": main[1], "outer_encrypted": False}
    if u32(header, 0) == 0x2020:
        metadata = read_cia(reader, verify_contents=False)
        if not metadata["title_id"].startswith("00040000"):
            fail("unsupported_cia_title", "Only a base-application CIA is supported")
        main = next((c for c in metadata['contents'] if c['included'] and c['index'] == 0), None)
        if main is None: fail("missing_cia_executable", "CIA contains no executable content index 0")
        return {"format": "CIA", "offset": main['offset'], "size": main['size'],
                "outer_encrypted": main['encrypted'], "content_index": main['index'],
                "tmd_sha256": main['tmd_sha256'], "title_id": metadata['title_id'],
                "ticket": metadata['sections']['ticket']}
    fail("unsupported_format", "Preparation requires a CIA, NCSD cartridge or NCCH executable")


def title_key(reader, container, keys):
    identity = container['title_id']
    if keys.has('cia_title_key_files', identity):
        return keys.read('cia_title_key_files', identity), "local_title_key"
    section = container['ticket']; ticket = reader.read(section['offset'], section['size'], "CIA ticket")
    body = signed_body(ticket, "ticket"); check_range(body, 0xB2, len(ticket), "ticket key fields")
    if any(ticket[body + 0x98:body + 0x9C]):
        fail("personalized_ticket", "Console-bound ticket needs an already recovered local title key; ticket depersonalization is not implemented")
    index = ticket[body + 0xB1]
    if index > 5: fail("unsupported_common_key", "CIA common-key index is unsupported")
    key = keys.read('cia_common_key_files', str(index))
    decrypt = cipher(key, bytes.fromhex(identity) + bytes(8)).decryptor()
    return decrypt.update(ticket[body + 0x7F:body + 0x8F]) + decrypt.finalize(), "local_common_key_and_ticket"


def copy_main(reader, output, container, keys):
    transform = None; role = "none"
    if container['outer_encrypted']:
        if container['size'] % 16: fail("invalid_encrypted_content", "CIA AES-CBC content must be block aligned")
        key, role = title_key(reader, container, keys())
        iv = container['content_index'].to_bytes(2, 'big') + bytes(14)
        transform = cipher(key, iv).decryptor()
    digest = hashlib.sha256()
    with output.open('xb') as target:
        for offset in range(0, container['size'], CHUNK):
            data = reader.read(container['offset'] + offset, min(CHUNK, container['size'] - offset))
            if transform: data = transform.update(data)
            target.write(data); digest.update(data)
        if transform:
            tail = transform.finalize(); target.write(tail); digest.update(tail)
    if container['format'] == 'CIA' and digest.hexdigest() != container['tmd_sha256']:
        fail("cia_content_hash_mismatch", "Recovered CIA content does not match its TMD; key, content or metadata may be incorrect")
    return role


def ncch_layout(header, size):
    if header[0x100:0x104] != b'NCCH': fail("invalid_magic", "Recovered content is not NCCH")
    if struct.unpack_from('<H', header, 0x112)[0] != 2:
        fail("unsupported_ncch_version", "Preparation supports NCCH v2 only")
    if header[0x18E] > 8: fail("invalid_unit_size", "NCCH unit exponent is excessive")
    unit = 512 << header[0x18E]
    if u32(header, 0x104) * unit != size:
        fail("invalid_bounds", "NCCH declared size differs from its main content extent")
    if not header[0x18D] & 2: fail("not_executable", "Main NCCH is not executable")
    if u32(header, 0x180) != 0x400:
        fail("unsupported_exheader", "Require the standard executable extended header")
    regions = [(0, 512, 'NCCH header'), (512, 0x800, 'extended header')]
    check_range(512, 0x800, size, 'extended header')
    sections = {}
    for name, at in (('plain', 0x190), ('logo', 0x198), ('exefs', 0x1A0), ('romfs', 0x1B0)):
        offset, length = u32(header, at) * unit, u32(header, at + 4) * unit
        if bool(offset) != bool(length): fail("invalid_bounds", "NCCH section offset/size disagree")
        if length:
            check_range(offset, length, size, name); regions.append((offset, length, name))
            sections[name] = (offset, length)
            if name in ('exefs', 'romfs') and not 0 < u32(header, at + 8) * unit <= length:
                fail("invalid_hash_region", "NCCH superblock hash extent is invalid")
    check_overlaps(regions)
    if 'exefs' not in sections: fail("missing_exefs", "Executable contains no ExeFS")
    if header[0x18F] & 2 and 'romfs' in sections: fail("invalid_flags", "No-RomFS flag conflicts with a RomFS section")
    return sections


def crypt_range(handle, start, length, key, counter, *, relative=0):
    """CTR supports unaligned file offsets; bounded chunks never expose raw pointers."""
    initial = int.from_bytes(counter, 'big') + relative // 16
    if initial + (relative % 16 + length + 15) // 16 >= 1 << 128:
        fail("invalid_counter", "AES counter range overflows")
    transform = cipher(key, initial.to_bytes(16, 'big'), ctr=True).decryptor()
    if relative % 16: transform.update(bytes(relative % 16))
    for offset in range(0, length, CHUNK):
        count = min(CHUNK, length - offset); handle.seek(start + offset); data = handle.read(count)
        if len(data) != count: fail("short_read", "Prepared content was truncated")
        handle.seek(start + offset); handle.write(transform.update(data))
    transform.finalize()


def decrypt_ncch(path, keys):
    with path.open('r+b') as handle:
        reader = Reader(handle); header = bytearray(reader.read(0, 512)); sections = ncch_layout(header, reader.size)
        if header[0x18F] & 4: return {"encrypted": False, "method": None, "seed_used": False}
        if header[0x18F] & 1: fail("unsupported_fixed_key", "Fixed-key NCCH preparation is not implemented")
        method = header[0x18B]
        if method not in METHOD_SLOTS: fail("unsupported_ncch_crypto", "NCCH crypto method is unsupported")
        provider = keys(); key_y = bytes(header[:16]); primary = scramble(provider.read('ncch_key_x_files', '2c'), key_y)
        seeded = bool(header[0x18F] & 0x20); identity = f"{u64(header, 0x118):016x}"
        if seeded:
            seed = provider.read('title_seed_files', identity)
            if hashlib.sha256(seed + header[0x118:0x120]).digest()[:4] != header[0x114:0x118]:
                fail("invalid_title_seed", "Title seed does not match the NCCH seed check")
            key_y = hashlib.sha256(key_y + seed).digest()[:16]
        secondary = scramble(provider.read('ncch_key_x_files', METHOD_SLOTS[method]), key_y)
        def counter(section): return bytes(header[0x108:0x110][::-1]) + bytes([section]) + bytes(7)
        crypt_range(handle, 512, 0x800, primary, counter(1))
        # The header has its own unseeded key. Validate before trusting file extents.
        offset, length = sections['exefs']; crypt_range(handle, offset, 512, primary, counter(2))
        table = reader.read(offset, 512)
        hashed = u32(header, 0x1A8) * (512 << header[0x18E])
        if hashed == 512 and hashlib.sha256(table).digest() != header[0x1C0:0x1E0]:
            fail("hash_mismatch", "Decrypted ExeFS header hash mismatch")
        files = []; names = set(); operations = []
        for index in range(10):
            name, at, size = struct.unpack_from('<8sII', table, index * 16)
            if not any(name):
                if at or size: fail("invalid_exefs_entry", "Unnamed ExeFS entry has a range")
                continue
            if not size or at % 512: fail("invalid_exefs_entry", "ExeFS file range is empty or not media-unit aligned")
            if b'\0' in name and any(name[name.index(0):]): fail("invalid_exefs_name", "ExeFS name padding is invalid")
            name = name.split(b'\0', 1)[0]
            if name in names: fail("duplicate_exefs_name", "ExeFS contains duplicate names")
            names.add(name); padded = (size + 511) // 512 * 512
            check_range(512 + at, padded, length, 'ExeFS file/padding')
            files.append((512 + at, padded, 'ExeFS file'))
            operations.append((name, at, size, padded))
        check_overlaps(files)
        for name, at, size, padded in operations:
            crypt_range(handle, offset + 512 + at, size, primary if name in (b'icon', b'banner') else secondary,
                        counter(2), relative=512 + at)
            if padded != size: crypt_range(handle, offset + 512 + at + size, padded - size, primary,
                                          counter(2), relative=512 + at + size)
        if 'romfs' in sections:
            offset, length = sections['romfs']; crypt_range(handle, offset, length, secondary, counter(3))
        # This is a derived view. Its original header signature is not authenticated.
        header[0x18B] = 0; header[0x18F] = (header[0x18F] & ~(1 | 0x20)) | 4
        handle.seek(0); handle.write(header)
        return {"encrypted": True, "method": method, "seed_used": seeded}


def prepare_rom(path, output, *, keys_config=None):
    path, output = Path(path).absolute(), Path(output).absolute()
    if path.is_symlink() or not path.is_file(): fail("unsafe_input", "Preparation requires a regular input file")
    if output.exists() or output.is_symlink(): fail("output_exists", "Output already exists and was preserved")
    output.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix='.ctr2wup-prepare-', dir=output.parent)); provider = None
    def keys():
        nonlocal provider
        if provider is None: provider = RomKeys(keys_config)
        return provider
    try:
        with path.open('rb') as handle:
            reader = Reader(handle); container = locate(reader)
            if container['size'] > MAX_MAIN_BYTES: fail("preparation_size_limit", "Main content exceeds the 8 GiB preparation limit")
            if shutil.disk_usage(stage).free < container['size'] + CHUNK:
                fail("insufficient_space", "Not enough space for the private prepared executable")
            source_hash = reader.digest(0, reader.size)
            game = stage / 'game.cxi'; role = copy_main(reader, game, container, keys)
            with game.open('rb') as plain:
                if container['format'] == 'CIA' and f"{u64(Reader(plain).read(0, 512), 0x118):016x}" != container['title_id']:
                    fail("cia_ncch_identity_mismatch", "Recovered NCCH and CIA title identities differ")
            ncch = decrypt_ncch(game, keys); checked = inspect(game)
            if reader.digest(0, reader.size) != source_hash:
                fail("input_changed", "Source changed during preparation")
            after = os.fstat(handle.fileno())
            if (reader.initial_stat.st_size, reader.initial_stat.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
                fail("input_changed", "Source metadata changed during preparation")
        report = {"ok": True, "schema_version": 1, "version": __version__, "stage": "prepare-rom",
                  "input_format": container['format'], "source_sha256": source_hash,
                  "outer_cia_decrypted": container['outer_encrypted'], "cia_key_route": role,
                  "ncch": ncch, "output_file": 'game.cxi', "output_sha256": checked['input_sha256'],
                  "identity": checked['identity'], "executable_revision": checked['executable_revision'],
                  "romfs_preserved": 'romfs' in checked['partitions'][0]['sections'],
                  "other_contents_exported": False, "keys_exported": False,
                  "signature_authenticity_verified": False, "full_ivfc_verified": False,
                  "commercial_profile_enabled": False, "console_verified": False,
                  "inspection": checked}
        (stage / 'preparation-report.json').write_text(json.dumps(report, indent=2) + '\n')
        publish_directory(stage, output)
        return {**report, "output": str(output)}
    except HostError as exc:
        if exc.stage == 'inspect': exc.stage = 'prepare-rom'
        raise
    finally:
        if stage.exists(): shutil.rmtree(stage)
