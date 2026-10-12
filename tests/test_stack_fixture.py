from pathlib import Path
import struct
import tempfile
import unittest
from host.stack_fixture import fixture_bytes,image_code,generated_sources,PROGRAM_ID
from host.loader import load_image
from translator.aot import translate
class StackFixtureTests(unittest.TestCase):
    def test_original_sources_reproduce(self):
        folder=Path(__file__).parent/'fixtures/stack'
        self.assertEqual((folder/'stack.code.bin').read_bytes(),image_code())
        for name,text in generated_sources().items():self.assertEqual((folder/name).read_text(),text)
    def test_loader_preserves_literal_targets_and_no_service_requirements(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'original stack.cxi';path.write_bytes(fixture_bytes());result=load_image(path)
        self.assertEqual(result['code'],image_code());self.assertEqual(result['bss_size'],0)
        self.assertEqual(result['inspection']['identity']['program_id'],f'{PROGRAM_ID:016x}')
        self.assertEqual(result['inspection']['partitions'][0]['service_access'],[])
    def test_generated_transfers_are_decoded_and_invalid_forms_fail_closed(self):
        for opcode,thumb in ((0xe92d40f0,False),(0xe92d6000,False),(0xe8ac5000,False),(0xe8bd80f0,False),(0xb530,True),(0xbd30,True),(0xc630,True),(0xce0c,True)):
            source=translate(struct.pack('<H' if thumb else '<I',opcode),0x100000,'transfer',thumb)
            self.assertIn('ctr_cpu_multiple(cpu,',source);self.assertNotIn('ctr_cpu_step(',source)
        for opcode,thumb in ((0xe8cc0001,False),(0xe8ac1001,False),(0xe8bc1001,False),(0xe89f0001,False),(0xe88f8000,False),
                             (0xe8ac0000,False),(0xb400,True),(0xbc00,True),(0xc000,True),(0xc800,True),(0xc103,True),(0x47f8,True)):
            with self.subTest(opcode=hex(opcode)),self.assertRaises(ValueError):translate(struct.pack('<H' if thumb else '<I',opcode),0x100000,'invalid',thumb)
