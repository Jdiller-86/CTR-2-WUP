"""Legacy redirects preserve reviewed scope, owner edits, history and deep links."""
import hashlib
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('legacy_migration', ROOT / 'tools/migrate-legacy-docs.py')
migration = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(migration)


class LegacyMigrationTests(unittest.TestCase):
    def files(self):
        files = {'site/index.html': b'Original generated overview',
                 'site/docs/guide/index.html': b'Original generated guide',
                 'site/404.html': b'Original missing-page fixture',
                 'README.md': b'Original generated README',
                 'site/manual.txt': b'Keep owner material',
                 'site/progress/progress.json': b'Old available counts'}
        files['SHA256SUMS.txt'] = ''.join(hashlib.sha256(data).hexdigest() + '  ' + name + '\n'
                                         for name, data in files.items() if name.endswith('.html')).encode()
        return files

    def test_only_reviewed_pages_and_progress_are_replaced_with_canonical_links(self):
        files = self.files()
        changes = migration.plan(files)
        self.assertNotIn('site/manual.txt', changes)
        self.assertIn((migration.CANONICAL + 'docs/guide/').encode(), changes['site/docs/guide/index.html'])
        self.assertIn(b'location.search+location.hash', changes['site/docs/guide/index.html'])
        self.assertNotIn(b'Old available counts', changes['site/progress/progress.json'])
        self.assertIn(b'canonical_url', changes['site/progress/progress.json'])
        self.assertIn(b'Original generated README', changes['README.md'])
        self.assertEqual(files['site/manual.txt'], b'Keep owner material')

    def test_owner_modified_article_refuses_before_any_plan_is_applied(self):
        files = self.files()
        files['site/docs/guide/index.html'] += b'Owner edit'
        with self.assertRaisesRegex(migration.publisher.PublishError, 'owner edits'):
            migration.plan(files)

    def test_unsafe_manifest_and_missing_provenance_refuse(self):
        for manifest in (b'', b'not-a-digest  site/index.html\n', b'0' * 64 + b'  site/../owner.html\n'):
            files = self.files()
            files['SHA256SUMS.txt'] = manifest
            with self.assertRaises((migration.publisher.PublishError, ValueError)):
                migration.plan(files)

    def test_redirect_target_cannot_leave_the_canonical_site(self):
        with self.assertRaises(migration.publisher.PublishError):
            migration.redirect('https://example.org/elsewhere')
        self.assertIn(b'rel="canonical"', migration.redirect(migration.CANONICAL))

    def test_archive_requires_verified_redirects_and_preserves_history(self):
        class Client:
            def __init__(self):
                self.archived = False
                self.writes = []
            def api(self, path, method='GET', payload=None):
                if method == 'PATCH':
                    self.writes.append(payload)
                    self.archived = payload['archived']
                if path.endswith('/git/ref/heads/main'):
                    return {'object': {'sha': 'b' * 40}}
                return {'full_name': migration.LEGACY, 'private': False,
                        'default_branch': 'main', 'archived': self.archived}
        client = Client()
        calls = []
        def verify(commit, legacy):
            calls.append((commit, legacy))
        with patch.object(migration.subprocess, 'check_output', return_value='a' * 40):
            dry = migration.retire(client, verify=verify)
            self.assertFalse(dry['legacy_archived'])
            self.assertEqual(client.writes, [])
            result = migration.retire(client, apply=True, verify=verify)
            self.assertTrue(result['legacy_archived'])
            self.assertTrue(result['history_preserved'])
            self.assertEqual(client.writes, [{'archived': True}])
            self.assertTrue(all(legacy for _, legacy in calls))
            migration.retire(client, apply=True, verify=verify)
            self.assertEqual(client.writes, [{'archived': True}])
            client.archived = False
            def fail_after_archive(commit, legacy):
                if client.archived:
                    raise ValueError('Original simulated Pages loss')
            with self.assertRaisesRegex(migration.publisher.PublishError, 'rolled back'):
                migration.retire(client, apply=True, verify=fail_after_archive)
            self.assertFalse(client.archived)


if __name__ == '__main__':
    unittest.main()
