"""Focused handoff layout, payload binding and privacy regression checks."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def module(name, file):
    spec = importlib.util.spec_from_file_location(name, ROOT/'tools'/file)
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


tool = module('m1_package', 'package-m1-suite.py')
audio = module('m1_audio', 'package-audio-round.py')
COMMIT = 'a'*40


class M1PackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.native = self.root/'native'
        self.installed = self.root/'installed'
        self.source = self.root/'source'
        self.licenses = self.root/'licenses'
        for directory in (self.native, self.installed, self.source/'docs', self.licenses):
            directory.mkdir(parents=True)
        (self.source/'docs/m1-acceptance.md').write_text('# Install check\n\nOriginal focused guide.\n')
        (self.source/'LICENSE').write_text('Original project license')
        (self.source/'THIRD_PARTY_NOTICES').write_text('Original third-party notices')
        (self.licenses/'LICENSE.txt').write_text('Original SDK license fixture')
        self.sdk_receipt = self.root/'sdk-image-digest.txt'
        self.sdk_receipt.write_text(audio.release.SDK_IMAGE+'\n')
        header = bytearray(92)
        header[:8] = b'\x7fELF\x01\x02\x01\xca'
        header[16:20] = b'\xfe\x01\x00\x14'
        struct.pack_into('>I', header, 32, 52)
        for at, n in ((40, 52), (46, 40), (48, 1), (50, 0)):
            struct.pack_into('>H', header, at, n)
        artifacts = []
        for name in tool.NAMES:
            data = bytes(header) if name.endswith('.rpx') else b'WUHBoriginal install fixture'
            (self.native/name).write_bytes(data)
            artifacts.append(dict(name=name, size=len(data), sha256=audio.digest(data)))
        self.native_report = dict(ok=True, target='install-check', profile='native-install',
                                  runtime_version=tool.VERSION, source_commit=COMMIT,
                                  source_dirty=False, input_identity=None, artifacts=artifacts)
        (self.native/'build-report.json').write_text(json.dumps(self.native_report))
        payload = {'title.tmd': b'Original TMD fixture', 'title.tik': b'Original ticket fixture',
                   'title.cert': b'Original certificate fixture', '00000000.app': bytes(header)}
        for name, data in payload.items():
            (self.installed/name).write_bytes(data)
        self.candidate = dict(ok=True, kind='ctr2wup-install-candidate', source_commit=COMMIT,
                              source_dirty=False, original_inputs_only=True,
                              profile='wiiu_plaintext_homebrew', title_id=tool.EXPECTED_TITLE_ID,
                              title_name='CTR2WUP Install Check', install_test_ready=True,
                              requires_signature_patches=True, console_verified=False,
                              rpx_sha256=audio.digest(bytes(header)),
                              files=[dict(path=n, size=len(d), sha256=audio.digest(d)) for n, d in payload.items()])
        self.write_candidate()
        self.verified = dict(ok=True, title_id=self.candidate['title_id'],
                             files=[dict(path='code/ctr2wup.rpx', bytes=len(header), sha256=self.candidate['rpx_sha256'])],
                             installation='unverified')
        for mocked in (patch.object(tool, 'ROOT', self.source),
                       patch.object(tool, 'helpers', return_value=audio),
                       patch.object(audio, 'identity', return_value=COMMIT),
                       patch.object(audio, 'checked_source', return_value=b'Original exact source fixture'),
                       patch('host.package_integrity.verify_package', return_value=self.verified)):
            mocked.start()
            self.addCleanup(mocked.stop)

    def write_candidate(self):
        (self.installed/'report.json').write_text(json.dumps(self.candidate))

    def package(self):
        return tool.package(self.native, None, self.root/'out', self.installed,
                            self.licenses, self.sdk_receipt)

    def test_one_guide_one_ftp_folder_no_repeated_apps_and_identity_returns_with_logs(self):
        report = self.package()
        stage = self.root/'out/stage'
        self.assertEqual({p.name for p in stage.iterdir()}, {'START-HERE.md', 'sd-card', 'support'})
        self.assertFalse(list((stage/'sd-card').rglob('*.wuhb')))
        self.assertFalse(list(stage.rglob('*.rpx')))
        self.assertFalse(list(stage.rglob('*.wav')))
        self.assertFalse((stage/'AUDIO-STREAMS.md').exists())
        self.assertFalse((stage/'KIT-STATUS.md').exists())
        self.assertTrue((stage/'support/licenses/sdk/LICENSE.txt').is_file())
        manifest = json.loads((stage/'support/manifest.json').read_text())
        self.assertEqual(manifest['scope'], 'new-installed-title-route-only')
        self.assertTrue(manifest['prior_native_evidence_carried_forward'])
        self.assertFalse(manifest['install_check']['full_native_guide'])
        self.assertEqual(manifest['install_check']['log_directory'], 'ctr2wup/install-check')
        self.assertTrue(manifest['package']['available'])
        self.assertFalse(manifest['console_verified'])
        self.assertFalse(manifest['full_milestone_acceptance'])
        self.assertEqual((stage/'support/manifest.json').read_bytes(),
                         (stage/'sd-card/ctr2wup/install-check/kit.json').read_bytes())
        self.assertIn(self.candidate['title_id'], (stage/'START-HERE.md').read_text())
        for row in manifest['files']:
            data = (stage/row['path']).read_bytes()
            self.assertEqual(len(data), row['size'])
            self.assertEqual(hashlib.sha256(data).hexdigest(), row['sha256'])
        with zipfile.ZipFile(self.root/'out'/report['archive']) as archive:
            self.assertIsNone(archive.testzip())
            self.assertEqual(len(archive.namelist()), report['files'])
        with self.assertRaisesRegex(ValueError, 'Preserve'):
            self.package()

    def test_no_payload_means_no_empty_retest_kit(self):
        with self.assertRaisesRegex(ValueError, 'No install payload'):
            tool.package(self.native, None, self.root/'out')
        self.assertFalse((self.root/'out').exists())

    def test_independently_extracted_rpx_must_match_clean_build(self):
        self.verified['files'][0]['sha256'] = 'b'*64
        with self.assertRaisesRegex(ValueError, 'Independent'):
            self.package()
        self.assertFalse((self.root/'out').exists())
        self.verified['files'][0]['sha256'] = self.candidate['rpx_sha256']
        self.candidate['source_commit'] = 'b'*40
        self.write_candidate()
        with self.assertRaisesRegex(ValueError, 'source-bound'):
            self.package()
        self.assertFalse((self.root/'out').exists())

    def test_payload_changes_unknown_files_and_traversal_never_get_published(self):
        path = self.installed/'00000000.app'
        original = path.read_bytes()
        path.write_bytes(b'Changed original fixture')
        with self.assertRaisesRegex(ValueError, 'differs'):
            self.package()
        path.write_bytes(original)
        extra = self.installed/'unrelated.bin'
        extra.write_bytes(b'Original unrelated private fixture')
        with self.assertRaisesRegex(ValueError, 'Unexpected files'):
            self.package()
        self.candidate['files'].append(dict(path='../unrelated.bin', size=0, sha256='0'*64))
        self.write_candidate()
        with self.assertRaisesRegex(ValueError, 'Unexpected or repeated'):
            self.package()
        self.assertFalse((self.root/'out').exists())

    def test_sdk_receipt_and_dirty_native_build_fail_before_output(self):
        self.sdk_receipt.write_text('different SDK')
        with self.assertRaisesRegex(ValueError, 'pinned SDK'):
            self.package()
        self.sdk_receipt.write_text(audio.release.SDK_IMAGE)
        self.native_report['source_dirty'] = True
        (self.native/'build-report.json').write_text(json.dumps(self.native_report))
        with self.assertRaisesRegex(ValueError, 'exact-source'):
            self.package()
        self.assertFalse((self.root/'out').exists())


if __name__ == '__main__':
    unittest.main()
