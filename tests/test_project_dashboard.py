"""The online Project graph publishes only a bounded public snapshot."""
from copy import deepcopy
from datetime import datetime, timedelta, timezone
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('graph_hosting', ROOT / 'tools/publish-docs-site.py')
hosting = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(hosting)
COMMIT = 'a' * 40


def snapshot():
    data = {'schema_version': 2, 'repository': hosting.SOURCE,
            'project_url': 'https://github.com/users/Jdiller-86/projects/2',
            'source_commit': COMMIT, 'state': 'available', 'stale_after_seconds': 7200,
            'generated_at': datetime.now(timezone.utc).isoformat(), 'milestones': [], 'items': []}
    for number in range(1, 8):
        url = f'https://github.com/{hosting.SOURCE}/issues/{number}'
        data['milestones'].append({'number': number, 'title': f'M{number}: Original gate',
                                  'url': url, 'status': 'Planned', 'accepted': False,
                                  'issues': [12] if number == 1 else [], 'depends_on': []})
        data['items'].append({'number': number, 'title': f'M{number}: Original gate',
                              'url': url, 'state': 'OPEN', 'status': 'Planned',
                              'milestone': number, 'parent': None, 'labels': [], 'blocked_by': [],
                              'summary': 'Original public scope.', 'acceptance': {'done': 0, 'total': 1},
                              'evidence': [], 'work_type': 'Milestone', 'archived': False, 'waiting': []})
    data['items'].append({**deepcopy(data['items'][0]), 'number': 12, 'title': 'Original task',
                          'url': f'https://github.com/{hosting.SOURCE}/issues/12', 'parent': 1,
                          'work_type': 'Task', 'status': 'Awaiting evidence',
                          'evidence': [{'text': 'Public build', 'url': f'https://github.com/{hosting.SOURCE}/actions/runs/42'}],
                          'waiting': ['Console testing']})
    return data


class ProjectDashboardTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)
        self.path = self.folder / 'project.json'

    def write(self, data):
        self.path.write_text(json.dumps(data), encoding='utf-8')

    def read(self, data):
        self.write(data)
        return hosting.public_project_snapshot(self.path, COMMIT)

    def test_public_allowlist_preserves_hierarchy_evidence_and_timestamp_without_extra_fields(self):
        data = snapshot()
        data['private_project_notes'] = 'must not be published'
        data['items'][-1]['private_field'] = 'must not be published'
        data['items'][-1]['waiting'].append('private project note')
        output = self.read(data)
        self.assertEqual(output['state'], 'available')
        self.assertEqual(output['generated_at'], data['generated_at'])
        self.assertEqual(output['items'][-1]['parent'], 1)
        self.assertEqual(output['items'][-1]['waiting'], ['Console testing'])
        self.assertEqual(output['items'][-1]['evidence'], data['items'][-1]['evidence'])
        self.assertNotIn('must not be published', json.dumps(output))
        self.assertNotIn('private project note', json.dumps(output))

    def test_source_mismatch_missing_gate_unsafe_link_and_false_acceptance_are_unavailable(self):
        for change in ('source', 'gate', 'evidence', 'accepted', 'future', 'duplicate'):
            with self.subTest(change=change):
                data = snapshot()
                if change == 'source':
                    data['source_commit'] = 'b' * 40
                elif change == 'gate':
                    data['milestones'].pop()
                elif change == 'evidence':
                    data['items'][-1]['evidence'][0]['url'] = 'https://example.com/private'
                elif change == 'accepted':
                    data['milestones'][0]['accepted'] = True
                elif change == 'future':
                    data['generated_at'] = (datetime.now(timezone.utc) + timedelta(hours=1)).isoformat()
                else:
                    data['items'].append(deepcopy(data['items'][0]))
                self.assertEqual(self.read(data)['state'], 'unavailable')

    def test_full_gate_acceptance_requires_both_project_status_and_closed_issue(self):
        data = snapshot()
        data['milestones'][0]['status'] = data['items'][0]['status'] = 'Accepted'
        self.assertFalse(self.read(data)['milestones'][0]['accepted'])
        data['milestones'][0]['accepted'] = True
        self.assertEqual(self.read(data)['state'], 'unavailable')
        data['items'][0]['state'] = 'CLOSED'
        self.assertTrue(self.read(data)['milestones'][0]['accepted'])

    def test_unavailable_malformed_or_missing_snapshot_never_prevents_documentation_staging(self):
        for data in (None, [], {'state': 'unavailable'}, snapshot() | {'state': 'unavailable'}):
            with self.subTest(data_type=type(data).__name__):
                self.assertEqual(self.read(data)['state'], 'unavailable')
        self.path.unlink()
        self.assertEqual(hosting.public_project_snapshot(self.path, COMMIT)['state'], 'unavailable')

    def test_online_graph_is_separate_from_checked_offline_archive(self):
        self.write(snapshot())
        output = self.folder / 'site'
        output.mkdir()
        (output / 'index.html').write_text('Original documentation')
        state = hosting.stage_progress(output, self.path, COMMIT)
        self.assertEqual(state, 'available')
        self.assertEqual((output / 'index.html').read_text(), 'Original documentation')
        self.assertIn('Project progress', (output / 'progress/index.html').read_text())
        self.assertEqual(json.loads((output / 'progress/progress.json').read_text())['source_commit'], COMMIT)


if __name__ == '__main__':
    unittest.main()
