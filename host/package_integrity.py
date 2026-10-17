"""Independent reader for original per-file plaintext and grouped encrypted WUPs.

The reader does not call the writer's FST, inventory or serialization functions.
It verifies integrity, not a trusted signature or successful console install.
"""
from __future__ import annotations

import hashlib
from pathlib import Path
import stat
import struct
import xml.etree.ElementTree as ET

from .errors import HostError
from .package_crypto import certificate_subjects, cipher, common_key, fail
from .package_validator import CONTENT_START, ValidatorError, validate_package

UNIT = 0x8000
READ_SIZE = 1024 * 1024


def _plaintext(path, key, index, expected, size):
    decryptor = cipher(key, index.to_bytes(2, "big") + bytes(14)).decryptor() if key is not None else None
    digest = hashlib.sha1()
    read = 0
    with path.open("rb") as handle:
        for encrypted in iter(lambda: handle.read(READ_SIZE), b""):
            read += len(encrypted)
            if read > size:
                fail("content_changed", "Encrypted content grew after structural validation")
            data = decryptor.update(encrypted) if decryptor is not None else encrypted
            digest.update(data)
            yield data
    if read != size:
        fail("content_changed", "Encrypted content shrank after structural validation")
    tail = decryptor.finalize() if decryptor is not None else b""
    if tail:
        digest.update(tail)
        yield tail
    if digest.digest() != expected:
        fail("plaintext_digest_mismatch", "Decrypted content SHA-1 does not match its TMD record; check local key and package integrity")


def _fst(data, records, title, group, *, plaintext=False):
    count = len(records)
    per_file = plaintext and count != 4
    if len(data) < 0x20 + count * 0x20 + 16 or data[:4] != b"FST\0":
        fail("invalid_fst", "Boot content is not a supported FST")
    factor, declared = struct.unpack_from(">II", data, 4)
    if (factor != 32 or declared != count or data[12] != int(plaintext) or any(data[13:32])):
        fail("invalid_fst", "FST header is outside the supported unmodified application profile")
    extents, ownership = [], []
    for number in range(count):
        start, length, owner, gid, mode = struct.unpack_from(">IIQIB", data, 32 + 32 * number)
        tail = data[32 + 32 * number + 21:32 + 32 * (number + 1)]
        expected_owner = int(title, 16) if number == 2 else 0
        expected_group = group if number == 2 else 0x400 if number == 3 else 0
        expected_length = 0 if plaintext and number == 0 else records[number]["size"] // UNIT
        if (length != expected_length or (not per_file and (owner != expected_owner or gid != expected_group))
                or mode != (0 if number == 0 else 1) or any(tail)):
            fail("invalid_fst", "FST content descriptors do not match the scoped TMD/ownership profile")
        if number == 0 and start != 0 or number and start < extents[-1][1]:
            fail("invalid_fst", "FST logical content extents overlap")
        extents.append((start, start + length))
        ownership.append((owner, gid))
    if per_file and ownership[0] != (0, 0):
        fail("invalid_fst", "The FST stream must not have a file owner")
    base = 32 + count * 32
    kind, parent, nodes, flags, content = struct.unpack_from(">IIIHH", data, base)
    if kind != 0x01000000 or parent or flags or content or not 4 <= nodes <= 100000:
        fail("invalid_fst", "FST root is invalid or exceeds the supported node count")
    strings = base + nodes * 16
    if strings >= len(data) or data[strings] != 0:
        fail("invalid_fst", "FST nodes exceed the boot content or omit the root name")
    stack = [(0, "", nodes)]
    paths = set()
    directories = set()
    directory_streams = set()
    files = [[] for _ in records]
    root_names = set()
    names = [(0, 1)]
    for number in range(1, nodes):
        while stack and number == stack[-1][2]:
            stack.pop()
        if not stack:
            fail("invalid_fst", "FST directory boundaries are inconsistent")
        field, first, second, flags, content = struct.unpack_from(">IIIHH", data, base + number * 16)
        kind, name_offset = field >> 24, field & 0xFFFFFF
        name_start = strings + name_offset
        name_end = data.find(b"\0", name_start, min(name_start + 256, len(data)))
        if kind not in (0, 1) or name_start >= len(data) or name_end < name_start or flags != (0x666 if plaintext else 0):
            fail("invalid_fst", "FST node type, flags or name is unsupported")
        try:
            name = data[name_start:name_end].decode("utf-8")
        except UnicodeDecodeError:
            fail("invalid_fst", "FST names must be valid UTF-8")
        if (not name or name in (".", "..") or name.endswith((".", " "))
                or any(ord(c) < 32 or c in '/\\:<>"|?*' for c in name)):
            fail("invalid_fst", "FST contains an unsafe path component")
        names.append((name_offset, name_end - strings + 1))
        directory_number, parent_path, limit = stack[-1]
        path = parent_path + "/" + name if parent_path else name
        if path.casefold() in paths:
            fail("invalid_fst", "FST paths collide after case normalization")
        paths.add(path.casefold())
        top = path.split("/", 1)[0]
        expected_content = {"code": 1, "content": 2, "meta": 3}.get(top)
        if (not per_file and content != expected_content) or per_file and content >= count:
            fail("invalid_fst", "FST node is assigned to the wrong top-level content")
        if kind:
            if first != directory_number or not number < second <= limit or len(stack) > 65:
                fail("invalid_fst", "FST directory parent, depth or subtree boundary is invalid")
            if not parent_path:
                root_names.add(name)
            directories.add(path)
            if per_file:
                expected = (int(title, 16), group) if top == 'content' else (0, 0x400 if top == 'meta' else 0)
                if not content or ownership[content] != expected:
                    fail("invalid_fst", "Directory stream ownership must match its top-level folder")
                directory_streams.add(content)
            stack.append((number, path, second))
        else:
            if not parent_path:
                fail("invalid_fst", "Files must be inside code, content or meta")
            offset = first * factor
            if per_file and (not content or offset or files[content]):
                fail("invalid_fst", "Each plaintext file requires its own stream at offset zero")
            if offset + second > records[content]["size"]:
                fail("invalid_fst", "FST file exceeds its decrypted content")
            files[content].append({"path": path, "offset": offset, "bytes": second})
    if root_names != {"code", "content", "meta"}:
        fail("invalid_fst", "FST requires exactly the code, content and meta roots")
    if per_file:
        for number in range(1, count):
            if not files[number]:
                if number not in directory_streams or records[number]['size'] != UNIT:
                    fail("invalid_fst", "An empty plaintext stream requires an owned directory reference")
                continue
            item = files[number][0]
            top = item['path'].split('/', 1)[0]
            expected = (int(title, 16), group) if top == 'content' else (0, 0x400 if top == 'meta' else 0)
            padded = max(UNIT, (item['bytes'] + UNIT - 1) // UNIT * UNIT)
            if ownership[number] != expected or records[number]['size'] != padded:
                fail("invalid_fst", "Plaintext stream ownership or padded file size is invalid")
    used = 0
    for start, end in sorted(names):
        if start > used and any(data[strings + used:strings + start]):
            fail("invalid_fst", "FST unused name-table bytes must be zero")
        used = max(used, end)
    if any(data[strings + used:]):
        fail("invalid_fst", "FST padding must be zero")
    for group_files in files:
        group_files.sort(key=lambda item: (item["offset"], item["bytes"]))
        end = 0
        for item in group_files:
            if item["offset"] < end:
                fail("invalid_fst", "FST file ranges overlap")
            end = item["offset"] + item["bytes"]
    return files, directories


def _content_files(chunks, files, xml):
    results = []
    index = 0
    position = 0
    digest = hashlib.sha256()

    def finish(item):
        nonlocal index, digest
        results.append({**item, "sha256": digest.hexdigest()})
        index += 1
        digest = hashlib.sha256()

    for chunk in chunks:
        cursor = 0
        while cursor < len(chunk):
            while index < len(files) and not files[index]["bytes"] and files[index]["offset"] <= position:
                finish(files[index])
            item = files[index] if index < len(files) else None
            if item is None or position < item["offset"]:
                size = len(chunk) - cursor if item is None else min(len(chunk) - cursor, item["offset"] - position)
                if any(chunk[cursor:cursor + size]):
                    fail("invalid_content_padding", "Decrypted data outside FST file ranges must be zero")
            else:
                size = min(len(chunk) - cursor, item["offset"] + item["bytes"] - position)
                part = chunk[cursor:cursor + size]
                digest.update(part)
                if item["path"] in ("code/app.xml", "code/cos.xml", "meta/meta.xml"):
                    if item["bytes"] > READ_SIZE:
                        fail("invalid_title_metadata", "Title XML exceeds its supported size")
                    xml.setdefault(item["path"], bytearray()).extend(part)
                if position + size == item["offset"] + item["bytes"]:
                    finish(item)
            cursor += size
            position += size
    while index < len(files) and not files[index]["bytes"] and files[index]["offset"] <= position:
        finish(files[index])
    if index != len(files):
        fail("truncated_content", "Decrypted content ended before all FST files")
    return results


def _title_metadata(xml, tmd, files):
    documents = []
    for name, tag in (("code/app.xml", "app"), ("meta/meta.xml", "menu"), ("code/cos.xml", "app")):
        data = bytes(xml.get(name, b""))
        if b"<!DOCTYPE" in data.upper() or b"<!ENTITY" in data.upper():
            fail("invalid_title_metadata", "Title XML declarations are unsupported")
        try:
            doc = ET.fromstring(data)
        except ET.ParseError:
            fail("invalid_title_metadata", "Decrypted title XML is missing or invalid")
        if doc.tag != tag or len({item.tag for item in doc}) != len(doc):
            fail("invalid_title_metadata", "Decrypted title XML root or fields are invalid")
        documents.append({item.tag: item.text or "" for item in doc})
    app, meta, cos = documents
    try:
        matches = (int(app["title_id"], 16) == int(meta["title_id"], 16) == int.from_bytes(tmd[0x18C:0x194], "big")
                   and int(app["title_version"], 16) == int(meta["title_version"], 10) == int.from_bytes(tmd[0x1DC:0x1DE], "big")
                   and int(app["group_id"], 16) == int.from_bytes(tmd[0x198:0x19A], "big")
                   and int(app["os_version"], 16) == int.from_bytes(tmd[0x184:0x18C], "big")
                   and int(app["app_type"], 16) == int.from_bytes(tmd[0x19A:0x19E], "big") == 0x80000000)
    except (KeyError, ValueError):
        matches = False
    rpx = cos.get("argstr", "")
    if (not matches or not rpx.endswith(".rpx") or "/" in rpx or "\\" in rpx
            or "code/" + rpx not in {item["path"] for item in files}):
        fail("title_metadata_mismatch", "Decrypted XML identity/launch entry differs from the package metadata")


def _compare_staging(source, files, directories):
    source = Path(source)
    if source.is_symlink() or not source.is_dir():
        fail("unsafe_staging", "Expected staging must be a regular directory")
    seen_files, seen_dirs = {}, set()
    for root in ("code", "content", "meta"):
        folder = source / root
        if folder.is_symlink() or not folder.is_dir():
            fail("unsafe_staging", "Expected staging roots are missing or unsafe")
        seen_dirs.add(root)
        for path in folder.rglob("*"):
            if path.is_symlink():
                fail("unsafe_staging", "Expected staging contains a symlink")
            relative = path.relative_to(source).as_posix()
            mode = path.stat().st_mode
            if stat.S_ISDIR(mode):
                seen_dirs.add(relative)
            elif stat.S_ISREG(mode):
                digest = hashlib.sha256()
                length = 0
                with path.open("rb") as handle:
                    for data in iter(lambda: handle.read(READ_SIZE), b""):
                        digest.update(data)
                        length += len(data)
                seen_files[relative] = (length, digest.hexdigest())
            else:
                fail("unsafe_staging", "Expected staging contains a nonregular entry")
    packed = {item["path"]: (item["bytes"], item["sha256"]) for item in files}
    if seen_dirs != directories or seen_files != packed:
        fail("staging_mismatch", "Decrypted package files or directories differ from expected staging")


def verify_package(path, key_file=None, *, expected_staging=None):
    path = Path(path)
    try:
        report = validate_package(path)
    except ValidatorError as exc:
        raise HostError(exc.code, str(exc), stage="packaging", details=exc.details) from exc
    tmd = (path / "title.tmd").read_bytes()
    ticket = (path / "title.tik").read_bytes()
    cert = (path / "title.cert").read_bytes()
    subjects = certificate_subjects(cert)
    for document, prefix in ((tmd, "CP"), (ticket, "XS")):
        try:
            issuer = document[0x140:0x180].split(b"\0", 1)[0].decode("ascii")
        except UnicodeDecodeError:
            fail("certificate_issuer_mismatch", "Package signer names are invalid")
        if issuer not in subjects or not subjects[issuer]["name"].startswith(prefix) or subjects[issuer]["key_type"] != 1:
            fail("certificate_issuer_mismatch", "Package issuers do not name the supplied CP/XS certificates")
    plaintext = all(not row["encrypted"] for row in report["contents"])
    if plaintext:
        from .package_test_signing import verify_test_chain
        signing = verify_test_chain(cert, tmd, ticket)
        title_key = None
    else:
        if ticket[0x1F1] or any(ticket[0x1D8:0x1DC]) or any(tmd[4:0x104]) or any(ticket[4:0x104]):
            fail("unsupported_ticket_profile", "Encrypted verification supports common-key-0, unpersonalized, unsigned homebrew packages")
        if key_file is None:
            fail("common_key_required", "Encrypted contents require the local common-key file; plaintext homebrew does not")
        title_bytes = ticket[0x1DC:0x1E4]
        decryptor = cipher(common_key(key_file), title_bytes + bytes(8)).decryptor()
        title_key = decryptor.update(ticket[0x1BF:0x1CF]) + decryptor.finalize()
        signing = None
    records = []
    for number in range(report["content_count"]):
        base = CONTENT_START + number * 48
        identifier, index, flags, size = struct.unpack_from(">IHHQ", tmd, base)
        digest = tmd[base + 16:base + 36]
        if (flags != (0x2000 if plaintext else 0x2001) or index != number or identifier != number or size % UNIT
                or any(tmd[base + 36:base + 48]) or size > 1024 * 1024 * 1024):
            fail("unsupported_integrity_profile", "Deep verification requires ordered content IDs, nonhashed padded SHA-1 streams, at most 1 GiB each")
        records.append({"path": path / f"{identifier:08x}.app", "index": index, "size": size, "digest": digest})
    if (not (4 <= len(records) <= 1024 if plaintext else len(records) == 4)
            or int.from_bytes(tmd[0x1E0:0x1E2], "big") != 0 or records[0]["size"] > 16 * 1024 * 1024):
        fail("unsupported_integrity_profile", "Deep verification requires a bounded original FST boot profile")
    if tuple(struct.unpack_from(">HHIIHHI", ticket, 0x2A4)) != (1, 0x14, 0xAC, 0x14, 1, 0x14, 0):
        fail("invalid_ticket_rights", "Ticket rights header is invalid")
    if tuple(struct.unpack_from(">IIIIHH", ticket, 0x2B8)) != (0x28, 1, 0x84, 0x84, 3, 0):
        fail("invalid_ticket_rights", "Ticket content-rights section is invalid")
    if any(ticket[0x2CC:0x2D0]) or ticket[0x2D0] & 15 != 15:
        fail("invalid_ticket_rights", "Ticket does not grant all four scoped content indices")
    if plaintext and len(records) != 4 and ticket[0x2D0:0x350] != b'\xff' * 128:
        fail("invalid_ticket_rights", "Per-file homebrew requires the complete content-access bitmap")
    boot = b"".join(_plaintext(records[0]["path"], title_key, 0, records[0]["digest"], records[0]["size"]))
    grouped, directories = _fst(boot, records, report["title_id"], int.from_bytes(tmd[0x198:0x19A], "big"), plaintext=plaintext)
    files, xml = [], {}
    for number in range(1, len(records)):
        record = records[number]
        files += _content_files(_plaintext(record["path"], title_key, number, record["digest"], record["size"]), grouped[number], xml)
    _title_metadata(xml, tmd, files)
    if expected_staging is not None:
        _compare_staging(expected_staging, files, directories)
    for item in report["contents"]:
        item["plaintext_integrity"] = "padded_sha1_matches_tmd"
    return {**report, "validation": "plaintext_integrity" if plaintext else "decrypted_integrity", "files": files,
            "content_layout": "one_file_per_content" if plaintext and len(records) != 4 else "legacy_grouped",
            "fst": "bounds/tree/names/ranges/zero-padding verified",
            "staging_comparison": "all files and directories match" if expected_staging is not None else "not_requested",
            "certificate_layout": "parsed; CP/XS subjects match metadata; trust chain unauthenticated",
            "test_signing": signing, "installation": "unverified", "console_verified": False}
