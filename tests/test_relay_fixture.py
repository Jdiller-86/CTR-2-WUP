from pathlib import Path
import tempfile
import unittest
from host.relay_fixture import relay_code,generated_sources,fixture_bytes,PROGRAM_ID
from host.gsp_fixture import BSS_SIZE
from host.loader import load_image
from translator.aot import arm

class RelayFixtureTests(unittest.TestCase):
    def test_original_sources_reproduce(self):
        folder=Path(__file__).parent/'fixtures/relay'
        self.assertEqual((folder/'relay.arm.bin').read_bytes(),relay_code())
        for name,text in generated_sources().items():self.assertEqual((folder/name).read_text(),text)

    def test_loader_checks_queued_display_container(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'queued display.cxi';path.write_bytes(fixture_bytes());result=load_image(path)
        self.assertEqual(result['text'],relay_code())
        self.assertEqual(result['bss_size'],BSS_SIZE)
        self.assertEqual(result['inspection']['identity']['program_id'],f'{PROGRAM_ID:016x}')
        self.assertEqual(result['inspection']['partitions'][0]['service_access'],['hid:USER','gsp::Gpu'])

    def test_word_exclusives_are_specialized_and_bad_operands_rejected(self):
        load='\n'.join(arm(0xe1920f9f,0x1000));store='\n'.join(arm(0xe1821f93,0x1004))
        self.assertIn('exclusive_generation = cpu->memory->generation',load)
        self.assertIn('exclusive_generation == cpu->memory->generation',store)
        self.assertNotIn('ctr_cpu_step',load+store)
        for instruction in (0xe19f0f9f,0xe192ff9f,0xe1822f93,0xe1823f93,0xe1821f9f):
            with self.subTest(instruction=hex(instruction)),self.assertRaises(ValueError):arm(instruction,0x1000)
