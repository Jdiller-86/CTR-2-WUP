"""Bounded, little-endian PICA command inspection, with no draw execution.

The documented basic command format uses an eight-bit extra-parameter count.
Extended header bits are rejected rather than interpreted speculatively.
This module is original code; it does not embed an emulator implementation.
"""

from dataclasses import dataclass
import struct


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


def decode_commands(data: bytes, *, max_writes: int = 65536) -> list[RegisterWrite]:
    """Inspect a complete word-aligned stream; reject truncation and unknown headers.

    Each record consists of a parameter, header, extra parameters, and padding
    to an eight-byte boundary. Consecutive writes advance the register index;
    repeated writes retain it. No GPU state is declared executable by this API.
    """
    if len(data) % 8:
        raise CommandError("command stream must include eight-byte record alignment")
    if max_writes < 0:
        raise CommandError("negative write limit")
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
        if register >= 0x300 or (grouped and register + count > 0x300):
            raise CommandError(f"register range outside inspected PICA bank at byte {offset}")
        for i in range(count):
            parameter = value if i == 0 else struct.unpack_from("<I", data, offset + 4 + 4 * i)[0]
            writes.append(RegisterWrite(offset, register + i if grouped else register, mask, parameter))
        offset = next_offset
    return writes


def execute_draw(*_args, **_kwargs):
    raise NotImplementedError("PICA vertex/shader/texture/TEV translation and GX2 submission are unavailable")
