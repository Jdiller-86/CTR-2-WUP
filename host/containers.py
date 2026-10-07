"""Strict read-only NCSD/NCCH inspection. No decryption or signature authentication.

Offsets, sizes and guest integers are explicitly little endian. ExeFS file data
is hashed as stored; .code is independently hashed after backwards-LZ expansion.
The format description is documented at https://www.3dbrew.org/wiki/NCCH and
https://www.3dbrew.org/wiki/ExeFS. This module is original implementation code.
"""

from __future__ import annotations

import hashlib
import os
from pathlib import Path
import struct
from typing import BinaryIO

from .errors import HostError

MAX_CODE_BYTES = 128 * 1024 * 1024
MAX_SECTION_METADATA = 16 * 1024 * 1024


def fail(code, message):
    raise HostError(code, message)


def u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def u64(data, offset):
    return struct.unpack_from("<Q", data, offset)[0]


def check_range(offset, length, limit, label):
    if offset < 0 or length < 0 or offset > limit or length > limit - offset:
        fail("invalid_bounds", f"{label} lies outside its containing region")


def check_overlaps(regions):
    ordered = sorted((start, start + size, name) for start, size, name in regions if size)
    for previous, current in zip(ordered, ordered[1:]):
        if current[0] < previous[1]:
            fail("overlapping_regions", f"{previous[2]} overlaps {current[2]}")


class Reader:
    def __init__(self, handle: BinaryIO):
        self.handle = handle
        self.initial_stat = os.fstat(handle.fileno())
        self.size = self.initial_stat.st_size

    def read(self, offset, size, label="data"):
        check_range(offset, size, self.size, label)
        self.handle.seek(offset)
        data = self.handle.read(size)
        if len(data) != size:
            fail("short_read", f"Input changed or was truncated while reading {label}")
        return data

    def digest(self, offset, size):
        check_range(offset, size, self.size, "hashed region")
        self.handle.seek(offset)
        digest = hashlib.sha256()
        remaining = size
        while remaining:
            data = self.handle.read(min(1024 * 1024, remaining))
            if not data:
                fail("short_read", "Input changed or was truncated while hashing")
            digest.update(data)
            remaining -= len(data)
        return digest.hexdigest()


def decompress_code(data: bytes, max_output=MAX_CODE_BYTES) -> bytes:
    """Expand Nintendo backwards LZ with bounded allocation and checked references.

    The footer contains compressed span (24 bits), footer/padding span (8 bits)
    and output growth. The uncompressed prefix must meet the backwards cursor
    exactly; partial streams and references to not-yet-produced bytes fail.
    """
    if len(data) < 8:
        fail("invalid_compression", ".code backwards-LZ footer is truncated")
    packed, growth = struct.unpack_from("<II", data, len(data) - 8)
    span, footer = packed & 0xFFFFFF, packed >> 24
    if footer < 8 or footer > span or span > len(data):
        fail("invalid_compression", ".code backwards-LZ span/footer is invalid")
    size = len(data) + growth
    if size > max_output:
        fail("decompression_limit", ".code exceeds the configured decompression limit")
    prefix = len(data) - span
    cursor = len(data) - footer
    out_cursor = size
    output = bytearray(size)
    output[:prefix] = data[:prefix]
    while cursor > prefix:
        cursor -= 1
        flags = data[cursor]
        for bit in range(7, -1, -1):
            if cursor == prefix:
                break
            if flags & (1 << bit):
                if cursor - prefix < 2:
                    fail("invalid_compression", ".code backwards-LZ token is truncated")
                cursor -= 2
                token = data[cursor] | data[cursor + 1] << 8
                count, distance = (token >> 12) + 3, (token & 0xFFF) + 3
                if count > out_cursor - prefix:
                    fail("invalid_compression", ".code backwards-LZ write crosses its prefix")
                for _ in range(count):
                    destination = out_cursor - 1
                    source = destination + distance
                    if source < out_cursor or source >= size:
                        fail("invalid_compression", ".code backwards-LZ reference is not produced yet")
                    output[destination] = output[source]
                    out_cursor -= 1
            else:
                if out_cursor <= prefix:
                    fail("invalid_compression", ".code backwards-LZ literal exceeds output")
                cursor -= 1
                out_cursor -= 1
                output[out_cursor] = data[cursor]
    if out_cursor != prefix:
        fail("invalid_compression", ".code backwards-LZ did not fill its declared output")
    return bytes(output)


def _ascii(raw, label):
    raw = raw.split(b"\0", 1)[0]
    try:
        return raw.decode("ascii")
    except UnicodeDecodeError:
        fail("invalid_metadata", f"{label} contains non-ASCII bytes")


def _smdh(data):
    if len(data) != 0x36C0 or data[:4] != b"SMDH":
        fail("invalid_metadata", "ExeFS icon is not a complete SMDH record")
    if struct.unpack_from("<H", data, 4)[0] != 0:
        fail("unsupported_metadata", "Unsupported SMDH version")
    def text(start, count):
        try:
            return data[start:start + count].decode("utf-16le").split("\0", 1)[0]
        except UnicodeDecodeError:
            fail("invalid_metadata", "SMDH contains invalid UTF-16")
    english = 8 + 0x200
    return {"short_title": text(english, 0x80),
            "long_title": text(english + 0x80, 0x100),
            "publisher": text(english + 0x180, 0x80),
            "region_mask": u32(data, 0x2018)}


def _ncch(reader, base, available, *, partition):
    check_range(base, available, reader.size, "NCCH partition")
    if available < 0x200:
        fail("truncated_header", "NCCH header requires 512 bytes")
    header = reader.read(base, 0x200, "NCCH header")
    if header[0x100:0x104] != b"NCCH":
        fail("invalid_magic", f"Partition {partition} is not NCCH")
    version = struct.unpack_from("<H", header, 0x112)[0]
    if version != 2:
        fail("unsupported_ncch_version", f"NCCH version {version} is unsupported; require modern v2")
    flags = header[0x188:0x190]
    if not flags[7] & 4:
        fail("encrypted_input", f"Partition {partition} is encrypted. Supply a fully decrypted user-owned dump; no keys are requested by this tool")
    if flags[6] > 8:
        fail("invalid_unit_size", "NCCH content unit exponent is excessive")
    unit = 0x200 << flags[6]
    size = u32(header, 0x104) * unit
    if size < 0x200 or size > available:
        fail("invalid_bounds", "NCCH declared content size exceeds its partition or is too small")
    exheader_size = u32(header, 0x180)
    if exheader_size not in (0, 0x400):
        fail("unsupported_exheader", "Only standard 0x400-byte NCCH extended headers are supported")
    regions = [(0, 0x200, "NCCH header")]
    result = {"partition": partition, "offset": base, "size": size,
              "program_id": f"{u64(header, 0x118):016x}",
              "partition_id": f"{u64(header, 0x108):016x}",
              "maker_code": _ascii(header[0x110:0x112], "maker code"),
              "product_code": _ascii(header[0x150:0x160], "product code"),
              "ncch_version": version, "platform_flag": flags[4],
              "encrypted": False, "executable": bool(flags[5] & 2),
              "content_type": (flags[5] >> 2) & 7, "sections": {}, "exefs": []}
    exheader = None
    if exheader_size:
        check_range(0x200, 0x800, size, "extended header and access descriptor")
        exheader = reader.read(base + 0x200, exheader_size, "extended header")
        if hashlib.sha256(exheader).digest() != header[0x160:0x180]:
            fail("hash_mismatch", "NCCH extended-header SHA-256 mismatch")
        regions.append((0x200, 0x800, "extended header/access descriptor"))
        result["code_compressed"] = bool(exheader[0xD] & 1)
        result["remaster_version"] = struct.unpack_from("<H", exheader, 0xE)[0]
        result["codeset_name"] = _ascii(exheader[:8], "codeset name")
        result["stack_size"] = u32(exheader, 0x1C)
        result["bss_size"] = u32(exheader, 0x3C)
        result["segments"] = {name: {"address": u32(exheader, off),
                                             "pages": u32(exheader, off + 4),
                                             "size": u32(exheader, off + 8)}
                              for name, off in (("text", 0x10), ("rodata", 0x20), ("data", 0x30))}
    else:
        result["code_compressed"] = False
    for name, offset in (("plain", 0x190), ("logo", 0x198), ("exefs", 0x1A0), ("romfs", 0x1B0)):
        relative, length = u32(header, offset) * unit, u32(header, offset + 4) * unit
        if bool(relative) != bool(length):
            fail("invalid_bounds", f"{name} offset and size must both be present or absent")
        if not length:
            continue
        check_range(relative, length, size, name)
        regions.append((relative, length, name))
        result["sections"][name] = {"offset": base + relative, "size": length}
        if name in ("exefs", "romfs"):
            hashed_size = u32(header, offset + 8) * unit
            if not hashed_size or hashed_size > length:
                fail("invalid_hash_region", f"{name} hashed region is absent or exceeds section")
            expected = header[0x1C0 if name == "exefs" else 0x1E0:0x1E0 if name == "exefs" else 0x200]
            actual = reader.digest(base + relative, hashed_size)
            if actual != expected.hex():
                fail("hash_mismatch", f"NCCH {name} superblock SHA-256 mismatch")
            result["sections"][name]["verified_superblock_sha256"] = actual
    check_overlaps(regions)
    if flags[7] & 2 and "romfs" in result["sections"]:
        fail("invalid_flags", "NCCH flags declare no RomFS but include a RomFS section")
    if "logo" in result["sections"]:
        section = result["sections"]["logo"]
        if reader.digest(section["offset"], section["size"]) != header[0x130:0x150].hex():
            fail("hash_mismatch", "NCCH logo-region SHA-256 mismatch")
    if "exefs" not in result["sections"]:
        if result["executable"]:
            fail("missing_exefs", "Executable NCCH does not contain ExeFS")
        return result
    exefs = result["sections"]["exefs"]
    if exefs["size"] < 0x200:
        fail("invalid_bounds", "ExeFS header requires 512 bytes")
    table = reader.read(exefs["offset"], 0x200, "ExeFS header")
    names = set()
    extents = []
    for index in range(10):
        raw, relative, length = struct.unpack_from("<8sII", table, index * 16)
        if raw == bytes(8):
            if relative or length:
                fail("invalid_exefs_entry", "Unnamed ExeFS entry contains an offset or size")
            continue
        if b"\0" in raw and any(raw[raw.index(0):]):
            fail("invalid_exefs_name", "ExeFS name padding must be zero")
        name = _ascii(raw, "ExeFS name")
        if not name or name in (".", "..") or any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-" for c in name):
            fail("invalid_exefs_name", "ExeFS name is unsafe or unsupported")
        if name.casefold() in names:
            fail("duplicate_exefs_name", f"Duplicate ExeFS file name: {name}")
        names.add(name.casefold())
        if not length:
            fail("invalid_exefs_entry", f"ExeFS {name} has an empty payload")
        check_range(0x200 + relative, length, exefs["size"], f"ExeFS {name}")
        extents.append((relative, length, name))
        location = exefs["offset"] + 0x200 + relative
        digest = reader.digest(location, length)
        expected = table[0xC0 + (9 - index) * 0x20:0xE0 + (9 - index) * 0x20]
        if digest != expected.hex():
            fail("hash_mismatch", f"ExeFS {name} SHA-256 mismatch")
        record = {"name": name, "offset": location, "size": length, "sha256": digest}
        result["exefs"].append(record)
        if name == ".code":
            if length > MAX_CODE_BYTES:
                fail("code_limit", ".code exceeds supported 128 MiB limit")
            data = reader.read(location, length, ".code")
            code = decompress_code(data) if result["code_compressed"] else data
            result["executable_sha256"] = hashlib.sha256(code).hexdigest()
            result["executable_size"] = len(code)
            if exheader:
                material = hashlib.sha256(exheader).digest() + bytes.fromhex(result["executable_sha256"])
                result["executable_revision"] = hashlib.sha256(material).hexdigest()
        elif name == "icon":
            if length > MAX_SECTION_METADATA:
                fail("metadata_limit", "ExeFS icon metadata is excessive")
            result["title"] = _smdh(reader.read(location, length, "icon"))
    check_overlaps(extents)
    if result["executable"] and not exheader:
        fail("missing_exheader", "Executable NCCH has no supported extended header")
    if result["executable"] and "executable_sha256" not in result:
        fail("missing_code", "Executable NCCH does not contain an ExeFS .code payload")
    return result


def inspect(path: str | Path):
    path = Path(path)
    if path.suffix.lower() == ".3dsx":
        fail("unsupported_format", ".3dsx uses a different executable loader. Supply NCSD .3ds or NCCH .cxi")
    try:
        with path.open("rb") as handle:
            reader = Reader(handle)
            if reader.size < 0x200:
                fail("truncated_header", "Input requires a 512-byte NCSD/NCCH header")
            header = reader.read(0, 0x200, "container header")
            magic = header[0x100:0x104]
            if magic == b"NCCH":
                partitions = [_ncch(reader, 0, reader.size, partition=0)]
                kind = "NCCH"
                capacity = partitions[0]["size"]
                if capacity != reader.size:
                    fail("trailing_data", "Standalone NCCH has bytes outside its declared content")
            elif magic == b"NCSD":
                kind = "NCSD"
                capacity = u32(header, 0x104) * 0x200
                if capacity < reader.size or capacity < 0x200:
                    fail("invalid_bounds", "NCSD declared media capacity is smaller than its file")
                extents = [(0, 0x200, "NCSD header")]
                slots = []
                for index in range(8):
                    relative, length = struct.unpack_from("<II", header, 0x120 + index * 8)
                    relative, length = relative * 0x200, length * 0x200
                    if bool(relative) != bool(length):
                        fail("invalid_bounds", "NCSD partition offset and size must both be present or absent")
                    if length:
                        check_range(relative, length, reader.size, f"NCSD partition {index}")
                        if header[0x110 + index] != 0:
                            fail("unsupported_partition", "NCSD NAND/non-NCCH partitions are unsupported")
                        extents.append((relative, length, f"partition {index}"))
                        slots.append((index, relative, length))
                check_overlaps(extents)
                if not slots or slots[0][0] != 0:
                    fail("missing_main_partition", "NCSD has no main executable partition 0")
                partitions = [_ncch(reader, offset, length, partition=index) for index, offset, length in slots]
            else:
                fail("unsupported_format", "Input is not NCSD or NCCH; renamed files and .3dsx are not supported")
            main = partitions[0]
            if not main["executable"]:
                fail("not_executable", "The primary NCCH is not application executable content")
            before = reader.initial_stat
            digest = reader.digest(0, reader.size)
            after = os.fstat(handle.fileno())
            if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
                fail("input_changed", "Input changed during inspection; retry with a stable file")
            return {"schema_version": 1, "format": kind, "file_size": reader.size,
                    "media_capacity": capacity, "input_sha256": digest,
                    "identity": {"program_id": main["program_id"],
                                 "save_identity": "ctr-" + main["program_id"],
                                 "product_code": main["product_code"]},
                    "executable_revision": main["executable_revision"],
                    "compatibility": {"state": "untested", "profile": None,
                                      "reason": "No commercial revision has been console verified"},
                    "partitions": partitions,
                    "integrity": {"section_hashes_verified": True,
                                  "signature_authenticity_verified": False,
                                  "romfs_filesystem_verified": False}}
    except OSError as exc:
        raise HostError("input_io", str(exc)) from exc
