"""The diagnostic package must be explicit, bounded and preserve its inputs."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('install_capture_package',ROOT/'tools/package-install-capture.py')
capture=importlib.util.module_from_spec(spec)
spec.loader.exec_module(capture)


class CapturePackageTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        self.document=json.loads((ROOT/'packaging/install-capture.json').read_text())
        self.path=self.root/'identity.json'

    def check_config(self,document):
        self.path.write_text(json.dumps(document))
        return capture.configuration(self.path)

    def test_exact_fixed_candidate_and_header_bind_all_seven_bytestrings(self):
        document,data=self.check_config(self.document)
        self.assertEqual(json.loads(data),self.document)
        self.assertEqual(sum(row['size'] for row in document['files']),4397332)
        generated=capture.header(document)
        self.assertIn('UINT64_C(0x0005000043543257)',generated)
        self.assertIn(document['package_source'],generated)
        for row in document['files']:
            expected=','.join(f'0x{byte:02x}' for byte in bytes.fromhex(row['sha256']))
            self.assertIn('{"'+row['name']+'",'+str(row['size'])+', {'+expected+'}}',generated)

    def test_other_titles_missing_duplicates_and_injected_paths_are_rejected(self):
        bad=copy.deepcopy(self.document);bad['title_id']='0005001010040100'
        cases=[bad]
        for name in ('../../other.app','";bad();','title.key','extra.app'):
            bad=copy.deepcopy(self.document);bad['files'][0]['name']=name;cases.append(bad)
        bad=copy.deepcopy(self.document);bad['files'][0]=bad['files'][1];cases.append(bad)
        bad=copy.deepcopy(self.document);bad['files'].pop();cases.append(bad)
        for value in (-1,0,True,16*1024*1024+1):
            bad=copy.deepcopy(self.document);bad['files'][0]['size']=value;cases.append(bad)
        for value in ('0'*63,'x'*64,None):
            bad=copy.deepcopy(self.document);bad['files'][0]['sha256']=value;cases.append(bad)
        for bad in cases:
            with self.subTest(document=bad),self.assertRaises(ValueError):self.check_config(bad)

    def test_new_candidate_binds_every_stream_and_manifest_without_sd_configuration(self):
        document=copy.deepcopy(self.document)
        document['schema_version']=2
        document['package_manifest_sha256']=hashlib.sha256(b'Original candidate receipt').hexdigest()
        del document['package_artifact_sha256']
        original={r['name']:r for r in document['files']}
        document['files']=[dict(name=f'{i:08x}.app',size=32768,sha256=f'{i:064x}') for i in range(9)]
        document['files'] += [original[n] for n in ('title.tmd','title.tik','title.cert')]
        checked,_=self.check_config(document)
        generated=capture.header(checked)
        self.assertIn('#define CTR_CAPTURE_FILE_COUNT 12',generated)
        self.assertIn('#define CTR_CAPTURE_M1_COMPLETION 1',generated)
        self.assertIn('#define CTR_CAPTURE_REPAIR_VERSION 31',generated)
        self.assertIn('#define CTR_CAPTURE_REPAIR_FILE_COUNT 9',generated)
        previous=json.loads((ROOT/'packaging/install-predecessor.json').read_bytes())
        self.assertIn(previous['source_commit'],generated)
        for row in previous['files']:
            self.assertIn('"'+row['name']+'"',generated)
            self.assertIn(','.join(f'0x{byte:02x}' for byte in bytes.fromhex(row['sha256'])),generated)
        current=[dict(path=r['name'],bytes=r['size'],sha256=r['sha256']) for r in previous['files']]
        installed_header=capture.header(checked,current,30)
        self.assertIn('#define CTR_CAPTURE_CURRENT_FILE_COUNT 9',installed_header)
        self.assertIn('#define CTR_CAPTURE_CURRENT_VERSION 30',installed_header)
        self.assertIn('#define CTR_CAPTURE_DIRECTORY "CTR2WUP-M1-r30"',installed_header)
        for malformed in (current[:-1],[{**r,'path':'../'+r['path']} for r in current],
                          [{**r,'sha256':'bad'} for r in current]):
            with self.assertRaises(ValueError):capture.header(checked,malformed,30)
        for version in (None,29,True,65536):
            with self.assertRaises(ValueError):capture.header(checked,current,version)
        for version in ('30"\n#error injected',30,True):
            with self.assertRaises(ValueError):capture.header(checked,title_version=version)
        scoped=copy.deepcopy(checked)
        for row in scoped['files']:
            if row['name'].endswith('.app'):row['name']=f'{(30<<16)+int(row["name"][:8],16):08x}.app'
        capture.validate_configuration(scoped)
        self.assertIn('001e0000.app',capture.header(scoped,current,30))
        with self.assertRaises(ValueError):capture.header(scoped,current,31)
        for value in ('001e000f.app','001d0001.app','001e0001.app'):
            bad=copy.deepcopy(scoped);bad['files'][0]['name']=value
            with self.assertRaises(ValueError):capture.validate_configuration(bad)
        self.assertIn('manifest_sha256='+document['package_manifest_sha256'],generated)
        self.assertNotIn('artifact_sha256=',generated)
        for row in document['files']:
            self.assertIn('"'+row['name']+'"',generated)
        for mutate in ('gap','extra','duplicate','legacy-digest','oversized'):
            bad=copy.deepcopy(document)
            if mutate=='gap':bad['files'][4]['name']='000000ff.app'
            elif mutate=='extra':bad['files'].append(dict(name='title.key',size=16,sha256='a'*64))
            elif mutate=='duplicate':bad['files'][4]=bad['files'][3]
            elif mutate=='legacy-digest':bad['package_artifact_sha256']=bad.pop('package_manifest_sha256')
            else:bad['files']*=6
            with self.subTest(mutate=mutate),self.assertRaises(ValueError):self.check_config(bad)

    def test_preexisting_output_is_preserved_without_invoking_the_compiler(self):
        sdk=self.root/'sdk';sdk.mkdir()
        (sdk/'ctr2wup-image-digest.txt').write_text(capture.audio.release.SDK_IMAGE+'\n')
        output=self.root/'out';output.mkdir();(output/'keep').write_bytes(b'original')
        with patch.object(capture,'identity',return_value='a'*40),patch.object(capture.subprocess,'run') as run:
            with self.assertRaisesRegex(ValueError,'preserved'):capture.package(output,sdk)
        run.assert_not_called();self.assertEqual((output/'keep').read_bytes(),b'original')

    def test_sdk_mismatch_cannot_compile_and_guide_stays_three_short_paragraphs(self):
        sdk=self.root/'sdk';sdk.mkdir();(sdk/'ctr2wup-image-digest.txt').write_text('unverified')
        with patch.object(capture,'identity',return_value='a'*40),patch.object(capture.subprocess,'run') as run:
            with self.assertRaisesRegex(ValueError,'pinned'):capture.package(self.root/'out',sdk)
        run.assert_not_called();self.assertFalse((self.root/'out').exists())
        paragraphs=(ROOT/'tools/templates/install-capture.md').read_text().strip().split('\n\n')
        self.assertEqual(len(paragraphs),3);self.assertLess(sum(len(p.split()) for p in paragraphs),160)
