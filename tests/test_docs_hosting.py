"""Public documentation publication boundaries; original fixtures, no network."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('docs_hosting', ROOT / 'tools/publish-docs-site.py')
hosting = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hosting)
COMMIT = 'a' * 40


class FakeGitHub:
    def __init__(self):
        self.target = {'full_name': hosting.TARGET, 'private': False, 'default_branch': 'main'}
        self.source_private = True
        self.login = 'Jdiller-86'
        self.writes = []

    def api(self, path, method='GET', payload=None, missing=False):
        if method != 'GET':
            self.writes.append((path, method, copy.deepcopy(payload)))
            if path == 'user/repos':
                self.target = {'full_name': hosting.TARGET, 'private': False, 'default_branch': 'main'}
                return self.target
            return {'sha': 'd' * 40}
        if path == 'repos/' + hosting.SOURCE:
            return {'full_name': hosting.SOURCE, 'private': self.source_private}
        if path == 'repos/' + hosting.TARGET:
            return self.target
        if path == 'user':
            return {'login': self.login}
        if path.endswith('/pages'):
            return None
        if path.endswith('/git/ref/heads/main'):
            return {'object': {'sha': 'b' * 40}}
        if '/git/commits/' in path:
            return {'tree': {'sha': 'c' * 40}}
        if '/git/trees/' in path:
            return {'truncated': False, 'tree': [{'path': 'site/progress/progress.json', 'type': 'blob', 'sha': 'e' * 40}]}
        raise AssertionError(path)


class DocsHostingTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.folder = Path(self.temporary.name)
        self.api = FakeGitHub()
        self.entries = {'README.md': b'Original docs fixture',
                        'documentation-provenance.json': json.dumps({'source_commit': COMMIT}).encode(),
                        '.github/workflows/pages.yml': hosting.docs.PAGES_WORKFLOW.encode(),
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
        result = hosting.publish(self.folder, COMMIT, self.api)
        self.assertTrue(result['ok'])
        self.assertEqual(self.api.writes, [])

    def test_publication_preserves_existing_tree_and_progress_and_never_forces(self):
        result = hosting.publish(self.folder, COMMIT, self.api, apply=True)
        tree = next(payload for path, _, payload in self.api.writes if path.endswith('/git/trees'))
        self.assertEqual(tree['base_tree'], 'c' * 40)
        self.assertFalse(any(row['path'].startswith('site/progress/') for row in tree['tree']))
        update = self.api.writes[-1]
        self.assertEqual(update[2]['force'], False)
        self.assertFalse(result['pages_deployment_verified'])
        self.assertTrue(all(path.startswith('repos/' + hosting.TARGET + '/') for path, _, _ in self.api.writes))

    def test_creation_is_explicit_and_dry_run_is_read_only(self):
        self.api.target = None
        with self.assertRaisesRegex(hosting.PublishError, 'explicit --create'):
            hosting.publish(self.folder, COMMIT, self.api, apply=True)
        result = hosting.publish(self.folder, COMMIT, self.api, create=True)
        self.assertTrue(result['would_create_repository'])
        self.assertEqual(self.api.writes, [])
        result = hosting.publish(self.folder, COMMIT, self.api, create=True, apply=True)
        self.assertTrue(result['repository_created'])
        self.assertEqual(self.api.writes[0][0], 'user/repos')
        self.assertFalse(self.api.writes[0][2]['private'])

    def test_creation_never_uses_another_identity(self):
        self.api.target = None
        self.api.login = 'another-owner'
        with self.assertRaisesRegex(hosting.PublishError, 'authorized owner'):
            hosting.publish(self.folder, COMMIT, self.api, create=True, apply=True)
        self.assertEqual(self.api.writes, [])

    def test_preserves_unexpected_existing_visibility_or_branch(self):
        for change in ({'private': True}, {'default_branch': 'owner-branch'}):
            with self.subTest(change=change):
                self.api.target.update(change)
                with self.assertRaises(hosting.PublishError):
                    hosting.publish(self.folder, COMMIT, self.api, apply=True)
                self.assertEqual(self.api.writes, [])
                self.api.target.update(private=False, default_branch='main')

    def test_wrong_source_commit_and_archive_tampering_fail_before_mutation(self):
        with self.assertRaisesRegex(hosting.PublishError, 'identity/privacy'):
            hosting.publish(self.folder, 'b' * 40, self.api, apply=True)
        path = self.folder / 'CTR2WUP-Public-Docs-0.20.0.zip'
        path.write_bytes(path.read_bytes() + b'tamper')
        with self.assertRaisesRegex(hosting.PublishError, 'checksum'):
            hosting.publish(self.folder, COMMIT, self.api, apply=True)
        self.assertEqual(self.api.writes, [])

    def test_source_keys_traversal_and_executable_payloads_are_rejected(self):
        for path, data in [('site/main.c', b'original test source'), ('site/user.keys', b'not a real key'),
                           ('site/../private.txt', b'private fixture'), ('site/fake.txt', b'\x7fELFfixture')]:
            with self.subTest(path=path):
                self.entries[path] = data
                self.write_bundle()
                with self.assertRaises(hosting.PublishError):
                    hosting.publish(self.folder, COMMIT, self.api, apply=True)
                self.assertEqual(self.api.writes, [])
                del self.entries[path]

    def test_unreviewed_workflow_cannot_be_published(self):
        self.entries['.github/workflows/pages.yml'] = b'name: arbitrary workflow'
        self.write_bundle()
        with self.assertRaisesRegex(hosting.PublishError, 'workflow differs'):
            hosting.publish(self.folder, COMMIT, self.api, apply=True)
        self.assertEqual(self.api.writes, [])

    def test_application_visibility_is_never_changed(self):
        self.api.source_private = False
        with self.assertRaisesRegex(hosting.PublishError, 'remain private'):
            hosting.publish(self.folder, COMMIT, self.api, apply=True)
        self.assertEqual(self.api.writes, [])


if __name__ == '__main__':
    unittest.main()
