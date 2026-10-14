"""Repository mutation scope and free-plan policy; no GitHub calls."""
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('repository_features', ROOT / 'tools/configure-github-repository.py')
features = importlib.util.module_from_spec(spec)
spec.loader.exec_module(features)


class FeatureGitHub:
    def __init__(self):
        self.repository = {'full_name': features.REPOSITORY, 'private': False}
        self.writes = []
        self.pages = None

    def api(self, path, method='GET', payload=None, missing=False):
        if path.endswith('/pages'):
            if method == 'POST':
                self.writes.append((path, method, payload))
                self.pages = {'build_type': payload['build_type'], 'cname': None,
                              'html_url': 'https://jdiller-86.github.io/CTR-2-WUP/'}
            return self.pages
        if method != 'GET':
            self.writes.append((path, method, payload))
            if method == 'PATCH':
                self.repository.update(payload)
            elif path.endswith('/topics'):
                self.repository['topics'] = payload['names']
        if path.endswith('/vulnerability-alerts'):
            return None
        return dict(self.repository)


class RepositoryFeatureTests(unittest.TestCase):
    def setUp(self):
        self.settings = json.loads((ROOT / '.github/repository-settings.json').read_text())
        self.api = FeatureGitHub()

    def test_dry_run_never_writes(self):
        features.configure(self.api, self.settings)
        self.assertEqual(self.api.writes, [])

    def test_only_requested_features_change_and_visibility_is_preserved(self):
        result = features.configure(self.api, self.settings, True)
        self.assertTrue(result['live_settings_verified'])
        self.assertFalse(self.api.repository['private'])
        self.assertEqual(len(self.api.writes), 4)
        self.assertTrue(result['pages_configuration_verified'])
        self.assertFalse(result['pages_deployment_verified'])
        values = self.api.writes[0][2]
        self.assertEqual(set(values), {'description', 'homepage', 'has_discussions', 'delete_branch_on_merge'})
        self.assertFalse(any('security-and-analysis' in path or 'protection' in path for path, _, _ in self.api.writes))

    def test_paid_or_protection_policy_cannot_be_overridden(self):
        self.settings['paid_features_enabled'] = True
        with self.assertRaises(features.publisher.PublishError):
            features.configure(self.api, self.settings, True)
        self.assertEqual(self.api.writes, [])

    def test_unexpected_private_source_is_preserved(self):
        self.api.repository['private'] = True
        with self.assertRaises(features.publisher.PublishError):
            features.configure(self.api, self.settings, True)
        self.assertEqual(self.api.writes, [])

    def test_existing_workflow_pages_are_preserved_and_custom_settings_refuse(self):
        self.api.pages = {'build_type': 'workflow', 'cname': None,
                          'html_url': 'https://jdiller-86.github.io/CTR-2-WUP/'}
        features.configure(self.api, self.settings, True)
        self.assertFalse(any(path.endswith('/pages') for path, _, _ in self.api.writes))
        self.api.writes.clear()
        self.api.pages['cname'] = 'owner.example'
        with self.assertRaises(features.publisher.PublishError):
            features.configure(self.api, self.settings, True)
        self.assertEqual(self.api.writes, [])
