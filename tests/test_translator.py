from pathlib import Path
import struct
import unittest
from translator.aot import translate


class TranslatorTests(unittest.TestCase):
    def test_fixture_reproducibility(self):
        root = Path(__file__).parent / "fixtures" / "cpu"
        for source, output, base, symbol, thumb in [
            ("arithmetic.arm.bin", "arithmetic.generated.c", 0x1000, "ctr_arm_fixture", False),
            ("exit.thumb.bin", "exit.generated.c", 0x1100, "ctr_thumb_fixture", True),
        ]:
            self.assertEqual(translate((root / source).read_bytes(), base, symbol, thumb), (root / output).read_text())

    def test_reject_unsupported_vfp(self):
        with self.assertRaisesRegex(ValueError, "0x00001000 ARM instruction"):
            translate(struct.pack("<I", 0xEE300A00), 0x1000, "test", False)

    def test_reject_unknown_thumb(self):
        with self.assertRaisesRegex(ValueError, "Thumb instruction"):
            translate(struct.pack("<H", 0xDE00), 0x1000, "test", True)

    def test_reject_bounds_alignment_empty_and_symbol(self):
        for data, base, symbol in [(b"", 0, "x"), (b"x", 0, "x"),
                                   (bytes(4), -4, "x"), (bytes(4), 1, "x"),
                                   (bytes(8), 0xFFFFFFFC, "x"), (bytes(4), 0, "x;evil")]:
            with self.assertRaises(ValueError):
                translate(data, base, symbol, False)


if __name__ == "__main__":
    unittest.main()
