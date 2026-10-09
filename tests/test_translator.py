from pathlib import Path
import struct
import unittest
from translator.aot import translate


class TranslatorTests(unittest.TestCase):
    def test_checked_unsigned_halfword_subset(self):
        for instruction,action in ((0xE1C100B0,'store'),(0xE1D120B2,'load')):
            s=translate(struct.pack('<I',instruction),0x1000,'halfword',False)
            self.assertIn('ctr_cpu_'+action+'(cpu, address, 2u,',s)
        for instruction in (0xE0C100B0,0xE18100B0,0xE1E100B0,0xE1C1F0B0):
            with self.assertRaises(ValueError):translate(struct.pack('<I',instruction),0x1000,'invalid_halfword',False)
    def test_user_tls_read_is_specialized(self):
        source=translate(struct.pack("<I", 0xEE1D6F70), 0x1000, "tls_probe", False)
        self.assertIn("cpu->r[6] = cpu->tls_address;", source)
        self.assertNotIn("ctr_cpu_step", source)

    def test_tls_write_flag_destination_and_cache_operations_are_rejected(self):
        for instruction in (0xEE0D0F70, 0xEE1DFF70, 0xEE070F15, 0xEE1D0F50):
            with self.assertRaises(ValueError):
                translate(struct.pack("<I", instruction), 0x1000, "invalid_tls", False)

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
