"""Original ARM shared-memory and synchronization probe; no title data."""
import hashlib
import struct
from .guest_fixture import Program

TEXT_ADDRESS = 0x00100000
DATA_ADDRESS = 0x08000000
PROGRAM_ID = 0x000400000FF00F06


def kernel_program():
    p = Program()
    expected = []
    p.constant(7, DATA_ADDRESS)

    def svc(number, value=0):
        p.emit(0xEF000000 | number)
        p.emit(0xE5870000 | len(expected) * 4)
        expected.append(value)

    def move(out, source):
        p.emit(0xE1A00000 | out << 12 | source)

    def wait(register, result=0):
        move(0, register)
        p.constant(2, 0)
        p.constant(3, 0)
        svc(0x24, result)

    def close(register):
        move(0, register)
        svc(0x23)

    p.constant(0, 3)
    p.constant(1, 0)
    p.constant(2, 0)
    p.constant(3, 4096)
    p.constant(4, 3)
    svc(1)
    move(8, 1)
    p.constant(0, 0x44332211)
    p.emit(0xE5880000)
    p.constant(0, 3)
    move(1, 8)
    p.constant(2, 4096)
    p.constant(3, 3)
    svc(0x1E)
    move(5, 1)
    move(0, 5)
    p.constant(1, 0x10001000)
    p.constant(2, 1)
    p.constant(3, 0x10000000)
    svc(0x1F)
    p.constant(10, 0x10001000)
    p.emit(0xE59A0000)
    p.emit(0xE5870000 | len(expected) * 4)
    expected.append(0x44332211)
    move(1, 5)
    svc(0x27)
    move(9, 1)
    close(5)
    p.constant(1, 1)
    svc(0x13)
    move(11, 1)
    wait(11)
    move(0, 11)
    svc(0x14)
    move(0, 11)
    svc(0x14)
    wait(11)
    move(0, 11)
    svc(0x14)
    p.constant(1, 2)
    p.constant(2, 3)
    svc(0x15)
    move(12, 1)
    wait(12)
    wait(12)
    wait(12, 0x09401BFE)
    move(1, 12)
    p.constant(2, 1)
    svc(0x16)
    p.constant(1, 0)
    svc(0x17)
    move(6, 1)
    p.emit(0xE587C200)
    p.emit(0xE5876204)

    def wait_many(index):
        p.constant(0, 0)
        p.constant(1, DATA_ADDRESS + 0x200)
        p.constant(2, 2)
        p.constant(3, 0)
        p.constant(4, 0)
        svc(0x25)
        p.emit(0xE5871000 | len(expected) * 4)
        expected.append(index)

    wait_many(0)
    move(0, 6)
    svc(0x18)
    wait_many(1)
    move(0, 9)
    p.constant(1, 0x10001000)
    svc(0x20)
    close(9)
    p.constant(0, 1)
    move(1, 8)
    p.constant(2, 0)
    p.constant(3, 4096)
    p.constant(4, 0)
    svc(1)
    close(11)
    close(12)
    close(6)
    p.emit(0xEF000003)
    return p.finish(), expected


def image_code():
    code = bytearray(8192)
    text, _ = kernel_program()
    code[:len(text)] = text
    return bytes(code)


def fixture_bytes():
    from .heap_fixture import fixture_bytes as template
    out = bytearray(template())
    code = image_code()
    for at in (0x108, 0x118):
        struct.pack_into("<Q", out, at, PROGRAM_ID)
    out[0x150:0x160] = b"CTR-P-KERNELTEST".ljust(16, b"\0")
    out[0x200:0x208] = b"CTRKERN\0"
    struct.pack_into("<I", out, 0x218, len(kernel_program()[0]))
    out[0xC00:] = code
    out[0xBE0:0xC00] = hashlib.sha256(code).digest()
    out[0x160:0x180] = hashlib.sha256(out[0x200:0x600]).digest()
    out[0x1C0:0x1E0] = hashlib.sha256(out[0xA00:0xC00]).digest()
    return bytes(out)


def generated_sources():
    from translator.aot import translate
    code = image_code()
    text, expected = kernel_program()
    blob = ",\n".join("    " + ",".join(f"0x{b:02x}" for b in code[i:i+32])
                     for i in range(0, len(code), 32))
    metadata = '#include "ctr_kernel_fixture.h"\n'
    metadata += f"static const uint8_t kernel_image_bytes[] = {{\n{blob}\n}};\n"
    metadata += "const uint32_t ctr_kernel_expected[] = {" + ",".join(f"0x{n:08x}u" for n in expected) + "};\n"
    metadata += f"const size_t ctr_kernel_expected_count = {len(expected)};\n"
    metadata += "const ctr_guest_image ctr_kernel_image = {\n    kernel_image_bytes,sizeof(kernel_image_bytes),\n"
    metadata += f"    {{0x{TEXT_ADDRESS:08x}u,1u,{len(text)}u}},{{0,0,0}},{{0x{DATA_ADDRESS:08x}u,1u,4096u}},\n"
    metadata += f'    0,16384u,"{hashlib.sha256(code).hexdigest()}"\n}};\n'
    return {"kernel.generated.c": translate(text, TEXT_ADDRESS, "ctr_kernel_fixture", False),
            "kernel-image.generated.c": metadata}
