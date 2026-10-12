"""Original deterministic command parameters, independently specified layouts.

The libctru builder oracle chunks records at 256 parameters. Its output cannot
prove the interpretation of larger raw records, register meaning, or rendering.
"""

from dataclasses import dataclass
import struct


@dataclass(frozen=True)
class BuilderVector:
    name: str
    register: int
    mask: int
    grouped: bool
    parameters: tuple[int, ...]
    api_count: int
    finalize: bool = False

    @property
    def header(self) -> int:
        return self.register | (self.mask << 16) | (int(self.grouped) << 31)

    @property
    def source(self) -> bytes:
        return struct.pack('<' + 'I' * len(self.parameters), *self.parameters)


def parameter_values(count: int, seed: int) -> tuple[int, ...]:
    """Easy-to-identify asymmetric byte patterns; does not use an emulator."""
    return tuple(((seed * 0x0103070B) ^ (i * 0x9E3779B9) ^ 0xD4C3B2A1) & 0xFFFFFFFF
                 for i in range(count))


def builder_vectors() -> list[BuilderVector]:
    result = []
    for mask in range(16):
        for count in (1, 2, 3, 255, 256, 257, 511, 512, 513, 2048):
            result.append(BuilderVector(
                f'repeat-mask-{mask:01x}-count-{count}', 0x141, mask, False,
                parameter_values(count, mask + count), count))
        for count in (1, 2, 3, 255, 256, 257, 511, 512, 513, 769):
            result.append(BuilderVector(
                f'group-mask-{mask:01x}-count-{count}', 0x40, mask, True,
                parameter_values(count, mask + count + 19), count))
    result.append(BuilderVector('api-zero-means-one', 0x41, 15, False,
                                (0x12345678,), 0))
    for count in (1, 2, 3, 256, 257):
        result.append(BuilderVector(
            f'finalize-count-{count}', 0x41, 15, False,
            parameter_values(count, count + 71), count, True))
    return result


def expected_layout(vector: BuilderVector) -> tuple[bytes, list[list[int]]]:
    """Independent record layout and write expectations for GPUCMD_Add/Split.

    Write rows: [record byte offset, register, byte mask, parameter value].
    Padding bytes are construction expectations, not hardware-required zeroes.
    """
    words: list[int] = []
    writes: list[list[int]] = []
    register = vector.register
    count = max(vector.api_count, 1)
    for first in range(0, count, 256):
        parameters = vector.parameters[first:first + 256]
        offset = len(words) * 4
        header = register | (vector.mask << 16) | ((len(parameters) - 1) << 20)
        if vector.grouped:
            header |= 1 << 31
        words.extend((parameters[0], header, *parameters[1:]))
        if len(parameters) % 2 == 0:
            words.append(0)
        writes.extend([offset, register + i if vector.grouped else register,
                       vector.mask, value] for i, value in enumerate(parameters))
        if vector.grouped:
            register += len(parameters)
    if vector.finalize:
        words.extend((0x12345678, 0x000F0010))
        writes.append([(len(words) - 2) * 4, 0x10, 15, 0x12345678])
        if len(words) % 4:
            words.extend((0x12345678, 0x000F0010))
            writes.append([(len(words) - 2) * 4, 0x10, 15, 0x12345678])
    return struct.pack('<' + 'I' * len(words), *words), writes


def raw_record(register: int, mask: int, values: tuple[int, ...],
               *, grouped: bool = False, padding: int = 0) -> bytes:
    """Construct one original record, optionally exercising the full count."""
    if not values or len(values) > 2048:
        raise ValueError('one record requires 1..2048 parameters')
    header = register | (mask << 16) | ((len(values) - 1) << 20)
    if grouped:
        header |= 1 << 31
    words = [values[0], header, *values[1:]]
    if len(values) % 2 == 0:
        words.append(padding)
    return struct.pack('<' + 'I' * len(words), *words)
