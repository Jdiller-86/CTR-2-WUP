"""GX2 native-profile isolation and validation argument boundaries."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which('make'), 'GNU Make is needed for native recipe inspection')
class Gx2BuildProfileTests(unittest.TestCase):
    def profile(self, *flags):
        recipe = 'profile-info:;@printf "%s\\n" "$(TARGET)" "$(APP_NAME)" "$(SOURCES)" "$(CFLAGS)" "$(BUILD)"'
        return subprocess.run(['make', '-s', '--no-print-directory', '-C', str(ROOT/'runtime/wiiu'),
                               '--eval='+recipe, 'profile-info', *flags],
                              capture_output=True, text=True)

    def test_software_profile_preserves_existing_name_and_excludes_gpu_adapter(self):
        result = self.profile('PICA_HOMEBREW=1', 'COMPILED_HOMEBREW=1', 'EXTERNAL_AOT=1')
        self.assertEqual(result.returncode, 0, result.stderr)
        target, app, sources, flags, objects = result.stdout.splitlines()
        self.assertEqual(target, 'ctr2wup-prototype')
        self.assertIn('CTR2WUP Guest Draws ', app)
        self.assertNotIn('gx2_scene.c', sources)
        self.assertNotIn('-DCTR_GX2_GUEST', flags)
        self.assertTrue(objects.endswith('/objects'))

    def test_gpu_profile_has_distinct_identity_and_adapter(self):
        result = self.profile('GX2_GUEST=1', 'PICA_HOMEBREW=1',
                              'COMPILED_HOMEBREW=1', 'EXTERNAL_AOT=1')
        self.assertEqual(result.returncode, 0, result.stderr)
        target, app, sources, flags, objects = result.stdout.splitlines()
        self.assertEqual(target, 'ctr2wup-gx2-guest')
        self.assertIn('CTR2WUP GX2 Guest ', app)
        self.assertIn('gx2_scene.c', sources.split())
        self.assertIn('-DCTR_GX2_GUEST', flags.split())
        self.assertTrue(objects.endswith('/objects-gx2'))

    def test_gpu_profile_rejects_incomplete_guest_or_mixed_native_profile(self):
        required = ('PICA_HOMEBREW=1', 'COMPILED_HOMEBREW=1', 'EXTERNAL_AOT=1')
        for missing in required:
            with self.subTest(missing=missing):
                result = self.profile('GX2_GUEST=1', *(x for x in required if x != missing))
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('GX2_GUEST requires', result.stderr)
        result = self.profile('GX2_GUEST=1', *required, 'SCENE_ONLY=1')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('own compiled PICA profile', result.stderr)

    def test_textured_profile_has_distinct_application_binary_and_objects(self):
        result = self.profile('GX2_GUEST=1', 'TEXTURED_HOMEBREW=1', 'PICA_HOMEBREW=1',
                              'COMPILED_HOMEBREW=1', 'EXTERNAL_AOT=1')
        self.assertEqual(result.returncode, 0, result.stderr)
        target, app, sources, flags, objects = result.stdout.splitlines()
        self.assertEqual(target, 'ctr2wup-textured-guest')
        self.assertIn('CTR2WUP Textured Guest ', app)
        self.assertIn('gx2_scene.c', sources.split())
        self.assertIn('-DCTR_TEXTURED_HOMEBREW', flags.split())
        self.assertTrue(objects.endswith('/objects-gx2-textured'))
        result = self.profile('TEXTURED_HOMEBREW=1')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('TEXTURED_HOMEBREW requires GX2_GUEST=1', result.stderr)


class Gx2ValidationArguments(unittest.TestCase):
    def test_gx2_requires_original_draw_and_native_build_before_creating_output(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp)/'validation'
            for options in ([], ['--application', 'draw'], ['--application', 'transfer', '--wiiu']):
                with self.subTest(options=options):
                    result = subprocess.run([sys.executable, str(ROOT/'tools/validate-libctru.py'),
                                             '--output', str(output), '--gx2', *options],
                                            capture_output=True, text=True)
                    self.assertEqual(result.returncode, 2, result.stderr)
                    self.assertIn('--gx2 requires --application draw or textured and --wiiu', result.stderr)
                    self.assertFalse(output.exists())

    def test_textured_native_request_requires_gpu_profile_before_output(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp)/'validation'
            result = subprocess.run([sys.executable, str(ROOT/'tools/validate-libctru.py'),
                                     '--output', str(output), '--application', 'textured', '--wiiu'],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 2, result.stderr)
            self.assertIn('--application textured --wiiu requires --gx2', result.stderr)
            self.assertFalse(output.exists())


if __name__ == '__main__':
    unittest.main()
