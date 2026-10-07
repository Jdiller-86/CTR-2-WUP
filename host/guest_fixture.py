"""Original NCCH ARM guest fixture; explicit harness I/O, no commercial bytes.

This executes genuine ARM writes and standard SVCs. The input/framebuffer memory
contract belongs to the test harness; it does not impersonate HID/GSP services.
"""
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import shutil

from .atomic import publish_directory
from .errors import HostError

TEXT_ADDRESS = 0x00100000
DATA_ADDRESS = 0x08000000
TOP_ADDRESS = DATA_ADDRESS + 4096
BOTTOM_ADDRESS = TOP_ADDRESS + 400 * 240 * 4
BSS_SIZE = (400 + 320) * 240 * 4
PROGRAM_ID = 0x000400000FF00F02
DEBUG_MESSAGE = b"CTR2WUP ARM guest framebuffer fixture"


def immediate(value):
    for rotation in range(16):
        shift = rotation * 2
        candidate = value if not shift else ((value << shift) | (value >> (32-shift))) & 0xFFFFFFFF
        if candidate <= 255:
            return (rotation << 8) | candidate
    raise ValueError(f"ARM rotated immediate cannot encode {value:#x}")


class Program:
    def __init__(self):
        self.words, self.labels, self.branches = [], {}, []

    def emit(self, value):
        self.words.append(value)

    def constant(self, register, value):
        self.emit(0xE3A00000 | register << 12 | (value & 255))
        for shift in (8, 16, 24):
            part = value & (255 << shift)
            if part:
                self.emit(0xE3800000 | register << 16 | register << 12 | immediate(part))

    def label(self, name):
        self.labels[name] = len(self.words)

    def branch(self, name, condition=14):
        self.branches.append((len(self.words), name, condition))
        self.emit(0)

    def finish(self):
        for index, name, condition in self.branches:
            offset = self.labels[name] - index - 2
            self.words[index] = condition << 28 | 0x0A000000 | (offset & 0xFFFFFF)
        return struct.pack("<" + "I" * len(self.words), *self.words)


def guest_code():
    p = Program()
    p.constant(0, DATA_ADDRESS + 32)
    p.constant(1, len(DEBUG_MESSAGE)); p.emit(0xEF00003D)
    p.constant(3, DATA_ADDRESS)
    p.label("frame")
    p.emit(0xE5930014); p.emit(0xE3500000); p.branch("exit", 1)
    p.emit(0xE5930000); p.emit(0xE3100001); p.branch("normal_top", 0)
    p.constant(0, 0xFF22BB10); p.branch("top")
    p.label("normal_top"); p.constant(0, 0xFFC03010)
    p.label("top"); p.constant(4, TOP_ADDRESS); p.constant(5, 400*240)
    p.label("top_pixel")
    p.emit(0xE5840000); p.emit(0xE2844004); p.emit(0xE2555001); p.branch("top_pixel", 1)
    p.emit(0xE593000C); p.emit(0xE3500000); p.branch("normal_bottom", 0)
    p.constant(0, 0xFFBB2277); p.branch("bottom")
    p.label("normal_bottom"); p.constant(0, 0xFF775522)
    p.label("bottom"); p.constant(4, BOTTOM_ADDRESS); p.constant(5, 320*240)
    p.label("bottom_pixel")
    p.emit(0xE5840000); p.emit(0xE2844004); p.emit(0xE2555001); p.branch("bottom_pixel", 1)
    p.emit(0xE593000C); p.emit(0xE3500000); p.branch("done", 0)
    p.emit(0xE5936004); p.emit(0xE3560000 | immediate(320)); p.branch("done", 2)
    p.emit(0xE5937008); p.emit(0xE3570000 | immediate(240)); p.branch("done", 2)
    p.constant(4, BOTTOM_ADDRESS)
    p.emit(0xE0844507)  # add r4,r4,r7,lsl #10 (y*1024)
    p.emit(0xE0844407)  # add r4,r4,r7,lsl #8  (y*256)
    p.emit(0xE0844106)  # add r4,r4,r6,lsl #2  (x*4)
    p.constant(0, 0xFFFFFFFF); p.emit(0xE5840000)
    p.label("done")
    p.emit(0xE5930010); p.emit(0xE2800001); p.emit(0xE5830010)
    p.constant(0, 1000000); p.constant(1, 0); p.emit(0xEF00000A); p.branch("frame")
    p.label("exit"); p.emit(0xEF000003)
    return p.finish()


def fixture_bytes():
    text = guest_code()
    code = bytearray(8192); code[:len(text)] = text
    code[4096+32:4096+32+len(DEBUG_MESSAGE)] = DEBUG_MESSAGE
    exefs = bytearray(512+len(code))
    struct.pack_into("<8sII", exefs, 0, b".code", 0, len(code))
    exefs[0x1E0:0x200] = hashlib.sha256(code).digest(); exefs[512:] = code
    result = bytearray(0xA00+len(exefs))
    result[0x100:0x104] = b"NCCH"
    struct.pack_into("<I", result, 0x104, len(result)//512)
    struct.pack_into("<Q", result, 0x108, PROGRAM_ID); struct.pack_into("<Q", result, 0x118, PROGRAM_ID)
    result[0x110:0x112] = b"00"; struct.pack_into("<H", result, 0x112, 2)
    result[0x150:0x160] = b"CTR-P-CTR2TEST".ljust(16, b"\0")
    struct.pack_into("<I", result, 0x180, 0x400)
    result[0x18C], result[0x18D], result[0x18F] = 1, 2, 6
    struct.pack_into("<III", result, 0x1A0, 5, len(exefs)//512, 1)
    result[0x200:0x208] = b"CTRGUEST"
    struct.pack_into("<III", result, 0x210, TEXT_ADDRESS, 1, len(text))
    struct.pack_into("<I", result, 0x21C, 0x4000)
    struct.pack_into("<III", result, 0x230, DATA_ADDRESS, 1, 4096)
    struct.pack_into("<I", result, 0x23C, BSS_SIZE)
    result[0x160:0x180] = hashlib.sha256(result[0x200:0x600]).digest()
    result[0xA00:] = exefs
    result[0x1C0:0x1E0] = hashlib.sha256(exefs[:512]).digest()
    return bytes(result)


def create(output):
    output = Path(output).absolute()
    if output.exists():
        raise HostError("output_exists", "Fixture output already exists; choose a new directory", stage="fixture")
    output.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=".ctr2wup-fixture-", dir=output.parent))
    try:
        data = fixture_bytes()
        (staging / "ctr2wup-guest.cxi").write_bytes(data)
        report = {"schema_version": 1, "ok": True, "profile": "ctr2wup-framebuffer-fixture-v1",
                  "sha256": hashlib.sha256(data).hexdigest(), "original_code": True,
                  "stock_3ds_graphics_compatible": False, "console_verified": False,
                  "io_contract": "host-injected input and raw RGBA8 framebuffers; HID/GSP/PICA not implemented"}
        (staging / "fixture-report.json").write_text(json.dumps(report, indent=2)+"\n")
        publish_directory(staging, output)
        return {**report, "output": str(output)}
    finally:
        if staging.exists(): shutil.rmtree(staging)
