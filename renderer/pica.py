"""Bounded, little-endian PICA command inspection, with no draw execution.

The wire header has a 16-bit command ID, four byte-mask bits, an eight-bit
extra-parameter count (bits 20--27), unused bits 28--30 and an incremental bit.
GBATEK documents IDs 0x000--0x3ff; libctru's GPUCMD_HEADER masks IDs to ten bits
and its GPUCMD_Add splits writes into at most 256 parameters per record.
Unused header bits and IDs outside that range are rejected. Padding words are
skipped by this structural inspector; their contents do not become writes.
This module is original code; it does not embed an emulator implementation.

Wire-format sources:
https://problemkaputt.de/gbatek-3ds-gpu-internal-registers-command-lists.htm
devkitPro/libctru 9b55eda44cf80b971503991e0c78f9bc8fe50425,
include/3ds/gpu/gpu.h and source/gpu/gpu.c.
"""

from dataclasses import dataclass
import struct


PICA_REGISTER_COUNT = 0x400
PICA_MAX_PARAMETERS = 0x100
DEFAULT_MAX_WRITES = 65536
DEFAULT_MAX_BYTES = 4 * 1024 * 1024


class CommandError(ValueError):
    pass


@dataclass(frozen=True)
class RegisterWrite:
    byte_offset: int
    register: int
    byte_mask: int
    value: int

    def merge(self, previous: int) -> int:
        mask = sum(0xFF << (8 * i) for i in range(4) if self.byte_mask & (1 << i))
        return ((previous & ~mask) | (self.value & mask)) & 0xFFFFFFFF


def decode_commands(data: bytes, *, max_writes: int = DEFAULT_MAX_WRITES,
                    max_bytes: int = DEFAULT_MAX_BYTES) -> list[RegisterWrite]:
    """Inspect a complete word-aligned stream; reject truncation and unknown headers.

    Each record consists of a parameter, header, extra parameters, and padding
    to an eight-byte boundary. Consecutive writes advance the register index;
    repeated writes retain it. The ID range is the structural wire range; an ID
    in that range is not a promise that its register or side effects are known.
    Padding is ignored, and FINALIZE or draw triggers remain ordinary inspection
    results. No GPU state is declared executable by this API. Byte and write
    budgets bound inspection before result allocation.
    """
    for name, limit, ceiling in (("write", max_writes, DEFAULT_MAX_WRITES),
                                 ("byte", max_bytes, DEFAULT_MAX_BYTES)):
        if isinstance(limit, bool) or not isinstance(limit, int) or limit < 0:
            raise CommandError(f"{name} limit must be a non-negative integer")
        if limit > ceiling:
            raise CommandError(f"{name} limit exceeds the supported budget")
    if len(data) > max_bytes:
        raise CommandError("command byte limit exceeded")
    if len(data) % 8:
        raise CommandError("command stream must include eight-byte record alignment")
    writes: list[RegisterWrite] = []
    offset = 0
    while offset < len(data):
        value, header = struct.unpack_from("<II", data, offset)
        if header & 0x70000000:
            raise CommandError(f"unsupported extended header at byte {offset + 4}")
        register = header & 0xFFFF
        mask = (header >> 16) & 0xF
        count = 1 + ((header >> 20) & 0xFF)
        grouped = bool(header & 0x80000000)
        end = offset + 8 + (count - 1) * 4
        next_offset = (end + 7) & ~7
        if next_offset > len(data):
            raise CommandError(f"truncated parameters or padding at byte {offset}")
        if len(writes) + count > max_writes:
            raise CommandError("command write limit exceeded")
        if register >= PICA_REGISTER_COUNT or (grouped and register + count > PICA_REGISTER_COUNT):
            raise CommandError(f"register range outside PICA wire IDs at byte {offset}")
        for i in range(count):
            parameter = value if i == 0 else struct.unpack_from("<I", data, offset + 4 + 4 * i)[0]
            writes.append(RegisterWrite(offset, register + i if grouped else register, mask, parameter))
        offset = next_offset
    return writes


def execute_draw(*_args, **_kwargs):
    raise NotImplementedError("PICA vertex/shader/texture/TEV translation and GX2 submission are unavailable")
