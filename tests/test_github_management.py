"""Direct API access, source guards and original Project review failure cases."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from test_github_project_export import fixture, set_status

ROOT = Path(__file__).resolve().parents[1]


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'tools' / filename)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


review = module('management_review_test', 'review-github-project.py')
manage = module('direct_management_test', 'manage-github.py')
local = module('local_validation_test', 'validate-local.py')


class Client:
    def __init__(self):
        self.source, self.data = fixture()
        self.plan = json.loads((ROOT / 'docs/project-reviews.json').read_text())
        self.writes, self.recorded = [], []
        self.denied = False
        self.race = None
        self.fail_at = None
        self.corrupt_readback = False
        for row in self.data['items']:
            if 'ctr2wup:milestone:m5 ' in row['content']['body'] or 'ctr2wup:work-item:m5-' in row['content']['body']:
                set_status(row, 'Blocked')
                row['waiting'] = {'__typename': 'ProjectV2ItemFieldTextValue', 'text': 'Owned decrypted executable and assets;Wii U console results; keep this manual note '}

    def project(self):
        if self.denied:
            raise manage.tracking.TrackingError('Project access denied.')
        return copy.deepcopy(self.data)

    def target(self, key):
        return next(row for row in self.data['items'] if ':' + key + ' -->' in row['content']['body'])

    def api(self, path, method='GET', payload=None):
        if method != 'GET':
            self.writes.append((path, payload))
        if '/comments' in path:
            if method == 'POST':
                self.recorded.append({'body': payload['body']})
                return self.recorded[-1]
            return copy.deepcopy(self.recorded)
        if path == 'repos/' + manage.tracking.REPOSITORY:
            return {'full_name': manage.tracking.REPOSITORY, 'private': False,
                    'default_branch': 'main', 'permissions': {'push': True}}
        if path == 'repos/' + manage.DOCS:
            return {'full_name': manage.DOCS, 'private': False,
                    'default_branch': 'main', 'permissions': {'push': True}}
        if path.endswith('/commits/main'):
            return {'sha': 'a' * 40}
        raise AssertionError(path)

    def graphql(self, query, variables):
        if query.startswith('query'):
            row = next(row for row in self.data['items'] if row['id'] == variables['id'])
            if self.race:
                self.race(row)
            return {'node': copy.deepcopy(row)}
        payload = variables['input']
        row = next(row for row in self.data['items'] if row['id'] == payload['itemId'])
        if self.fail_at is not None and len(self.writes) == self.fail_at:
            raise review.tracking.TrackingError('Original simulated partial API failure.')
        self.writes.append(('graphql', payload))
        if payload['fieldId'] == 'status-field':
            option = next(value for value in self.data['fields'][0]['options'] if value['id'] == payload['value']['singleSelectOptionId'])
            set_status(row, option['name'] if not self.corrupt_readback else 'Awaiting evidence')
        else:
            row['waiting'] = {'__typename': 'ProjectV2ItemFieldTextValue', 'text': payload['value']['text']} if 'value' in payload else None
        return {'updateProjectV2ItemFieldValue': {'projectV2Item': {'id': row['id']}}}


class ManagementTests(unittest.TestCase):
    def apply(self, client, apply=True, report=None):
        return review.review(client, client.source, client.plan, 'mario-input-received-v023', apply, report)

    def test_dry_run_has_no_writes_and_six_bounded_targets(self):
        client = Client()
        result = self.apply(client, False)
        self.assertTrue(result['ok'])
        self.assertEqual(len(result['targets']), 6)
        self.assertEqual(client.writes, [])

    def test_apply_activates_three_preserves_notes_and_console_waits(self):
        client = Client()
        result = self.apply(client)
        self.assertTrue(result['review_recorded'])
        for key in ('m5', 'm5-intake', 'm5-requirements'):
            self.assertEqual(client.target(key)['status']['name'], 'In progress')
        for key in ('m5-runtime-integration', 'm5-gameplay-route', 'm5-title-acceptance'):
            self.assertEqual(client.target(key)['status']['name'], 'Blocked')
        for change in client.plan['reviews'][0]['changes']:
            self.assertEqual(client.target(change['id'])['waiting']['text'], 'Wii U console results; keep this manual note ')
        self.assertFalse(result['full_gate_accepted'])

    def test_replay_preserves_owner_edits_after_recorded_review(self):
        client = Client()
        self.apply(client)
        set_status(client.target('m5'), 'Awaiting evidence')
        client.target('m5')['waiting']['text'] = 'owner changed this later'
        previous = len(client.writes)
        self.assertTrue(self.apply(client)['already_reviewed'])
        self.assertEqual(len(client.writes), previous)
        self.assertEqual(client.target('m5')['status']['name'], 'Awaiting evidence')

    def test_review_plan_is_immutable_after_receipt(self):
        client = Client()
        self.apply(client)
        client.plan['reviews'][0]['reason'] += ' changed'
        previous = len(client.writes)
        with self.assertRaisesRegex(review.tracking.TrackingError, 'immutable'):
            self.apply(client)
        self.assertEqual(len(client.writes), previous)

    def test_conflicting_last_target_rejects_whole_batch_before_writes(self):
        client = Client()
        set_status(client.target('m5-title-acceptance'), 'Done')
        with self.assertRaisesRegex(review.tracking.TrackingError, 'conflicting'):
            self.apply(client)
        self.assertEqual(client.writes, [])

    def test_archived_duplicate_and_moved_target_refused(self):
        for change in ('archived', 'duplicate', 'moved'):
            with self.subTest(change=change):
                client = Client()
                row = client.target('m5-requirements')
                if change == 'archived':
                    row['isArchived'] = True
                elif change == 'duplicate':
                    client.data['items'].append(copy.deepcopy(row))
                else:
                    row['content']['repository']['nameWithOwner'] = 'other/repository'
                with self.assertRaises(review.tracking.TrackingError):
                    self.apply(client)
                self.assertEqual(client.writes, [])

    def test_done_closed_accepted_and_cross_parent_plans_refused(self):
        for value in ('Done', 'Closed', 'Accepted', 'm4'):
            client = Client()
            change = client.plan['reviews'][0]['changes'][0]
            change['id' if value == 'm4' else 'to_status'] = value
            with self.assertRaises(review.tracking.TrackingError):
                self.apply(client)
            self.assertEqual(client.writes, [])

    def test_race_changes_raw_notes_without_changing_category(self):
        client = Client()
        client.race = lambda row: row['waiting'].update(text=row['waiting']['text'] + 'owner addition')
        with self.assertRaisesRegex(review.tracking.TrackingError, 'changed during'):
            self.apply(client)
        self.assertEqual(client.writes, [])

    def test_partial_failure_has_no_completed_marker_and_retry_finishes(self):
        client = Client()
        client.fail_at = 1
        receipt = {}
        with self.assertRaises(review.tracking.TrackingError):
            self.apply(client, report=receipt)
        self.assertTrue(receipt['writes_attempted'])
        self.assertEqual(len(receipt['operations_completed']), 1)
        self.assertEqual(client.recorded, [])
        client.fail_at = None
        self.assertTrue(self.apply(client)['review_recorded'])

    def test_wrong_readback_never_records_completion(self):
        client = Client()
        client.corrupt_readback = True
        with self.assertRaises(review.tracking.TrackingError):
            self.apply(client)
        self.assertEqual(client.recorded, [])

    def test_resource_removal_is_exact_and_preserves_similar_notes(self):
        resource = 'Owned decrypted executable and assets'
        self.assertEqual(review.remove_resources('note about ' + resource, [resource]), 'note about ' + resource)
        self.assertIsNone(review.remove_resources(' ' + resource + ' ', [resource]))
        self.assertEqual(review.remove_resources(' a ;' + resource + '; b ', [resource]), ' a ; b ')

    def test_duplicate_receipt_refused_without_writes(self):
        client = Client()
        self.apply(client)
        client.recorded.append(copy.deepcopy(client.recorded[0]))
        previous = len(client.writes)
        with self.assertRaises(review.tracking.TrackingError):
            self.apply(client)
        self.assertEqual(len(client.writes), previous)

    def test_management_credential_binding_does_not_fallback(self):
        with self.assertRaises(manage.tracking.TrackingError):
            manage.credential_environment({'GH_TOKEN': 'original-fixture'})
        env = manage.credential_environment({manage.SECRET: 'original-fixture', 'GH_TOKEN': 'other-fixture', 'HTTPS_PROXY': 'original-proxy'})
        self.assertEqual(env['GH_TOKEN'], 'original-fixture')
        self.assertEqual(env['HTTPS_PROXY'], 'original-proxy')

    def test_management_access_denial_precedes_every_write(self):
        client = Client()
        client.denied = True
        receipt = {}
        with self.assertRaises(manage.tracking.TrackingError):
            manage.preflight(client, client.source, receipt)
        self.assertEqual(receipt['stage'], 'project')
        self.assertEqual(client.writes, [])

    def test_preflight_does_not_claim_project_write_access_from_read(self):
        client = Client()
        result = manage.preflight(client, client.source, {})
        self.assertTrue(result['ok'])
        self.assertFalse(result['project_write_verified'])
        self.assertEqual(client.writes, [])

    def test_evidence_and_docs_do_not_require_unrelated_project_access(self):
        for operation in ('evidence', 'docs'):
            client = Client()
            client.denied = True
            result = manage.preflight(client, client.source, {}, operation)
            self.assertTrue(result['ok'])
            self.assertFalse(result['project_read_verified'])
            self.assertEqual(result['docs_write_permission_verified'], operation == 'docs')
            self.assertEqual(client.writes, [])

    def test_apply_requires_clean_main_and_exact_remote_commit(self):
        client = Client()
        for branch, dirty, remote in [('topic', '', 'a'*40), ('main', ' M tools/script.py', 'a'*40), ('main', '', 'b'*40)]:
            values = {'rev-parse': 'a'*40, 'branch': branch, 'status': dirty}
            with patch.object(manage, 'git', side_effect=lambda *args: values[args[0]]):
                original = client.api
                client.api = lambda *args: {'sha': remote}
                with self.assertRaises(manage.tracking.TrackingError):
                    manage.trusted_source(client)
                client.api = original

    def test_local_profiles_do_not_skip_runtime_or_workflow_changes(self):
        self.assertEqual(local.profile(['tools/manage-github.py', 'docs/direct-development.md']), 'maintenance')
        self.assertEqual(local.profile(['runtime/wiiu/main.c']), 'runtime')
        self.assertEqual(local.profile(['host/main.py']), 'runtime')
        self.assertEqual(local.profile(['tools/package-console.py']), 'runtime')
        self.assertEqual(local.profile(['AGENTS.md.fake', 'tests/test_github_evil.c']), 'runtime')
        with self.assertRaises(ValueError):
            local.profile(['.github/workflows/ci.yml'])

    def test_local_failure_and_commit_change_do_not_claim_pass(self):
        for result_code, final_sha in [(2, 'a'*40), (0, 'b'*40)]:
            with tempfile.TemporaryDirectory(dir=ROOT / 'artifacts') as directory:
                output = Path(directory) / 'new'
                calls = 0
                def fake_git(*args):
                    nonlocal calls
                    if args[0] == 'status':
                        return ''
                    if args[0] == 'diff':
                        return 'tools/manage-github.py'
                    if args[0] == 'rev-parse' and args[1] == 'HEAD':
                        calls += 1
                        return 'a'*40 if calls == 1 else final_sha
                    return 'c'*40
                def runner(command, **kwargs):
                    kwargs['stdout'].write('original validation output')
                    return type('Result', (), {'returncode': result_code})()
                with patch.object(local, 'git', side_effect=fake_git):
                    receipt = local.validate(output, runner=runner)
                self.assertFalse(receipt['ok'])
                self.assertFalse(receipt['hosted_ci'])
                self.assertTrue((output / 'validation.json').is_file())


if __name__ == '__main__':
    unittest.main()
