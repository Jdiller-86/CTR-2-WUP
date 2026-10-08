from pathlib import Path
import tempfile
import unittest
from host.gsp_fixture import gsp_code,generated_sources,fixture_bytes,PROGRAM_ID,BSS_SIZE
from host.loader import load_image
class GspFixtureTests(unittest.TestCase):
    def test_original_sources_reproduce(self):
        p=Path(__file__).parent/'fixtures/gsp'
        self.assertEqual((p/'gsp.arm.bin').read_bytes(),gsp_code())
        for name,s in generated_sources().items():self.assertEqual((p/name).read_text(),s)
    def test_checked_loader_preserves_services_and_backing(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'standard graphics.cxi';p.write_bytes(fixture_bytes());r=load_image(p)
        self.assertEqual(r['text'],gsp_code());self.assertEqual(r['inspection']['identity']['program_id'],f'{PROGRAM_ID:016x}')
        self.assertEqual(r['bss_size'],BSS_SIZE)
        self.assertEqual(r['inspection']['partitions'][0]['service_access'],['hid:USER','gsp::Gpu'])
