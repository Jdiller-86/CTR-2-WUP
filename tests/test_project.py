"""Public snapshots preserve native tracking without leaking Project-only data."""
import copy
from datetime import datetime, timezone
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('project_tracking', ROOT / 'tools/project.py')
project = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(project)
SHA = 'a' * 40


def issue(number, status='In progress', *, archived=False, state='OPEN', body='Original scope.'):
    return {'id': f'item-{number}', 'isArchived': archived, 'content': {
        '__typename': 'Issue', 'id': f'issue-{number}', 'number': number,
        'title': f'M{number}: Gate' if number < 8 else 'Original task',
        'url': f'https://github.com/{project.REPOSITORY}/issues/{number}', 'body': body,
        'state': state, 'repository': {'nameWithOwner': project.REPOSITORY, 'isPrivate': False},
        'milestone': {'number': 20, 'title': 'M3: Runtime', 'url': 'https://github.com/example'},
        'parent': None, 'labels': {'nodes': [], 'pageInfo': {'hasNextPage': False}},
        'blockedBy': {'nodes': [], 'pageInfo': {'hasNextPage': False}}},
        'status': {'__typename': 'ProjectV2ItemFieldSingleSelectValue', 'optionId': status, 'name': status},
        'waiting': None, 'workType': None}


def fixture():
    return {'id': 'project', 'fields': [
        {'id': 'status', 'name': 'Status', 'dataType': 'SINGLE_SELECT',
         'options': [{'id': status, 'name': status} for status in project.STATUSES]},
        {'id': 'waiting', 'name': 'Waiting resource', 'dataType': 'TEXT'},
        {'id': 'type', 'name': 'Work type', 'dataType': 'SINGLE_SELECT', 'options': []}],
        'items': [issue(number) for number in range(1, 8)]}


class PublicSnapshotTests(unittest.TestCase):
    def test_native_hierarchy_blockers_acceptance_and_archived_history(self):
        data = fixture()
        row = issue(25, 'Done', state='CLOSED', archived=True, body=(
            'Checked guest audio path.\n\n- [x] Buffer reference\n- [ ] Console acceptance\n\n'
            '[Evidence](https://github.com/Jdiller-86/CTR-2-WUP/issues/25#issuecomment-1)\n'
            '[Private](https://github.com/private/repository/issues/8)'))
        row['content']['parent'] = {'number': 3, 'repository': {'nameWithOwner': project.REPOSITORY}}
        row['content']['blockedBy']['nodes'] = [
            {'number': 37, 'repository': {'nameWithOwner': project.REPOSITORY}},
            {'number': 99, 'repository': {'nameWithOwner': 'private/repository'}}]
        data['items'].append(row)
        output = project.snapshot(data, SHA, datetime(2026, 10, 16, tzinfo=timezone.utc))
        item = output['items'][-1]
        self.assertEqual(item['parent'], 3)
        self.assertEqual(item['milestone'], 3)
        self.assertEqual(item['blocked_by'], [37])
        self.assertTrue(item['archived'])
        self.assertEqual(item['acceptance'], {'done': 1, 'total': 2})
        self.assertEqual(len(item['evidence']), 1)
        self.assertEqual(output['milestones'][2]['issues'], [25])
        self.assertFalse(any(gate['accepted'] for gate in output['milestones']))
        self.assertEqual(output['generated_at'], '2026-10-16T00:00:00Z')

    def test_known_duplicate_is_excluded_without_discarding_other_history(self):
        data = fixture()
        duplicate = issue(121, 'Closed', state='CLOSED', archived=True)
        duplicate['content']['labels']['nodes'] = [{'name': 'duplicate'}]
        data['items'] += [duplicate, issue(119, 'Done', state='CLOSED', archived=True)]
        numbers = [item['number'] for item in project.snapshot(data, SHA)['items']]
        self.assertIn(119, numbers)
        self.assertNotIn(121, numbers)

    def test_native_implementation_links_include_only_public_repository_pull_requests(self):
        data = fixture()
        nodes = []
        for number, repository, private in [
            (120, project.REPOSITORY, False),
            (121, 'private/repository', False),
            (122, project.REPOSITORY, True)]:
            nodes.append({'number': number, 'repository': {'nameWithOwner': repository, 'isPrivate': private},
                          'url': f'https://github.com/{repository}/pull/{number}', 'state': 'MERGED'})
        data['items'][0]['content']['closedByPullRequestsReferences'] = {'nodes': nodes}
        links = project.snapshot(data, SHA)['items'][0]['evidence']
        self.assertEqual(links, [{'text': 'PR #120 (merged)',
                                 'url': f'https://github.com/{project.REPOSITORY}/pull/120'}])

    def test_full_gate_acceptance_requires_project_accepted_and_closed_issue(self):
        data = fixture()
        data['items'][0] = issue(1, 'Accepted')
        data['items'][1] = issue(2, 'Done', state='CLOSED')
        data['items'][2] = issue(3, 'Accepted', state='CLOSED')
        gates = project.snapshot(data, SHA)['milestones']
        self.assertEqual([gate['accepted'] for gate in gates], [False, False, True, False, False, False, False])

    def test_private_repository_drafts_pull_requests_and_project_notes_never_export(self):
        data = fixture()
        for number, kind, repository, private in [
            (90, 'Issue', project.REPOSITORY, True),
            (91, 'Issue', 'private/repository', False),
            (92, 'DraftIssue', project.REPOSITORY, False),
            (93, 'PullRequest', project.REPOSITORY, False)]:
            row = issue(number, body='SECRET body')
            row['content']['__typename'] = kind
            row['content']['repository'] = {'nameWithOwner': repository, 'isPrivate': private}
            data['items'].append(row)
        data['items'][0]['waiting'] = {'__typename': 'ProjectV2ItemFieldTextValue',
            'text': 'Wii U console results; SECRET notes; Native Windows conversion validation'}
        data['items'][0]['private_note'] = 'SECRET arbitrary Project metadata'
        data['readme'] = 'SECRET Project README'
        data['shortDescription'] = 'SECRET Project description'
        output = project.snapshot(data, SHA)
        self.assertEqual(len(output['items']), 7)
        self.assertEqual(output['items'][0]['waiting'], ['Console testing', 'Windows testing'])
        self.assertNotIn('SECRET', json.dumps(output))
        self.assertNotIn('private/repository', json.dumps(output))

    def test_status_is_case_insensitive_but_unknown_names_are_not_published(self):
        data = fixture()
        for option in data['fields'][0]['options']:
            option['name'] = option['name'].upper()
        for row in data['items']:
            row['status']['name'] = row['status']['name'].upper()
        data['fields'][0]['options'].append({'id': 'unknown', 'name': 'PRIVATE STATUS'})
        data['items'][0]['status'].update(optionId='unknown', name='PRIVATE STATUS')
        result = project.snapshot(data, SHA)
        self.assertEqual(result['items'][0]['status'], 'Other')
        self.assertEqual(result['items'][1]['status'], 'In progress')
        self.assertNotIn('PRIVATE STATUS', json.dumps(result))

    def test_duplicate_missing_gate_and_truncated_dependency_lists_fail_closed(self):
        for change in ('duplicate', 'gate', 'blocked'):
            with self.subTest(change=change):
                data = fixture()
                if change == 'duplicate':
                    data['items'].append(copy.deepcopy(data['items'][0]))
                elif change == 'gate':
                    data['items'].pop()
                else:
                    data['items'][0]['content']['blockedBy']['pageInfo']['hasNextPage'] = True
                with self.assertRaises(project.TrackingError):
                    project.snapshot(data, SHA)

    def test_unavailable_export_is_fresh_and_contains_no_raw_error(self):
        class Client:
            def project(self):
                raise project.TrackingError('SECRET API server response')
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'nested/progress.json'
            result = project.write_snapshot(Client(), path, SHA)
            self.assertEqual(result['state'], 'unavailable')
            self.assertEqual(result['source_commit'], SHA)
            self.assertEqual(result['items'], [])
            self.assertEqual(json.loads(path.read_text()), result)
            self.assertNotIn('SECRET', path.read_text())


class UpdateTests(unittest.TestCase):
    class Client:
        def __init__(self, data=None):
            self.calls = []
            self.data = copy.deepcopy(data or fixture())
            self.issue_response = {}
        def api(self, path):
            return self.issue_response
        def project(self):
            return copy.deepcopy(self.data)
        def mutate(self, name, input_type, payload):
            self.calls.append((name, input_type, payload))
            if name == 'addProjectV2ItemById':
                self.data['items'].append(issue(self.issue_response['number']))
                return
            row = next(row for row in self.data['items'] if row['id'] == payload['itemId'])
            if name == 'unarchiveProjectV2Item':
                row['isArchived'] = False
            elif name == 'clearProjectV2ItemFieldValue':
                row['waiting'] = None
            elif payload['fieldId'] == 'status':
                option = payload['value']['singleSelectOptionId']
                row['status'] = {'__typename': 'ProjectV2ItemFieldSingleSelectValue',
                                 'optionId': option, 'name': option}
            else:
                row['waiting'] = {'__typename': 'ProjectV2ItemFieldTextValue',
                                  'text': payload['value']['text']}

    def test_preview_never_writes_and_apply_updates_only_named_public_fields(self):
        client, data = self.Client(), fixture()
        result = project.update_item(client, data, 1, 'Awaiting evidence', ['Console testing'])
        self.assertFalse(result['applied'])
        self.assertEqual(client.calls, [])
        with patch.object(project, 'trusted_main', return_value=SHA) as trust:
            result = project.update_item(client, data, 1, 'Awaiting evidence', [], True)
        trust.assert_called_once_with(client)
        self.assertTrue(result['applied'])
        self.assertEqual([call[0] for call in client.calls],
                         ['updateProjectV2ItemFieldValue', 'clearProjectV2ItemFieldValue'])
        self.assertEqual(client.calls[0][2]['fieldId'], 'status')
        self.assertEqual(client.calls[1][2]['fieldId'], 'waiting')

    def test_new_issue_preview_then_add_update_and_fresh_readback(self):
        client, data = self.Client(), fixture()
        client.issue_response = {'number': 130, 'node_id': 'issue-130', 'state': 'open',
            'repository_url': f'https://api.github.com/repos/{project.REPOSITORY}',
            'html_url': f'https://github.com/{project.REPOSITORY}/issues/130'}
        preview = project.update_item(client, data, 130, 'Awaiting evidence', ['Console testing'])
        self.assertTrue(preview['will_add'])
        self.assertFalse(preview['applied'])
        self.assertEqual(client.calls, [])
        with patch.object(project, 'trusted_main', return_value=SHA):
            result = project.update_item(client, data, 130, 'Awaiting evidence', ['Console testing'], True)
        self.assertTrue(result['applied'])
        self.assertTrue(result['verified'])
        self.assertEqual([call[0] for call in client.calls], [
            'addProjectV2ItemById', 'updateProjectV2ItemFieldValue', 'updateProjectV2ItemFieldValue'])
        self.assertEqual(client.project()['items'][-1]['status']['name'], 'Awaiting evidence')
        client.issue_response['pull_request'] = {}
        with self.assertRaises(project.TrackingError):
            project.update_item(client, data, 130, 'In progress')

    def test_explicit_archived_update_unarchives_and_mismatched_readback_fails(self):
        data = fixture()
        data['items'][0]['isArchived'] = True
        client = self.Client(data)
        with patch.object(project, 'trusted_main', return_value=SHA):
            result = project.update_item(client, data, 1, 'Awaiting evidence', apply=True)
        self.assertTrue(result['will_unarchive'])
        self.assertEqual(client.calls[0][0], 'unarchiveProjectV2Item')
        self.assertFalse(client.project()['items'][0]['isArchived'])
        for status, waiting in [('Awaiting evidence', None), (None, ['Console testing'])]:
            client = self.Client()
            client.mutate = lambda *args: client.calls.append(args)
            with patch.object(project, 'trusted_main', return_value=SHA):
                with self.assertRaisesRegex(project.TrackingError, 'readback did not match'):
                    project.update_item(client, fixture(), 2, status, waiting, True)

    def test_exact_planned_option_wins_when_default_todo_still_exists(self):
        client, data = self.Client(), fixture()
        data['fields'][0]['options'].append({'id': 'default-todo', 'name': 'Todo'})
        with patch.object(project, 'trusted_main', return_value=SHA):
            project.update_item(client, data, 2, 'Planned', apply=True)
        self.assertEqual(client.calls[0][2]['value']['singleSelectOptionId'], 'Planned')

    def test_updates_fail_before_writing_for_unknown_issue_or_invalid_gate_acceptance(self):
        client, data = self.Client(), fixture()
        data['items'].append(issue(25))
        for number, status in [(99, 'Done'), (1, 'Done'), (1, 'Accepted'), (25, 'Accepted')]:
            with self.subTest(number=number, status=status):
                with self.assertRaises(project.TrackingError):
                    project.update_item(client, data, number, status, apply=True)
        self.assertEqual(client.calls, [])

    def test_apply_rejects_nontrusted_checkout_before_writing(self):
        client = self.Client()
        with patch.object(project, 'trusted_main', side_effect=project.TrackingError('not current main')):
            with self.assertRaises(project.TrackingError):
                project.update_item(client, fixture(), 2, 'In progress', apply=True)
        self.assertEqual(client.calls, [])

    def test_trusted_main_requires_clean_exact_remote_commit(self):
        client = self.Client()
        client.api = lambda path: {'sha': SHA}
        def output(value):
            return subprocess.CompletedProcess([], 0, value, '')
        with patch.object(project.subprocess, 'run', side_effect=[output(''), output(SHA)]):
            self.assertEqual(project.trusted_main(client), SHA)
        for outputs in ([output(' M tools/project.py')], [output(''), output('b' * 40)]):
            with patch.object(project.subprocess, 'run', side_effect=outputs):
                with self.assertRaises(project.TrackingError):
                    project.trusted_main(client)


class PaginationTests(unittest.TestCase):
    def test_connection_reads_archived_rows_across_multiple_pages(self):
        client = project.GitHub()
        pages = [
            {'node': {'items': {'nodes': [{'id': 'active', 'isArchived': False}],
                               'pageInfo': {'hasNextPage': True, 'endCursor': 'next'}}}},
            {'node': {'items': {'nodes': [{'id': 'archived', 'isArchived': True}],
                               'pageInfo': {'hasNextPage': False}}}}]
        with patch.object(client, 'graphql', side_effect=pages) as query:
            rows = client.connection('project', 'items', project.ITEMS)
        self.assertEqual([row['id'] for row in rows], ['active', 'archived'])
        for call in query.call_args_list:
            self.assertIn('archivedStates:[ARCHIVED,NOT_ARCHIVED]', call.args[0])

    def test_archived_states_argument_is_not_applied_to_field_pagination(self):
        client = project.GitHub()
        page = {'node': {'fields': {'nodes': [{'id': 'status'}],
                                   'pageInfo': {'hasNextPage': False}}}}
        with patch.object(client, 'graphql', return_value=page) as query:
            client.connection('project', 'fields', project.FIELDS)
        self.assertNotIn('archivedStates', query.call_args.args[0])

    def test_repeated_cursors_and_items_fail_instead_of_silently_truncating(self):
        client = project.GitHub()
        page = {'node': {'items': {'nodes': [{'id': 'repeated'}],
                                  'pageInfo': {'hasNextPage': True, 'endCursor': 'same'}}}}
        with patch.object(client, 'graphql', return_value=page):
            with self.assertRaises(project.TrackingError):
                client.connection('project', 'items', project.ITEMS)


if __name__ == '__main__':
    unittest.main()
