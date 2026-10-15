"""Deployed identity, freshness and legacy-link evidence uses fixed public routes."""
from datetime import datetime, timedelta, timezone
import importlib.util
import json
from pathlib import Path
import unittest
from urllib.error import HTTPError

from test_github_project_export import fixture

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('project_site_test', ROOT / 'tools/verify-project-site.py')
site = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(site)
NOW = datetime(2026, 10, 14, 16, 0, tzinfo=timezone.utc)


class ProjectSiteTests(unittest.TestCase):
    def endpoints(self):
        source, project = fixture()
        milestones, managed = site.progress.EXPORT.collect(project, source)
        data = site.progress.EXPORT.aggregate(milestones, managed, NOW.strftime('%Y-%m-%dT%H:%M:%SZ'))
        endpoints = {site.CANONICAL + 'documentation-provenance.json': json.dumps({
            'ok': True, 'source_commit': 'a' * 40, 'public_repository': 'Jdiller-86/CTR-2-WUP',
            'application_source_bundled': False, 'private_payloads_bundled': False}).encode(),
            site.CANONICAL + 'progress/progress.json': json.dumps(data).encode()}
        for route in ('', 'docs/first-title/', 'progress/'):
            endpoints[site.CANONICAL + route] = b'<html>Original page</html>'
            endpoints[site.LEGACY + route] = (b'<html>' + site.MARKER +
                (site.CANONICAL + route).encode() + b'location.search+location.hash</html>')
        endpoints[site.LEGACY + 'progress/progress.json'] = json.dumps({
            'schema_version': 1, 'state': 'unavailable',
            'canonical_url': site.CANONICAL + 'progress/progress.json',
            'message': 'This endpoint moved; read the canonical Project snapshot.'}).encode()
        return endpoints

    def test_canonical_and_legacy_routes_verify_without_private_fields(self):
        result = site.verify('a' * 40, self.endpoints().__getitem__, legacy=True, now=NOW)
        self.assertTrue(result['legacy_verified'])
        self.assertEqual(result['full_gates'], {'accepted': 0, 'total': 7})
        self.assertNotIn('PRIVATE', json.dumps(result))

    def test_wrong_source_and_stale_project_refuse(self):
        endpoints = self.endpoints()
        with self.assertRaisesRegex(ValueError, 'source identity'):
            site.verify('b' * 40, endpoints.__getitem__, now=NOW)
        with self.assertRaisesRegex(ValueError, 'stale'):
            site.verify('a' * 40, endpoints.__getitem__, now=NOW + timedelta(hours=3))

    def test_unknown_public_fields_and_independent_legacy_snapshot_refuse(self):
        endpoints = self.endpoints()
        path = site.CANONICAL + 'progress/progress.json'
        data = json.loads(endpoints[path])
        data['raw_notes'] = 'Original forbidden fixture'
        endpoints[path] = json.dumps(data).encode()
        with self.assertRaisesRegex(ValueError, 'privacy'):
            site.verify('a' * 40, endpoints.__getitem__, now=NOW)
        endpoints = self.endpoints()
        endpoints[site.LEGACY + 'progress/progress.json'] = endpoints[path]
        with self.assertRaisesRegex(ValueError, 'independent snapshot'):
            site.verify('a' * 40, endpoints.__getitem__, legacy=True, now=NOW)

    def test_old_source_and_transient_http_recover_with_full_verification(self):
        endpoints = self.endpoints();calls = [];waits = []
        def get(url):
            calls.append(url)
            if len(calls) == 1:raise HTTPError(url, 503, 'PRIVATE response reason', {}, None)
            if len(calls) == 2:
                return endpoints[url].replace(('a'*40).encode(), ('b'*40).encode())
            return endpoints[url]
        report = site.verify_until_ready('a'*40, get=get, wait=waits.append, now=NOW)
        self.assertTrue(report['ok']);self.assertEqual(report['attempts'], 3)
        self.assertEqual(waits, [10, 10])
        self.assertEqual([f['code'] for f in report['retry_failures']], ['http_503', 'source_identity'])
        self.assertNotIn('PRIVATE', json.dumps(report))
        self.assertEqual(report['full_gates'], {'accepted': 0, 'total': 7})

    def test_exhausted_budget_preserves_specific_failure(self):
        waits = []
        report = site.verify_until_ready('b'*40, get=self.endpoints().__getitem__, attempts=3, wait=waits.append, now=NOW)
        self.assertFalse(report['ok']);self.assertEqual(report['error_code'], 'source_identity')
        self.assertEqual(report['attempts'], 3);self.assertEqual(waits, [10, 10])

    def test_policy_schema_and_forbidden_http_fail_without_retry(self):
        endpoints = self.endpoints();path = site.CANONICAL+'progress/progress.json'
        data = json.loads(endpoints[path]);data['raw_notes'] = 'PRIVATE fixture'
        endpoints[path] = json.dumps(data).encode();waits = []
        report = site.verify_until_ready('a'*40, get=endpoints.__getitem__, wait=waits.append, now=NOW)
        self.assertEqual(report['error_code'], 'project_privacy');self.assertEqual(report['attempts'], 1)
        def forbidden(url):raise HTTPError(url, 403, 'PRIVATE server reason', {}, None)
        report = site.verify_until_ready('a'*40, get=forbidden, wait=waits.append, now=NOW)
        self.assertEqual(report['error_code'], 'http_403');self.assertEqual(report['attempts'], 1)
        self.assertEqual(waits, []);self.assertNotIn('PRIVATE', json.dumps(report))

    def test_retry_budget_is_bounded_and_success_does_not_wait(self):
        waits = []
        report = site.verify_until_ready('a'*40, get=self.endpoints().__getitem__, wait=waits.append, now=NOW)
        self.assertTrue(report['ok']);self.assertEqual(report['attempts'], 1);self.assertEqual(waits, [])
        for args in ({'attempts': 0}, {'attempts': 13}, {'delay': -1}, {'delay': 31}):
            with self.assertRaises(ValueError):site.verify_until_ready('a'*40, **args)


if __name__ == '__main__':
    unittest.main()
