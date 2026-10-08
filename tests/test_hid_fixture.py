from pathlib import Path
import tempfile
import unittest
from host.hid_fixture import hid_code,generated_sources,fixture_bytes,PROGRAM_ID
from host.loader import load_image
class HidFixtureTests(unittest.TestCase):
    def test_original_consumer_reproduces(self):
        p=Path(__file__).parent/'fixtures/hid'
        self.assertEqual((p/'hid.arm.bin').read_bytes(),hid_code())
        for name,s in generated_sources().items():self.assertEqual((p/name).read_text(),s)
    def test_loader_preserves_standard_hid_service_and_text(self):
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory)/'original hid.cxi';p.write_bytes(fixture_bytes());r=load_image(p)
        self.assertEqual(r['text'],hid_code())
        self.assertEqual(r['inspection']['identity']['program_id'],f'{PROGRAM_ID:016x}')
        self.assertEqual(r['inspection']['partitions'][0]['service_access'],['hid:USER'])
