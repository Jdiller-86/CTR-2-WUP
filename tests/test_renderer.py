import struct
import unittest

from renderer.framebuffer import ppm, rgb565_to_rgb8, touch_to_guest, viewport
from renderer.pica import (
    DEFAULT_MAX_BYTES, PICA_MAX_PARAMETERS, PICA_REGISTER_COUNT, CommandError,
    decode_commands, execute_draw,
)


def command_record(register, parameters, *, mask=15, incremental=False, padding=0):
    """Original wire fixture builder, separate from the inspected implementation."""
    if not 1 <= len(parameters) <= 256:
        raise ValueError("fixture record parameter count")
    header = register + mask * 65536 + (len(parameters) - 1) * 1048576
    if incremental:
        header += 2147483648
    words = [parameters[0], header, *parameters[1:]]
    if len(words) % 2:
        words.append(padding)
    return b"".join(word.to_bytes(4, "little") for word in words)


def builder_commands(register, parameters, *, mask=15, incremental=False):
    """Exercise the audited libctru chunking contract without importing its code."""
    chunks = []
    for start in range(0, len(parameters), 256):
        chunks.append(command_record(register + start if incremental else register,
                                     parameters[start:start + 256], mask=mask,
                                     incremental=incremental))
    return b"".join(chunks)


class PicaTests(unittest.TestCase):
    def test_mask_and_little_endian(self):
        writes = decode_commands(struct.pack("<II", 0x12345678, 0x00050010))
        self.assertEqual((writes[0].register, writes[0].byte_mask), (0x10, 5))
        self.assertEqual(writes[0].merge(0xAABBCCDD), 0xAA34CC78)

    def test_grouped_and_repeated_records(self):
        for grouped, expected in [(0, [0x20, 0x20]), (1, [0x20, 0x21])]:
            data = struct.pack("<IIIIII", 1, (grouped << 31) | 0x001F0020, 2, 0xDEADBEEF, 3, 0x000F0022)
            writes = decode_commands(data)
            self.assertEqual([w.register for w in writes], expected + [0x22])
            self.assertEqual([w.value for w in writes], [1, 2, 3])

    def test_reject_malformed_and_limits(self):
        for data in [b"x", struct.pack("<II", 0, 0x10000000),
                     struct.pack("<II", 0, 0x00100010),
                     struct.pack("<II", 0, 0x00000400),
                     struct.pack("<IIII", 0, 0x801003FF, 0, 0)]:
            with self.assertRaises(CommandError):
                decode_commands(data)
        with self.assertRaises(CommandError):
            decode_commands(struct.pack("<II", 0, 0), max_writes=0)

    def test_all_byte_masks_preserve_unselected_bytes(self):
        previous = 0xAABBCCDD
        replacement = 0x12345678
        before = previous.to_bytes(4, "little")
        after = replacement.to_bytes(4, "little")
        for mask in range(16):
            with self.subTest(mask=mask):
                write, = decode_commands(command_record(0x41, [replacement], mask=mask))
                expected = bytes(after[i] if mask & (1 << i) else before[i] for i in range(4))
                self.assertEqual(write.byte_mask, mask)
                self.assertEqual(write.merge(previous).to_bytes(4, "little"), expected)

    def test_wire_id_boundary_is_separate_from_register_execution(self):
        self.assertEqual(PICA_REGISTER_COUNT, 1024)
        for register in (0, 0x2FF, 0x300, 0x3FE, 0x3FF):
            for incremental in (False, True):
                with self.subTest(register=register, incremental=incremental):
                    write, = decode_commands(command_record(register, [0xDEADBEEF],
                                                            incremental=incremental))
                    self.assertEqual((write.register, write.value), (register, 0xDEADBEEF))
        for register in (0x400, 0x7FF, 0xFFFF):
            with self.subTest(register=register), self.assertRaises(CommandError):
                decode_commands(command_record(register, [0]))
        writes = decode_commands(command_record(0x3FE, [1, 2], incremental=True))
        self.assertEqual([w.register for w in writes], [0x3FE, 0x3FF])
        with self.assertRaises(CommandError):
            decode_commands(command_record(0x3FF, [1, 2], incremental=True))

    def test_maximum_parameter_count_and_grouped_bank_end(self):
        self.assertEqual(PICA_MAX_PARAMETERS, 256)
        parameters = [0x12340000 + i for i in range(256)]
        for incremental, register in ((False, 0x3FF), (True, 0x300)):
            with self.subTest(incremental=incremental):
                data = command_record(register, parameters, incremental=incremental)
                self.assertEqual((int.from_bytes(data[4:8], "little") >> 20) & 255, 255)
                writes = decode_commands(data, max_writes=256)
                expected_registers = list(range(0x300, 0x400)) if incremental else [0x3FF] * 256
                self.assertEqual([w.register for w in writes], expected_registers)
                self.assertEqual([w.value for w in writes], parameters)
                with self.assertRaises(CommandError):
                    decode_commands(data, max_writes=255)
        with self.assertRaises(CommandError):
            decode_commands(command_record(0x301, parameters, incremental=True))

    def test_builder_chunking_above_one_record(self):
        for count in (255, 256, 257, 511, 512, 768):
            parameters = [i * 7919 for i in range(count)]
            for incremental in (False, True):
                with self.subTest(count=count, incremental=incremental):
                    data = builder_commands(0x40, parameters, mask=9, incremental=incremental)
                    writes = decode_commands(data)
                    self.assertEqual([w.value for w in writes], parameters)
                    expected = list(range(0x40, 0x40 + count)) if incremental else [0x40] * count
                    self.assertEqual([w.register for w in writes], expected)
                    self.assertEqual([w.byte_mask for w in writes], [9] * count)
                    offset = 0
                    expected_offsets = []
                    for start in range(0, count, 256):
                        chunk_count = min(256, count - start)
                        expected_offsets.extend([offset] * chunk_count)
                        size = 4 * (chunk_count + 1)
                        offset += size + size % 8
                    self.assertEqual([w.byte_offset for w in writes], expected_offsets)
                    self.assertEqual(offset, len(data))

    def test_parameter_parity_padding_and_record_offsets(self):
        for count in (1, 2, 3, 4, 255, 256):
            with self.subTest(count=count):
                first = command_record(0x41, list(range(count)), padding=0xF00DBAAD)
                second = command_record(0x3FF, [0x11223344])
                writes = decode_commands(first + second)
                self.assertEqual([w.value for w in writes], list(range(count)) + [0x11223344])
                self.assertEqual([w.byte_offset for w in writes], [0] * count + [len(first)])
                self.assertNotIn(0xF00DBAAD, [w.value for w in writes])

    def test_truncation_of_parameters_and_required_padding(self):
        for count in (2, 3, 4, 255, 256):
            valid = command_record(0x41, [0x11000000 + i for i in range(count)])
            for cut in range(1, len(valid)):
                with self.subTest(count=count, cut=cut), self.assertRaises(CommandError):
                    decode_commands(valid[:cut])
        valid = command_record(0x41, [1])
        truncated_second = struct.pack("<II", 2, 0x002F0042)
        with self.assertRaisesRegex(CommandError, "byte 8"):
            decode_commands(valid + truncated_second)

    def test_reserved_header_bits_are_not_extra_parameters(self):
        # Bits28--30 are unused, not an extension of the eight-bit count.
        for reserved in (1 << 28, 1 << 29, 1 << 30, 0x70000000):
            for incremental in (False, True):
                with self.subTest(reserved=reserved, incremental=incremental):
                    header = 0x000F0041 | reserved | (0x80000000 if incremental else 0)
                    with self.assertRaisesRegex(CommandError, "header at byte 4"):
                        decode_commands(struct.pack("<II", 0, header))
        extended_after_valid = command_record(0x41, [7]) + struct.pack("<II", 0, 0x100F0042)
        with self.assertRaisesRegex(CommandError, "header at byte 12"):
            decode_commands(extended_after_valid)

    def test_byte_and_write_budgets_are_independent(self):
        single = command_record(0x41, [1])
        repeated = command_record(0x41, [1, 2])
        self.assertEqual(len(decode_commands(single, max_bytes=8, max_writes=1)), 1)
        self.assertEqual(len(decode_commands(repeated, max_bytes=16, max_writes=2)), 2)
        with self.assertRaisesRegex(CommandError, "byte limit"):
            decode_commands(repeated, max_bytes=8, max_writes=2)
        with self.assertRaisesRegex(CommandError, "write limit"):
            decode_commands(repeated, max_bytes=16, max_writes=1)
        self.assertEqual(decode_commands(b"", max_bytes=0, max_writes=0), [])
        with self.assertRaisesRegex(CommandError, "byte limit"):
            decode_commands(b"\0" * (DEFAULT_MAX_BYTES + 8))

    def test_limits_require_non_negative_integers(self):
        for bad in (-1, 0.5, float("nan"), float("inf"), True, False, "1", None):
            for name in ("max_bytes", "max_writes"):
                with self.subTest(name=name, limit=bad), self.assertRaises(CommandError):
                    decode_commands(b"", **{name: bad})

    def test_finalize_is_inspected_without_simulating_interrupt_or_autostop(self):
        canonical = command_record(0x10, [0x12345678])
        # Split emits one/two finalizers for16-byte alignment; inspection sees
        # bytes only and never substitutes for executable IRQ semantics.
        for suffix in (canonical, canonical * 2,
                       canonical + command_record(0x41, [0x00ABCDEF])):
            writes = decode_commands(suffix)
            self.assertEqual((writes[0].register, writes[0].value), (0x10, 0x12345678))
            self.assertEqual(len(writes), len(suffix) // 8)

    def test_draw_unavailable(self):
        with self.assertRaises(NotImplementedError):
            execute_draw()


class FramebufferTests(unittest.TestCase):
    def test_endian_and_color_extremes(self):
        self.assertEqual(rgb565_to_rgb8(b"\x00\xf8\xe0\x07\x1f\x00\xff\xff"),
                         b"\xff\0\0\0\xff\0\0\0\xff\xff\xff\xff")
        with self.assertRaises(ValueError):
            rgb565_to_rgb8(b"x")

    def test_aspect_ratio(self):
        self.assertEqual(viewport(1280, 720, 400, 240), (40, 0, 1200, 720))
        self.assertEqual(viewport(854, 480, 320, 240), (107, 0, 640, 480))
        with self.assertRaises(ValueError):
            viewport(0, 480, 320, 240)

    def test_touch_edges(self):
        bounds = viewport(854, 480, 320, 240)
        self.assertIsNone(touch_to_guest(106, 120, bounds, 320, 240))
        self.assertIsNone(touch_to_guest(747, 120, bounds, 320, 240))
        self.assertEqual(touch_to_guest(107, 0, bounds, 320, 240), (0, 0))
        self.assertEqual(touch_to_guest(746.9, 479.9, bounds, 320, 240), (319, 239))

    def test_ppm(self):
        self.assertEqual(ppm(b"\xff\0\0", 1, 1), b"P6\n1 1\n255\n\xff\0\0")
        with self.assertRaises(ValueError):
            ppm(b"", 1, 1)


if __name__ == "__main__":
    unittest.main()
