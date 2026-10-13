"""Audio delivery selection, provenance and tamper checks; original structural fixtures."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('audio_package', ROOT / 'tools/package-audio-checks.py')
kit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(kit)


class AudioPackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.folder = self.root / 'release'
        self.audio = self.root / 'audio'
        self.gx = self.root / 'gx'
        for path in (self.folder, self.audio, self.gx):
            path.mkdir()
        header = bytearray(92)
        header[:8] = b'\x7fELF\x01\x02\x01\xca'
        header[16:20] = b'\xfe\x01\x00\x14'
        struct.pack_into('>I', header, 32, 52)
        for offset, value in ((40, 52), (46, 40), (48, 1), (50, 0)):
            struct.pack_into('>H', header, offset, value)
        def artifacts(folder, names):
            records = []
            for name in names:
                data = bytes(header) if name.endswith('.rpx') else b'WUHBoriginal structural fixture'
                (folder / name).write_bytes(data)
                records.append({'name': name, 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
            return records
        native = {'ok': True, 'runtime_version': kit.__version__, 'target': 'audio-check',
                  'profile': 'native-audio-check', 'input_identity': None,
                  'artifacts': artifacts(self.audio, kit.AUDIO_NAMES)}
        (self.audio / 'build-report.json').write_text(json.dumps(native))
        transfer = {'ok': True, 'version': kit.__version__, 'libctru_pin': kit.release.LIBCTRU,
                    'applications': [{'application': 'transfer', 'guest': {'state': 'exited', 'fault': 'ok', 'fallback': 0},
                                      'generation': {'input_identity': {'program_id': '000400000ff00f0c'}},
                                      'wiiu_artifacts': artifacts(self.gx, kit.release.TRANSFER_NAMES)}]}
        self.report = self.root / 'transfer.json'
        self.report.write_text(json.dumps(transfer))
        source = self.folder / f'CTR2WUP-source-{kit.__version__}.zip'
        with zipfile.ZipFile(source, 'w') as archive:
            archive.writestr('CTR2WUP/README.md', 'Original source fixture')
        self.manifest = {'version': kit.__version__, 'source_commit': 'a' * 40, 'original_fixtures_only': True,
                         'artifacts': [{'name': source.name, 'size': source.stat().st_size, 'sha256': kit.release.sha256(source)}]}
        (self.folder / 'manifest.json').write_text(json.dumps(self.manifest))
        (self.folder / 'release-notes.md').write_text('Original development release')

    def package(self):
        return kit.package(self.folder, self.audio, self.report, self.gx)

    def test_two_selected_apps_matching_source_and_all_bytes_are_checked(self):
        result = self.package()
        with zipfile.ZipFile(self.folder / result['name']) as archive:
            names = archive.namelist()
            self.assertEqual(len([name for name in names if name.endswith('.wuhb')]), 2)
            self.assertTrue(any(name.startswith('source-and-notices/') for name in names))
            receipt = json.loads(archive.read('manifest.json'))
            self.assertEqual(receipt['source_commit'], 'a' * 40)
            self.assertFalse(receipt['console_verified'])
            for row in receipt['files']:
                self.assertEqual(hashlib.sha256(archive.read(row['path'])).hexdigest(), row['sha256'])
        self.assertIn(result['sha256'], (self.folder / 'SHA256SUMS.txt').read_text())
        with self.assertRaisesRegex(ValueError, 'existing audio kit'):
            self.package()

    def test_modified_app_is_rejected_before_any_kit_is_written(self):
        (self.audio / kit.AUDIO_NAMES[1]).write_bytes(b'WUHBmodified')
        with self.assertRaisesRegex(ValueError, 'receipt'):
            self.package()
        self.assertFalse(list(self.folder.glob('*launch-audio*')))

    def test_guest_or_wrong_profile_cannot_be_packaged_as_audio_check(self):
        path = self.audio / 'build-report.json'
        report = json.loads(path.read_text())
        report['input_identity'] = {'program_id': 'original but different input'}
        path.write_text(json.dumps(report))
        with self.assertRaisesRegex(ValueError, 'original native Audio Check'):
            self.package()

    def test_release_path_traversal_and_source_tampering_are_rejected(self):
        original = self.manifest['artifacts'][0]['name']
        self.manifest['artifacts'][0]['name'] = '../private.key'
        (self.folder / 'manifest.json').write_text(json.dumps(self.manifest))
        with self.assertRaisesRegex(ValueError, 'local filenames'):
            self.package()
        self.manifest['artifacts'][0]['name'] = original
        (self.folder / 'manifest.json').write_text(json.dumps(self.manifest))
        (self.folder / original).write_bytes(b'modified source archive')
        with self.assertRaisesRegex(ValueError, 'receipt'):
            self.package()
