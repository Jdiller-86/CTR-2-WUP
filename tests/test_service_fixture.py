from pathlib import Path
import tempfile
import unittest
from host.loader import load_image
from host.service_fixture import generated_sources, service_code, fixture_bytes, PROGRAM_ID


class ServiceFixtureTests(unittest.TestCase):
    def test_original_sources_are_reproducible(self):
        root=Path(__file__).parent/"fixtures/services"
        self.assertEqual((root/"boot.arm.bin").read_bytes(),service_code())
        for name, source in generated_sources().items():
            self.assertEqual((root/name).read_text(),source)

    def test_real_loader_preserves_startup_segments_and_service_identity(self):
        with tempfile.TemporaryDirectory() as tmp:
            rom=Path(tmp)/"original service.cxi"
            rom.write_bytes(fixture_bytes())
            image=load_image(rom)
        self.assertEqual(image["inspection"]["identity"]["program_id"],f"{PROGRAM_ID:016x}")
        self.assertEqual(image["text"],service_code())
        self.assertEqual(image["code"][4096:4101],b"srv:\0")
        self.assertEqual(image["inspection"]["partitions"][0]["service_access"],["cfg:u"])


if __name__=="__main__":unittest.main()
