"""Original structural delivery fixtures: provenance, byte checks and evidence limits."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('guest_draw_kit', ROOT / 'tools/package-guest-draws.py')
kit = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(kit)


class GuestDrawPackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.release = self.root / 'release'
        self.app = self.root / 'draw'
        self.release.mkdir()
        (self.app / 'wiiu').mkdir(parents=True)
        self.sdk = self.root / 'sdk.txt'
        self.sdk.write_text(kit.release.SDK_IMAGE + '\n')
        header = bytearray(92)
        header[:8] = b'\x7fELF\x01\x02\x01\xca'
        header[16:20] = b'\xfe\x01\x00\x14'
        struct.pack_into('>I', header, 32, 52)
        for offset, value in ((40, 52), (46, 40), (48, 1), (50, 0)):
            struct.pack_into('>H', header, offset, value)
        artifacts = []
        for name in kit.NAMES:
            data = bytes(header) if name.endswith('.rpx') else b'WUHBoriginal structural fixture'
            (self.app / 'wiiu' / name).write_bytes(data)
            artifacts.append({'name': name, 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
        frames, checks = [], []
        for screen, width in (('top', 400), ('bottom', 320)):
            name = f'frame-0-{screen}.ppm'
            data = f'P6\n{width} 240\n255\n'.encode() + bytes(width * 240 * 3)
            (self.app / name).write_bytes(data)
            digest = hashlib.sha256(data).hexdigest()
            frames.append({'name': name, 'sha256': digest})
            checks.append({'screen': screen, 'width': width, 'height': 240, 'pixels': width * 240,
                           'exact_match': True, 'sha256': digest})
        self.report = {'ok': True, 'version': '0.23.0', 'source_commit': 'a' * 40,
                       'source_dirty': False, 'libctru_pin': kit.release.LIBCTRU,
                       'applications': [{'application': 'draw', 'frames': frames,
                         'independent_frame_checks': checks, 'wiiu_artifacts': artifacts,
                         'generation': {'input_identity': {'program_id': '000400000ff00f0c'}},
                         'guest': {'state': 'exited', 'fault': 'ok', 'fallback': 0,
                          'messages': ['CTR2WUP compiled PICA draws PASS'],
                          'pica_render': {'draws': 4, 'triangles': 4, 'fragments': 97200, 'shader_instructions': 36},
                          'gx': {'commands': 6, 'dma': 0, 'fills': 2, 'transfers': 2, 'copies': 0, 'cache': 0, 'pica': 2}}}]}
        self.report_path = self.root / 'report.json'
        source = self.release / 'CTR2WUP-source-0.23.0.zip'
        with zipfile.ZipFile(source, 'w') as archive:
            archive.writestr('CTR2WUP/README.md', 'Original source fixture')
        self.manifest = {'version': '0.23.0', 'source_commit': 'a' * 40, 'original_fixtures_only': True,
                         'artifacts': [{'name': source.name, 'size': source.stat().st_size,
                                        'sha256': kit.release.sha256(source)}]}
        (self.release / 'manifest.json').write_text(json.dumps(self.manifest))
        (self.release / 'release-notes.md').write_text('Original prerelease fixture')

    def package(self):
        self.report_path.write_text(json.dumps(self.report))
        return kit.package(self.release, self.report_path, self.app, self.sdk)

    def test_source_matched_kit_checks_all_published_bytes_and_preserves_limits(self):
        result = self.package()
        with zipfile.ZipFile(self.release / result['name']) as archive:
            self.assertIsNone(archive.testzip())
            self.assertEqual(len([n for n in archive.namelist() if n.endswith('.wuhb')]), 1)
            receipt = json.loads(archive.read('manifest.json'))
            self.assertFalse(receipt['console_verified'])
            self.assertFalse(receipt['installable_wup'])
            for record in receipt['files']:
                self.assertEqual(hashlib.sha256(archive.read(record['path'])).hexdigest(), record['sha256'])
        self.assertIn(result['sha256'], (self.release / 'SHA256SUMS.txt').read_text())
        with self.assertRaisesRegex(ValueError, 'existing Guest Draws'):
            self.package()

    def test_wrong_source_dirty_input_or_missing_independent_pixels_are_rejected(self):
        original = copy.deepcopy(self.report)
        for change in ({'source_commit': 'b' * 40}, {'source_dirty': True}, {'libctru_pin': 'wrong'}):
            with self.subTest(change=change):
                self.report = copy.deepcopy(original)
                self.report.update(change)
                with self.assertRaises(ValueError):
                    self.package()
                self.assertFalse(list(self.release.glob('*guest-draws*')))
        self.report = copy.deepcopy(original)
        self.report['applications'][0]['independent_frame_checks'][1]['exact_match'] = False
        with self.assertRaisesRegex(ValueError, 'frame bytes'):
            self.package()
        self.assertFalse(list(self.release.glob('*guest-draws*')))

    def test_late_frame_tamper_and_wrong_sdk_preserve_release(self):
        frame = self.app / kit.FRAMES[1]
        old = frame.read_bytes()
        frame.write_bytes(old[:-1] + b'\x01')
        with self.assertRaisesRegex(ValueError, 'frame bytes'):
            self.package()
        self.assertFalse(list(self.release.glob('*guest-draws*')))
        frame.write_bytes(old)
        self.sdk.write_text('different SDK')
        with self.assertRaisesRegex(ValueError, 'pinned SDK'):
            self.package()
        self.assertEqual(json.loads((self.release / 'manifest.json').read_text()), self.manifest)

    def test_renamed_input_or_unchecked_binary_cannot_be_shipped(self):
        self.report['applications'][0]['generation']['input_identity']['program_id'] = 'different'
        with self.assertRaisesRegex(ValueError, 'original guest'):
            self.package()
        self.report['applications'][0]['generation']['input_identity']['program_id'] = '000400000ff00f0c'
        (self.app / 'wiiu' / kit.NAMES[1]).write_bytes(b'WUHBchanged')
        with self.assertRaisesRegex(ValueError, 'receipt'):
            self.package()
        self.assertFalse(list(self.release.glob('*guest-draws*')))


if __name__ == '__main__':
    unittest.main()
