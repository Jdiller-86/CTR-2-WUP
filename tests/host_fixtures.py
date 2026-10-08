"""Original synthetic NCCH/NCSD bytes, not a commercial ROM or a .3dsx.

This fixture proves container inspection only. It has a tiny ARM instruction
payload and no Nintendo signature, encryption, assets, services or filesystem.
Generate locally: python -m tests.host_fixtures OUTPUT_DIRECTORY
"""

import hashlib
from pathlib import Path
import struct
import sys

PROGRAM_ID = 0x000400000FF00F00
CODE = struct.pack("<II", 0xE3A0002A, 0xE12FFF1E)  # mov r0,#42 ; bx lr


def align(value, size=0x200):
    return (value + size - 1) // size * size


def cia(contents=None, *, encrypted=False, title_id=PROGRAM_ID, version=48):
    """Original unsigned CIA fixture, never an installable game or credential."""
    contents = contents or [(0, bytes(ncch(program_id=title_id)))]
    tmd = bytearray(0xB04 + len(contents) * 48)
    struct.pack_into(">I", tmd, 0, 0x10004)
    tmd[0x180] = 1
    struct.pack_into(">Q", tmd, 0x18C, title_id)
    struct.pack_into(">HH", tmd, 0x1DC, version, len(contents))
    for number, (index, data) in enumerate(contents):
        offset = 0xB04 + number * 48
        struct.pack_into(">IHHQ", tmd, offset, number + 1, index, int(encrypted and index == 0), len(data))
        tmd[offset + 16:offset + 48] = hashlib.sha256(data).digest()
    struct.pack_into(">HH", tmd, 0x204, 0, len(contents))
    tmd[0x208:0x228] = hashlib.sha256(tmd[0xB04:]).digest()
    tmd[0x1E4:0x204] = hashlib.sha256(tmd[0x204:0xB04]).digest()
    ticket = bytearray(0x350)
    struct.pack_into(">I", ticket, 0, 0x10004)
    struct.pack_into(">Q", ticket, 0x1DC, title_id)
    cert = b"Original test certificate placeholder"
    start = 0x2040
    cert_at = start
    ticket_at = align(cert_at + len(cert), 64)
    tmd_at = align(ticket_at + len(ticket), 64)
    content_at = align(tmd_at + len(tmd), 64)
    payload = bytearray()
    for _, data in contents:
        payload += bytes((-len(payload)) % 64)
        payload += data
    result = bytearray(content_at + len(payload))
    struct.pack_into("<IHHIIIIQ", result, 0, 0x2020, 0, 0, len(cert), len(ticket), len(tmd), 0, len(payload))
    for index, _ in contents:result[0x20 + index // 8] |= 0x80 >> (index % 8)
    result[cert_at:cert_at + len(cert)] = cert
    result[ticket_at:ticket_at + len(ticket)] = ticket
    result[tmd_at:tmd_at + len(tmd)] = tmd
    result[content_at:] = payload
    return result


def blz_repeat():
    # Three A literals, then distance-three runs of eighteen and fifteen bytes.
    # Decodes to 36 A bytes and exercises references to overlapping output.
    return bytes.fromhex("00c000f04141411810000008") + struct.pack("<I", 20)


def ncch(files=None, *, compressed=False, program_id=PROGRAM_ID):
    files = files or [(".code", CODE)]
    payload_size = sum(align(len(data)) for _, data in files)
    exefs = bytearray(0x200 + payload_size)
    relative = 0
    for index, (name, data) in enumerate(files):
        if index >= 10:
            raise ValueError("ExeFS has ten entry slots")
        struct.pack_into("<8sII", exefs, index * 16, name.encode("ascii"), relative, len(data))
        exefs[0x200 + relative:0x200 + relative + len(data)] = data
        hash_offset = 0xC0 + (9 - index) * 0x20
        exefs[hash_offset:hash_offset + 32] = hashlib.sha256(data).digest()
        relative += align(len(data))
    result = bytearray(0xA00 + len(exefs))
    result[0x100:0x104] = b"NCCH"
    struct.pack_into("<I", result, 0x104, len(result) // 0x200)
    struct.pack_into("<Q", result, 0x108, program_id)
    result[0x110:0x112] = b"00"
    struct.pack_into("<H", result, 0x112, 2)
    struct.pack_into("<Q", result, 0x118, program_id)
    result[0x150:0x160] = b"CTR-P-TEST\0\0\0\0\0\0"
    struct.pack_into("<I", result, 0x180, 0x400)
    result[0x18C] = 1  # Old 3DS platform.
    result[0x18D] = 2  # Executable.
    result[0x18F] = 6  # No crypto, no RomFS.
    struct.pack_into("<III", result, 0x1A0, 5, len(exefs) // 0x200, 1)
    result[0x200:0x208] = b"CTRTEST\0"
    result[0x20D] = int(compressed)
    struct.pack_into("<H", result, 0x20E, 1)
    code = next((data for name, data in files if name == ".code"), b"")
    struct.pack_into("<III", result, 0x210, 0x00100000, 1, 36 if compressed else len(code))
    struct.pack_into("<I", result, 0x21C, 0x4000)
    result[0x160:0x180] = hashlib.sha256(result[0x200:0x600]).digest()
    result[0xA00:] = exefs
    rehash_exefs(result)
    return result


def rehash_exefs(data):
    # Caller mutates an ExeFS header for malformed-entry tests, then recomputes
    # the header hash so the parser reaches the desired structural validation.
    unit = 0x200 << data[0x18E]
    base, hashed = struct.unpack_from("<III", data, 0x1A0)[0::2]
    data[0x1C0:0x1E0] = hashlib.sha256(data[base * unit:(base + hashed) * unit]).digest()


def ncsd(partitions=None):
    partitions = partitions or [(0, ncch())]
    result = bytearray(0x200)
    result[0x100:0x104] = b"NCSD"
    for slot, data in partitions:
        start = len(result)
        result.extend(data)
        struct.pack_into("<II", result, 0x120 + slot * 8, start // 0x200, len(data) // 0x200)
    struct.pack_into("<I", result, 0x104, len(result) // 0x200)
    return result


def smdh():
    result = bytearray(0x36C0)
    result[:4] = b"SMDH"
    start = 8 + 0x200
    for offset, title in ((0, "CTR2WUP synthetic"), (0x80, "Container inspection fixture"), (0x180, "CTR2WUP")):
        encoded = title.encode("utf-16le")
        result[start + offset:start + offset + len(encoded)] = encoded
    struct.pack_into("<I", result, 0x2018, 2)
    return bytes(result)


def main(argv):
    if len(argv) != 1:
        raise SystemExit("usage: python -m tests.host_fixtures OUTPUT_DIRECTORY")
    out = Path(argv[0])
    files = {"synthetic.cxi": ncch(), "synthetic.3ds": ncsd(),
             "synthetic-compressed.cxi": ncch([(".code", blz_repeat())], compressed=True)}
    for name, data in files.items():
        path = out / name
        if path.exists() and path.read_bytes() != data:
            raise SystemExit(f"refusing to overwrite an existing different file: {path}")
    out.mkdir(parents=True, exist_ok=True)
    for name, data in files.items():
        if not (out / name).exists():
            (out / name).write_bytes(data)


if __name__ == "__main__":
    main(sys.argv[1:])
