"""Validate the documented, RSA-2048 Wii U application package profile.

Binary field offsets are format facts, independently implemented here. This
module never decrypts content, verifies Nintendo signatures, or installs titles.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import sys
import tempfile


SIGNATURE_TYPE = 0x00010004  # RSA-2048 / SHA-256
SIGNED_BODY = 0x140
INFO_START = 0x204
INFO_SIZE = 0x24
INFO_COUNT = 64
CONTENT_START = INFO_START + INFO_SIZE * INFO_COUNT
CONTENT_SIZE = 0x30
TITLE_RE = re.compile(r"00050000[0-9a-f]{8}")
CONTENT_RE = re.compile(r"([0-9a-f]{8})\.(app|h3)", re.IGNORECASE)


class ValidatorError(ValueError):
    def __init__(self, code, message, *, details=None):
        super().__init__(message)
        self.code = code
        self.details = details or {}


def _fail(code, message, **details):
    raise ValidatorError(code, message, details=details)


def _title_id(value):
    if not isinstance(value, str) or not TITLE_RE.fullmatch(value.lower()):
        _fail("invalid_title_id", "Expected a 16-digit Wii U application title ID beginning 00050000")
    result = value.lower()
    if result[8:] == "00000000":
        _fail("invalid_title_id", "The application title ID must have a nonzero unique identifier")
    return result


def _regular_file(path):
    if path.is_symlink():
        _fail("unsafe_file", "Package entries must not be symbolic links", file=path.name)
    if not path.is_file():
        _fail("missing_file", "Required package entry must be a regular file", file=path.name)


def _metadata(path, limit):
    _regular_file(path)
    try:
        if path.stat().st_size > limit:
            _fail("oversized_metadata", "Package metadata exceeds the supported size", file=path.name)
        with path.open("rb") as handle:
            result = handle.read(limit + 1)
        if len(result) > limit:
            _fail("oversized_metadata", "Package metadata exceeds the supported size", file=path.name)
        return result
    except OSError as exc:
        raise ValidatorError("unreadable_file", "Package metadata could not be read", details={"file": path.name}) from exc


def _digest_file(path, algorithm):
    try:
        digest = hashlib.new(algorithm)
        with path.open("rb") as handle:
            for block in iter(lambda: handle.read(1024 * 1024), b""):
                digest.update(block)
        return digest.digest()
    except OSError as exc:
        raise ValidatorError("unreadable_file", "Package content could not be read", details={"file": path.name}) from exc


def _registry(path):
    path = Path(path)
    if path.is_symlink():
        _fail("invalid_registry", "Title registry must not be a symbolic link")
    if not path.exists():
        _fail("missing_registry", "Title registry does not exist; reserve explicitly to create one")
    try:
        document = json.loads(_metadata(path, 1024 * 1024))
    except (json.JSONDecodeError, UnicodeDecodeError) as exc:
        raise ValidatorError("invalid_registry", "Title registry must contain valid JSON") from exc
    if not isinstance(document, dict) or document.get("schema_version") != 1 or not isinstance(document.get("titles"), dict):
        _fail("invalid_registry", "Title registry requires schema_version 1 and a titles object")
    normalized = set()
    for title, entry in document["titles"].items():
        title = _title_id(title)
        if title in normalized:
            _fail("invalid_registry", "Title registry contains duplicate case-normalized title IDs")
        normalized.add(title)
        if not isinstance(entry, dict) or not isinstance(entry.get("owner"), str) or not entry["owner"].strip():
            _fail("invalid_registry", "Every title reservation requires an owner")
        digest = entry.get("tmd_sha256")
        if digest is not None and (not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{64}", digest)):
            _fail("invalid_registry", "Reservation tmd_sha256 must be lowercase hex or null")
    document["titles"] = {title.lower(): entry for title, entry in document["titles"].items()}
    return document


def validate_package(path, registry=None):
    """Return structure evidence or raise ValidatorError; never mutate a package.

    The optional registry is checked without changing it. Plaintext 0x2000
    contents have their hashes checked here; encrypted contents need the
    independent integrity reader and a local key.
    """
    directory = Path(path)
    if directory.is_symlink() or not directory.is_dir():
        _fail("invalid_package_path", "Package path must be a directory containing title.tmd, title.tik, and title.cert")
    tmd = _metadata(directory / "title.tmd", 4 * 1024 * 1024)
    if len(tmd) < CONTENT_START:
        _fail("truncated_tmd", "Title metadata is shorter than its header and content-info table")
    if struct.unpack_from(">I", tmd)[0] != SIGNATURE_TYPE:
        _fail("unsupported_signature_type", "Only RSA-2048/SHA-256 metadata headers are supported")
    if tmd[0x180] != 1:
        _fail("unsupported_tmd_version", "Only Wii U title metadata version 1 is supported")
    title = _title_id(tmd[0x18C:0x194].hex())
    count = struct.unpack_from(">H", tmd, 0x1DE)[0]
    boot_index = struct.unpack_from(">H", tmd, 0x1E0)[0]
    if not count:
        _fail("empty_content_table", "Package must contain at least one content record")
    expected_size = CONTENT_START + CONTENT_SIZE * count
    if len(tmd) != expected_size:
        _fail("invalid_tmd_size", "TMD size must match its declared content count; embedded trailing certificates are outside this profile", expected=expected_size, actual=len(tmd))
    info = tmd[INFO_START:CONTENT_START]
    if hashlib.sha256(info).digest() != tmd[0x1E4:0x204]:
        _fail("metadata_digest_mismatch", "TMD content-info table SHA-256 does not match its header")
    covered = set()
    for number in range(INFO_COUNT):
        record = info[number * INFO_SIZE:(number + 1) * INFO_SIZE]
        first, length = struct.unpack_from(">HH", record)
        if not length:
            if any(record):
                _fail("invalid_content_info", "Unused content-info records must be zero", record=number)
            continue
        positions = set(range(first, first + length))
        if first + length > count or covered.intersection(positions):
            _fail("invalid_content_info", "Content-info groups overlap or exceed the content table", record=number)
        covered.update(positions)
        start = CONTENT_START + first * CONTENT_SIZE
        grouped = tmd[start:start + length * CONTENT_SIZE]
        if hashlib.sha256(grouped).digest() != record[4:]:
            _fail("metadata_digest_mismatch", "TMD content-record group SHA-256 does not match", record=number)
    if covered != set(range(count)):
        _fail("invalid_content_info", "Content-info groups must cover every content record exactly once")

    ticket = _metadata(directory / "title.tik", 16 * 1024)
    if len(ticket) != 0x350:
        _fail("invalid_ticket_size", "The supported Wii U ticket profile is exactly 0x350 bytes")
    if struct.unpack_from(">I", ticket)[0] != SIGNATURE_TYPE:
        _fail("unsupported_signature_type", "Only RSA-2048/SHA-256 ticket headers are supported")
    if ticket[0x1DC:0x1E4].hex() != title:
        _fail("ticket_title_mismatch", "Ticket and TMD must identify the same title")
    cert = _metadata(directory / "title.cert", 16 * 1024)
    if len(cert) < 4:
        _fail("truncated_certificate", "Certificate file must contain at least a signature-type field")

    files = {}
    try:
        entries = list(directory.iterdir())
    except OSError as exc:
        raise ValidatorError("unreadable_package", "Package directory could not be listed") from exc
    for entry in entries:
        match = CONTENT_RE.fullmatch(entry.name)
        if match:
            _regular_file(entry)
            key = (int(match[1], 16), match[2].lower())
            if key in files:
                _fail("duplicate_content_file", "Package has duplicate case-normalized content filenames", file=entry.name)
            files[key] = entry
        elif entry.suffix.lower() in (".app", ".h3"):
            _fail("invalid_content_filename", "Content filenames require eight hex digits", file=entry.name)
    seen_ids, seen_indices, expected_files, results = set(), set(), set(), []
    for position in range(count):
        record = tmd[CONTENT_START + position * CONTENT_SIZE:CONTENT_START + (position + 1) * CONTENT_SIZE]
        content_id, index, flags, size = struct.unpack_from(">IHHQ", record)
        if content_id in seen_ids or index in seen_indices:
            _fail("duplicate_content", "Content IDs and indices must each be unique", record=position)
        seen_ids.add(content_id)
        seen_indices.add(index)
        if flags not in (0x2000, 0x2001, 0x2003):
            _fail("unsupported_content_type", "Supported content types are plaintext 0x2000, encrypted 0x2001 and encrypted/hashed 0x2003", record=position, content_type=f"{flags:04x}")
        if not size or size % 16:
            _fail("invalid_content_size", "Encrypted content must have a positive AES-block-aligned size", record=position)
        key = (content_id, "app")
        app = files.get(key)
        if app is None:
            _fail("missing_content", "Content declared by TMD is missing", file=f"{content_id:08x}.app")
        expected_files.add(key)
        if app.stat().st_size != size:
            _fail("content_size_mismatch", "Content size differs from the TMD", file=app.name, expected=size, actual=app.stat().st_size)
        hashed = bool(flags & 2)
        item = {"content_id": f"{content_id:08x}", "index": index, "size": size,
                "encrypted": bool(flags & 1), "hashed": hashed,
                "plaintext_integrity": "unverified_requires_local_key"}
        if flags == 0x2000:
            if record[36:] != bytes(12) or _digest_file(app, "sha1") != record[16:36]:
                _fail("content_digest_mismatch", "Plaintext content SHA-1 does not match its TMD record", file=app.name)
            item["plaintext_integrity"] = "padded_sha1_matches_tmd"
        if hashed:
            if size % 0x10000:
                _fail("invalid_hashed_content_size", "Hashed content size must be aligned to 0x10000-byte blocks", file=app.name)
            h3key = (content_id, "h3")
            h3 = files.get(h3key)
            if h3 is None:
                _fail("missing_h3", "Hashed content requires its H3 file", file=f"{content_id:08x}.h3")
            expected_files.add(h3key)
            h3size = h3.stat().st_size
            if not h3size or h3size % 20:
                _fail("invalid_h3_size", "H3 data must contain complete 20-byte SHA-1 hashes", file=h3.name)
            if record[36:] != bytes(12):
                _fail("unsupported_content_digest", "This validator supports the SHA-1 plus zero-padding digest profile", file=h3.name)
            if _digest_file(h3, "sha1") != record[16:36]:
                _fail("content_digest_mismatch", "H3 SHA-1 does not match the TMD content record", file=h3.name)
            item["h3_integrity"] = "sha1_matches_tmd"
        results.append(item)
    if boot_index not in seen_indices:
        _fail("invalid_boot_index", "TMD boot content index does not name a declared content")
    if set(files) != expected_files:
        extras = sorted(files[key].name for key in set(files) - expected_files)
        _fail("unexpected_content", "Package contains undeclared content or H3 files", files=extras)

    tmd_digest = hashlib.sha256(tmd).hexdigest()
    collision = {"status": "not_checked", "scope": "explicit_local_registry_only"}
    if registry is not None:
        document = _registry(registry)
        reservation = document["titles"].get(title)
        if reservation is None:
            collision["status"] = "unregistered"
        elif reservation.get("tmd_sha256") != tmd_digest:
            _fail("title_id_collision", "Title ID is already reserved for a different or unspecified package", title_id=title, owner=reservation["owner"])
        else:
            collision.update(status="matches_registered_package", owner=reservation["owner"])
    return {"ok": True, "stage": "packaging", "validation": "structure_only",
            "profile": "wiiu_application_rsa2048_sha256_plaintext" if all(not item["encrypted"] for item in results) else "wiiu_application_rsa2048_sha256_encrypted",
            "title_id": title, "tmd_sha256": tmd_digest, "content_count": count,
            "metadata_integrity": "sha256_tables_verified", "contents": results,
            "title_id_registry": collision,
            "signatures": {"authenticated": False, "tmd": "absent" if not any(tmd[4:0x104]) else "unverified",
                           "ticket": "absent" if not any(ticket[4:0x104]) else "unverified",
                           "certificate_chain": "unverified"},
            "installation": "unverified", "device_test": "not_run"}


def reserve_title_id(registry, report, owner):
    """Explicitly reserve a validated package ID; refuse conflicting ownership.

    The small JSON registry is written atomically. Callers must serialize writes
    from multiple processes; it is a local project registry, not a global one.
    """
    path = Path(registry)
    if not isinstance(owner, str) or not owner.strip():
        _fail("invalid_owner", "Title reservation requires a nonempty owner label")
    if report.get("ok") is not True or report.get("validation") not in ("structure_only", "decrypted_integrity"):
        _fail("invalid_reservation", "Reserve only after successful package validation")
    title = _title_id(report["title_id"])
    digest = report.get("tmd_sha256")
    if not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{64}", digest):
        _fail("invalid_reservation", "Reservation requires the validated TMD digest")
    if path.is_symlink():
        _fail("invalid_registry", "Title registry must not be a symbolic link")
    document = _registry(path) if path.exists() else {"schema_version": 1, "titles": {}}
    entry = {"owner": owner.strip(), "tmd_sha256": digest}
    prior = document["titles"].get(title)
    if prior is not None and prior.get("owner") != entry["owner"]:
        _fail("title_id_collision", "Existing title reservation belongs to a different owner or package", title_id=title, owner=prior["owner"])
    document["titles"][title] = entry
    temporary = None
    try:
        with tempfile.NamedTemporaryFile("w", encoding="utf-8", dir=path.parent, prefix=".title-registry-", delete=False) as handle:
            temporary = Path(handle.name)
            json.dump(document, handle, indent=2, sort_keys=True)
            handle.write("\n")
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
    except OSError as exc:
        raise ValidatorError("registry_write_failed", "Title registry could not be saved") from exc
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    return entry


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package")
    parser.add_argument("--registry")
    parser.add_argument("--reserve", metavar="OWNER", help="Explicitly reserve this validated title ID in --registry")
    args = parser.parse_args(argv)
    try:
        if args.reserve and not args.registry:
            _fail("missing_registry", "--reserve requires --registry")
        report = validate_package(args.package, registry=None if args.reserve else args.registry)
        if args.reserve:
            reserve_title_id(args.registry, report, args.reserve)
            report = validate_package(args.package, registry=args.registry)
        print(json.dumps(report, indent=2))
        return 0
    except ValidatorError as exc:
        print(json.dumps({"ok": False, "stage": "packaging", "error": {"code": exc.code, "message": str(exc), "details": exc.details}}, indent=2), file=sys.stderr)
        return 2
    except OSError as exc:
        print(json.dumps({"ok": False, "stage": "packaging", "error": {"code": "io_error", "message": "Package filesystem operation failed"}}, indent=2), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
