"""Canonical Pages archive staging boundaries; original fixtures, no network."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('docs_hosting', ROOT / 'tools/publish-docs-site.py')
hosting = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hosting)
COMMIT = 'a' * 40


class DocsHostingTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.folder = self.root / 'artifacts/publication'
        self.folder.mkdir(parents=True)
        self.entries = {'README.md': b'Original docs fixture',
                        'documentation-provenance.json': json.dumps({'source_commit': COMMIT}).encode(),
                        'site/index.html': b'<html>Original docs</html>',
                        'site/search/search_index.json': b'{"docs":[]}'}
        self.write_bundle()

    def write_bundle(self):
        entries = dict(self.entries)
        entries['SHA256SUMS.txt'] = ''.join(hashlib.sha256(data).hexdigest() + '  ' + name + '\n'
                                           for name, data in sorted(entries.items())).encode()
        name = 'CTR2WUP-Public-Docs-0.20.0.zip'
        with zipfile.ZipFile(self.folder / name, 'w') as archive:
            for path, data in entries.items():
                archive.writestr(path, data)
        report = {'ok': True, 'zip_crc_verified': True, 'archive_contains_documentation_only': True,
                  'all_maintained_article_pages_retained': True, 'source_commit': COMMIT,
                  'public_repository': hosting.TARGET, 'application_source_bundled': False,
                  'private_payloads_bundled': False, 'version': '0.20.0', 'public_bundle': name,
                  'artifacts': [{'name': name, 'sha256': hashlib.sha256((self.folder / name).read_bytes()).hexdigest()}]}
        (self.folder / 'publication-report.json').write_text(json.dumps(report))

    def test_staging_copies_only_site_and_provenance_into_fresh_ignored_output(self):
        output = self.root / 'artifacts/site'
        with patch.object(hosting, 'ROOT', self.root):
            self.assertTrue(hosting.stage(self.folder, COMMIT, output)['ok'])
            self.assertEqual((output / 'index.html').read_bytes(), self.entries['site/index.html'])
            self.assertEqual(json.loads((output / 'documentation-provenance.json').read_text())['source_commit'], COMMIT)
            self.assertFalse((output / '.github').exists())
            self.assertFalse((output / 'README.md').exists())
            with self.assertRaises(hosting.PublishError):
                hosting.stage(self.folder, COMMIT, output)
            with self.assertRaises(hosting.PublishError):
                hosting.stage(self.folder, COMMIT, self.root / 'source')

    def test_wrong_source_and_archive_tampering_fail_before_mutation(self):
        with self.assertRaisesRegex(hosting.PublishError, 'identity/privacy'):
            hosting.bundle(self.folder, 'b' * 40)
        path = self.folder / 'CTR2WUP-Public-Docs-0.20.0.zip'
        path.write_bytes(path.read_bytes() + b'tamper')
        with self.assertRaisesRegex(hosting.PublishError, 'checksum'):
            hosting.bundle(self.folder, COMMIT)

    def test_source_keys_traversal_workflows_and_executables_are_rejected(self):
        for path, data in [('site/main.c', b'original test source'), ('site/user.keys', b'not a real key'),
                           ('site/../private.txt', b'private fixture'), ('site/fake.txt', b'\x7fELFfixture'),
                           ('.github/workflows/pages.yml', b'name: arbitrary workflow')]:
            with self.subTest(path=path):
                self.entries[path] = data
                self.write_bundle()
                with self.assertRaises(hosting.PublishError):
                    hosting.bundle(self.folder, COMMIT)
                del self.entries[path]


if __name__ == '__main__':
    unittest.main()
