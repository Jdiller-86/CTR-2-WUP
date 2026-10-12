"""Projects setup policy checks with original mocked API data; no live writes."""
import copy
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('github_project_setup', ROOT / 'tools/setup-github-project.py')
setup = importlib.util.module_from_spec(spec)
spec.loader.exec_module(setup)


def source():
    settings = json.loads((ROOT / '.github/repository-settings.json').read_text())
    # Legacy title discovery/creation still has coverage. Explicit user-selected
    # boards are tested separately and must never fall back to creation.
    settings['project_requested'].pop('existing_url', None)
    return settings, json.loads((ROOT / 'docs/milestone-status.json').read_text())


def managed(state):
    trackers = [dict(m, kind='milestone', milestone_title=m['title']) for m in state['milestones']]
    tasks = [dict(task, kind='work-item', milestone_title=m['title'], milestone_id=m['id'])
             for m in state['milestones'] for task in m.get('work_items', [])]
    return trackers + tasks


def issues(state):
    return [{'node_id': 'issue-' + m['id'], 'number': i + 1, 'title': m['title'],
             'html_url': f'https://github.com/example/fixture/issues/{i + 1}',
             'body': '<!-- ctr2wup:' + m['kind'] + ':' + m['id'] + ' -->\nMaintainer notes preserved'
                     + ('\n<!-- ctr2wup:parent-milestone:' + m['milestone_id'] + ' -->'
                        if m['kind'] == 'work-item' else ''),
             'milestone': {'title': m['milestone_title']}}
            for i, m in enumerate(managed(state))]


def project(state):
    options = [{'id': 'option-' + str(i), 'name': name, 'color': setup.COLORS[name],
                'description': 'Maintainer option description'}
               for i, name in enumerate(setup.STATUSES)]
    options.append({'id': 'manual-option', 'name': 'Paused by maintainer', 'color': 'PURPLE', 'description': 'Keep this'})
    fields = [{'id': 'title', 'name': 'Title', 'dataType': 'TITLE'},
              {'id': 'status', 'name': 'Status', 'dataType': 'SINGLE_SELECT', 'options': options},
              {'id': 'milestone', 'name': 'Milestone', 'dataType': 'MILESTONE'},
              {'id': 'waiting', 'name': 'Waiting resource', 'dataType': 'TEXT'}]
    items = [{'id': 'item-' + m['id'], 'isArchived': False,
              'content': {'id': 'issue-' + m['id'], '__typename': 'Issue'},
              'status': {'__typename': 'ProjectV2ItemFieldSingleSelectValue', 'optionId': 'manual-option'},
              'waiting': {'__typename': 'ProjectV2ItemFieldTextValue', 'text': 'Maintainer resource note'},
              'milestone': {'__typename': 'ProjectV2ItemFieldMilestoneValue'}}
             for m in managed(state)]
    items.append({'id': 'manual-item', 'isArchived': False,
                  'content': {'id': 'manual-draft', '__typename': 'DraftIssue'},
                  'status': None, 'waiting': None, 'milestone': None})
    visible = {'nodes': [{'id': f['id']} for f in fields], 'pageInfo': {'hasNextPage': False}}
    return {'id': 'project-1', 'title': 'CTR2WUP Development', 'public': False,
            'closed': False, 'viewerCanUpdate': True, 'number': 1,
            'url': 'https://github.com/users/example/projects/1',
            'readme': 'Keep private project notes',
            'repositories': {'nodes': [{'id': 'repo-1'}], 'pageInfo': {'hasNextPage': False}},
            'fields': fields, 'items': items,
            'views': [{'id': 'view-1', 'name': setup.VIEW_NAME, 'layout': 'BOARD_LAYOUT',
                       'configuration': {'visibleFields': visible}}]}


class FakeGitHub:
    def __init__(self, state, board=True):
        self.state = copy.deepcopy(state)
        self.remote_issues = issues(state)
        self.remote_project = project(state) if board else None
        self.project_list_override = None
        self.denied = False
        self.writes = []
        self.snapshot_count = 0
        self.before_snapshot = None

    def rest(self, path):
        return {'full_name': 'Jdiller-86/CTR-2-WUP', 'node_id': 'repo-1',
                'owner': {'node_id': 'owner-1', 'login': 'Jdiller-86'}}

    def issues(self, repo):
        return copy.deepcopy(self.remote_issues)

    def projects(self, owner):
        if self.denied:
            raise setup.SetupError('GitHub Projects access/request failed: Resource not accessible by integration')
        if self.project_list_override is not None:
            return copy.deepcopy(self.project_list_override)
        return [copy.deepcopy(self.remote_project)] if self.remote_project else []

    def project(self, owner, number):
        if self.denied:
            raise setup.SetupError('GitHub Projects access/request failed: Resource not accessible by integration')
        if not self.remote_project or self.remote_project['number'] != number:
            return None
        return {'id': self.remote_project['id']}

    def snapshot(self, identifier):
        self.snapshot_count += 1
        if self.before_snapshot:
            self.before_snapshot(self)
        return copy.deepcopy(self.remote_project)

    def mutate(self, name, input_type, payload, selection):
        self.writes.append((name, copy.deepcopy(payload)))
        if name == 'createProjectV2':
            self.remote_project = project(self.state)
            self.remote_project['items'] = []
            self.remote_project['views'] = []
            self.remote_project['fields'] = self.remote_project['fields'][:3]
            self.remote_project['fields'][1]['options'] = [
                {'id': 'default-' + str(i), 'name': label, 'color': 'GRAY', 'description': ''}
                for i, label in enumerate(('Todo', 'In Progress', 'Done'))]
            return {'projectV2': {'id': self.remote_project['id'], 'url': self.remote_project['url'], 'public': False}}
        if name == 'createProjectV2Field':
            identifier = 'waiting' if payload['name'] == 'Waiting resource' else 'new-field'
            self.remote_project['fields'].append({'id': identifier, 'name': payload['name'], 'dataType': payload['dataType']})
            return {'projectV2Field': {'id': identifier}}
        if name == 'createProjectV2View':
            self.remote_project['views'].append({'id': 'view-1', 'name': payload['name'], 'layout': payload['layout'],
                 'configuration': {'visibleFields': {'nodes': [{'id': identifier} for identifier in payload['configuration']['visibleFieldIds']],
                                                     'pageInfo': {'hasNextPage': False}}}})
            return {'projectV2View': {'id': 'view-1'}}
        if name == 'addProjectV2ItemById':
            identifier = 'item-' + payload['contentId'].removeprefix('issue-')
            self.remote_project['items'].append({'id': identifier, 'isArchived': False,
                 'content': {'id': payload['contentId'], '__typename': 'Issue'},
                 'status': None, 'waiting': None, 'milestone': {'__typename': 'ProjectV2ItemFieldMilestoneValue'}})
            return {'item': {'id': identifier}}
        if name == 'updateProjectV2Field':
            field = next(f for f in self.remote_project['fields'] if f['id'] == payload['fieldId'])
            field['options'] = [dict(option, id=option.get('id', 'new-option-' + str(i)))
                                for i, option in enumerate(payload['singleSelectOptions'])]
            return {'projectV2Field': {'id': field['id']}}
        if name == 'updateProjectV2ItemFieldValue':
            item = next(i for i in self.remote_project['items'] if i['id'] == payload['itemId'])
            key = {'status': 'status', 'waiting': 'waiting', 'milestone': 'milestone'}[payload['fieldId']]
            value = payload['value']
            item[key] = ({'__typename': 'ProjectV2ItemFieldSingleSelectValue', 'optionId': value['singleSelectOptionId']}
                         if 'singleSelectOptionId' in value else {'__typename': 'ProjectV2ItemFieldTextValue', 'text': value['text']})
            return {'projectV2Item': {'id': item['id']}}
        raise AssertionError('Unexpected mocked write: ' + name)


class GithubProjectTests(unittest.TestCase):
    def setUp(self):
        self.settings, self.state = source()
        self.api = FakeGitHub(self.state)

    def select_owner_project(self):
        self.settings['project_requested']['existing_url'] = 'https://github.com/users/Jdiller-86/projects/2'
        self.api.remote_project.update(number=2, url=self.settings['project_requested']['existing_url'],
                                       title='Owner’s existing board name')
        def unrelated_projects(_owner):
            raise AssertionError('Explicit selection must not list unrelated Projects')
        self.api.projects = unrelated_projects

    def test_explicit_existing_project_preserves_identity_and_content(self):
        self.select_owner_project()
        original = copy.deepcopy(self.api.remote_project)
        report = setup.base_report(self.settings, self.state)
        context = setup.preflight(self.api, self.settings, self.state, report)
        setup.plan(context, report)
        self.assertEqual(report['operations'], [])
        self.assertTrue(report['live_project_verified'])
        self.assertEqual(report['project_url'], self.settings['project_requested']['existing_url'])
        self.assertEqual(self.api.remote_project, original)
        self.assertEqual(self.api.writes, [])

    def test_denied_explicit_project_never_falls_back_to_creation(self):
        self.select_owner_project()
        self.api.denied = True
        report = setup.base_report(self.settings, self.state)
        with self.assertRaisesRegex(setup.SetupError, 'Resource not accessible'):
            setup.preflight(self.api, self.settings, self.state, report)
        self.assertEqual(self.api.writes, [])
        self.assertEqual(report['operations'], [])
        self.assertFalse(report['live_project_verified'])

    def test_missing_explicit_project_never_falls_back_to_creation(self):
        self.select_owner_project()
        self.api.remote_project = None
        report = setup.base_report(self.settings, self.state)
        with self.assertRaisesRegex(setup.SetupError, 'no new Project will be created'):
            setup.preflight(self.api, self.settings, self.state, report)
        self.assertEqual(self.api.writes, [])

    def test_explicit_project_identity_mismatch_blocks_writes(self):
        self.select_owner_project()
        self.api.remote_project['url'] = 'https://github.com/users/Jdiller-86/projects/3'
        report = setup.base_report(self.settings, self.state)
        with self.assertRaisesRegex(setup.SetupError, 'identity changed'):
            setup.preflight(self.api, self.settings, self.state, report)
        self.assertEqual(self.api.writes, [])

    def test_explicit_project_url_rejects_wrong_owner_and_untrusted_addresses(self):
        for url in ('https://github.com/users/another-owner/projects/2',
                    'https://example.com/users/Jdiller-86/projects/2',
                    'https://github.com/users/Jdiller-86/projects/0',
                    'https://github.com/users/Jdiller-86/projects/2?extra=1',
                    'https://github.com/users/Jdiller-86/projects/../../2'):
            with self.subTest(url=url):
                self.settings['project_requested']['existing_url'] = url
                with self.assertRaisesRegex(setup.SetupError, 'Existing Project URL'):
                    setup.base_report(self.settings, self.state)
        self.assertEqual(self.api.writes, [])

    def test_milestone_state_takes_precedence_over_waiting_resources(self):
        for state, expected in setup.WORK_ITEM_STATUSES.items():
            with self.subTest(state=state):
                milestone = dict(self.state['milestones'][0], state=state,
                                 waiting_labels=['needs-console'])
                self.assertEqual(setup.initial_status(milestone), expected)
        report = setup.base_report(self.settings, self.state)
        for source_milestone, planned in zip(self.state['milestones'], report['trackers']):
            self.assertEqual(planned['initial_status'],
                             setup.WORK_ITEM_STATUSES[source_milestone['state']])
        milestone['project_status'] = 'Ready for review'
        self.assertEqual(setup.initial_status(milestone), 'Ready for review')
        self.state['milestones'][0]['state'] = 'accepted'
        with self.assertRaisesRegex(setup.SetupError, 'Unknown milestone state'):
            setup.base_report(self.settings, self.state)

    def preflight(self):
        report = setup.base_report(self.settings, self.state)
        context = setup.preflight(self.api, self.settings, self.state, report)
        setup.plan(context, report)
        return context, report

    def add_actionable_fixture(self):
        tasks = [
            {'id': 'm1-project-alpha', 'title': 'Verify lifecycle evidence', 'summary': 'Original fixture',
             'acceptance_criteria': ['Record the lifecycle result'], 'depends_on': ['m2'],
             'waiting_labels': ['needs-console'], 'state': 'awaiting_evidence'},
            {'id': 'm1-project-beta', 'title': 'Verify resource cleanup', 'summary': 'Original fixture',
             'acceptance_criteria': ['Record cleanup result'], 'depends_on': ['m1-project-alpha'],
             'waiting_labels': [], 'state': 'blocked'},
            {'id': 'm2-project-gamma', 'title': 'Review CPU trace', 'summary': 'Original fixture',
             'acceptance_criteria': ['Compare trace'], 'depends_on': [],
             'waiting_labels': [], 'state': 'in_progress'},
        ]
        self.state['milestones'][0].setdefault('work_items', []).extend(tasks[:2])
        self.state['milestones'][1].setdefault('work_items', []).append(tasks[2])
        self.api = FakeGitHub(self.state)
        return tasks

    def test_new_project_dry_run_reuses_all_source_issues_without_writes(self):
        self.api.remote_project = None
        _, report = self.preflight()
        self.assertTrue(report['ok'])
        self.assertFalse(report['applied'])
        self.assertFalse(report['live_project_verified'])
        self.assertEqual([m['number'] for m in report['trackers']], list(range(1, 8)))
        self.assertEqual(sum(o['kind'] == 'add-existing-issue' for o in report['operations']), len(managed(self.state)))
        self.assertEqual(len(report['work_items']), len(managed(self.state)) - 7)
        self.assertEqual(self.api.writes, [])

    def test_matching_board_is_idempotent_and_preserves_all_manual_content(self):
        before = copy.deepcopy(self.api.remote_project)
        context, report = self.preflight()
        self.assertEqual(report['operations'], [])
        setup.apply(self.api, context, report)
        self.assertTrue(report['applied'])
        self.assertTrue(report['live_project_verified'])
        self.assertEqual(self.api.writes, [])
        self.assertEqual(self.api.remote_project, before)

    def test_new_board_apply_uses_all_options_and_issues_and_is_idempotent(self):
        self.api.remote_project = None
        context, report = self.preflight()
        setup.apply(self.api, context, report)
        self.assertTrue(report['applied'])
        self.assertFalse(self.api.remote_project['public'])
        self.assertEqual([option['name'] for option in self.api.remote_project['fields'][1]['options']], list(setup.STATUSES))
        self.assertEqual(len(self.api.remote_project['items']), len(managed(self.state)))
        self.assertEqual({item['content']['id'] for item in self.api.remote_project['items']},
                         {'issue-' + m['id'] for m in managed(self.state)})
        before = copy.deepcopy(self.api.remote_project)
        self.api.writes = []
        context, report = self.preflight()
        self.assertEqual(report['operations'], [])
        setup.apply(self.api, context, report)
        self.assertEqual(self.api.writes, [])
        self.assertEqual(self.api.remote_project, before)

    def test_appended_options_preserve_existing_ids_descriptions_colors_and_values(self):
        field = self.api.remote_project['fields'][1]
        field['options'] = [field['options'][2], field['options'][-1]]
        prior = copy.deepcopy(field['options'])
        context, report = self.preflight()
        setup.apply(self.api, context, report)
        payload = self.api.writes[0][1]
        self.assertEqual(payload['singleSelectOptions'][:2], prior)
        self.assertEqual([name for name, _ in self.api.writes], ['updateProjectV2Field'])
        self.assertEqual(len(field['options']), len(setup.STATUSES) + 1)
        for item in self.api.remote_project['items'][:-1]:
            self.assertEqual(item['status']['optionId'], 'manual-option')

    def test_only_empty_managed_values_are_initialized(self):
        self.api.remote_project['items'][0]['status'] = None
        context, report = self.preflight()
        setup.apply(self.api, context, report)
        self.assertEqual(len(self.api.writes), 1)
        name, payload = self.api.writes[0]
        self.assertEqual(name, 'updateProjectV2ItemFieldValue')
        self.assertEqual(payload['fieldId'], 'status')
        self.assertEqual(self.api.remote_project['items'][0]['waiting']['text'], 'Maintainer resource note')
        self.assertIsNone(self.api.remote_project['items'][-1]['status'])

    def test_concurrent_nonempty_value_is_preserved_after_refresh(self):
        self.api.remote_project['items'][0]['status'] = None
        context, report = self.preflight()
        def fill(api):
            api.remote_project['items'][0]['status'] = {'optionId': 'manual-option'}
        self.api.before_snapshot = fill
        setup.apply(self.api, context, report)
        self.assertEqual(self.api.writes, [])

    def test_duplicate_tracker_issue_or_marker_fails_before_writes(self):
        for duplicate in ('issue', 'marker'):
            with self.subTest(duplicate=duplicate):
                self.api = FakeGitHub(self.state)
                if duplicate == 'issue':
                    self.api.remote_issues.append(copy.deepcopy(self.api.remote_issues[0]))
                else:
                    self.api.remote_issues[0]['body'] += '<!-- ctr2wup:milestone:m1 -->'
                with self.assertRaises(setup.SetupError):
                    self.preflight()
                self.assertEqual(self.api.writes, [])

    def test_duplicate_project_names_items_fields_and_options_fail_before_writes(self):
        for duplicate in ('project', 'item', 'field', 'option'):
            with self.subTest(duplicate=duplicate):
                self.api = FakeGitHub(self.state)
                board = self.api.remote_project
                if duplicate == 'project':
                    self.api.project_list_override = [board, dict(board, id='project-2')]
                elif duplicate == 'item':
                    board['items'].append(copy.deepcopy(board['items'][0]))
                elif duplicate == 'field':
                    board['fields'].append(copy.deepcopy(board['fields'][0]))
                else:
                    board['fields'][1]['options'].append(copy.deepcopy(board['fields'][1]['options'][0]))
                with self.assertRaises(setup.SetupError):
                    self.preflight()
                self.assertEqual(self.api.writes, [])

    def test_public_closed_readonly_or_archived_board_is_not_changed(self):
        for change in ('public', 'closed', 'readonly', 'archived'):
            with self.subTest(change=change):
                self.api = FakeGitHub(self.state)
                board = self.api.remote_project
                if change == 'readonly':
                    board['viewerCanUpdate'] = False
                elif change == 'archived':
                    board['items'][0]['isArchived'] = True
                else:
                    board[change] = True
                with self.assertRaises(setup.SetupError):
                    self.preflight()
                self.assertEqual(self.api.writes, [])

    def test_access_denial_is_clear_and_never_claims_board_live(self):
        self.api.denied = True
        report = setup.base_report(self.settings, self.state)
        with self.assertRaisesRegex(setup.SetupError, 'Resource not accessible by integration'):
            setup.preflight(self.api, self.settings, self.state, report)
        self.assertFalse(report['applied'])
        self.assertFalse(report['live_project_verified'])
        self.assertEqual(self.api.writes, [])

    def test_existing_association_and_field_type_mismatches_are_preserved_for_review(self):
        self.api.remote_issues[0]['milestone'] = None
        with self.assertRaisesRegex(setup.SetupError, 'milestone association'):
            self.preflight()
        self.api = FakeGitHub(self.state)
        self.api.remote_project['fields'][3]['dataType'] = 'NUMBER'
        with self.assertRaisesRegex(setup.SetupError, 'incompatible type'):
            self.preflight()
        self.assertEqual(self.api.writes, [])

    def test_configuration_cannot_infer_acceptance_or_change_privacy(self):
        self.settings['project_requested']['public'] = True
        with self.assertRaises(setup.SetupError):
            setup.base_report(self.settings, self.state)
        self.settings, self.state = source()
        self.state['milestones'][0]['project_status'] = 'Accepted'
        with self.assertRaisesRegex(setup.SetupError, 'explicit accepted evidence'):
            setup.base_report(self.settings, self.state)

    def test_multiple_tasks_in_one_milestone_have_independent_items_and_statuses(self):
        tasks = self.add_actionable_fixture()
        self.api.remote_project = None
        context, report = self.preflight()
        setup.apply(self.api, context, report)
        options = {o['id']: o['name'] for o in self.api.remote_project['fields'][1]['options']}
        board_items = {i['content']['id']: i for i in self.api.remote_project['items']}
        for task in tasks:
            item = board_items['issue-' + task['id']]
            self.assertEqual(options[item['status']['optionId']], setup.WORK_ITEM_STATUSES[task['state']])
        self.assertNotEqual(board_items['issue-' + tasks[0]['id']]['id'],
                            board_items['issue-' + tasks[1]['id']]['id'])
        self.assertEqual([t['milestone_id'] for t in report['work_items'] if t['id'] in {x['id'] for x in tasks[:2]}],
                         ['m1', 'm1'])
        self.api.writes = []
        _, repeated = self.preflight()
        self.assertEqual(repeated['operations'], [])  # Empty waiting-resource requirements stay empty.
        self.assertEqual(self.api.writes, [])

    def test_actionable_manual_notes_values_options_and_items_are_preserved(self):
        self.add_actionable_fixture()
        before = copy.deepcopy(self.api.remote_project)
        prior_issues = copy.deepcopy(self.api.remote_issues)
        context, report = self.preflight()
        self.assertEqual(report['operations'], [])
        setup.apply(self.api, context, report)
        self.assertEqual(self.api.remote_project, before)
        self.assertEqual(self.api.remote_issues, prior_issues)
        self.assertEqual(self.api.writes, [])

    def test_text_milestone_field_uses_parent_title_for_every_actionable_issue(self):
        tasks = self.add_actionable_fixture()
        self.api.remote_project['fields'][2]['dataType'] = 'TEXT'
        task_ids = {'issue-' + t['id'] for t in tasks}
        for item in self.api.remote_project['items']:
            if item.get('content', {}).get('id') in task_ids:
                item['milestone'] = None
            elif item.get('content', {}).get('__typename') == 'Issue':
                item['milestone'] = {'__typename': 'ProjectV2ItemFieldTextValue',
                                     'text': 'Maintainer milestone note'}
        context, report = self.preflight()
        setup.apply(self.api, context, report)
        actual = {i['content']['id']: i['milestone']['text'] for i in self.api.remote_project['items']
                  if i.get('content', {}).get('id') in task_ids}
        self.assertEqual(actual['issue-' + tasks[0]['id']], self.state['milestones'][0]['title'])
        self.assertEqual(actual['issue-' + tasks[1]['id']], self.state['milestones'][0]['title'])
        self.assertEqual(actual['issue-' + tasks[2]['id']], self.state['milestones'][1]['title'])
        self.assertEqual(len(self.api.writes), 3)

    def test_missing_actionable_issue_fails_before_project_reads_or_writes(self):
        tasks = self.add_actionable_fixture()
        self.api.remote_issues = [i for i in self.api.remote_issues if i['node_id'] != 'issue-' + tasks[1]['id']]
        self.api.denied = True  # A missing source issue is diagnosed before Projects access.
        with self.assertRaisesRegex(setup.SetupError, 'm1-project-beta; run the issue sync'):
            self.preflight()
        self.assertEqual(self.api.writes, [])
        self.assertEqual(self.api.snapshot_count, 0)

    def test_duplicate_or_ambiguous_actionable_markers_fail_before_writes(self):
        tasks = self.add_actionable_fixture()
        for duplicate in ('issue', 'marker', 'other-task-marker', 'tracker-marker', 'node-id'):
            with self.subTest(duplicate=duplicate):
                self.api = FakeGitHub(self.state)
                a = next(i for i in self.api.remote_issues if i['node_id'] == 'issue-' + tasks[0]['id'])
                b = next(i for i in self.api.remote_issues if i['node_id'] == 'issue-' + tasks[1]['id'])
                if duplicate == 'issue':
                    self.api.remote_issues.append(copy.deepcopy(a))
                elif duplicate == 'marker':
                    a['body'] += '\n<!-- ctr2wup:work-item:' + tasks[0]['id'] + ' -->'
                elif duplicate == 'other-task-marker':
                    a['body'] += '\n<!-- ctr2wup:work-item:' + tasks[1]['id'] + ' -->'
                elif duplicate == 'tracker-marker':
                    a['body'] += '\n<!-- ctr2wup:milestone:m1 -->'
                else:
                    b['node_id'] = a['node_id']
                with self.assertRaises(setup.SetupError):
                    self.preflight()
                self.assertEqual(self.api.writes, [])
                self.assertEqual(self.api.snapshot_count, 0)

    def test_actionable_issue_requires_parent_association_and_unarchived_item(self):
        tasks = self.add_actionable_fixture()
        issue = next(i for i in self.api.remote_issues if i['node_id'] == 'issue-' + tasks[0]['id'])
        issue['milestone'] = {'title': tasks[0]['title']}
        with self.assertRaisesRegex(setup.SetupError, 'milestone association'):
            self.preflight()
        self.api = FakeGitHub(self.state)
        item = next(i for i in self.api.remote_project['items'] if i.get('content', {}).get('id') == 'issue-' + tasks[0]['id'])
        item['isArchived'] = True
        with self.assertRaisesRegex(setup.SetupError, 'archived'):
            self.preflight()
        self.assertEqual(self.api.writes, [])

    def test_actionable_configuration_cannot_accept_or_complete_a_milestone(self):
        tasks = self.add_actionable_fixture()
        for invalid in ('accepted', 'status', 'state', 'duplicate-id', 'dependency', 'cycle'):
            with self.subTest(invalid=invalid):
                state = copy.deepcopy(self.state)
                a, b = state['milestones'][0]['work_items'][-2:]
                if invalid == 'accepted':
                    a['accepted'] = True
                elif invalid == 'status':
                    a['project_status'] = 'Accepted'
                elif invalid == 'state':
                    a['state'] = 'done'
                elif invalid == 'duplicate-id':
                    b['id'] = a['id']
                elif invalid == 'dependency':
                    a['depends_on'] = ['missing-work-item']
                else:
                    a['depends_on'] = [b['id']]
                with self.assertRaises(setup.SetupError):
                    setup.base_report(self.settings, state)
                self.assertEqual(self.api.writes, [])
        self.assertEqual(self.state['full_milestones_accepted'], 0)


if __name__ == '__main__':
    unittest.main()
