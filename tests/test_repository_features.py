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
        self.repository = {'full_name': features.REPOSITORY, 'private': True}
        self.writes = []

    def api(self, path, method='GET', payload=None):
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
        self.assertTrue(self.api.repository['private'])
        self.assertEqual(len(self.api.writes), 3)
        values = self.api.writes[0][2]
        self.assertEqual(set(values), {'description', 'homepage', 'has_discussions', 'delete_branch_on_merge'})
        self.assertFalse(any('security-and-analysis' in path or 'protection' in path for path, _, _ in self.api.writes))

    def test_paid_or_protection_policy_cannot_be_overridden(self):
        self.settings['paid_features_enabled'] = True
        with self.assertRaises(features.publisher.PublishError):
            features.configure(self.api, self.settings, True)
        self.assertEqual(self.api.writes, [])

    def test_public_source_is_never_changed(self):
        self.api.repository['private'] = False
        with self.assertRaises(features.publisher.PublishError):
            features.configure(self.api, self.settings, True)
        self.assertEqual(self.api.writes, [])
