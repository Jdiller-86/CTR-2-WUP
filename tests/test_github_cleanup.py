"""The one-time cleanup preserves evidence and cannot accept milestone gates."""
import copy
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('github_cleanup', ROOT / 'tools/simplify-github.py')
cleanup = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(cleanup)


def fixture():
    issues = {n: {'number': n, 'node_id': f'issue{n}', 'state': 'open', 'title': 'Old planning title',
                   'body': 'Original evidence retained', 'labels': [{'name': 'status:planned'}]}
              for n in cleanup.KEEP | {13, 500}}
    issues[121] = {'number': 121, 'node_id': 'issue121', 'state': 'closed', 'body': 'Duplicate history'}
    options = [{'id': f'option{i}', 'name': name} for i, name in enumerate(('Planned', 'In progress', 'Awaiting evidence'))]
    project = {'id': 'project2', 'fields': {'nodes': [{'id': 'status', 'name': 'Status', 'options': options}]},
               'items': [{'id': f'item{n}', 'isArchived': False, 'status': {'name': 'Planned'},
                          'content': {'__typename': 'Issue', 'id': f'issue{n}', 'number': n,
                                      'state': 'OPEN', 'repository': {'nameWithOwner': cleanup.REPO}}}
                         for n in cleanup.KEEP]}
    project['items'] += [
        {'id': 'duplicate121', 'isArchived': False, 'content': {'__typename': 'Issue', 'number': 121,
          'state': 'CLOSED', 'repository': {'nameWithOwner': cleanup.REPO}}},
        {'id': 'pr120', 'isArchived': False, 'content': {'__typename': 'PullRequest', 'number': 120,
          'repository': {'nameWithOwner': cleanup.REPO}}},
        {'id': 'foreign', 'isArchived': False, 'content': {'__typename': 'Issue', 'number': 1,
          'state': 'CLOSED', 'repository': {'nameWithOwner': 'other/repo'}}}]
    hierarchy = {f'g{n}': {'id': f'issue{n}', 'subIssues': {'nodes': [], 'pageInfo': {'hasNextPage': False}}} for n in cleanup.GATES}
    hierarchy.update({f't{n}': {'id': f'issue{n}', 'parent': {'id': f'issue{row[0]}', 'number': row[0]}} for n, row in cleanup.TASKS.items()})
    hierarchy['t80']['parent'] = {'id': 'issue28', 'number': 28}
    project['hierarchy'] = hierarchy
    project['milestones'] = [{'number': n, 'title': f'M{n}: Original gate', 'description': 'Old notes'} for n in cleanup.GATES]
    return issues, project


class FakeGitHub:
    def __init__(self):
        self.issue_rows, self.project_row = fixture()
        self.writes = []

    def api(self, path, method='GET', payload=None):
        if method == 'GET':
            return {'full_name': cleanup.REPO, 'private': False, 'default_branch': 'main'}
        self.writes.append((path, payload))
        n = int(path.rsplit('/', 1)[1])
        if '/milestones/' in path:
            next(m for m in self.project_row['milestones'] if m['number'] == n).update(payload)
            return
        for key, value in payload.items():
            self.issue_rows[n][key] = [{'name': label} for label in value] if key == 'labels' else value

    def issues(self):
        return copy.deepcopy(self.issue_rows)

    def project(self):
        return copy.deepcopy(self.project_row)

    def milestones(self):
        return copy.deepcopy(self.project_row['milestones'])

    def hierarchy(self):
        return copy.deepcopy(self.project_row['hierarchy'])

    def mutate(self, name, input_type, payload):
        self.writes.append((name, payload))
        if name == 'updateProjectV2':
            self.project_row.update({k: v for k, v in payload.items() if k != 'projectId'})
        elif name == 'removeSubIssue':
            n = int(payload['subIssueId'].removeprefix('issue'))
            self.project_row['hierarchy'][f't{n}']['parent'] = None
        elif name == 'addSubIssue':
            n = int(payload['subIssueId'].removeprefix('issue'))
            self.project_row['hierarchy'][f't{n}']['parent'] = {'id': payload['issueId'], 'number': int(payload['issueId'].removeprefix('issue'))}
        else:
            item = next(i for i in self.project_row['items'] if i['id'] == payload['itemId'])
            if name == 'archiveProjectV2Item':
                item['isArchived'] = True
            elif name == 'updateProjectV2ItemFieldValue':
                options = self.project_row['fields']['nodes'][0]['options']
                item['status'] = {'name': next(o['name'] for o in options if o['id'] == payload['value']['singleSelectOptionId'])}
            else:
                raise AssertionError(name)


class GitHubCleanupTests(unittest.TestCase):
    def test_dry_run_closes_only_fixed_superseded_scope_and_preserves_evidence(self):
        api = FakeGitHub()
        result = cleanup.cleanup(api)
        self.assertEqual(api.writes, [])
        self.assertEqual(result['close_superseded'], [13])
        self.assertEqual(result['archive_items'], 2)
        operations = cleanup.plan(*fixture())
        closing = next(o for o in operations if o['kind'] == 'issue' and o['number'] == 13)
        self.assertEqual(closing['payload']['state_reason'], 'not_planned')
        self.assertIn('Original evidence retained', closing['payload']['body'])
        self.assertFalse(any(o.get('number') == 500 for o in operations))
        self.assertFalse(any(o.get('payload', {}).get('state') == 'closed' and o.get('number') in cleanup.GATES for o in operations))

    def test_apply_requires_reviewed_exact_plan_and_reads_back_idempotently(self):
        api = FakeGitHub(); dry = cleanup.cleanup(api)
        with patch.object(cleanup, 'trusted_source', return_value='a' * 40) as trusted:
            with self.assertRaisesRegex(cleanup.CleanupError, 'plan changed'):
                cleanup.cleanup(api, True, '0' * 64)
            self.assertEqual(api.writes, [])
            result = cleanup.cleanup(api, True, dry['plan_sha256'])
            self.assertTrue(result['readback_verified'])
            self.assertEqual(result['milestones_accepted_by_cleanup'], 0)
            self.assertEqual(trusted.call_count, 2)
            writes = len(api.writes)
            dry = cleanup.cleanup(api)
            self.assertEqual(dry['operation_count'], 0)
            cleanup.cleanup(api, True, dry['plan_sha256'])
            self.assertEqual(len(api.writes), writes)
        self.assertTrue(all(api.issue_rows[n]['state'] == 'open' for n in cleanup.GATES))
        self.assertEqual(api.project_row['hierarchy']['t80']['parent']['number'], 4)


if __name__ == '__main__':
    unittest.main()
