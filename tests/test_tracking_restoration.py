"""The one-time restoration preserves reviewed work and native issue identity."""
import base64
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
PROJECT_SPEC = importlib.util.spec_from_file_location('project', ROOT / 'tools/project.py')
project = importlib.util.module_from_spec(PROJECT_SPEC)
PROJECT_SPEC.loader.exec_module(project)
RESTORE_SPEC = importlib.util.spec_from_file_location(
    'restore_tracking', ROOT / 'tools/restore-project-tracking.py')
restoration = importlib.util.module_from_spec(RESTORE_SPEC)
with patch.dict(sys.modules, {'project': project}):
    RESTORE_SPEC.loader.exec_module(restoration)

FEATURES = (60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73,
            74, 75, 76, 77, 78, 79, 80, 81, 82, 84, 86, 87, 88, 91,
            92, 95, 97, 99, 102, 103, 107, 110, 113, 115, 117, 119)
BASELINE = (12, 14, 27)


def fixture():
    """Synthetic original issue IDs, with the exact post-cleanup state shape."""
    requirements = {'schema_version': 1, 'milestones': []}
    for gate in range(1, 8):
        tasks = []
        for offset in range(5):
            number = 12 + (gate - 1) * 5 + offset
            task_id = restoration.TASK_IDS.get(number, f'task-{number}')
            tasks.append({'id': task_id, 'title': f'Task {number}',
                          'summary': f'Bounded scope {number}.',
                          'acceptance_criteria': [f'Observable behavior {number}.'],
                          'depends_on': []})
        requirements['milestones'].append({
            'id': f'm{gate}', 'summary': f'Gate {gate} scope.',
            'verified_foundations': [f'Existing foundation {gate}.'],
            'acceptance_remaining': [f'Full acceptance {gate}.'], 'work_items': tasks})
    requirements['milestones'][0]['work_items'][4]['depends_on'] = ['task-13', 'task-14', 'task-15']
    requirements['milestones'][2]['work_items'][3]['depends_on'] = ['task-23']
    feature_source = {'schema_version': 1, 'features': [{
        'id': 'pica-gx2', 'scope': ['Translate checked vertex programs.'],
        'acceptance': ['Match independently checked scene output.']}]}
    repo, rows = {}, []
    for number in (*range(1, 8), *range(12, 47), *FEATURES):
        closed = number not in (*range(1, 8), 25, 37, 80)
        feature = number in FEATURES and number != 80
        labels = ['feature', 'work-item'] if feature else ['work-item']
        reason = 'not_planned' if number in restoration.REOPEN else 'completed' if closed else None
        body = (f'<!-- ctr2wup:work-item:task-{number} -->\n'
                '- [x] Previously reviewed foundation.\n- [ ] Remaining criterion.\n\n'
                'Original evidence and maintainer note.\n')
        if number in range(1, 8):
            body = f'Milestone M{number}: shortened overview.'
        elif number in (25, 37, 80):
            body = f'Current implementation and evidence #{number}.'
        elif number in restoration.REOPEN:
            body = restoration.BANNER + body
        issue = {'number': number, 'node_id': f'issue-{number}', 'title': f'Issue {number}',
                 'state': 'closed' if closed else 'open', 'state_reason': reason,
                 'labels': [{'name': name} for name in labels], 'body': body}
        repo[number] = issue
        status = 'Done' if closed else 'In progress'
        if number == 7 or number == 80:
            status = 'Planned'
        parent = 4 if number == 80 else 3 if number == 25 else 6 if number == 37 else None
        rows.append({'id': f'item-{number}', 'isArchived': number not in (*range(1, 8), 25, 37, 80),
                     'content': {'__typename': 'Issue', 'id': issue['node_id'], 'number': number,
                         'repository': {'nameWithOwner': project.REPOSITORY, 'isPrivate': False},
                         'parent': {'number': parent} if parent else None,
                         'blockedBy': {'nodes': [], 'pageInfo': {'hasNextPage': False}}},
                     'status': {'__typename': 'ProjectV2ItemFieldSingleSelectValue',
                                'optionId': status, 'name': status},
                     'waiting': {'__typename': 'ProjectV2ItemFieldTextValue',
                                 'text': f'Preserved resource note {number}.'},
                     'workType': None})
    # Unrelated history and another repository are neither restored nor removed.
    repo[121] = {'number': 121, 'node_id': 'issue-121', 'state': 'closed',
                 'state_reason': 'not_planned', 'labels': [{'name': 'duplicate'}], 'body': 'Duplicate.'}
    rows.extend([
        {'id': 'item-121', 'isArchived': True, 'content': {'__typename': 'Issue',
            'number': 121, 'id': 'issue-121',
            'repository': {'nameWithOwner': project.REPOSITORY, 'isPrivate': False}}},
        {'id': 'pull-120', 'isArchived': True, 'content': {'__typename': 'PullRequest',
            'number': 120, 'id': 'pr-120',
            'repository': {'nameWithOwner': project.REPOSITORY, 'isPrivate': False}}},
        {'id': 'other-issue', 'isArchived': True, 'content': {'__typename': 'Issue',
            'number': 17, 'id': 'other-17',
            'repository': {'nameWithOwner': 'other/repository', 'isPrivate': False}}}])
    live = {'id': 'project', **copy.deepcopy(restoration.COMPACTED_PROJECT), 'fields': [
        {'id': 'status', 'name': 'Status', 'dataType': 'SINGLE_SELECT',
         'options': [{'id': status, 'name': status} for status in project.STATUSES]},
        {'id': 'waiting', 'name': 'Waiting resource', 'dataType': 'TEXT'},
        {'id': 'type', 'name': 'Work type', 'dataType': 'SINGLE_SELECT',
         'options': [{'id': 'Feature', 'name': 'Feature'}]}], 'items': rows}
    return repo, live, requirements, feature_source


class MemoryGitHub:
    def __init__(self, repo, live):
        self.repo, self.live = copy.deepcopy(repo), copy.deepcopy(live)
        self.calls = []

    def project(self):
        return copy.deepcopy(self.live)

    def api(self, path, method='GET', payload=None):
        self.calls.append((method, path, copy.deepcopy(payload)))
        if method == 'GET' and '/issues?' in path:
            return list(copy.deepcopy(self.repo).values())
        if method == 'PATCH':
            number = int(path.rsplit('/', 1)[1])
            self.repo[number].update(copy.deepcopy(payload))
            if payload.get('state') == 'open':
                self.repo[number]['state_reason'] = None
            if 'labels' in payload:
                self.repo[number]['labels'] = [{'name': name} for name in payload['labels']]
            return copy.deepcopy(self.repo[number])
        raise AssertionError((method, path))

    def mutate(self, name, input_type, payload):
        self.calls.append((name, input_type, copy.deepcopy(payload)))
        by_item = {row['id']: row for row in self.live['items']}
        by_issue = {row['content']['id']: row for row in self.live['items']}
        if name == 'unarchiveProjectV2Item':
            by_item[payload['itemId']]['isArchived'] = False
        elif name in ('addSubIssue', 'removeSubIssue'):
            row = by_issue[payload['subIssueId']]
            row['content']['parent'] = ({'number': int(payload['issueId'].split('-')[1]),
                'repository': {'nameWithOwner': project.REPOSITORY}} if name == 'addSubIssue' else None)
        elif name == 'addBlockedBy':
            by_issue[payload['issueId']]['content']['blockedBy']['nodes'].append({
                'number': int(payload['blockingIssueId'].split('-')[1]),
                'repository': {'nameWithOwner': project.REPOSITORY}})
        elif name == 'updateProjectV2ItemFieldValue':
            row = by_item[payload['itemId']]
            value = payload['value']['singleSelectOptionId']
            field = 'status' if payload['fieldId'] == 'status' else 'workType'
            row[field] = {'__typename': 'ProjectV2ItemFieldSingleSelectValue',
                          'optionId': value, 'name': value}
        elif name == 'updateProjectV2':
            self.live.update({key: value for key, value in payload.items() if key != 'projectId'})
        else:
            raise AssertionError((name, input_type, payload))


class RestorationTests(unittest.TestCase):
    def setUp(self):
        self.repo, self.live, self.requirements, self.features = fixture()

    def plan(self):
        return restoration.restoration_plan(self.repo, self.live, self.requirements, self.features)

    def test_preview_is_read_only_and_restores_exact_original_scope(self):
        client = MemoryGitHub(self.repo, self.live)
        before = copy.deepcopy((client.repo, client.live))
        with patch.object(restoration, 'historical', side_effect=[self.requirements, self.features]), \
                patch.object(project, 'trusted_main') as trusted:
            result = restoration.restore(client)
        self.assertEqual(result['reopen_issues'], sorted(restoration.REOPEN))
        self.assertEqual(result['expected_issue_count'], 82)
        self.assertEqual(result['unarchive_issues'], 72)
        self.assertEqual(result['milestones_accepted'], 0)
        self.assertEqual((client.repo, client.live), before)
        self.assertTrue(all(call[0] == 'GET' for call in client.calls))
        trusted.assert_not_called()

    def test_apply_readback_and_repeated_apply_are_idempotent(self):
        client = MemoryGitHub(self.repo, self.live)
        original_notes = {row['id']: row.get('waiting') for row in client.live['items']}
        with patch.object(restoration, 'historical', side_effect=lambda _, name:
                          self.requirements if name == 'milestone-status.json' else self.features), \
                patch.object(project, 'trusted_main') as trusted:
            result = restoration.restore(client, apply=True)
            again = restoration.restore(client, apply=True)
        self.assertTrue(result['readback_verified'])
        self.assertEqual(result['completed_operations'], result['operation_count'])
        self.assertEqual(again['operation_count'], 0)
        self.assertEqual(trusted.call_count, 2)
        self.assertTrue(all(client.repo[n]['state'] == 'open' for n in restoration.REOPEN))
        self.assertTrue(all(client.repo[n]['state'] == 'closed' for n in BASELINE))
        self.assertTrue(all(client.repo[n]['state'] == 'open' for n in range(1, 8)))
        self.assertEqual(client.repo[121], self.repo[121])
        for key, value in restoration.RESTORED_PROJECT.items():
            self.assertEqual(client.live[key], value)
        for row in client.live['items']:
            self.assertEqual(row.get('waiting'), original_notes[row['id']])
            number = row['content']['number']
            if row['id'] in ('pull-120', 'item-121', 'other-issue'):
                self.assertTrue(row['isArchived'])
                continue
            self.assertFalse(row['isArchived'])
            if number in BASELINE:
                self.assertEqual(row['status']['name'], 'Done')
            if number in restoration.STATUSES:
                self.assertEqual(row['status']['name'], restoration.STATUSES[number])
            if number in restoration.PARENTS:
                self.assertEqual(row['content']['parent']['number'], restoration.PARENTS[number])
        self.assertEqual(client.repo[13]['body'], self.repo[13]['body'][len(restoration.BANNER):])
        self.assertIn('- [x] Previously reviewed foundation.', client.repo[13]['body'])
        for number in (25, 37, 80):
            self.assertIn('## Acceptance criteria', client.repo[number]['body'])
            self.assertIn(f'Current implementation and evidence #{number}.', client.repo[number]['body'])
        self.assertIn('- [x] #12:', client.repo[1]['body'])
        self.assertIn('- [ ] #13:', client.repo[1]['body'])
        self.assertIn('Full acceptance 1.', client.repo[1]['body'])
        self.assertIn('## Implementation history', client.repo[1]['body'])
        self.assertNotIn('## Foundations', client.repo[1]['body'])
        self.assertIn('at their recorded revisions.', client.repo[1]['body'])

    def test_dependencies_are_added_without_removing_existing_relationships(self):
        row = next(row for row in self.live['items'] if row['id'] == 'item-16')
        row['content']['blockedBy']['nodes'] = [
            {'number': 14, 'repository': {'nameWithOwner': project.REPOSITORY}},
            {'number': 999, 'repository': {'nameWithOwner': 'other/repository'}}]
        operations, _ = self.plan()
        dependencies = [op for op in operations if op['kind'] == 'blocker']
        self.assertEqual({op['blocker'] for op in dependencies if op['issue'] == 'issue-16'},
                         {'issue-13', 'issue-15'})
        self.assertTrue(any(op == {'kind': 'blocker', 'issue': 'issue-25',
                                  'blocker': 'issue-23'} for op in dependencies))
        self.assertFalse(any(op.get('issue') in {f'issue-{n}' for n in range(1, 8)}
                             for op in dependencies))
        self.assertEqual(len(row['content']['blockedBy']['nodes']), 2)

    def test_changed_consolidation_state_or_banner_requires_review(self):
        for change in ({'state': 'open', 'state_reason': None},
                       {'state_reason': 'completed'},
                       {'body': 'A subsequent scope decision replaced the cleanup banner.'},
                       {'body': restoration.PREFIX + 'changed instructions\n---\n\nOriginal.'}):
            with self.subTest(change=change):
                previous = copy.deepcopy(self.repo[13])
                self.repo[13].update(change)
                with self.assertRaisesRegex(project.TrackingError, 'Consolidated issue state changed'):
                    self.plan()
                self.repo[13] = previous

    def test_completed_baseline_and_full_acceptance_are_never_reinterpreted(self):
        self.repo[12]['state_reason'] = 'not_planned'
        with self.assertRaisesRegex(project.TrackingError, 'Completed baseline'):
            self.plan()
        self.repo[12]['state_reason'] = 'completed'
        row = next(row for row in self.live['items'] if row['id'] == 'item-14')
        row['status'].update(optionId='Accepted', name='Accepted')
        with self.assertRaisesRegex(project.TrackingError, 'baseline Project statuses'):
            self.plan()
        row['status'].update(optionId='Done', name='Done')
        self.live['items'][0]['status'].update(optionId='Accepted', name='Accepted')
        with self.assertRaisesRegex(project.TrackingError, 'Full milestone acceptance'):
            self.plan()
        self.live['items'][0]['status'].update(optionId='In progress', name='In progress')
        self.repo[1]['state'] = 'closed'
        with self.assertRaisesRegex(project.TrackingError, 'Seven open milestone gates'):
            self.plan()

    def test_missing_duplicate_or_unexpected_memberships_fail_before_writes(self):
        for mutate in ('missing', 'duplicate', 'new-feature'):
            with self.subTest(mutate=mutate):
                repo, live, requirements, features = fixture()
                if mutate == 'missing':
                    live['items'] = [row for row in live['items'] if row['id'] != 'item-13']
                elif mutate == 'duplicate':
                    live['items'].append(copy.deepcopy(live['items'][0]))
                else:
                    repo[125] = {'number': 125, 'labels': [{'name': 'feature'}]}
                with self.assertRaises(project.TrackingError):
                    restoration.restoration_plan(repo, live, requirements, features)

    def test_failed_readback_does_not_claim_success(self):
        client = MemoryGitHub(self.repo, self.live)
        mutate = client.mutate
        def omit_unarchive(name, input_type, payload):
            if name != 'unarchiveProjectV2Item':
                mutate(name, input_type, payload)
        with patch.object(restoration, 'historical', side_effect=[self.requirements, self.features]), \
                patch.object(project, 'trusted_main'), patch.object(client, 'mutate', side_effect=omit_unarchive):
            with self.assertRaisesRegex(project.TrackingError, 'readback differs'):
                restoration.restore(client, apply=True)

    def test_ignored_status_or_work_type_mutations_cannot_pass_after_banner_removal(self):
        for ignored_field in ('status', 'type'):
            with self.subTest(field=ignored_field):
                client = MemoryGitHub(self.repo, self.live)
                mutate = client.mutate
                def omit_field(name, input_type, payload):
                    if name != 'updateProjectV2ItemFieldValue' or payload['fieldId'] != ignored_field:
                        mutate(name, input_type, payload)
                with patch.object(restoration, 'historical', side_effect=[self.requirements, self.features]), \
                        patch.object(project, 'trusted_main'), patch.object(client, 'mutate', side_effect=omit_field):
                    with self.assertRaisesRegex(project.TrackingError, 'field readback differs'):
                        restoration.restore(client, apply=True)
                # The issue writes succeeded, so replanning alone cannot detect the old Done status.
                self.assertEqual(client.repo[13]['state'], 'open')
                self.assertFalse(client.repo[13]['body'].startswith(restoration.PREFIX))

    def test_field_readback_requires_the_requested_item_and_option_identity(self):
        operations = [{'kind': 'field', 'item': 'item-13', 'field': 'status',
                       'value': {'singleSelectOptionId': 'Awaiting evidence'}}]
        fresh = copy.deepcopy(self.live)
        row = next(row for row in fresh['items'] if row['id'] == 'item-13')
        row['status'].update(optionId='Awaiting evidence', name='Awaiting evidence')
        restoration.verify_field_readback(operations, self.live, fresh)
        row['status']['name'] = 'Done'
        with self.assertRaisesRegex(project.TrackingError, 'field readback differs'):
            restoration.verify_field_readback(operations, self.live, fresh)
        row['status']['name'] = 'Awaiting evidence'
        row['content']['id'] = 'replacement-issue'
        with self.assertRaisesRegex(project.TrackingError, 'field readback identity differs'):
            restoration.verify_field_readback(operations, self.live, fresh)

    def test_project_text_updates_only_exact_prior_compaction_values(self):
        for custom in (None, 'readme', 'shortDescription', 'both'):
            with self.subTest(custom=custom):
                repo, live, requirements, features = fixture()
                for field in restoration.COMPACTED_PROJECT:
                    if custom in (field, 'both'):
                        live[field] = f'Owner-edited {field}.'
                operations, _ = restoration.restoration_plan(repo, live, requirements, features)
                updates = [op for op in operations if op['kind'] == 'project']
                if custom == 'both':
                    self.assertEqual(updates, [])
                    continue
                expected = {field: value for field, value in restoration.RESTORED_PROJECT.items()
                            if custom != field}
                self.assertEqual(updates, [{'kind': 'project', 'payload': {'projectId': 'project', **expected}}])

    def test_ignored_project_text_mutation_fails_fresh_readback(self):
        client = MemoryGitHub(self.repo, self.live)
        mutate = client.mutate
        def omit_text(name, input_type, payload):
            if name != 'updateProjectV2':
                mutate(name, input_type, payload)
        with patch.object(restoration, 'historical', side_effect=[self.requirements, self.features]), \
                patch.object(project, 'trusted_main'), patch.object(client, 'mutate', side_effect=omit_text):
            with self.assertRaisesRegex(project.TrackingError, 'Project text readback differs'):
                restoration.restore(client, apply=True)


class ImmutableHistoryTests(unittest.TestCase):
    def test_history_requires_exact_ref_path_blob_identity_and_decoded_content(self):
        content = b'{"schema_version":1,"milestones":[]}'
        blob_hash = hashlib.sha1(b'blob ' + str(len(content)).encode() + b'\0' + content).hexdigest()
        reply = {'type': 'file', 'path': 'docs/milestone-status.json', 'sha': blob_hash,
                 'encoding': 'base64', 'content': base64.b64encode(content).decode()}
        class Client:
            def __init__(self):
                self.paths = []
            def api(self, path):
                self.paths.append(path)
                return copy.deepcopy(reply)
        client = Client()
        with patch.dict(restoration.HISTORY_BLOBS, {'milestone-status.json': blob_hash}):
            self.assertEqual(restoration.historical(client, 'milestone-status.json'), json.loads(content))
            self.assertEqual(client.paths, [f'repos/{project.REPOSITORY}/contents/docs/milestone-status.json'
                                           f'?ref={restoration.HISTORY}'])
            for key, value in [('path', 'docs/features.json'), ('sha', '0' * 40),
                               ('type', 'dir'), ('content', base64.b64encode(b'{}').decode())]:
                with self.subTest(key=key):
                    previous, reply[key] = reply[key], value
                    with self.assertRaises(project.TrackingError):
                        restoration.historical(client, 'milestone-status.json')
                    reply[key] = previous
            with self.assertRaisesRegex(project.TrackingError, 'outside the reviewed scope'):
                restoration.historical(client, 'other.json')


if __name__ == '__main__':
    unittest.main()
