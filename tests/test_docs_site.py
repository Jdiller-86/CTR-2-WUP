"""Pages identity is verified independently of optional GitHub Project access."""
from datetime import datetime, timedelta, timezone
import importlib.util
import json
from pathlib import Path
import unittest
from urllib.error import HTTPError
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('docs_site_test', ROOT / 'tools/verify-docs-site.py')
site = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(site)


class DocsSiteTests(unittest.TestCase):
    def endpoints(self):
        data = {site.CANONICAL + 'documentation-provenance.json': json.dumps({
            'ok': True, 'source_commit': 'a' * 40, 'public_repository': 'Jdiller-86/CTR-2-WUP',
            'application_source_bundled': False, 'private_payloads_bundled': False}).encode()}
        for route in ('', 'docs/getting-started/'):
            data[site.CANONICAL + route] = b'<html>Original guide</html>'
        return data

    def project_endpoints(self, snapshot):
        data = self.endpoints()
        data[site.CANONICAL + 'progress/'] = b'<html>Project progress</html>'
        data[site.CANONICAL + 'progress/progress.json'] = json.dumps(snapshot).encode()
        data[site.CANONICAL + 'progress/gates.svg'] = ET.tostring(ET.Element(
            '{http://www.w3.org/2000/svg}svg', {
                'data-source-commit': snapshot['source_commit'],
                'data-generated-at': snapshot['generated_at'], 'data-state': snapshot['state']}))
        return data

    def test_current_source_and_usage_page_verify_without_project(self):
        self.assertTrue(site.verify('a' * 40, self.endpoints().__getitem__)['ok'])

    def test_old_source_and_transient_http_retry_without_printing_server_details(self):
        endpoints = self.endpoints(); calls = []; waits = []
        def get(url):
            calls.append(url)
            if len(calls) == 1:
                raise HTTPError(url, 503, 'PRIVATE response detail', {}, None)
            if len(calls) == 2:
                return endpoints[url].replace(('a' * 40).encode(), ('b' * 40).encode())
            return endpoints[url]
        result = site.verify_until_ready('a' * 40, get=get, wait=waits.append)
        self.assertTrue(result['ok'])
        self.assertEqual(result['attempts'], 3)
        self.assertEqual(waits, [10, 10])
        self.assertNotIn('PRIVATE', json.dumps(result))

    def test_unsafe_provenance_and_forbidden_http_fail_immediately(self):
        endpoints = self.endpoints()
        path = site.CANONICAL + 'documentation-provenance.json'
        endpoints[path] = endpoints[path].replace(b'"private_payloads_bundled": false', b'"private_payloads_bundled": true')
        result = site.verify_until_ready('a' * 40, get=endpoints.__getitem__)
        self.assertEqual(result['error_code'], 'provenance_policy')
        self.assertEqual(result['attempts'], 1)

    def test_online_graph_verifies_even_when_project_access_is_unavailable(self):
        endpoints = self.project_endpoints({
            'schema_version': 2, 'repository': 'Jdiller-86/CTR-2-WUP',
            'project_url': 'https://github.com/users/Jdiller-86/projects/2',
            'source_commit': 'a' * 40, 'state': 'unavailable', 'stale_after_seconds': 7200,
            'generated_at': datetime.now(timezone.utc).isoformat()})
        result = site.verify('a' * 40, endpoints.__getitem__, with_project=True)
        self.assertTrue(result['ok'])
        self.assertEqual(result['project_snapshot_state'], 'unavailable')
        self.assertFalse(result['project_snapshot_stale'])
        self.assertTrue(result['embedded_graph_verified'])

    def test_online_graph_reports_stale_data_and_rejects_wrong_source(self):
        snapshot = {'schema_version': 2, 'repository': 'Jdiller-86/CTR-2-WUP',
                    'project_url': 'https://github.com/users/Jdiller-86/projects/2',
                    'source_commit': 'a' * 40, 'state': 'available', 'stale_after_seconds': 7200,
                    'generated_at': (datetime.now(timezone.utc) - timedelta(hours=3)).isoformat()}
        endpoints = self.project_endpoints(snapshot)
        self.assertTrue(site.verify('a' * 40, endpoints.__getitem__, with_project=True)['project_snapshot_stale'])
        snapshot['source_commit'] = 'b' * 40
        endpoints[site.CANONICAL + 'progress/progress.json'] = json.dumps(snapshot).encode()
        result = site.verify_until_ready('a' * 40, get=endpoints.__getitem__, with_project=True,
                                         attempts=1, wait=lambda _: None)
        self.assertFalse(result['ok'])
        self.assertEqual(result['error_code'], 'project_source')

    def test_readme_graph_requires_svg_matching_source_timestamp_and_state(self):
        snapshot = {'schema_version': 2, 'repository': 'Jdiller-86/CTR-2-WUP',
                    'project_url': 'https://github.com/users/Jdiller-86/projects/2',
                    'source_commit': 'a' * 40, 'state': 'available', 'stale_after_seconds': 7200,
                    'generated_at': datetime.now(timezone.utc).isoformat()}
        url = site.CANONICAL + 'progress/gates.svg'
        for case in ('missing', 'html', 'source', 'timestamp', 'state'):
            with self.subTest(case=case):
                endpoints = self.project_endpoints(snapshot)
                if case == 'missing':
                    endpoints[url] = b''
                elif case == 'html':
                    endpoints[url] = b'<html>Error page</html>'
                else:
                    image = ET.fromstring(endpoints[url])
                    attribute = {'source': 'data-source-commit', 'timestamp': 'data-generated-at',
                                 'state': 'data-state'}[case]
                    image.set(attribute, 'different')
                    endpoints[url] = ET.tostring(image)
                result = site.verify_until_ready('a' * 40, get=endpoints.__getitem__,
                                                 with_project=True, attempts=1)
                self.assertFalse(result['ok'])
                self.assertTrue(result['error_code'].startswith('project_image'))

    def test_source_propagation_retry_is_bounded(self):
        waits = []
        result = site.verify_until_ready('b' * 40, attempts=3, get=self.endpoints().__getitem__, wait=waits.append)
        self.assertFalse(result['ok']); self.assertEqual(result['attempts'], 3)
        self.assertEqual(waits, [10, 10])


if __name__ == '__main__':
    unittest.main()
