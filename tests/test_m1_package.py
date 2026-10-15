"""Original bounded M1 bundle fixtures; no installed/physical acceptance."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT=Path(__file__).resolve().parents[1]
def module(name,file):
    spec=importlib.util.spec_from_file_location(name,ROOT/'tools'/file)
    value=importlib.util.module_from_spec(spec);spec.loader.exec_module(value);return value
tool=module('m1_package','package-m1-suite.py')
audio=module('m1_audio','package-audio-round.py')
COMMIT='a'*40


class M1PackageTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.native=self.root/'native';self.kit=self.root/'audio'
        self.native.mkdir();self.kit.mkdir();self.source=self.root/'source'
        (self.source/'docs').mkdir(parents=True);(self.source/'tools').mkdir()
        (self.source/'docs/m1-acceptance.md').write_text('Original M1 guide [Gate](milestones.md#m1).\n')
        (self.source/'tools/review-m1-results.py').write_text('# original reviewer fixture\n')
        header=bytearray(92);header[:8]=b'\x7fELF\x01\x02\x01\xca';header[16:20]=b'\xfe\x01\x00\x14'
        struct.pack_into('>I',header,32,52)
        for at,n in ((40,52),(46,40),(48,1),(50,0)):struct.pack_into('>H',header,at,n)
        records=[]
        for name in tool.NAMES:
            data=bytes(header) if name.endswith('.rpx') else b'WUHBoriginal m1 fixture'
            (self.native/name).write_bytes(data)
            records.append(dict(name=name,size=len(data),sha256=audio.digest(data)))
        self.report=dict(ok=True,target='m1-check',profile='native-m1',runtime_version='0.26.0',
                         source_commit=COMMIT,source_dirty=False,input_identity=None,artifacts=records)
        self.write_report()
        self.payload={
            'START-HERE.md':b'Original Audio Streams guide', 'LICENSE':b'Original license',
            'THIRD_PARTY_NOTICES':b'Original notices',
            'source-and-notices/CTR2WUP-source-0.26.0.zip':b'Original exact source fixture',
            'sd-card/wiiu/apps/ctr2wup-audio-stream-check.wuhb':b'WUHBoriginal',
            'rpx/ctr2wup-audio-stream-check.rpx':bytes(header),
            'build-reports/native-audio-streams.json':b'{}',
            'build-reports/independent-audio-reference.json':b'{}',
            'reference/scene-left-48k.wav':b'Original wav',
            'sdk-image-digest.txt':audio.release.SDK_IMAGE.encode(),
        }
        self.payload['build-reports/native-audio-streams.json']=json.dumps(dict(ok=True,
            profile='native-audio-streams',source_commit=COMMIT,source_dirty=False)).encode()
        self.payload['build-reports/independent-audio-reference.json']=json.dumps(dict(ok=True,
            source_commit=COMMIT,source_dirty=False,cases=240,modes=['normal','sanitized'],
            **{key:True for key in audio.REFERENCE_FLAGS})).encode()
        for name,data in self.payload.items():
            p=self.kit/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
        self.manifest=dict(kind='ctr2wup-audio-round',version='0.26.0',source_commit=COMMIT,
                           source_dirty=False,original_inputs_only=True,
                           files=[dict(path=n,size=len(d),sha256=audio.digest(d)) for n,d in self.payload.items()])
        self.write_manifest()
        for target,value in ((tool,'ROOT'),):
            p=patch.object(target,value,self.source);p.start();self.addCleanup(p.stop)
        for p in (patch.object(tool,'helpers',return_value=audio),
                  patch.object(audio,'identity',return_value=COMMIT),
                  patch.object(audio,'checked_source',return_value=self.payload['source-and-notices/CTR2WUP-source-0.26.0.zip'])):
            p.start();self.addCleanup(p.stop)

    def write_report(self):
        (self.native/'build-report.json').write_text(json.dumps(self.report))
    def write_manifest(self):
        (self.kit/'manifest.json').write_text(json.dumps(self.manifest))
    def package(self):return tool.package(self.native,self.kit,self.root/'out')

    def test_original_suite_exact_hash_crc_and_installed_gate_blocked(self):
        report=self.package();self.assertTrue(report['ok']);self.assertFalse(report['full_milestone_acceptance'])
        manifest=json.loads((self.root/'out/stage/manifest.json').read_text())
        self.assertFalse(manifest['package']['available']);self.assertEqual(manifest['native_app'],'native-m1')
        self.assertTrue(manifest['recovery']['available'])
        status=(self.root/'out/stage/KIT-STATUS.md').read_text()
        self.assertIn('Not available: an installable WUP',status)
        self.assertIn('No PC command is needed',status)
        for row in manifest['files']:
            data=(self.root/'out/stage'/row['path']).read_bytes();self.assertEqual(len(data),row['size'])
            self.assertEqual(hashlib.sha256(data).hexdigest(),row['sha256'])
        with zipfile.ZipFile(self.root/'out'/report['archive']) as z:self.assertIsNone(z.testzip())
        self.assertNotIn('install/', ''.join(row['path'] for row in manifest['files']))
        with self.assertRaisesRegex(ValueError,'Preserve'):self.package()

    def test_wrong_native_source_and_dirty_inputs_fail_before_output(self):
        for field,value in (('source_commit','b'*40),('source_dirty',True),('profile','native'),('target','prototype')):
            previous=self.report[field];self.report[field]=value;self.write_report()
            with self.subTest(field=field),self.assertRaisesRegex(ValueError,'M1 native'):self.package()
            self.assertFalse((self.root/'out').exists());self.report[field]=previous
        self.write_report()

    def test_mixed_audio_source_tamper_and_traversal_are_rejected(self):
        self.manifest['source_commit']='b'*40;self.write_manifest()
        with self.assertRaisesRegex(ValueError,'exact-source'):self.package()
        self.manifest['source_commit']=COMMIT;self.manifest['files'][0]['path']='../secret';self.write_manifest()
        with self.assertRaisesRegex(ValueError,'Unsafe'):self.package()
        self.manifest['files'][0]['path']='START-HERE.md';self.write_manifest()
        (self.kit/'START-HERE.md').write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError,'differs'):self.package()
        self.assertFalse((self.root/'out').exists())

    def test_mismatched_source_archive_and_missing_evidence_are_rejected(self):
        with patch.object(audio,'checked_source',return_value=b'other source'):
            with self.assertRaisesRegex(ValueError,'archive'):self.package()
        row=next(r for r in self.manifest['files'] if r['path']=='build-reports/native-audio-streams.json')
        data=(self.kit/row['path']).read_bytes()
        row['path']='reference/extra.txt';(self.kit/row['path']).write_bytes(data);self.write_manifest()
        with self.assertRaisesRegex(ValueError,'Unexpected|missing'):self.package()

    def test_rehashed_unreviewed_payload_is_not_publicly_copied(self):
        name='reference/unrelated.bin';data=b'Original unrelated private fixture'
        (self.kit/name).write_bytes(data)
        self.manifest['files'].append(dict(path=name,size=len(data),sha256=audio.digest(data)))
        self.write_manifest()
        with self.assertRaisesRegex(ValueError,'Unexpected'):self.package()
        self.assertFalse((self.root/'out').exists())


if __name__=='__main__':unittest.main()
