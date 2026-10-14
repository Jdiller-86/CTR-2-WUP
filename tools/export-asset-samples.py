#!/usr/bin/env python3
"""Export explicit, hash-checked private RomFS samples using only the stdlib.

This script deliberately has no project imports: copy it to a Windows/Python
3.12+ PC alongside existing prepare-rom / prepare-assets outputs. It does not
extract arbitrary host paths or establish container authenticity/compatibility.
"""
import argparse
from contextlib import ExitStack
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import sys
import tempfile
import unicodedata
import zipfile


CHUNK = 1024 * 1024
MAX_REPORT_BYTES = 8 * CHUNK
MAX_BLOB_BYTES = 4 * 1024**3
MAX_FILES = 10000
MAX_SELECTED_FILES = 64
MAX_SELECTED_BYTES = 30 * CHUNK
MAX_ZIP_BYTES = 32 * CHUNK  # Publication must be strictly below this limit.
HASH = re.compile(r"[0-9a-f]{64}\Z")
PROGRAM_ID = re.compile(r"[0-9a-f]{16}\Z")
RESERVED = {"con", "prn", "aux", "nul"} | {
    f"{prefix}{digit}" for prefix in ("com", "lpt") for digit in "123456789¹²³"
}


class SampleError(Exception):
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code


def fail(code, message):
    raise SampleError(code, message)


def integer(value, maximum, label):
    if type(value) is not int or not 0 <= value <= maximum:
        fail("invalid_manifest", f"Invalid bounded integer: {label}")
    return value


def digest_value(value):
    if not isinstance(value, str) or not HASH.fullmatch(value):
        fail("invalid_manifest", "Expected a lowercase SHA-256 digest")
    return value


def guest_path(value):
    if not isinstance(value, str) or not value.startswith("/"):
        fail("unsafe_guest_path", "Select absolute guest paths from the manifest")
    try:
        if len(value.encode("utf-8")) > 512:
            fail("unsafe_guest_path", "Guest path exceeds the sample path limit")
    except UnicodeError:
        fail("unsafe_guest_path", "Guest path contains invalid Unicode")
    for component in value[1:].split("/"):
        if (not component or component in (".", "..") or
                any(ord(c) < 32 or ord(c) == 127 or c in '\\:*?"<>|' for c in component) or
                component.endswith((".", " ")) or
                component.split(".", 1)[0].casefold() in RESERVED):
            fail("unsafe_guest_path", "Guest path is unsafe or ambiguous on Windows")
    return value


def path_key(value):
    return unicodedata.normalize("NFC", value).casefold()


def fingerprint(value):
    return (value.st_dev, value.st_ino, value.st_size,
            value.st_mtime_ns, value.st_ctime_ns)


class Input:
    def __init__(self, path, stack):
        self.path = Path(path).absolute()
        self.initial_path = os.stat(self.path, follow_symlinks=False)
        if not stat.S_ISREG(self.initial_path.st_mode):
            fail("unsafe_input", "Inputs must be regular files, not symbolic links")
        flags = os.O_RDONLY | getattr(os, "O_BINARY", 0) | getattr(os, "O_NOFOLLOW", 0)
        fd = os.open(self.path, flags)
        self.handle = stack.enter_context(os.fdopen(fd, "rb"))
        self.initial = os.fstat(self.handle.fileno())
        if not stat.S_ISREG(self.initial.st_mode):
            fail("unsafe_input", "Inputs must be regular files")
        # Windows path stat and CRT handle fstat can expose different timestamp
        # precision/identity fields. Compare each API against its own snapshot;
        # keep the path identity for alias detection and the handle for reads.
        if self.initial.st_size != self.initial_path.st_size:
            fail("input_changed", "An input changed while opening it")
        if os.name != "nt" and not os.path.samestat(self.initial, self.initial_path):
            fail("input_changed", "An input was replaced while opening it")
        self.unchanged()

    def unchanged(self):
        current = os.stat(self.path, follow_symlinks=False)
        if (not stat.S_ISREG(current.st_mode) or
                fingerprint(current) != fingerprint(self.initial_path) or
                fingerprint(os.fstat(self.handle.fileno())) != fingerprint(self.initial)):
            fail("input_changed", "An input changed during export; retry with stable files")

    def chunks(self, offset, size):
        self.handle.seek(offset)
        while size:
            data = self.handle.read(min(CHUNK, size))
            if not data:
                fail("input_changed", "An input became truncated during export")
            size -= len(data)
            yield data

    def sha256(self):
        result = hashlib.sha256()
        for data in self.chunks(0, self.initial.st_size):
            result.update(data)
        return result.hexdigest()


def unique_keys(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            fail("invalid_report", "Duplicate JSON keys are not supported")
        result[key] = value
    return result


def invalid_constant(_):
    fail("invalid_report", "Nonfinite JSON values are not supported")


def read_report(source, stage):
    if not 0 < source.initial.st_size <= MAX_REPORT_BYTES:
        fail("report_size_limit", "A report exceeds the bounded JSON size limit")
    source.handle.seek(0)
    data = source.handle.read(MAX_REPORT_BYTES + 1)
    try:
        result = json.loads(data.decode("utf-8"), object_pairs_hook=unique_keys,
                            parse_constant=invalid_constant)
    except (ValueError, UnicodeError, RecursionError):
        fail("invalid_report", "A report is not supported UTF-8 JSON")
    if (not isinstance(result, dict) or result.get("ok") is not True or
            type(result.get("schema_version")) is not int or
            result["schema_version"] != 1 or result.get("stage") != stage):
        fail("invalid_report", "Expected successful schema-1 preparation/asset reports")
    source.unchanged()
    return result, hashlib.sha256(data).hexdigest()


def validate_reports(asset, preparation, blob_size):
    identity = asset.get("identity")
    if not isinstance(identity, dict):
        fail("invalid_manifest", "Missing executable identity")
    program = identity.get("program_id")
    product = identity.get("product_code")
    if (not isinstance(program, str) or not PROGRAM_ID.fullmatch(program) or
            identity.get("save_identity") != "ctr-" + program or
            not isinstance(product, str) or len(product) > 16 or
            any(not 32 <= ord(c) <= 126 for c in product)):
        fail("invalid_manifest", "Invalid executable identity")
    revision = digest_value(asset.get("executable_revision"))
    source_hash = digest_value(asset.get("source_sha256"))
    digest_value(preparation.get("source_sha256"))
    if (preparation.get("identity") != identity or
            preparation.get("executable_revision") != revision or
            preparation.get("output_sha256") != source_hash):
        fail("provenance_mismatch", "Preparation and assets describe different sources/revisions")
    ivfc, blob, metadata = asset.get("ivfc"), asset.get("romfs_file"), asset.get("metadata")
    if not all(isinstance(v, dict) for v in (ivfc, blob, metadata)):
        fail("invalid_manifest", "Missing RomFS integrity, blob or inventory metadata")
    if (preparation.get("full_ivfc_verified") is not True or
            preparation.get("romfs_preserved") is not True or
            preparation.get("keys_exported") is not False or
            preparation.get("other_contents_exported") is not False or
            preparation.get("romfs_integrity") != ivfc or
            ivfc.get("all_declared_blocks_verified") is not True or
            ivfc.get("ncch_superblock_anchored") is not True):
        fail("provenance_mismatch", "Reports do not agree on complete local IVFC verification")
    expected = digest_value(blob.get("sha256"))
    if (integer(blob.get("bytes"), MAX_BLOB_BYTES, "blob size") != blob_size or
            integer(ivfc.get("level3_size"), MAX_BLOB_BYTES, "Level-3 size") != blob_size or
            ivfc.get("level3_sha256") != expected):
        fail("blob_mismatch", "Local blob size/hash binding differs from the reports")
    if metadata.get("guest_paths_only") is not True or metadata.get("host_paths_extracted") is not False:
        fail("invalid_manifest", "Expected a guest-only asset inventory")
    integer(metadata.get("directories"), MAX_FILES, "directory count")
    data_offset = integer(metadata.get("file_data_offset"), blob_size, "data offset")
    records = metadata.get("files")
    if not isinstance(records, list) or not 0 < len(records) <= MAX_FILES:
        fail("inventory_limit", "Asset inventory is empty or exceeds the file limit")
    index, aliases, spans, total = {}, set(), [], 0
    for record in records:
        if not isinstance(record, dict):
            fail("invalid_manifest", "Invalid asset inventory record")
        path = guest_path(record.get("path"))
        key = path_key(path)
        if key in aliases:
            fail("duplicate_guest_path", "Duplicate or Unicode/case-aliased guest paths")
        aliases.add(key)
        offset = integer(record.get("offset"), blob_size, "file offset")
        size = integer(record.get("size"), blob_size, "file size")
        if offset < data_offset or size > blob_size - offset:
            fail("invalid_extent", "An asset extent is outside the blob data region")
        if size:
            spans.append((offset, offset + size))
        total += size
        index[path] = {"path": path, "offset": offset, "size": size,
                       "sha256": digest_value(record.get("sha256")), "archive_name": "files" + path}
    if total != integer(metadata.get("file_bytes"), MAX_BLOB_BYTES, "total file bytes"):
        fail("invalid_manifest", "Inventory file-byte total is inconsistent")
    spans.sort()
    if any(a[1] > b[0] for a, b in zip(spans, spans[1:])):
        fail("invalid_extent", "Asset file extents overlap")
    for key in aliases:
        parts = key.split("/")
        if any("/".join(parts[:i]) in aliases for i in range(2, len(parts))):
            fail("duplicate_guest_path", "A guest file path aliases an ancestor directory")
    return index, {k: identity[k] for k in ("program_id", "save_identity", "product_code")}, expected


def select_records(index, paths):
    if not 0 < len(paths) <= MAX_SELECTED_FILES:
        fail("selection_limit", "Select between one and 64 explicit guest files")
    selected, seen = [], set()
    for path in paths:
        guest_path(path)
        if path_key(path) in seen:
            fail("duplicate_selection", "Select each guest file only once")
        seen.add(path_key(path))
        if path not in index:
            fail("missing_guest_path", "A selected exact guest path is absent from the manifest")
        selected.append(index[path])
    if sum(r["size"] for r in selected) > MAX_SELECTED_BYTES:
        fail("selection_limit", "Selected uncompressed files exceed 30 MiB; use separate batches")
    return selected


def zip_entry(name):
    entry = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
    entry.compress_type = zipfile.ZIP_DEFLATED
    entry.external_attr = 0o100600 << 16
    return entry


def publish(stage, output):
    # POSIX rename replaces destinations; linking is atomic and never replaces.
    # Windows rename already refuses an existing destination, including a race.
    try:
        if os.name == "nt":
            os.rename(stage, output)
        else:
            os.link(stage, output)
            stage.unlink()
    except FileExistsError:
        fail("output_exists", "Output appeared during export and was preserved")


def export_samples(manifest, preparation_report, romfs, paths, output):
    output = Path(output).absolute()
    if os.path.lexists(output):
        fail("output_exists", "Output already exists and was preserved; choose a new ZIP name")
    stage = None
    try:
        with ExitStack() as stack:
            manifest_input, preparation_input, blob_input = [Input(p, stack) for p in
                                                            (manifest, preparation_report, romfs)]
            inputs = (manifest_input, preparation_input, blob_input)
            identities = {(s.initial_path.st_dev, s.initial_path.st_ino) for s in inputs}
            if len(identities) != len(inputs):
                fail("input_alias", "Each input must be a distinct regular file")
            asset, manifest_hash = read_report(manifest_input, "prepare-assets")
            preparation, preparation_hash = read_report(preparation_input, "prepare-rom")
            index, identity, expected = validate_reports(asset, preparation, blob_input.initial.st_size)
            selected = select_records(index, paths)
            if blob_input.sha256() != expected:
                fail("blob_hash_mismatch", "Full local RomFS hash does not match the asset manifest")
            for source in inputs:
                source.unchanged()
            report = {"ok": True, "stage": "export-asset-samples", "schema_version": 1,
                      "identity": identity, "executable_revision": asset["executable_revision"],
                      "prepared_main_sha256": asset["source_sha256"],
                      "manifest_sha256": manifest_hash, "preparation_report_sha256": preparation_hash,
                      "source_blob_sha256": expected, "source_blob_bytes": blob_input.initial.st_size,
                      "selected_files": selected, "selected_bytes": sum(r["size"] for r in selected),
                      "source_blob_hash_verified": True, "selected_file_hashes_verified": True,
                      "reported_full_ivfc_verification": True, "full_ivfc_reverified": False,
                      "original_container_hash_verified": False,
                      "signature_authenticity_verified": False,
                      "commercial_profile_enabled": False, "console_verified": False}
            output.parent.mkdir(parents=True, exist_ok=True)
            fd, temporary = tempfile.mkstemp(prefix=".ctr2wup-samples-", suffix=".zip", dir=output.parent)
            stage = Path(temporary)
            with os.fdopen(fd, "w+b") as target:
                with zipfile.ZipFile(target, "w", compression=zipfile.ZIP_DEFLATED, allowZip64=False) as archive:
                    for record in selected:
                        digest = hashlib.sha256()
                        with archive.open(zip_entry(record["archive_name"]), "w") as member:
                            for data in blob_input.chunks(record["offset"], record["size"]):
                                digest.update(data)
                                member.write(data)
                        if digest.hexdigest() != record["sha256"]:
                            fail("file_hash_mismatch", "A selected file hash differs from the manifest")
                    archive.writestr(zip_entry("asset-sample-report.json"),
                                     json.dumps(report, ensure_ascii=True, indent=2) + "\n")
                target.flush()
                os.fsync(target.fileno())
                archive_bytes = target.tell()
            if archive_bytes >= MAX_ZIP_BYTES:
                fail("archive_size_limit", "Sample ZIP must be below 32 MiB; use a smaller batch")
            # Recheck every original byte and path identity before publication.
            if (blob_input.sha256() != expected or manifest_input.sha256() != manifest_hash or
                    preparation_input.sha256() != preparation_hash):
                fail("input_changed", "An input changed while creating the sample ZIP")
            for source in inputs:
                source.unchanged()
            publish(stage, output)
            return {"ok": True, "stage": "export-asset-samples", "schema_version": 1,
                    "files": len(selected), "selected_bytes": report["selected_bytes"],
                    "archive_bytes": archive_bytes, "output": str(output)}
    finally:
        if stage is not None and stage.exists():
            stage.unlink()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True, help="Local asset-manifest.json")
    parser.add_argument("--preparation-report", required=True, help="Matching preparation-report.json")
    parser.add_argument("--romfs", required=True, help="Local prepared Level-3 romfs.bin")
    parser.add_argument("--paths", nargs="+", required=True, help="Exact absolute guest paths to select")
    parser.add_argument("--output", required=True, help="New private ZIP; existing outputs are preserved")
    args = parser.parse_args(argv)
    try:
        if sys.version_info < (3, 12):
            fail("python_version", "Python 3.12 or newer is required")
        result = export_samples(args.manifest, args.preparation_report, args.romfs, args.paths, args.output)
    except SampleError as exc:
        result = {"ok": False, "stage": "export-asset-samples",
                  "error": {"code": exc.code, "message": str(exc)}}
    except OSError:
        result = {"ok": False, "stage": "export-asset-samples",
                  "error": {"code": "filesystem_error", "message": "Unable to read inputs or publish the ZIP; existing files were preserved"}}
    print(json.dumps(result, indent=2, ensure_ascii=True))
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
