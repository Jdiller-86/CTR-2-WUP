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
import zlib

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

    def package(self, backend='software'):
        self.report_path.write_text(json.dumps(self.report))
        return kit.package(self.release, self.report_path, self.app, self.sdk, backend)

    def gx2_profile(self):
        folder = self.app/'wiiu-gx2'
        folder.mkdir()
        records = []
        for original, name in zip(kit.NAMES, kit.GX2_NAMES):
            data = (self.app/'wiiu'/original).read_bytes()
            (folder/name).write_bytes(data)
            records.append({'name': name, 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
        app = self.report['applications'][0]
        app.update({'wiiu_gx2_artifacts': records, 'independent_frame_checks_backend': 'software',
                    'gx2_guest': {'backend': 'gx2', 'native_cross_build': True,
                                  'software_frame_checks': True, 'gpu_hardware_verified': False}})
        self.report['gx2_shader_validation'] = {'ok': True, 'source_commit': 'a'*40,
                                              'source_dirty': False, 'console_verified': False}

    def textured_profile(self):
        self.gx2_profile()
        self.report['version'] = self.manifest['version'] = '0.27.0'
        old_source = self.release/'CTR2WUP-source-0.23.0.zip'
        new_source = self.release/'CTR2WUP-source-0.27.0.zip'
        old_source.rename(new_source)
        self.manifest['artifacts'][0]['name'] = new_source.name
        (self.release/'manifest.json').write_text(json.dumps(self.manifest))
        app = self.report['applications'][0]
        app['application'] = 'textured'
        app['gx2_guest']['textured'] = True
        app['guest']['messages'] = ['CTR2WUP compiled D16 depth PASS', 'CTR2WUP compiled stencil preparation PASS', 'CTR2WUP compiled D24S8 stencil PASS', 'CTR2WUP compiled textured PICA PASS']
        app['guest']['pica_render'] = {'draws': 9, 'triangles': 9, 'fragments': 210570, 'shader_instructions': 108}
        app['guest']['gx'] = {'commands': 7, 'dma': 0, 'fills': 2, 'transfers': 2, 'copies': 0, 'cache': 0, 'pica': 3}
        for frame, check in zip(app['frames'], app['independent_frame_checks']):
            width = check['width']
            pixels = b''.join(bytes((x % 256, y % 256, (x + 3*y) % 256))
                              for y in range(240) for x in range(width))
            data = f'P6\n{width} 240\n255\n'.encode() + pixels
            (self.app/frame['name']).write_bytes(data)
            frame['sha256'] = check['sha256'] = hashlib.sha256(data).hexdigest()
        records = []
        for original, name in zip(kit.GX2_NAMES, kit.TEXTURED_NAMES):
            data = (self.app/'wiiu-gx2'/original).read_bytes()
            (self.app/'wiiu-gx2'/name).write_bytes(data)
            records.append({'name': name, 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
        app['wiiu_gx2_artifacts'] = records


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

    def test_gx2_kit_uses_only_distinct_native_profile_and_labels_reference_scope(self):
        self.gx2_profile()
        result = self.package('gx2')
        self.assertEqual(result['name'], 'CTR2WUP-gx2-guest-0.23.0.zip')
        with zipfile.ZipFile(self.release/result['name']) as archive:
            self.assertIn('sd-card/wiiu/apps/ctr2wup-gx2-guest.wuhb', archive.namelist())
            self.assertNotIn('sd-card/wiiu/apps/ctr2wup-libctru-draw.wuhb', archive.namelist())
            receipt = json.loads(archive.read('manifest.json'))
            self.assertEqual(receipt['render_backend'], 'gx2')
            self.assertEqual(receipt['reference_frames_backend'], 'software')
            self.assertTrue(receipt['native_cross_build'])
            self.assertFalse(receipt['gpu_hardware_verified'])
            self.assertFalse(receipt['console_verified'])
            self.assertIn(b'CTR2WUP GX2 Guest', archive.read('START-HERE.md'))

    def test_software_report_cannot_be_relabelled_as_gpu_output(self):
        with self.assertRaisesRegex(ValueError, 'distinct cross-built GX2'):
            self.package('gx2')
        self.assertFalse(list(self.release.glob('*gx2-guest*')))
        self.gx2_profile()
        original = copy.deepcopy(self.report)
        for field, value in (('source_commit', 'b'*40), ('source_dirty', True), ('console_verified', True)):
            with self.subTest(field=field):
                self.report = copy.deepcopy(original)
                self.report['gx2_shader_validation'][field] = value
                with self.assertRaisesRegex(ValueError, 'distinct cross-built GX2'):
                    self.package('gx2')
        self.report = copy.deepcopy(original)
        self.report['applications'][0]['gx2_guest']['gpu_hardware_verified'] = True
        with self.assertRaisesRegex(ValueError, 'distinct cross-built GX2'):
            self.package('gx2')

    def test_gx2_binary_tamper_or_software_names_preserves_release(self):
        self.gx2_profile()
        original = copy.deepcopy(self.report)
        self.report['applications'][0]['wiiu_gx2_artifacts'] = self.report['applications'][0]['wiiu_artifacts']
        with self.assertRaisesRegex(ValueError, 'receipt'):
            self.package('gx2')
        self.report = original
        (self.app/'wiiu-gx2'/kit.GX2_NAMES[1]).write_bytes(b'WUHBchanged')
        with self.assertRaisesRegex(ValueError, 'receipt'):
            self.package('gx2')
        self.assertEqual(json.loads((self.release/'manifest.json').read_text()), self.manifest)
        self.assertFalse(list(self.release.glob('*gx2-guest*')))

    def test_textured_kit_has_distinct_identity_and_previews_checked_reference_pixels(self):
        self.textured_profile()
        result = self.package('textured')
        self.assertEqual(result['name'], 'CTR2WUP-textured-guest-0.27.0.zip')
        with zipfile.ZipFile(self.release/result['name']) as archive:
            self.assertIn('sd-card/wiiu/apps/ctr2wup-textured-guest.wuhb', archive.namelist())
            self.assertNotIn('sd-card/wiiu/apps/ctr2wup-gx2-guest.wuhb', archive.namelist())
            receipt = json.loads(archive.read('manifest.json'))
            self.assertEqual(receipt['render_backend'], 'gx2')
            self.assertTrue(receipt['textured'])
            self.assertFalse(receipt['gpu_hardware_verified'])
            self.assertIn(b'CTR2WUP Textured Guest', archive.read('START-HERE.md'))
            for screen, width in (('top', 400), ('bottom', 320)):
                data = archive.read(f'images/textured-guest-{screen}.png')
                self.assertEqual(data[:8], b'\x89PNG\r\n\x1a\n')
                at, compressed = 8, b''
                while at < len(data):
                    length = int.from_bytes(data[at:at+4], 'big')
                    kind, payload = data[at+4:at+8], data[at+8:at+8+length]
                    self.assertEqual(zlib.crc32(kind+payload), int.from_bytes(data[at+8+length:at+12+length], 'big'))
                    if kind == b'IDAT':compressed += payload
                    if kind == b'IHDR':self.assertEqual(struct.unpack('>II', payload[:8]), (width, 240))
                    at += 12+length
                raw = zlib.decompress(compressed)
                ppm = archive.read(f'reference/frame-0-{screen}.ppm').split(b'\n', 3)[3]
                self.assertEqual(len(raw), 240*(width*3+1))
                self.assertEqual(b''.join(raw[y*(width*3+1)+1:(y+1)*(width*3+1)] for y in range(240)), ppm)
                self.assertTrue(all(raw[y*(width*3+1)] == 0 for y in range(240)))
            self.assertFalse(any('guest-draw-' in name for name in archive.namelist()))

    def test_old_software_or_gx2_report_cannot_identify_a_textured_guest(self):
        with self.assertRaisesRegex(ValueError, 'source-matched'):
            self.package('textured')
        self.gx2_profile()
        with self.assertRaisesRegex(ValueError, 'source-matched'):
            self.package('textured')
        # Renaming the application still lacks the new program's PASS/counters.
        self.report['applications'][0]['application'] = 'textured'
        with self.assertRaisesRegex(ValueError, 'original guest'):
            self.package('textured')
        self.assertFalse(list(self.release.glob('*textured-guest*')))

    def test_textured_source_profile_and_binary_tampering_preserve_release(self):
        self.textured_profile()
        original = copy.deepcopy(self.report)
        changes = (
            lambda r: r.update(source_commit='b'*40),
            lambda r: r['gx2_shader_validation'].update(source_dirty=True),
            lambda r: r['applications'][0]['gx2_guest'].pop('textured'),
            lambda r: r['applications'][0]['gx2_guest'].update(gpu_hardware_verified=True),
            lambda r: r['applications'][0]['guest']['pica_render'].update(shader_instructions=36),
            lambda r: r['applications'][0]['guest']['messages'].remove('CTR2WUP compiled D16 depth PASS'),
            lambda r: r['applications'][0]['guest']['messages'].remove('CTR2WUP compiled stencil preparation PASS'),
            lambda r: r['applications'][0]['guest']['messages'].remove('CTR2WUP compiled D24S8 stencil PASS'),
            lambda r: r['applications'][0]['guest']['gx'].update(pica=2),
            lambda r: r['applications'][0].update(wiiu_gx2_artifacts=r['applications'][0]['wiiu_artifacts']),
        )
        for index, change in enumerate(changes):
            with self.subTest(change=index):
                self.report = copy.deepcopy(original)
                change(self.report)
                with self.assertRaises(ValueError):self.package('textured')
                self.assertFalse(list(self.release.glob('*textured-guest*')))
        self.report = original
        (self.app/'wiiu-gx2'/kit.TEXTURED_NAMES[1]).write_bytes(b'WUHBchanged')
        with self.assertRaisesRegex(ValueError, 'receipt'):self.package('textured')
        self.assertEqual(json.loads((self.release/'manifest.json').read_text()), self.manifest)


if __name__ == '__main__':
    unittest.main()
