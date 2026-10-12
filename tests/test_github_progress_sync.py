"""Managed work issue checks with source requirements and mocked GitHub writes."""
import copy
import importlib.util
import json
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('github_progress_sync', ROOT / 'tools/sync-github-progress.py')
sync = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sync)
REPO = 'original/fixture'


def source():
    return json.loads((ROOT / 'docs/milestone-status.json').read_text())


class FakeRemote:
    def __init__(self, state, trackers=True):
        self.labels = set(sync.COLORS)
        self.milestones = [{'number': i + 1, 'title': m['title'], 'description': m['summary'],
                            'state': 'open', 'due_on': None} for i, m in enumerate(state['milestones'])]
        self.issues, self.comments = {}, {}
        self.reads, self.writes = [], []
        self.fail_write = False
        self.before_issue_read = None
        refs = {m['id']: {'number': i + 1, 'url': f'https://github.com/{REPO}/issues/{i + 1}'}
                for i, m in enumerate(state['milestones'])}
        if trackers:
            for i, milestone in enumerate(state['milestones']):
                number = i + 1
                self.issues[number] = {'number': number, 'title': milestone['title'], 'state': 'open',
                    'html_url': refs[milestone['id']]['url'], 'milestone': copy.deepcopy(self.milestones[i]),
                    'labels': [{'name': name} for name in sync.desired_labels(milestone)] + [{'name': 'manual-label'}],
                    'body': 'Manual introduction\n' + sync.body(REPO, state, milestone, refs) + 'Manual evidence note\n'}
                self.comments[number] = ['Original maintainer comment']

    def request(self, path, method='GET', payload=None):
        route = path.split('?', 1)[0]
        if method == 'GET':
            self.reads.append(path)
            if route.endswith('/milestones'):
                return copy.deepcopy(self.milestones)
            if route.endswith('/labels'):
                return [{'name': name} for name in self.labels]
            if route.endswith('/issues'):
                return copy.deepcopy(list(self.issues.values()))
            milestone = re.search(r'/milestones/(\d+)$', route)
            if milestone:
                return copy.deepcopy(next(m for m in self.milestones if m['number'] == int(milestone.group(1))))
            match = re.search(r'/issues/(\d+)$', route)
            if match:
                number = int(match.group(1))
                if self.before_issue_read:
                    self.before_issue_read(self, number)
                return copy.deepcopy(self.issues[number])
            raise AssertionError('Unexpected read: ' + route)
        if self.fail_write:
            raise RuntimeError('Mocked GitHub access denied')
        self.writes.append((method, route, copy.deepcopy(payload)))
        self.assert_preserved_request(method, route, payload)
        if method == 'POST' and route.endswith('/labels') and '/issues/' not in route:
            self.labels.add(payload['name'])
            return {'name': payload['name']}
        if method == 'POST' and route.endswith('/milestones'):
            milestone = {'number': len(self.milestones) + 1, **payload}
            self.milestones.append(milestone)
            return copy.deepcopy(milestone)
        if method == 'POST' and route.endswith('/issues'):
            number = max(self.issues, default=0) + 1
            issue = {'number': number, 'title': payload['title'], 'body': payload['body'], 'state': 'open',
                     'html_url': f'https://github.com/{REPO}/issues/{number}',
                     'labels': [{'name': name} for name in payload['labels']],
                     'milestone': copy.deepcopy(next(m for m in self.milestones if m['number'] == payload['milestone']))}
            self.issues[number] = issue
            self.comments[number] = []
            return copy.deepcopy(issue)
        milestone = re.search(r'/milestones/(\d+)$', route)
        if milestone and method == 'PATCH':
            assert set(payload) == {'description'}
            remote = next(m for m in self.milestones if m['number'] == int(milestone.group(1)))
            remote['description'] = payload['description']
            return copy.deepcopy(remote)
        match = re.search(r'/issues/(\d+)(/labels)?$', route)
        if match:
            issue = self.issues[int(match.group(1))]
            if method == 'POST' and match.group(2):
                names = sync.label_names(issue)
                issue['labels'] += [{'name': name} for name in payload['labels'] if name not in names]
                return copy.deepcopy(issue['labels'])
            if method == 'PATCH':
                if 'body' in payload:
                    issue['body'] = payload['body']
                if 'milestone' in payload:
                    issue['milestone'] = copy.deepcopy(next(m for m in self.milestones if m['number'] == payload['milestone']))
                return copy.deepcopy(issue)
        raise AssertionError('Unexpected write: ' + route)

    @staticmethod
    def assert_preserved_request(method, route, payload):
        if '/issues' in route:
            assert 'state' not in payload and 'state_reason' not in payload
            if method == 'PATCH':
                assert set(payload) <= {'body', 'milestone'}


class GithubProgressSyncTests(unittest.TestCase):
    def setUp(self):
        self.state = source()
        self.remote = FakeRemote(self.state)

    def run_sync(self, apply=False):
        return sync.synchronize(REPO, self.state, apply, self.remote.request)

    def populate(self):
        report = self.run_sync(True)
        self.remote.writes = []
        return report

    def work_issue(self, identifier):
        marker = '<!-- ctr2wup:work-item:' + identifier + ' -->'
        return next(issue for issue in self.remote.issues.values() if marker in issue['body'])

    def test_source_contains_separate_valid_work_items_for_each_milestone(self):
        work = sync.validate_state(self.state)
        self.assertEqual(len(work), 35)
        self.assertTrue(all(len(m['work_items']) == 5 for m in self.state['milestones']))
        self.assertEqual(self.state['full_milestones_accepted'], 0)

    def test_dry_run_preserves_seven_ids_and_previews_35_new_issues_without_writes(self):
        report = self.run_sync()
        self.assertTrue(report['ok'])
        self.assertFalse(report['applied'])
        self.assertFalse(report['writes_attempted'])
        self.assertEqual(len([o for o in report['operations'] if o['kind'] == 'work-item' and o.get('create')]), 35)
        self.assertEqual([report['references'][f'm{i}']['number'] for i in range(1, 8)], list(range(1, 8)))
        self.assertEqual(self.remote.writes, [])

    def test_apply_creates_work_issues_associations_parent_dependency_links_and_initial_status(self):
        comments = copy.deepcopy(self.remote.comments)
        report = self.run_sync(True)
        self.assertEqual(len(self.remote.issues), 42)
        self.assertEqual(report['work_item_count'], 35)
        self.assertFalse(report['milestone_acceptance_inferred'])
        self.assertFalse(report['issue_states_changed'])
        for milestone in self.state['milestones']:
            for item in milestone['work_items']:
                issue = self.work_issue(item['id'])
                self.assertEqual(issue['milestone']['title'], milestone['title'])
                self.assertIn('work-item', sync.label_names(issue))
                self.assertIn('p2', sync.label_names(issue))
                self.assertIn(milestone['area'], sync.label_names(issue))
                self.assertIn('status:' + item['state'].replace('_', '-'), sync.label_names(issue))
                self.assertIn(report['references'][milestone['id']]['url'], issue['body'])
                for dependency in item['depends_on']:
                    self.assertIn(report['references'][dependency]['url'], issue['body'])
                self.assertNotIn('issue number pending creation', issue['body'])
        for number in range(1, 8):
            self.assertIn('Manual introduction', self.remote.issues[number]['body'])
            self.assertIn('Manual evidence note', self.remote.issues[number]['body'])
            self.assertEqual(self.remote.comments[number], comments[number])
            self.assertEqual(self.remote.issues[number]['state'], 'open')

    def test_repeated_apply_is_idempotent(self):
        self.populate()
        before = copy.deepcopy(self.remote.issues)
        report = self.run_sync(True)
        self.assertTrue(report['ok'])
        self.assertEqual(self.remote.writes, [])
        self.assertEqual(self.remote.issues, before)

    def test_milestone_descriptions_replace_exact_old_summary_and_preserve_lifecycle(self):
        self.remote.milestones[0].update(state='closed', due_on='2026-12-01T00:00:00Z')
        report = self.run_sync(True)
        for milestone, source_milestone in zip(self.remote.milestones, self.state['milestones']):
            description = milestone['description']
            self.assertTrue(description.startswith(sync.MILESTONE_BEGIN))
            self.assertEqual(description.count(sync.MILESTONE_BEGIN), 1)
            self.assertIn('**Goal:**', description)
            self.assertIn('**Planning state:**', description)
            self.assertIn('**Focused work:** 5 issues', description)
            self.assertIn('**Remaining full acceptance:**', description)
            self.assertIn(report['references'][source_milestone['id']]['url'], description)
            self.assertIn('/docs/milestones.md#' + source_milestone['id'], description)
            self.assertLessEqual(len(description), 1600)
        self.assertEqual(self.remote.milestones[0]['state'], 'closed')
        self.assertEqual(self.remote.milestones[0]['due_on'], '2026-12-01T00:00:00Z')

    def test_manual_milestone_description_surroundings_are_retained_across_updates(self):
        manual = 'Maintainer release plan\nDo not remove this manual evidence note.\n'
        self.remote.milestones[0]['description'] = manual
        self.run_sync(True)
        description = self.remote.milestones[0]['description']
        self.assertTrue(description.startswith(manual))
        self.assertEqual(description.count(sync.MILESTONE_BEGIN), 1)
        suffix = '\nManual QA follow-up after the managed section.'
        self.remote.milestones[0]['description'] += suffix
        self.state['milestones'][0]['summary'] += ' Revised source scope.'
        self.remote.writes = []
        self.run_sync(True)
        description = self.remote.milestones[0]['description']
        self.assertTrue(description.startswith(manual))
        self.assertTrue(description.endswith(suffix))
        self.assertEqual(description.count(sync.MILESTONE_BEGIN), 1)
        self.remote.writes = []
        self.run_sync(True)
        self.assertEqual(self.remote.writes, [])

    def test_ambiguous_milestone_description_boundaries_fail_before_all_writes(self):
        self.remote.labels = set()
        self.remote.milestones[-1]['description'] = 'Manual text\n' + sync.MILESTONE_BEGIN + '\nIncomplete managed data'
        with self.assertRaisesRegex(RuntimeError, 'description boundaries'):
            self.run_sync(True)
        self.assertEqual(self.remote.writes, [])

    def test_manual_notes_comments_title_labels_and_checked_criteria_are_preserved(self):
        self.populate()
        item = self.state['milestones'][0]['work_items'][0]
        issue = self.work_issue(item['id'])
        criterion = item['acceptance_criteria'][0]
        issue['body'] = 'Private manual intro\n' + issue['body'].replace('- [ ] ' + criterion, '- [x] ' + criterion) + '\nPrivate manual note'
        issue['title'] = 'Maintainer chosen title'
        issue['labels'] += [{'name': 'manual-extra'}, {'name': 'status:ready-for-review'}]
        self.remote.comments[issue['number']] = ['Manual comment stays untouched']
        item['summary'] += ' Additional source context.'
        report = self.run_sync(True)
        self.assertIn('- [x] ' + criterion, issue['body'])
        self.assertIn('Private manual intro', issue['body'])
        self.assertIn('Private manual note', issue['body'])
        self.assertEqual(issue['title'], 'Maintainer chosen title')
        self.assertIn('manual-extra', sync.label_names(issue))
        self.assertIn('status:ready-for-review', sync.label_names(issue))
        self.assertNotIn('Private manual note', json.dumps(report))
        self.assertEqual(self.remote.comments[issue['number']], ['Manual comment stays untouched'])

    def test_existing_manual_single_or_multiple_statuses_are_not_replaced(self):
        self.populate()
        issue = self.work_issue('m1-results')
        for selected in (['status:blocked'], ['status:blocked', 'status:in-progress']):
            issue['labels'] = [{'name': n} for n in ['work-item', 'area:runtime', 'manual-extra', *selected]]
            self.remote.writes = []
            self.run_sync(True)
            self.assertEqual([n for n in sync.label_names(issue) if n.startswith('status:')], selected)
            for method, route, payload in self.remote.writes:
                if route.endswith('/labels'):
                    self.assertEqual(method, 'POST')
                    self.assertFalse(any(n.startswith('status:') for n in payload['labels']))

    def test_manual_priorities_are_preserved_and_missing_work_priority_defaults_to_p2(self):
        self.populate()
        issue = self.work_issue('m1-results')
        issue['labels'] = [label for label in issue['labels'] if label['name'] != 'p2'] + [{'name': 'p0'}]
        self.run_sync(True)
        self.assertIn('p0', sync.label_names(issue))
        self.assertNotIn('p2', sync.label_names(issue))
        issue['labels'] = [label for label in issue['labels'] if label['name'] != 'p0']
        self.remote.writes = []
        self.run_sync(True)
        self.assertIn('p2', sync.label_names(issue))
        self.assertTrue(all('p2' not in sync.label_names(self.remote.issues[number]) for number in range(1, 8)))

    def test_new_triage_labels_have_meaningful_bounded_descriptions(self):
        self.remote.labels = set()
        self.run_sync(True)
        created = {payload['name']: payload for method, route, payload in self.remote.writes
                   if method == 'POST' and route.endswith('/labels') and '/issues/' not in route}
        for name in ('area:docs', 'area:github', 'p0', 'p1', 'p2', 'p3'):
            self.assertGreater(len(created[name]['description']), 25)
            self.assertLessEqual(len(created[name]['description']), 100)
        self.assertIn('does not establish a verified bug', created['p2']['description'])

    def test_label_case_variants_do_not_override_manual_state_or_priority(self):
        self.populate()
        self.remote.labels = {name.upper() for name in self.remote.labels}
        issue = self.work_issue('m1-results')
        issue['labels'] = [{'name': name.upper()} for name in ('work-item', 'area:runtime', 'needs-console', 'p0', 'status:blocked')]
        self.run_sync(True)
        self.assertEqual(self.remote.writes, [])
        self.assertIn('P0', sync.label_names(issue))
        self.assertIn('STATUS:BLOCKED', sync.label_names(issue))

    def test_closed_issue_remains_closed_and_does_not_accept_any_gate(self):
        self.populate()
        issue = self.work_issue('m1-results')
        issue['state'] = 'closed'
        issue['labels'] = [{'name': n} for n in sync.label_names(issue) if not n.startswith('status:')]
        self.run_sync(True)
        self.assertEqual(issue['state'], 'closed')
        self.assertIn('status:closed', sync.label_names(issue))
        self.assertEqual(self.state['full_milestones_accepted'], 0)
        self.assertEqual(self.remote.issues[1]['state'], 'open')

    def test_missing_status_and_labels_are_added_without_losing_manual_labels(self):
        self.remote.issues[1]['labels'] = [{'name': 'manual-extra'}]
        self.run_sync(True)
        names = sync.label_names(self.remote.issues[1])
        self.assertIn('manual-extra', names)
        self.assertIn('status:awaiting-evidence', names)
        self.assertIn('milestone', names)

    def test_source_duplicate_ids_unknown_dependencies_cycles_and_states_fail_before_reads(self):
        for bad in ('duplicate', 'unknown', 'self', 'cycle', 'state', 'waiting'):
            with self.subTest(bad=bad):
                state = copy.deepcopy(self.state)
                first, second = state['milestones'][0]['work_items'][:2]
                if bad == 'duplicate': second['id'] = first['id']
                elif bad == 'unknown': first['depends_on'] = ['m9-unknown']
                elif bad == 'self': first['depends_on'] = [first['id']]
                elif bad == 'cycle': first['depends_on'], second['depends_on'] = [second['id']], [first['id']]
                elif bad == 'state': first['state'] = 'accepted'
                else: first['waiting_labels'] = ['unknown-resource']
                remote = FakeRemote(self.state)
                with self.assertRaises(ValueError):
                    sync.synchronize(REPO, state, True, remote.request)
                self.assertEqual(remote.reads, [])
                self.assertEqual(remote.writes, [])

    def test_duplicate_remote_marker_detected_before_even_missing_label_creation(self):
        self.remote.labels = set()
        duplicate = copy.deepcopy(self.remote.issues[7]);duplicate['number'] = 50
        self.remote.issues[50] = duplicate
        with self.assertRaisesRegex(RuntimeError, 'Duplicate managed marker'):
            self.run_sync(True)
        self.assertEqual(self.remote.writes, [])

    def test_wrong_parent_or_milestone_association_fails_before_writes(self):
        self.populate()
        issue = self.work_issue('m1-results')
        before = copy.deepcopy(issue)
        for bad in ('parent', 'milestone', 'extra-parent'):
            with self.subTest(bad=bad):
                issue.clear();issue.update(copy.deepcopy(before))
                if bad == 'parent': issue['body'] = issue['body'].replace('parent-milestone:m1', 'parent-milestone:m2')
                elif bad == 'extra-parent': issue['body'] += '<!-- ctr2wup:parent-milestone:m8 -->'
                else: issue['milestone'] = copy.deepcopy(self.remote.milestones[1])
                with self.assertRaisesRegex(RuntimeError, 'Ambiguous'):
                    self.run_sync(True)
                self.assertEqual(self.remote.writes, [])

    def test_malformed_or_moved_managed_boundaries_are_not_overwritten(self):
        for bad in ('missing', 'reverse', 'identity'):
            with self.subTest(bad=bad):
                remote = FakeRemote(self.state)
                issue = remote.issues[7]
                if bad == 'missing': issue['body'] = issue['body'].replace(sync.END, '')
                elif bad == 'reverse': issue['body'] = issue['body'].replace(sync.BEGIN, 'TEMP').replace(sync.END, sync.BEGIN).replace('TEMP', sync.END)
                else:
                    marker = '<!-- ctr2wup:milestone:m7 -->'
                    issue['body'] = issue['body'].replace(marker, '').replace(sync.BEGIN, sync.BEGIN + '\n' + marker)
                with self.assertRaises(RuntimeError):
                    sync.synchronize(REPO, self.state, True, remote.request)
                self.assertEqual(remote.writes, [])

    def test_refresh_preserves_notes_and_status_added_after_preflight(self):
        self.remote.issues[1]['labels'] = [{'name': 'manual-extra'}]
        def change(remote, number):
            if number == 1:
                remote.issues[1]['body'] += '\nConcurrent manual note'
                remote.issues[1]['labels'].append({'name': 'status:blocked'})
                remote.before_issue_read = None
        self.remote.before_issue_read = change
        self.run_sync(True)
        self.assertIn('Concurrent manual note', self.remote.issues[1]['body'])
        self.assertIn('status:blocked', sync.label_names(self.remote.issues[1]))
        self.assertNotIn('status:awaiting-evidence', sync.label_names(self.remote.issues[1]))

    def test_retired_managed_work_issue_is_preserved_without_closing(self):
        self.populate()
        retired = self.state['milestones'][6]['work_items'].pop()
        issue = self.work_issue(retired['id'])
        before = copy.deepcopy(issue)
        report = self.run_sync(True)
        self.assertIn(retired['id'], report['unmanaged_existing_ids_preserved'])
        self.assertEqual(issue, before)

    def test_missing_tracker_and_milestone_can_be_created_before_work_links(self):
        self.remote = FakeRemote(self.state, trackers=False)
        self.remote.milestones = []
        report = self.run_sync(True)
        self.assertEqual(len(self.remote.milestones), 7)
        self.assertEqual(len(self.remote.issues), 42)
        self.assertEqual(len(report['references']), 42)
        self.assertTrue(all('issue number pending creation' not in issue['body'] for issue in self.remote.issues.values()))

    def test_write_denial_reports_attempt_without_confirmed_operations(self):
        self.remote.labels = set()
        self.remote.fail_write = True
        report = {}
        with self.assertRaisesRegex(RuntimeError, 'access denied'):
            sync.synchronize(REPO, self.state, True, self.remote.request, report)
        self.assertTrue(report['writes_attempted'])
        self.assertEqual(report['completed_operations'], [])
        self.assertEqual(self.remote.writes, [])


if __name__ == '__main__':
    unittest.main()
