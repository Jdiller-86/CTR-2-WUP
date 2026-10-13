"""Project authority, pagination, privacy and safe docs publication contracts."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'tools' / filename)
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


export = module('project_export_test', 'export-github-project.py')
publisher = module('project_publish_test', 'publish-project-progress.py')
NOW = '2026-10-12T12:00:00Z'


def fixture():
    source = json.loads((ROOT / 'docs/milestone-status.json').read_text())
    fields = [
        {'id': 'status-field', 'name': 'Status', '__typename': 'ProjectV2SingleSelectField',
         'dataType': 'SINGLE_SELECT', 'options': [{'id': 'option-' + str(i), 'name': name}
                                               for i, name in enumerate(export.STATUSES[:8])]},
        {'id': 'waiting-field', 'name': 'Waiting resource', '__typename': 'ProjectV2Field', 'dataType': 'TEXT'},
    ]
    rows = []
    for milestone in source['milestones']:
        for role, identifier in [('milestone', milestone['id'])] + [('work-item', item['id']) for item in milestone['work_items']]:
            index = len(rows) + 1
            body = '<!-- ctr2wup:' + role + ':' + identifier + ' -->\n'
            if role == 'work-item':
                body += '<!-- ctr2wup:parent-milestone:' + milestone['id'] + ' -->\n'
            body += 'PRIVATE_BODY_MUST_NEVER_BE_PUBLIC '
            rows.append({'id': 'item-' + str(index), 'isArchived': False,
                         'content': {'__typename': 'Issue', 'id': 'issue-' + str(index), 'number': index,
                                     'body': body, 'title': 'PRIVATE_TITLE', 'repository': {'nameWithOwner': export.REPOSITORY}},
                         'status': {'__typename': 'ProjectV2ItemFieldSingleSelectValue', 'optionId': 'option-0', 'name': 'Planned'},
                         'waiting': None})
    return source, {'id': 'project-2', 'url': export.PROJECT_URL, 'number': 2, 'closed': False,
                    'owner': {'login': 'Jdiller-86'}, 'fields': fields, 'items': rows}


def set_status(row, name):
    row['status'] = {'__typename': 'ProjectV2ItemFieldSingleSelectValue',
                     'optionId': 'option-' + str(export.STATUSES.index(name)), 'name': name}


class FakeGitHub(export.GitHub):
    def __init__(self, project):
        self.data = copy.deepcopy(project)
        self.calls, self.writes = [], []
        self.page_size = 100
        self.stalled = False
        self.denied = False
        self.repo_public = True
        self.race = False
        self.resource_race = False
        self.labels = {row['content']['number']: ['manual', 'status:blocked', 'status:private-custom']
                       for row in self.data['items'] if row.get('content', {}).get('__typename') == 'Issue'}

    def graphql(self, query, variables):
        self.calls.append(('graphql', variables.copy()))
        if self.denied:
            raise export.TrackingError('Project read failed; no current tracking state was published.')
        if 'repositoryOwner' in query:
            return {'repositoryOwner': {'login': 'Jdiller-86', 'projectV2': copy.deepcopy(self.data)}}
        if '... on ProjectV2Item {' in query:
            row = next(row for row in self.data['items'] if row['id'] == variables['id'])
            fresh = copy.deepcopy(row)
            if self.race:
                set_status(fresh, 'Done')
            if self.resource_race:
                fresh['waiting'] = {'__typename': 'ProjectV2ItemFieldTextValue', 'text': 'Wii U console results'}
            return {'node': {'content': {'id': row['content']['id']},
                             'status': fresh['status'], 'waiting': fresh['waiting']}}
        name = 'fields' if 'fields(first:' in query else 'items'
        start = int(variables['after'] or 0)
        batch = self.data[name][start:start + self.page_size]
        end = start + len(batch)
        return {'node': {name: {'nodes': copy.deepcopy(batch), 'pageInfo': {
            'hasNextPage': end < len(self.data[name]), 'endCursor': str(start if self.stalled else end)}}}}

    def api(self, path, method='GET', payload=None):
        self.calls.append((path, method))
        if method != 'GET':
            self.writes.append((path, method, payload))
        source = 'repos/' + export.REPOSITORY + '/issues/'
        if path.startswith(source):
            tail = path[len(source):].split('/')
            number = int(tail[0])
            row = next(row for row in self.data['items'] if row.get('content', {}).get('number') == number)
            if method == 'GET':
                return {'node_id': row['content']['id'], 'labels': [{'name': name} for name in self.labels[number]]}
            if method == 'POST':
                self.labels[number] += payload['labels']
                return None
            if method == 'DELETE':
                from urllib.parse import unquote
                self.labels[number].remove(unquote(tail[2]))
                return None
        target = 'repos/' + publisher.TARGET
        if path == target:
            return {'full_name': publisher.TARGET, 'private': not self.repo_public, 'default_branch': 'main'}
        if path == target + '/git/ref/heads/main':
            return {'object': {'sha': 'a' * 40}}
        if path == target + '/git/commits/' + 'a' * 40:
            return {'tree': {'sha': 'b' * 40}}
        if path == target + '/git/trees':
            return {'sha': 'c' * 40}
        if path == target + '/git/commits':
            return {'sha': 'd' * 40}
        if path == target + '/git/refs/heads/main':
            return None
        raise AssertionError((path, method))


class ProjectExportTests(unittest.TestCase):
    def test_explicit_result_review_changes_only_scoped_task_and_is_idempotent(self):
        class ReviewGitHub(FakeGitHub):
            def graphql(api, query, variables):
                if query.startswith('mutation'):
                    payload = variables['input']
                    api.writes.append(payload)
                    row = next(row for row in api.data['items'] if row['id'] == payload['itemId'])
                    if 'value' in payload:
                        self.assertIn('UpdateProjectV2ItemFieldValueInput', query)
                        set_status(row, 'Done')
                    else:
                        self.assertIn('ClearProjectV2ItemFieldValueInput', query)
                        self.assertIn('clearProjectV2ItemFieldValue(input:', query)
                        row['waiting'] = None
                    return {'mutation': {'projectV2Item': {'id': row['id']}}}
                answer = super().graphql(query, variables)
                if '... on ProjectV2Item {' in query:
                    answer['node']['isArchived'] = False
                return answer
        self.project['items'][1]['waiting'] = {'__typename': 'ProjectV2ItemFieldTextValue', 'text': 'Wii U console results'}
        api = ReviewGitHub(self.project)
        result = export.review_result_collection(api, self.source, apply=True)
        self.assertFalse(result['full_gate_accepted'])
        self.assertEqual(len(api.writes), 2)
        _, managed = export.collect(api.data, self.source)
        self.assertEqual(managed[('work-item', 'm1-results')]['status'], 'Done')
        self.assertEqual(managed[('work-item', 'm1-results')]['resources'], [])
        self.assertEqual(managed[('milestone', 'm1')]['status'], 'Planned')
        self.assertFalse(export.review_result_collection(api, self.source, apply=True)['changed'])
        # Resume the actual partial-success case: Done was stored, but resource
        # clearing failed. Retry only the supported clear operation.
        api.data['items'][1]['waiting'] = {'__typename': 'ProjectV2ItemFieldTextValue', 'text': 'Wii U console results'}
        self.assertTrue(export.review_result_collection(api, self.source, apply=True)['changed'])
        self.assertEqual(len(api.writes), 3)
        self.assertNotIn('value', api.writes[-1])

    def test_result_review_preserves_manual_block_and_requires_reviewed_evidence(self):
        api = FakeGitHub(self.project)
        self.source['console_result_collection_review'] = {'reviewed': False, 'task_id': 'm1-results'}
        with self.assertRaises(export.TrackingError):
            export.review_result_collection(api, self.source, apply=True)
        self.source['console_result_collection_review']['reviewed'] = True
        _, managed = export.collect(api.data, self.source)
        target = managed[('work-item', 'm1-results')]['item_id']
        set_status(next(row for row in api.data['items'] if row['id'] == target), 'Blocked')
        with self.assertRaisesRegex(export.TrackingError, 'blocked/custom'):
            export.review_result_collection(api, self.source, apply=True)
        self.assertEqual(api.writes, [])

    def test_chart_changes_with_project_status_and_has_visible_freshness(self):
        before = self.aggregate()
        first = export.gate_svg(before)
        set_status(self.project['items'][1], 'Done')
        changed = export.gate_svg(self.aggregate())
        self.assertNotEqual(first, changed)
        self.assertIn('Done 1/5', changed)
        self.assertIn(NOW.replace('T', ' ').replace('Z', ' UTC'), changed)
        self.assertIn('0/7 full milestones accepted', changed)
        self.assertNotIn('PRIVATE_BODY', changed)

    def test_unavailable_chart_has_no_invented_progress_totals(self):
        chart = export.gate_svg(export.unavailable(NOW))
        self.assertIn('tracking unavailable', chart)
        self.assertNotIn('0/7', chart)

    def test_github_default_todo_is_planned_without_changing_board_values(self):
        self.project['fields'][0]['options'].append({'id': 'todo', 'name': 'Todo'})
        row = self.project['items'][1]
        row['status'] = {'__typename': 'ProjectV2ItemFieldSingleSelectValue', 'optionId': 'todo', 'name': 'Todo'}
        self.assertEqual(self.aggregate()['work_items']['status_counts']['Other'], 0)
        self.assertEqual(row['status']['name'], 'Todo')

    def setUp(self):
        self.source, self.project = fixture()

    def aggregate(self):
        milestones, managed = export.collect(self.project, self.source)
        return export.aggregate(milestones, managed, NOW)

    def test_reads_selected_project_with_bounded_pagination(self):
        client = FakeGitHub(self.project)
        client.page_size = 5
        result = client.project()
        self.assertEqual(len(result['items']), 42)
        self.assertEqual(len(result['fields']), 2)
        self.assertEqual(client.calls[0][1], {'login': 'Jdiller-86', 'number': 2})
        self.assertEqual(client.writes, [])

    def test_denied_read_never_falls_back_to_other_boards_or_creates_one(self):
        client = FakeGitHub(self.project)
        client.denied = True
        with self.assertRaises(export.TrackingError):
            client.project()
        self.assertEqual(len(client.calls), 1)
        self.assertEqual(client.writes, [])

    def test_wrong_project_identity_is_rejected_before_item_reads(self):
        for key, value in [('url', 'https://github.com/users/Jdiller-86/projects/3'), ('number', 3), ('closed', True), ('owner', {'login': 'Other'})]:
            client = FakeGitHub(self.project)
            client.data[key] = value
            with self.subTest(key=key), self.assertRaises(export.TrackingError):
                client.project()
            self.assertEqual(len(client.calls), 1)

    def test_cursor_stall_and_duplicate_rows_are_rejected(self):
        client = FakeGitHub(self.project)
        client.page_size, client.stalled = 2, True
        with self.assertRaisesRegex(export.TrackingError, 'repeated an item|cursor'):
            client.project()
        client = FakeGitHub(self.project)
        client.data['items'].append(copy.deepcopy(client.data['items'][0]))
        with self.assertRaisesRegex(export.TrackingError, 'repeated an item'):
            client.project()

    def test_pagination_hard_budget(self):
        client = FakeGitHub(self.project)
        client.page_size = 1
        with self.assertRaisesRegex(export.TrackingError, 'review budget'):
            client.project()

    def test_done_work_or_overview_does_not_accept_a_full_gate(self):
        for row in self.project['items']:
            set_status(row, 'Done')
        result = self.aggregate()
        self.assertEqual(result['full_gates'], {'accepted': 0, 'total': 7})
        self.assertEqual(result['work_items']['status_counts']['Done'], 35)
        self.assertNotIn('implementation_percent', json.dumps(result))

    def test_only_accepted_overview_counts_gate(self):
        set_status(self.project['items'][0], 'Accepted')
        result = self.aggregate()
        self.assertEqual(result['full_gates']['accepted'], 1)
        self.assertEqual(result['work_items']['status_counts']['Accepted'], 0)
        self.assertTrue(result['milestones'][0]['accepted'])

    def test_accepted_child_archived_and_duplicate_content_fail_closed(self):
        for mode in ['accepted-child', 'archived', 'duplicate-id', 'duplicate-content', 'duplicate-number']:
            project = copy.deepcopy(self.project)
            if mode == 'accepted-child': set_status(project['items'][1], 'Accepted')
            if mode == 'archived': project['items'][1]['isArchived'] = True
            if mode == 'duplicate-id': project['items'][1]['id'] = project['items'][0]['id']
            if mode == 'duplicate-content': project['items'][1]['content']['id'] = project['items'][0]['content']['id']
            if mode == 'duplicate-number': project['items'][1]['content']['number'] = project['items'][0]['content']['number']
            with self.subTest(mode=mode), self.assertRaises(export.TrackingError):
                export.collect(project, self.source)

    def test_historical_source_states_and_acceptance_never_override_project(self):
        self.source['full_milestones_accepted'] = 7
        self.source['approximate_implementation_percent'] = 100
        for milestone in self.source['milestones']:
            milestone['state'] = 'blocked'
        result = self.aggregate()
        self.assertEqual(result['full_gates']['accepted'], 0)
        self.assertEqual(result['milestones'][0]['status'], 'Planned')

    def test_free_form_private_resource_and_titles_are_never_exported(self):
        self.project['items'][1]['waiting'] = {'__typename': 'ProjectV2ItemFieldTextValue', 'text': 'PRIVATE_ROM_FILENAME; Wii U console results'}
        result = self.aggregate()
        encoded = json.dumps(result)
        for private in ['PRIVATE', 'issue-', 'item-', 'number', '/issues/']:
            self.assertNotIn(private, encoded)
        self.assertEqual(result['milestones'][0]['waiting_resources']['Other resource'], 1)
        self.assertEqual(result['milestones'][0]['waiting_resources']['Console testing'], 1)

    def test_default_option_capitalization_is_read_without_changing_it(self):
        field = self.project['fields'][0]
        option = next(row for row in field['options'] if row['name'] == 'In progress')
        option['name'] = 'In Progress'
        row = self.project['items'][1]
        row['status'] = {'__typename': 'ProjectV2ItemFieldSingleSelectValue', 'optionId': option['id'], 'name': 'In Progress'}
        result = self.aggregate()
        self.assertEqual(result['work_items']['status_counts']['In progress'], 1)
        self.assertEqual(row['status']['name'], 'In Progress')

    def test_unknown_and_unassigned_statuses_remain_visible(self):
        self.project['fields'][0]['options'].append({'id': 'custom', 'name': 'PRIVATE_STATUS_NAME'})
        self.project['items'][1]['status'] = {'__typename': 'ProjectV2ItemFieldSingleSelectValue', 'optionId': 'custom', 'name': 'PRIVATE_STATUS_NAME'}
        self.project['items'][2]['status'] = None
        result = self.aggregate()
        self.assertEqual(result['work_items']['status_counts']['Other'], 1)
        self.assertEqual(result['work_items']['status_counts']['Unassigned'], 1)
        self.assertNotIn('PRIVATE_STATUS_NAME', json.dumps(result))

    def test_missing_duplicate_and_wrong_parent_fail_closed(self):
        for mode in ['missing', 'duplicate', 'parent', 'marker']:
            project = copy.deepcopy(self.project)
            if mode == 'missing': project['items'].pop()
            if mode == 'duplicate': project['items'].append(copy.deepcopy(project['items'][0]))
            if mode == 'parent': project['items'][1]['content']['body'] = project['items'][1]['content']['body'].replace('parent-milestone:m1', 'parent-milestone:m2')
            if mode == 'marker': project['items'][1]['content']['body'] += '<!-- ctr2wup:work-item: invalid -->'
            with self.subTest(mode=mode), self.assertRaises(export.TrackingError):
                export.collect(project, self.source)

    def test_unmanaged_repository_pr_and_draft_items_are_ignored(self):
        for kind in ['PullRequest', 'DraftIssue']:
            self.project['items'].append({'id': 'unmanaged-' + kind, 'content': {'__typename': kind}})
        unrelated = copy.deepcopy(self.project['items'][0])
        unrelated['content']['repository']['nameWithOwner'] = 'other/private'
        self.project['items'].append(unrelated)
        self.assertEqual(self.aggregate()['work_items']['total'], 35)

    def test_status_option_mismatch_is_rejected(self):
        self.project['items'][0]['status']['name'] = 'Done'
        with self.assertRaisesRegex(export.TrackingError, 'identity changed'):
            self.aggregate()

    def test_missing_duplicate_or_wrong_type_fields_are_rejected(self):
        for mode in ['missing', 'duplicate', 'type']:
            project = copy.deepcopy(self.project)
            if mode == 'missing': project['fields'].pop()
            if mode == 'duplicate': project['fields'].append(copy.deepcopy(project['fields'][0]))
            if mode == 'type': project['fields'][0]['__typename'] = 'ProjectV2Field'
            with self.subTest(mode=mode), self.assertRaises(export.TrackingError):
                export.collect(project, self.source)

    def test_unavailable_snapshot_has_no_invented_counts(self):
        result = export.unavailable(NOW)
        self.assertNotIn('full_gates', result)
        self.assertNotIn('work_items', result)
        self.assertIsNone(result['last_success_at'])

    def test_mirror_only_owned_labels_preserves_manual_and_unknown(self):
        client = FakeGitHub(self.project)
        _, managed = export.collect(self.project, self.source)
        report = export.mirror_labels(client, managed, apply=True)
        self.assertEqual(report['changed_issues'], 42)
        for labels in client.labels.values():
            self.assertEqual(labels, ['manual', 'status:private-custom', 'status:planned'])
        self.assertTrue(all(method in ['POST', 'DELETE'] and '/labels' in path for path, method, _ in client.writes))

    def test_mirror_dry_run_and_unassigned_do_not_write(self):
        self.project['items'][0]['status'] = None
        client = FakeGitHub(self.project)
        _, managed = export.collect(self.project, self.source)
        result = export.mirror_labels(client, managed)
        self.assertEqual(result['unmapped_items'], 1)
        self.assertEqual(client.writes, [])

    def test_mirror_stale_project_value_prevents_write(self):
        client = FakeGitHub(self.project)
        client.race = True
        _, managed = export.collect(self.project, self.source)
        with self.assertRaisesRegex(export.TrackingError, 'changed during mirroring'):
            export.mirror_labels(client, managed, True)
        self.assertEqual(client.writes, [])

    def test_resource_mirrors_use_project_categories_and_keep_manual_tags(self):
        self.project['items'][1]['waiting'] = {'__typename': 'ProjectV2ItemFieldTextValue',
                                              'text': 'Native Windows conversion validation; PRIVATE_RESOURCE_NOTE'}
        # Source claims console; Project explicitly chooses Windows instead.
        self.source['milestones'][0]['work_items'][0]['waiting_labels'] = ['needs-console']
        client = FakeGitHub(self.project)
        client.labels[2] += ['needs-console', 'manual-resource-tag']
        _, managed = export.collect(self.project, self.source)
        report = export.mirror_labels(client, managed, True)
        self.assertIn('needs-windows', client.labels[2])
        self.assertNotIn('needs-console', client.labels[2])
        self.assertNotIn('needs-input', client.labels[2])
        self.assertIn('manual-resource-tag', client.labels[2])
        self.assertEqual(report['unmapped_resource_items'], 1)
        self.assertNotIn('PRIVATE_RESOURCE_NOTE', json.dumps(report))

    def test_waiting_resource_race_prevents_label_changes(self):
        client = FakeGitHub(self.project)
        client.resource_race = True
        _, managed = export.collect(self.project, self.source)
        with self.assertRaisesRegex(export.TrackingError, 'resource changed'):
            export.mirror_labels(client, managed, True)
        self.assertEqual(client.writes, [])

    def test_mirror_issue_identity_preflight_before_all_writes(self):
        client = FakeGitHub(self.project)
        _, managed = export.collect(self.project, self.source)
        managed[('milestone', 'm7')]['issue_id'] = 'wrong-identity'
        with self.assertRaisesRegex(export.TrackingError, 'Issue identity changed'):
            export.mirror_labels(client, managed, True)
        self.assertEqual(client.writes, [])

    def test_public_site_contains_only_allowlisted_files(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary) / 'progress'
            export.write_site(directory, self.aggregate())
            self.assertEqual(sorted(p.name for p in directory.iterdir()), ['gates.svg', 'index.html', 'progress.json'])
            with self.assertRaises(export.TrackingError):
                export.write_site(directory, self.aggregate())
            publisher.validate_public(json.loads((directory / 'progress.json').read_text()))

    def test_publication_atomically_updates_only_reviewed_paths_without_force(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary) / 'progress'
            export.write_site(directory, self.aggregate())
            client = FakeGitHub(self.project)
            result = publisher.publish(directory, client, True)
            self.assertTrue(result['applied'])
            self.assertEqual(len(client.writes), 3)
            tree = client.writes[0][2]
            self.assertEqual(tree['base_tree'], 'b' * 40)
            self.assertEqual([entry['path'] for entry in tree['tree']], ['site/progress/index.html', 'site/progress/progress.json', 'site/progress/gates.svg'])
            self.assertEqual(client.writes[-1][2], {'sha': 'd' * 40, 'force': False})

    def test_publication_refuses_private_docs_repository(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary) / 'progress'
            export.write_site(directory, self.aggregate())
            client = FakeGitHub(self.project)
            client.repo_public = False
            with self.assertRaises(publisher.EXPORT.TrackingError):
                publisher.publish(directory, client, True)
            self.assertEqual(client.writes, [])

    def test_publication_rejects_extra_file_and_private_nested_fields(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary) / 'progress'
            export.write_site(directory, self.aggregate())
            (directory / 'private.txt').write_text('PRIVATE')
            with self.assertRaises(publisher.EXPORT.TrackingError):
                publisher.publish(directory, FakeGitHub(self.project), True)
            (directory / 'private.txt').unlink()
            data = json.loads((directory / 'progress.json').read_text())
            data['milestones'][0]['private_issue_url'] = 'PRIVATE'
            (directory / 'progress.json').write_text(json.dumps(data))
            with self.assertRaises(publisher.EXPORT.TrackingError):
                publisher.publish(directory, FakeGitHub(self.project), True)

    def test_publication_rejects_modified_template_or_svg(self):
        for filename in ['index.html', 'gates.svg']:
            with self.subTest(filename=filename), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary) / 'progress'
                export.write_site(directory, self.aggregate())
                with (directory / filename).open('a') as file:
                    file.write('PRIVATE')
                with self.assertRaises(publisher.EXPORT.TrackingError):
                    publisher.publish(directory, FakeGitHub(self.project), True)


if __name__ == '__main__':
    unittest.main()
