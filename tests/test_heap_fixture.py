from pathlib import Path
import tempfile
import unittest
from host.loader import load_image
from host.heap_fixture import generated_sources, heap_code, fixture_bytes, PROGRAM_ID


class HeapFixtureTests(unittest.TestCase):
    def test_original_sources_are_reproducible(self):
        root = Path(__file__).parent / "fixtures/heap"
        self.assertEqual((root / "heap.arm.bin").read_bytes(), heap_code())
        for name, source in generated_sources().items():
            self.assertEqual((root / name).read_text(), source)

    def test_checked_loader_preserves_heap_program_and_plain_data_page(self):
        with tempfile.TemporaryDirectory() as temporary:
            rom = Path(temporary) / "original heap 日本語.cxi"
            rom.write_bytes(fixture_bytes())
            image = load_image(rom)
        self.assertEqual(image["inspection"]["identity"]["program_id"], f"{PROGRAM_ID:016x}")
        self.assertEqual(image["text"], heap_code())
        self.assertEqual(image["code"][4096:], bytes(4096))
        self.assertEqual(image["inspection"]["partitions"][0]["service_access"], [])


if __name__ == "__main__":
    unittest.main()
