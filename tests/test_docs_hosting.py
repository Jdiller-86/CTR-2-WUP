"""Canonical Pages staging/dispatch boundaries; original fixtures, no network."""
import copy
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


class FakeGitHub:
    def __init__(self):
        self.source = {'full_name': hosting.SOURCE, 'private': False, 'default_branch': 'main'}
        self.commit = COMMIT
        self.writes = []

    def api(self, path, method='GET', payload=None, missing=False):
        if method != 'GET':
            self.writes.append((path, method, copy.deepcopy(payload)))
            return None
        if path == 'repos/' + hosting.SOURCE:
            return self.source
        if path == 'repos/' + hosting.SOURCE + '/commits/main':
            return {'sha': self.commit}
        raise AssertionError(path)


class DocsHostingTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.folder = self.root / 'artifacts/publication'
        self.folder.mkdir(parents=True)
        self.api = FakeGitHub()
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

    def test_dry_run_does_not_change_repository_or_pages(self):
        self.assertTrue(hosting.publish(self.folder, COMMIT, self.api)['ok'])
        self.assertEqual(self.api.writes, [])

    def test_publication_dispatches_only_current_main_without_git_writes(self):
        result = hosting.publish(self.folder, COMMIT, self.api, apply=True)
        self.assertEqual(self.api.writes, [('repos/' + hosting.SOURCE + '/actions/workflows/publish-docs.yml/dispatches', 'POST', {'ref': 'main'})])
        self.assertTrue(result['workflow_dispatched'])
        self.assertFalse(result['pages_deployment_verified'])

    def test_stale_main_and_creation_refuse_before_writes(self):
        self.api.commit = 'b' * 40
        with self.assertRaisesRegex(hosting.PublishError, 'current trusted main'):
            hosting.publish(self.folder, COMMIT, self.api, apply=True)
        with self.assertRaisesRegex(hosting.PublishError, 'creation is unsupported'):
            hosting.publish(self.folder, COMMIT, self.api, create=True, apply=True)
        self.assertEqual(self.api.writes, [])

    def test_preserves_unexpected_identity_visibility_and_branch(self):
        for change in ({'private': True}, {'default_branch': 'owner-branch'}, {'full_name': 'other/repo'}):
            with self.subTest(change=change):
                original = dict(self.api.source)
                self.api.source.update(change)
                with self.assertRaises(hosting.PublishError):
                    hosting.publish(self.folder, COMMIT, self.api, apply=True)
                self.assertEqual(self.api.writes, [])
                self.api.source = original

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
            hosting.publish(self.folder, 'b' * 40, self.api, apply=True)
        path = self.folder / 'CTR2WUP-Public-Docs-0.20.0.zip'
        path.write_bytes(path.read_bytes() + b'tamper')
        with self.assertRaisesRegex(hosting.PublishError, 'checksum'):
            hosting.publish(self.folder, COMMIT, self.api, apply=True)
        self.assertEqual(self.api.writes, [])

    def test_source_keys_traversal_workflows_and_executables_are_rejected(self):
        for path, data in [('site/main.c', b'original test source'), ('site/user.keys', b'not a real key'),
                           ('site/../private.txt', b'private fixture'), ('site/fake.txt', b'\x7fELFfixture'),
                           ('.github/workflows/pages.yml', b'name: arbitrary workflow')]:
            with self.subTest(path=path):
                self.entries[path] = data
                self.write_bundle()
                with self.assertRaises(hosting.PublishError):
                    hosting.publish(self.folder, COMMIT, self.api, apply=True)
                self.assertEqual(self.api.writes, [])
                del self.entries[path]


if __name__ == '__main__':
    unittest.main()
