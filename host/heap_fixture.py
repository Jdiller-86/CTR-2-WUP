"""Original ARM program using real heap/linear ControlMemory and QueryMemory."""
import hashlib
import struct
from .guest_fixture import Program

TEXT_ADDRESS = 0x00100000
DATA_ADDRESS = 0x08000000
PROGRAM_ID = 0x000400000FF00F05


def heap_code():
    p = Program()
    p.constant(7, DATA_ADDRESS)

    def control(operation, address_register, offset, size, permission, report):
        p.constant(0, operation)
        if address_register is None:
            p.constant(1, offset)
        else:
            p.emit(0xE1A01000 | address_register)
            if offset:
                from .guest_fixture import immediate
                p.emit(0xE2811000 | immediate(offset))
        p.constant(2, 0)
        p.constant(3, size)
        p.constant(4, permission)
        p.emit(0xEF000001)
        p.emit(0xE5870000 | report)

    def query(register, offset, report):
        p.emit(0xE1A02000 | register)
        if offset:
            from .guest_fixture import immediate
            p.emit(0xE2822000 | immediate(offset))
        p.emit(0xEF000002)
        for r in range(6):
            p.emit(0xE5870000 | r << 12 | (report + r * 4))

    control(3, None, 0, 0x3000, 3, 0)
    p.emit(0xE1A08001)
    p.emit(0xE5871004)
    p.constant(0, 0x78563412)
    p.emit(0xE5880000)
    p.emit(0xE288BA01)  # add r11,r8,#4096
    p.emit(0xE58B0000)
    p.emit(0xE28BBA01)  # add r11,r11,#4096
    p.emit(0xE58B0000)
    query(8, 0x1000, 0x10)
    control(6, 8, 0x1000, 0x1000, 1, 0x30)
    query(8, 0x1000, 0x40)
    p.emit(0xE288BA01)
    p.emit(0xE59B0000)
    p.emit(0xE5870060)
    control(6, 8, 0x1000, 0x1000, 3, 0x64)
    control(1, 8, 0x1000, 0x1000, 0, 0x68)
    query(8, 0x1000, 0x70)
    control(3, None, 0, 0x1000, 3, 0x90)
    p.emit(0xE1A0A001)
    p.emit(0xE5871094)
    p.emit(0xE59A0000)
    p.emit(0xE5870098)
    control(0x10003, None, 0, 0x2000, 3, 0x9C)
    p.emit(0xE1A09001)
    p.emit(0xE58710A0)
    p.constant(0, 0x44332211)
    p.emit(0xE5890000)
    p.emit(0xE5990000)
    p.emit(0xE58700A4)
    query(9, 0x1000, 0xB0)
    control(1, 8, 0, 0x1000, 0, 0xD0)
    control(1, 10, 0, 0x1000, 0, 0xD4)
    control(1, 8, 0x2000, 0x1000, 0, 0xD8)
    control(1, 9, 0, 0x2000, 0, 0xDC)
    p.emit(0xEF000003)
    return p.finish()


def image_code():
    code = bytearray(8192)
    text = heap_code()
    if len(text) > 4096:
        raise ValueError("Heap probe exceeds its original text page")
    code[:len(text)] = text
    return bytes(code)


def fixture_bytes():
    # The container layout is independently validated by the existing loader.
    from .service_fixture import fixture_bytes as service_fixture_bytes
    out = bytearray(service_fixture_bytes())
    code = image_code()
    struct.pack_into("<Q", out, 0x108, PROGRAM_ID)
    struct.pack_into("<Q", out, 0x118, PROGRAM_ID)
    out[0x150:0x160] = b"CTR-P-HEAPTEST".ljust(16, b"\0")
    out[0x200:0x208] = b"CTRHEAP\0"
    struct.pack_into("<I", out, 0x218, len(heap_code()))
    out[0x450:0x458] = bytes(8)
    out[0xC00:] = code
    out[0xBE0:0xC00] = hashlib.sha256(code).digest()
    out[0x160:0x180] = hashlib.sha256(out[0x200:0x600]).digest()
    out[0x1C0:0x1E0] = hashlib.sha256(out[0xA00:0xC00]).digest()
    return bytes(out)


def generated_sources():
    from translator.aot import translate
    code = image_code()
    blob = ",\n".join("    " + ",".join(f"0x{b:02x}" for b in code[i:i+32])
                     for i in range(0, len(code), 32))
    metadata = '#include "ctr_heap_fixture.h"\n\n'
    metadata += f"static const uint8_t heap_image_bytes[] = {{\n{blob}\n}};\n"
    metadata += "const ctr_guest_image ctr_heap_image = {\n    heap_image_bytes,sizeof(heap_image_bytes),\n"
    metadata += f"    {{0x{TEXT_ADDRESS:08x}u,1u,{len(heap_code())}u}},{{0,0,0}},{{0x{DATA_ADDRESS:08x}u,1u,4096u}},\n"
    metadata += f'    0,16384u,"{hashlib.sha256(code).hexdigest()}"\n}};\n'
    return {"heap.generated.c": translate(heap_code(), TEXT_ADDRESS, "ctr_heap_fixture", False),
            "heap-image.generated.c": metadata}
