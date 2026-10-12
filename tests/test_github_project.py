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
    return (json.loads((ROOT / '.github/repository-settings.json').read_text()),
            json.loads((ROOT / 'docs/milestone-status.json').read_text()))


def issues(state):
    return [{'node_id': 'issue-' + m['id'], 'number': i + 1, 'title': m['title'],
             'html_url': f'https://github.com/example/fixture/issues/{i + 1}',
             'body': '<!-- ctr2wup:milestone:' + m['id'] + ' -->\nMaintainer notes preserved',
             'milestone': {'title': m['title']}}
            for i, m in enumerate(state['milestones'])]


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
             for m in state['milestones']]
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

    def preflight(self):
        report = setup.base_report(self.settings, self.state)
        context = setup.preflight(self.api, self.settings, self.state, report)
        setup.plan(context, report)
        return context, report

    def test_new_project_dry_run_reuses_seven_issues_without_writes(self):
        self.api.remote_project = None
        _, report = self.preflight()
        self.assertTrue(report['ok'])
        self.assertFalse(report['applied'])
        self.assertFalse(report['live_project_verified'])
        self.assertEqual([m['number'] for m in report['trackers']], list(range(1, 8)))
        self.assertEqual(sum(o['kind'] == 'add-existing-issue' for o in report['operations']), 7)
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

    def test_new_board_apply_uses_four_options_seven_existing_issues_and_is_idempotent(self):
        self.api.remote_project = None
        context, report = self.preflight()
        setup.apply(self.api, context, report)
        self.assertTrue(report['applied'])
        self.assertFalse(self.api.remote_project['public'])
        self.assertEqual([option['name'] for option in self.api.remote_project['fields'][1]['options']], list(setup.STATUSES))
        self.assertEqual(len(self.api.remote_project['items']), 7)
        self.assertEqual({item['content']['id'] for item in self.api.remote_project['items']},
                         {'issue-' + m['id'] for m in self.state['milestones']})
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
        self.assertEqual(len(field['options']), 5)
        for item in self.api.remote_project['items'][:7]:
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


if __name__ == '__main__':
    unittest.main()
