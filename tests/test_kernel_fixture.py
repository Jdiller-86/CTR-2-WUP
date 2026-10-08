from pathlib import Path
import tempfile
import unittest
from host.kernel_fixture import kernel_program, generated_sources, fixture_bytes, PROGRAM_ID
from host.loader import load_image

class KernelFixtureTests(unittest.TestCase):
    def test_original_sources_reproduce(self):
        folder=Path(__file__).parent/"fixtures/kernel"
        self.assertEqual((folder/"kernel.arm.bin").read_bytes(),kernel_program()[0])
        for name, source in generated_sources().items():self.assertEqual((folder/name).read_text(),source)
    def test_loader_preserves_real_original_kernel_program(self):
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory)/"original kernel.cxi";p.write_bytes(fixture_bytes());r=load_image(p)
        self.assertEqual(r['text'],kernel_program()[0])
        self.assertEqual(r['inspection']['identity']['program_id'],f'{PROGRAM_ID:016x}')
        self.assertEqual(r['code'][4096:],bytes(4096))
