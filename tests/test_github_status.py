"""Original mocked GitHub status cases; no credentials, API calls or writes."""
import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('github_status', ROOT / 'tools/sync-github-status.py')
status = importlib.util.module_from_spec(spec)
spec.loader.exec_module(status)
HEAD, MERGE, MAIN = 'a' * 40, 'b' * 40, 'c' * 40
PREFIX = 'repos/example/fixture'


def labels(*names):
    return [{'name': name} for name in names]


def check(name, ident, conclusion='success', state='completed'):
    return {'name': name, 'id': ident, 'status': state, 'conclusion': conclusion,
            'app': {'slug': 'github-actions'}}


def issue(ident=1, *names):
    return {'number': ident, 'state': 'open', 'state_reason': None, 'labels': labels(*names),
            'title': 'Original mocked task', 'body': 'Maintainer text'}


def pr(ident=2, *names):
    item = issue(ident, *names)
    item.update({'pull_request': {'url': 'fixture-only'}, 'draft': False, 'merged_at': None,
                 'head': {'sha': HEAD, 'ref': 'feature/original', 'repo': {'full_name': 'fork/fixture'}},
                 'merge_commit_sha': MERGE})
    return item


class FakeGitHub:
    def __init__(self, items=()):
        self.items = {item['number']: copy.deepcopy(item) for item in items}
        self.checks = {HEAD: [], MERGE: [], MAIN: []}
        self.commits = {MERGE: {'sha': MERGE, 'parents': [{'sha': HEAD}, {'sha': MAIN}]}}
        self.runs = {}
        self.writes = []
        self.reads = []
        self.before_write_refresh = None
        self.item_reads = {}

    def request(self, path, method='GET', payload=None):
        parsed = urlsplit(path)
        endpoint = parsed.path
        if method != 'GET':
            self.writes.append((path, method, copy.deepcopy(payload)))
            parts = endpoint.split('/')
            target = int(parts[4])
            item = self.items[target]
            current = status.label_names(item)
            if method == 'POST':
                current.update(payload['labels'])
            elif method == 'DELETE':
                current.remove(unquote(parts[6]))
            else:
                raise AssertionError('Never replace the complete labels list')
            item['labels'] = labels(*sorted(current))
            return None
        self.reads.append(path)
        if endpoint == PREFIX:
            return {'full_name': 'example/fixture', 'default_branch': 'main'}
        if endpoint == PREFIX + '/branches/main':
            return {'commit': {'sha': MAIN}}
        if endpoint == PREFIX + '/pulls':
            return copy.deepcopy([item for item in self.items.values() if 'pull_request' in item and item['state'] == 'open'])
        if endpoint == PREFIX + '/issues':
            return copy.deepcopy(list(self.items.values()))
        if '/check-runs' in endpoint:
            return {'check_runs': copy.deepcopy(self.checks[endpoint.split('/')[-2]])}
        if endpoint.startswith(PREFIX + '/commits/'):
            return copy.deepcopy(self.commits[endpoint.split('/')[-1]])
        if '/actions/runs/' in endpoint:
            return copy.deepcopy(self.runs[int(endpoint.split('/')[-1])])
        if endpoint.startswith(PREFIX + '/issues/') or endpoint.startswith(PREFIX + '/pulls/'):
            ident = int(endpoint.split('/')[-1])
            self.item_reads[endpoint] = self.item_reads.get(endpoint, 0) + 1
            if self.before_write_refresh and self.item_reads[endpoint] == 2:
                self.before_write_refresh(self.items[ident])
            return copy.deepcopy(self.items[ident])
        raise AssertionError('Unexpected API endpoint ' + path)


class GitHubStatusTests(unittest.TestCase):
    def test_stale_merge_success_cannot_make_new_head_ready(self):
        api = FakeGitHub([pr(2)])
        api.commits[MERGE]['parents'] = [{'sha': 'd' * 40}, {'sha': MAIN}]
        api.checks[MERGE] = [check('Required CI', 10), check('Documentation build', 11)]
        report = status.reconcile(api, 'example/fixture', selected=2)
        self.assertEqual(report['items'][0]['state'], 'in_progress')
        self.assertIn('Missing required checks', report['items'][0]['reason'])
        self.assertFalse(any('/commits/' + MERGE + '/check-runs' in path for path in api.reads))
        self.assertTrue(any('/commits/' + HEAD + '/check-runs' in path for path in api.reads))

    def test_current_merge_parent_proof_allows_its_checks(self):
        api = FakeGitHub([pr(2)])
        api.checks[MERGE] = [check('Required CI', 10), check('Documentation build', 11)]
        report = status.reconcile(api, 'example/fixture', selected=2)
        self.assertEqual(report['items'][0]['state'], 'ready_for_review')
        self.assertIn(PREFIX + '/commits/' + MERGE, api.reads)

    def test_stale_merge_failure_does_not_block_passing_current_head(self):
        api = FakeGitHub([pr(2)])
        api.commits[MERGE]['parents'] = [{'sha': 'd' * 40}, {'sha': MAIN}]
        api.checks[HEAD] = [check('Required CI', 10), check('Documentation build', 11)]
        api.checks[MERGE] = [check('Required CI', 12, 'failure')]
        report = status.reconcile(api, 'example/fixture', selected=2)
        self.assertEqual(report['items'][0]['state'], 'ready_for_review')

    def test_capitalized_issue_choice_and_pr_evidence_hold_are_preserved(self):
        api = FakeGitHub([issue(1, 'Status:Planned', 'Area:CPU'), pr(2, 'Status:Awaiting-Evidence')])
        api.checks[HEAD] = [check('Required CI', 10), check('Documentation build', 11)]
        report = status.reconcile(api, 'example/fixture', apply=True)
        self.assertEqual([item['state'] for item in report['items']], ['planned', 'awaiting_evidence'])
        self.assertEqual(status.label_names(api.items[1]), {'Status:Planned', 'Area:CPU'})
        self.assertEqual(status.label_names(api.items[2]), {'Status:Awaiting-Evidence'})
        self.assertEqual(api.writes, [])

    def test_capitalized_event_status_deletes_actual_stale_spelling(self):
        api = FakeGitHub([issue(1, 'Status:Planned', 'STATUS:AWAITING-EVIDENCE', 'area:cpu')])
        event = {'action': 'labeled', 'issue': {'number': 1}, 'label': {'name': 'Status:Blocked'},
                 'sender': {'type': 'User'}}
        report = status.reconcile(api, 'example/fixture', apply=True, event=event)
        self.assertEqual(report['items'][0]['state'], 'blocked')
        self.assertEqual(status.label_names(api.items[1]), {'status:blocked', 'area:cpu'})
        deletions = {write[0] for write in api.writes if write[1] == 'DELETE'}
        self.assertEqual(deletions, {PREFIX + '/issues/1/labels/Status%3APlanned',
                                    PREFIX + '/issues/1/labels/STATUS%3AAWAITING-EVIDENCE'})
        api.writes.clear()
        status.reconcile(api, 'example/fixture', apply=True)
        self.assertEqual(api.writes, [])

    def test_exclusive_manual_issue_selection_preserves_area_wait_and_unknown_labels(self):
        api = FakeGitHub([issue(1, 'status:planned', 'status:in-progress', 'area:cpu', 'needs-console', 'manual:paused')])
        event = {'action': 'labeled', 'issue': {'number': 1}, 'label': {'name': 'status:awaiting-evidence'},
                 'sender': {'type': 'User'}}
        report = status.reconcile(api, 'example/fixture', apply=True, event=event)
        self.assertEqual(status.label_names(api.items[1]), {'status:awaiting-evidence', 'area:cpu', 'needs-console', 'manual:paused'})
        self.assertEqual(report['items'][0]['state'], 'awaiting_evidence')
        self.assertEqual([write[1] for write in api.writes], ['POST', 'DELETE', 'DELETE'])
        self.assertTrue(all('/labels' in write[0] for write in api.writes))

    def test_full_reconcile_preserves_an_ordinary_manual_issue_status(self):
        for state_name, label in status.STATES.items():
            api = FakeGitHub([issue(1, label, 'area:host')])
            report = status.reconcile(api, 'example/fixture', apply=True)
            self.assertEqual(report['items'][0]['state'], state_name)
            self.assertEqual(api.writes, [])

    def test_dry_run_then_apply_and_reapply_are_idempotent(self):
        api = FakeGitHub([issue()])
        report = status.reconcile(api, 'example/fixture')
        self.assertFalse(report['applied'])
        self.assertEqual(len(report['operations']), 1)
        self.assertEqual(api.writes, [])
        status.reconcile(api, 'example/fixture', apply=True)
        self.assertEqual(status.label_names(api.items[1]), {'status:planned'})
        api.writes.clear()
        report = status.reconcile(api, 'example/fixture', apply=True)
        self.assertEqual(report['operations'], [])
        self.assertEqual(api.writes, [])

    def test_conflicting_issue_statuses_require_explicit_selection(self):
        api = FakeGitHub([issue(1, 'status:blocked', 'status:awaiting-evidence')])
        report = status.reconcile(api, 'example/fixture', apply=True)
        self.assertIsNone(report['items'][0]['state'])
        self.assertIn('explicit', report['items'][0]['reason'])
        self.assertEqual(api.writes, [])

    def test_issue_close_reason_and_reopen_do_not_modify_acceptance_or_body(self):
        for reason, expected in [('completed', 'done'), ('not_planned', 'closed')]:
            item = issue(1, 'status:blocked', 'milestone')
            item['state'], item['state_reason'] = 'closed', reason
            api = FakeGitHub([item])
            report = status.reconcile(api, 'example/fixture', apply=True, event={'action': 'closed', 'issue': {'number': 1}})
            self.assertEqual(report['items'][0]['state'], expected)
            self.assertEqual(api.items[1]['body'], item['body'])
            self.assertEqual(api.items[1]['state'], 'closed')
            self.assertTrue(all('/labels' in write[0] for write in api.writes))
            api.items[1]['state'] = 'open'
            report = status.reconcile(api, 'example/fixture', apply=True, event={'action': 'reopened', 'issue': {'number': 1}})
            self.assertEqual(report['items'][0]['state'], 'planned')

    def test_pr_draft_failure_pending_and_missing_docs(self):
        item = pr(2)
        item['draft'] = True
        failed = [check('Required CI', 1, 'failure')]
        self.assertEqual(status.pr_state(item, failed)[0], 'in_progress')
        item['draft'] = False
        self.assertEqual(status.pr_state(item, failed)[0], 'blocked')
        self.assertEqual(status.pr_state(item, [check('Required CI', 1)])[0], 'in_progress')
        pending = [check('Required CI', 1), check('Documentation build', 2, None, 'in_progress')]
        self.assertEqual(status.pr_state(item, pending)[0], 'in_progress')
        self.assertEqual(status.pr_state(item, [check('Required CI', 1), check('Documentation build', 2)])[0], 'ready_for_review')

    def test_latest_rerun_is_authoritative_cancelled_or_failed_optional_checks_block(self):
        item = pr(2)
        initial = [check('Required CI', 1, 'failure'), check('Documentation build', 2)]
        self.assertEqual(status.pr_state(item, initial + [check('Required CI', 3)])[0], 'ready_for_review')
        self.assertEqual(status.pr_state(item, initial + [check('Required CI', 3, None, 'queued')])[0], 'in_progress')
        for conclusion in ('cancelled', 'timed_out', 'action_required', 'failure'):
            self.assertEqual(status.pr_state(item, initial + [check('Required CI', 3), check('Optional fixture', 4, conclusion)])[0], 'blocked')
        self.assertEqual(status.pr_state(item, [check('Required CI', 1, 'skipped'), check('Documentation build', 2)])[0], 'blocked')

    def test_evidence_hold_pr_closure_and_merged_historical_pr(self):
        item = pr(2, 'status:awaiting-evidence')
        passing = [check('Required CI', 1), check('Documentation build', 2)]
        self.assertEqual(status.pr_state(item, passing)[0], 'awaiting_evidence')
        self.assertEqual(status.pr_state(item, [], 'in_progress')[0], 'in_progress')
        item['state'] = 'closed'
        self.assertEqual(status.pr_state(item, passing)[0], 'closed')
        item['merged_at'] = '2026-10-12T00:00:00Z'
        self.assertEqual(status.pr_state(item, [])[0], 'done')

    def test_spoofed_check_names_from_other_apps_and_status_workflow_are_ignored(self):
        checks = [check('Required CI', 1), check('Documentation build', 2), check(status.OWN_CHECK, 3, 'failure')]
        foreign = check('Required CI', 4, 'failure')
        foreign['app']['slug'] = 'unrelated-fixture'
        self.assertEqual(status.check_state(checks + [foreign])[0], 'ready_for_review')

    def test_workflow_run_targets_only_associated_current_repository_prs(self):
        api = FakeGitHub([pr(2), issue(1, 'status:blocked')])
        api.checks[HEAD] = [check('Required CI', 1), check('Documentation build', 2)]
        api.runs[20] = {'name': 'CI', 'head_sha': HEAD, 'pull_requests': [{'number': 2}, {'number': 99}]}
        report = status.reconcile(api, 'example/fixture', apply=True, event={'workflow_run': {'id': 20}})
        self.assertEqual([item['number'] for item in report['items']], [2])
        self.assertEqual(status.label_names(api.items[1]), {'status:blocked'})
        self.assertEqual(status.label_names(api.items[2]), {'status:ready-for-review'})
        api.runs[21] = {'name': 'Unrelated fixture', 'head_sha': HEAD, 'pull_requests': [{'number': 2}]}
        report = status.reconcile(api, 'example/fixture', apply=True, event={'workflow_run': {'id': 21}})
        self.assertEqual(report['items'], [])

    def test_midflight_new_commit_or_manual_status_is_not_overwritten(self):
        for mutate in (lambda item: item['head'].update(sha='d' * 40),
                       lambda item: item.update(labels=labels('status:awaiting-evidence'))):
            api = FakeGitHub([pr(2)])
            api.checks[HEAD] = [check('Required CI', 1), check('Documentation build', 2)]
            api.before_write_refresh = mutate
            report = status.reconcile(api, 'example/fixture', apply=True, selected=2)
            self.assertIn('skipped', report['items'][0])
            self.assertEqual(api.writes, [])
            self.assertNotEqual(report['branches'][0]['state'], 'ready_for_review')
            self.assertEqual(report['branches'][0]['sha'], api.items[2]['head']['sha'])

    def test_untrusted_fork_body_and_head_names_are_data_only_and_summary_escaped(self):
        with tempfile.TemporaryDirectory() as directory:
            sentinel = Path(directory) / 'executed'
            text = '$(touch ' + str(sentinel) + ') <script>|unsafe\nbranch'
            item = pr(2)
            item['head']['ref'] = text
            item['title'] = item['body'] = text
            api = FakeGitHub([item])
            api.checks[HEAD] = [check('Required CI', 1), check('Documentation build', 2)]
            with patch.object(subprocess, 'run', side_effect=AssertionError('No code execution in reconciliation')):
                report = status.reconcile(api, 'example/fixture', apply=True, selected=2)
            self.assertFalse(sentinel.exists())
            self.assertEqual(api.items[2]['body'], text)
            summary = status.markdown(report)
            self.assertNotIn('<script>', summary)
            self.assertIn('&lt;script&gt;&#124;unsafe branch', summary)
            self.assertEqual(report['branches'][0]['state'], 'ready_for_review')
            self.assertEqual(report['branches'][0]['repository'], 'fork/fixture')

    def test_trusted_workflow_has_no_head_or_artifact_execution(self):
        workflow = (ROOT / '.github/workflows/status.yml').read_text()
        self.assertIn('pull_request_target:', workflow)
        self.assertIn('persist-credentials: false', workflow)
        self.assertIn('ref: ${{ steps.trusted.outputs.sha }}', workflow)
        self.assertNotIn('download-artifact@', workflow)
        self.assertNotIn('${{ github.event.pull_request.head', workflow)
        self.assertNotIn('${{ github.event.workflow_run.head', workflow)

    def test_argument_validation_and_numeric_api_scope(self):
        api = FakeGitHub()
        for repository in ('owner/repo;touch /tmp/x', '../repo', 'owner/repo/extra'):
            with self.assertRaises(status.StatusError):
                status.reconcile(api, repository)
        for value in (0, -1, True, '2', 2**31):
            with self.assertRaises(status.StatusError):
                status.number(value)
        with self.assertRaises(status.StatusError):
            status.reconcile(api, 'example/fixture', explicit='done')
        with self.assertRaises(status.StatusError):
            status.sha('$(false)')

    def test_bot_label_event_does_not_force_manual_status(self):
        api = FakeGitHub([issue(1, 'status:planned')])
        report = status.reconcile(api, 'example/fixture', apply=True, event={
            'action': 'labeled', 'issue': {'number': 1}, 'label': {'name': 'status:done'},
            'sender': {'type': 'Bot'}})
        self.assertEqual(report['items'][0]['state'], 'planned')
        self.assertEqual(api.writes, [])

    def test_gh_payload_is_stdin_and_token_never_enters_arguments(self):
        completed = subprocess.CompletedProcess([], 0, '{"ok":true}', '')
        with patch.object(subprocess, 'run', return_value=completed) as run:
            status.GitHub().request(PREFIX + '/issues/1/labels', 'POST', {'labels': ['status:planned']})
        args, kwargs = run.call_args
        self.assertEqual(args[0], ['gh', 'api', PREFIX + '/issues/1/labels', '--method', 'POST', '--input', '-'])
        self.assertEqual(json.loads(kwargs['input']), {'labels': ['status:planned']})
        self.assertNotIn('shell', kwargs)

    def test_deleted_fork_metadata_remains_a_data_only_branch_report(self):
        item = pr(2)
        item['head']['repo'] = None
        report = status.reconcile(FakeGitHub([item]), 'example/fixture', selected=2)
        self.assertIsNone(report['branches'][0]['repository'])
        self.assertEqual(report['branches'][0]['state'], 'in_progress')

    def test_default_branch_check_health_and_event_scope_are_separate(self):
        api = FakeGitHub([issue(1, 'status:awaiting-evidence'), pr(2, 'status:blocked')])
        api.checks[MAIN] = [check('Required CI', 10), check('Documentation build', 11)]
        report = status.reconcile(api, 'example/fixture', apply=True, event={'action': 'opened', 'issue': {'number': 1}})
        self.assertEqual(report['default_branch']['state'], 'ready_for_review')
        self.assertEqual([row['number'] for row in report['items']], [1])
        self.assertEqual(status.label_names(api.items[2]), {'status:blocked'})
        self.assertEqual(api.writes, [])

    def test_api_pagination_is_bounded(self):
        class TooMany:
            def __init__(self):
                self.calls = 0
            def request(self, path):
                self.calls += 1
                return [None] * 100
        api = TooMany()
        with self.assertRaises(status.StatusError):
            status.pages(api, 'fixture')
        self.assertEqual(api.calls, 10)


if __name__ == '__main__':
    unittest.main()
