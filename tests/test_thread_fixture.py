from pathlib import Path
import tempfile
import unittest
from host.thread_fixture import fixture_bytes,image_code,generated_sources,PROGRAM_ID
from host.loader import load_image
class ThreadFixtureTests(unittest.TestCase):
    def test_original_sources_reproduce(self):
        folder=Path(__file__).parent/'fixtures/threads'
        self.assertEqual((folder/'thread.code.bin').read_bytes(),image_code())
        for name,text in generated_sources().items():self.assertEqual((folder/name).read_text(),text)
    def test_loader_preserves_thread_entry_pointers(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'original thread.cxi';path.write_bytes(fixture_bytes());result=load_image(path)
        self.assertEqual(result['code'],image_code());self.assertEqual(result['stack_size'],16384)
        self.assertEqual(result['inspection']['identity']['program_id'],f'{PROGRAM_ID:016x}')
        self.assertEqual(result['inspection']['partitions'][0]['service_access'],[])
    def test_entries_are_translated_without_interpreter_calls(self):
        source=generated_sources()['thread.generated.c']
        for symbol in ('ctr_thread_main','ctr_thread_worker','ctr_thread_busy'):self.assertIn(symbol,source)
        self.assertNotIn('ctr_cpu_step(',source)
