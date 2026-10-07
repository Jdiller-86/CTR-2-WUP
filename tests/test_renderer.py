import struct
import unittest

from renderer.framebuffer import ppm, rgb565_to_rgb8, touch_to_guest, viewport
from renderer.pica import CommandError, decode_commands, execute_draw


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
                     struct.pack("<II", 0, 0x00000300),
                     struct.pack("<IIII", 0, 0x801002FF, 0, 0)]:
            with self.assertRaises(CommandError):
                decode_commands(data)
        with self.assertRaises(CommandError):
            decode_commands(struct.pack("<II", 0, 0), max_writes=0)

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
