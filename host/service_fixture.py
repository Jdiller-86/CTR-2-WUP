"""Original ARM startup probe using TLS and standard SRV/CFG commands."""
import hashlib
import struct
from .guest_fixture import Program

TEXT_ADDRESS = 0x00100000
DATA_ADDRESS = 0x08000000
PROGRAM_ID = 0x000400000FF00F04


def service_code():
    p = Program()
    p.constant(7, DATA_ADDRESS)
    p.emit(0xEE1D6F70)  # mrc p15,0,r6,c13,c0,3: user read-only TLS
    p.emit(0xE5876040)
    p.emit(0xE2866080)
    p.constant(1, DATA_ADDRESS)
    p.emit(0xEF00002D)
    p.emit(0xE5870044)
    p.emit(0xE1A04001)

    def word(index, value):
        p.constant(0, value)
        p.emit(0xE5860000 | index * 4)

    def request(session, transport_at, result_at):
        p.emit(0xE1A00000 | session)
        p.emit(0xEF000032)
        p.emit(0xE5870000 | transport_at)
        p.emit(0xE5960004)
        p.emit(0xE5870000 | result_at)

    word(0, 0x00010002)
    word(1, 0x20)
    word(2, 0)
    request(4, 0x48, 0x4C)

    word(0, 0x00050100)
    word(1, int.from_bytes(b"cfg:", "little"))
    word(2, ord("u"))
    word(3, 5)
    word(4, 1)  # nonblocking directory lookup
    request(4, 0x50, 0x54)
    p.emit(0xE596500C)
    p.emit(0xE5875058)

    word(0, 0x00020000)
    request(5, 0x5C, 0x60)
    p.emit(0xE5960008)
    p.emit(0xE5870064)

    word(0, 0x00050000)
    request(5, 0x68, 0x6C)
    p.emit(0xE5960008)
    p.emit(0xE5870070)

    word(0, 0x00010082)
    word(1, 1)
    word(2, 0x000A0002)
    word(3, 0x1C)
    word(4, DATA_ADDRESS + 0x20)
    request(5, 0x74, 0x78)

    p.emit(0xE1A00005)
    p.emit(0xEF000023)
    p.emit(0xE587007C)
    p.emit(0xE1A00004)
    p.emit(0xEF000023)
    p.emit(0xE5870080)
    p.emit(0xEF000003)
    return p.finish()


def image_code():
    text = service_code()
    if len(text) > 4096:
        raise ValueError("Original service probe exceeds its text page")
    code = bytearray(8192)
    code[:len(text)] = text
    code[4096:4101] = b"srv:\0"
    return bytes(code)


def fixture_bytes():
    code = image_code()
    exefs = bytearray(512 + len(code))
    struct.pack_into("<8sII", exefs, 0, b".code", 0, len(code))
    exefs[0x1E0:0x200] = hashlib.sha256(code).digest()
    exefs[512:] = code
    out = bytearray(0xA00 + len(exefs))
    out[0x100:0x104] = b"NCCH"
    struct.pack_into("<I", out, 0x104, len(out) // 512)
    struct.pack_into("<Q", out, 0x108, PROGRAM_ID)
    struct.pack_into("<Q", out, 0x118, PROGRAM_ID)
    out[0x110:0x112] = b"00"
    struct.pack_into("<H", out, 0x112, 2)
    out[0x150:0x160] = b"CTR-P-SRVTEST".ljust(16, b"\0")
    struct.pack_into("<I", out, 0x180, 0x400)
    out[0x18C], out[0x18D], out[0x18F] = 1, 2, 6
    struct.pack_into("<III", out, 0x1A0, 5, len(exefs) // 512, 1)
    out[0x200:0x208] = b"CTRSRV\0\0"
    struct.pack_into("<III", out, 0x210, TEXT_ADDRESS, 1, len(service_code()))
    struct.pack_into("<I", out, 0x21C, 0x4000)
    struct.pack_into("<III", out, 0x230, DATA_ADDRESS, 1, 4096)
    out[0x450:0x456] = b"cfg:u\0"
    out[0x160:0x180] = hashlib.sha256(out[0x200:0x600]).digest()
    out[0xA00:] = exefs
    out[0x1C0:0x1E0] = hashlib.sha256(exefs[:512]).digest()
    return bytes(out)


def generated_sources():
    from translator.aot import translate
    code = image_code()
    blob = ",\n".join("    " + ",".join(f"0x{b:02x}" for b in code[i:i+32])
                     for i in range(0, len(code), 32))
    metadata = '#include "ctr_service_fixture.h"\n\n'
    metadata += f"static const uint8_t service_image_bytes[] = {{\n{blob}\n}};\n"
    metadata += "const ctr_guest_image ctr_service_image = {\n    service_image_bytes,sizeof(service_image_bytes),\n"
    metadata += f"    {{0x{TEXT_ADDRESS:08x}u,1u,{len(service_code())}u}},{{0,0,0}},{{0x{DATA_ADDRESS:08x}u,1u,4096u}},\n"
    metadata += f'    0,16384u,"{hashlib.sha256(code).hexdigest()}"\n}};\n'
    return {"boot.generated.c": translate(service_code(), TEXT_ADDRESS, "ctr_service_fixture", False),
            "boot-image.generated.c": metadata}
