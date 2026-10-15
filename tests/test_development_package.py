"""The active Native/GX handoff has one guide, one deploy tree and auditable support."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('development_package', ROOT/'tools/package-development-release.py')
package = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(package)


class DevelopmentPackageTests(unittest.TestCase):
    def test_focused_layout_retains_exact_source_notices_artifacts_and_checksums(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            native, transfer, reference = (root/name for name in ('native', 'transfer', 'reference'))
            for folder in (native, transfer, reference):
                folder.mkdir()
            binaries = {}
            for folder, names in ((native, package.NATIVE_NAMES), (transfer, package.TRANSFER_NAMES)):
                for name in names:
                    data = ('Original structural fixture: '+name).encode()
                    (folder/name).write_bytes(data)
                    binaries[name] = data
            for name in package.FRAME_NAMES:
                (reference/name).write_bytes(b'P6\n1 1\n255\n\x00\xff\x00')
            (native/'build-report.json').write_text('{"ok":true}')
            report = root/'transfer.json'
            report.write_text('{"ok":true}')
            source = root/'CTR2WUP-source-0.27.0.zip'
            licenses = {p.relative_to(ROOT).as_posix(): p.read_bytes()
                        for p in (ROOT/'docs/licenses').rglob('*') if p.is_file()}
            with zipfile.ZipFile(source, 'w') as archive:
                for name, data in licenses.items():
                    archive.writestr('CTR2WUP/'+name, data)
                for name in ('LICENSE', 'THIRD_PARTY_NOTICES', 'tools/templates/native-gx.md'):
                    archive.writestr('CTR2WUP/'+name, (ROOT/name).read_bytes())
                archive.writestr('CTR2WUP/AGENTS.md', 'Developer instructions stay inside source ZIP.')
                archive.writestr('CTR2WUP/docs/architecture.md', 'Technical documentation stays inside source ZIP.')
            options = argparse.Namespace(native_dir=native, transfer_dir=transfer,
                                         reference_dir=reference, transfer_report=report)
            destination = root/'diagnostic.zip'
            commit = 'a'*40
            package.write_diagnostic(source, destination, options, '0.27.0', commit, (2026, 10, 15, 0, 0, 0))
            package.validate_zip(destination)
            with zipfile.ZipFile(destination) as archive:
                names = archive.namelist()
                self.assertEqual({name.split('/')[0] for name in names}, {'START-HERE.md', 'sd-card', 'support'})
                self.assertEqual(sum(name.endswith('.wuhb') for name in names), 2)
                self.assertEqual(archive.read('support/'+source.name), source.read_bytes())
                self.assertEqual(archive.read('support/licenses/devkitarm-crtls/3dsx_crt0.s'),
                                 licenses['docs/licenses/devkitarm-crtls/3dsx_crt0.s'])
                guide = archive.read('START-HERE.md').decode()
                self.assertIn(commit, guide)
                self.assertIn('0.27.0', guide)
                self.assertIn('sd:/ctr2wup/results/', guide)
                self.assertNotIn('@SOURCE_COMMIT@', guide)
                for name, data in binaries.items():
                    prefix = 'sd-card/wiiu/apps/' if name.endswith('.wuhb') else 'support/rpx/'
                    self.assertEqual(archive.read(prefix+name), data)
                receipt = json.loads(archive.read('support/manifest.json'))
                self.assertFalse(receipt['console_verified'])
                for row in receipt['files']:
                    data = archive.read(row['path'])
                    self.assertEqual(len(data), row['size'])
                    self.assertEqual(hashlib.sha256(data).hexdigest(), row['sha256'])
                for line in archive.read('support/SHA256SUMS.txt').decode().splitlines():
                    digest, name = line.split('  ', 1)
                    self.assertEqual(hashlib.sha256(archive.read(name)).hexdigest(), digest)


if __name__ == '__main__':
    unittest.main()
