from pathlib import Path
import json
import struct
import tempfile
import unittest
from host.module_fixture import module_files,manifest,generated_sources,fixture_bytes,image_code
from host.loader import load_image
from translator.modules import generate,prepare

ROOT=Path(__file__).resolve().parents[1]

class ModulePackageTests(unittest.TestCase):
    def test_original_package_is_reproducible(self):
        folder=ROOT/'tests/fixtures/modules'
        sources,_=generated_sources(folder)
        for name,source in sources.items():self.assertEqual(source,(folder/name).read_text())
        for name,data in module_files().items():self.assertEqual(data,(folder/name).read_bytes())

    def test_prepare_and_container_use_the_same_original_bytes(self):
        with tempfile.TemporaryDirectory() as temp:
            folder=Path(temp)
            for name,data in module_files().items():(folder/name).write_bytes(data)
            (folder/'package.json').write_text(json.dumps(manifest()))
            report=prepare(folder/'package.json',folder/'output')
            self.assertTrue(report['ok']);self.assertEqual(len(report['modules']),3)
            self.assertIn('ctr_cpu_branch_exchange',(folder/'output/modules.generated.c').read_text())
            self.assertNotIn('ctr_cpu_step',(folder/'output/modules.generated.c').read_text())
            (folder/'original.cxi').write_bytes(fixture_bytes())
            self.assertEqual(load_image(folder/'original.cxi')['code'],image_code())
            with self.assertRaisesRegex(ValueError,'Output already exists'):prepare(folder/'package.json',folder/'output')

    def test_unresolved_and_unsupported_relocations_stop_preparation(self):
        with tempfile.TemporaryDirectory() as temp:
            folder=Path(temp)
            for name,data in module_files().items():(folder/name).write_bytes(data)
            package=manifest();package['modules'].pop(1)
            with self.assertRaisesRegex(ValueError,'imported module'):generate(package,folder)
            data=bytearray((folder/'math.cro').read_bytes())
            external=struct.unpack_from('<I',data,0xf8)[0];data[external+4]=10
            (folder/'math.cro').write_bytes(data)
            (folder/'package.json').write_text(json.dumps(manifest()))
            with self.assertRaisesRegex(ValueError,'Unsupported relocation'):prepare(folder/'package.json',folder/'output')
            self.assertFalse((folder/'output').exists())
