"""Prevent premature gate acceptance and destructive hierarchy reconciliation."""
import copy
import importlib.util
import json
from pathlib import Path
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("traceability_tests", ROOT / "tools/reconcile-github-traceability.py")
trace = importlib.util.module_from_spec(spec)
spec.loader.exec_module(trace)


class ReadOnlyInventory:
    def __init__(self, parent_conflict=False, unmerged=False):
        self.parent_conflict, self.unmerged, self.writes = parent_conflict, unmerged, []

    def api(self, path, method="GET", payload=None):
        if method != "GET":
            self.writes.append(path)
            raise AssertionError("preflight must complete before writes")
        if "/pulls/" in path:
            return {"node_id": "pr-8", "base": {"ref": "main", "repo": {"full_name": trace.REPO}}, "merged_at": None if self.unmerged else "2026-10-01"}
        if "/pulls?" in path:
            data = json.loads((ROOT / "docs/features.json").read_text())
            return [{"number": n} for row in data["features"] for n in row["pull_requests"]]
        if "/milestones?" in path:
            return [{"number": n} for n in range(1, 8)]
        return [{"number": n, "node_id": f"issue-{n}", "body": "<!-- ctr2wup:work-item:m4-pica-commands -->" if n == 27 else ""}
                for n in (*range(1, 8), *range(12, 47))]

    def project(self):
        return {"id": "project-2", "fields": [{"name": "Status", "options": [{"name": n, "id": n} for n in trace.tracking.STATUSES[:8]]}]}

    def connection(self, project_id, name, selection):
        return []

    def graphql(self, query, variables):
        if "closingIssuesReferences" in query:
            return {"repository": {"pullRequest": {"closingIssuesReferences": {"nodes": [], "pageInfo": {"hasNextPage": False}}}}}
        return {"node": {"parent": {"id": "foreign-parent", "number": 999} if self.parent_conflict else None}}


class TraceabilityTests(unittest.TestCase):
    def setUp(self):
        self.source = json.loads((ROOT / "docs/features.json").read_text())

    def test_delivered_and_active_prs_have_single_primary_justification(self):
        rows = trace.catalog(self.source)
        self.assertGreaterEqual(len(rows), 22)
        self.assertTrue({8, 9, 10, 11, *range(47, 59)} <= {p for r in rows for p in r["pull_requests"]})

    def test_refuse_duplicate_pr_and_cross_milestone_parent(self):
        for field, value in (("pull_requests", [8]), ("parent_issue", 46)):
            data = copy.deepcopy(self.source)
            data["features"][1][field] = value
            with self.assertRaises(trace.tracking.TrackingError):
                trace.catalog(data)

    def test_no_gate_completion_via_reviewed_statuses(self):
        for value in ("Done", "Accepted", "Closed"):
            data = copy.deepcopy(self.source)
            data["reviewed_statuses"]["4"] = value
            client = ReadOnlyInventory()
            with self.assertRaises(trace.tracking.TrackingError):
                trace.reconcile(client, data, True, {"operations": []})
            self.assertEqual(client.writes, [])

    def test_completion_review_cannot_accept_a_milestone_or_unreviewed_task(self):
        for identifier in ("m4", "m4-vertex-shaders", "m1-results"):
            data = copy.deepcopy(self.source)
            data["reviewed_completions"] = [identifier]
            client = ReadOnlyInventory()
            with self.assertRaises(trace.tracking.TrackingError):
                trace.reconcile(client, data, True, {"operations": []})
            self.assertEqual(client.writes, [])

    def test_completion_review_requires_its_original_task_identity(self):
        client = ReadOnlyInventory()
        original = client.api
        def api(path, method="GET", payload=None):
            result = original(path, method, payload)
            if "/issues?" in path:
                next(row for row in result if row["number"] == 27)["body"] = "owner changed task identity"
            return result
        client.api = api
        with self.assertRaisesRegex(trace.tracking.TrackingError, "identified task"):
            trace.reconcile(client, self.source, True, {"operations": []})
        self.assertEqual(client.writes, [])

    def test_parent_conflict_fails_before_mutations(self):
        client = ReadOnlyInventory(parent_conflict=True)
        with self.assertRaisesRegex(trace.tracking.TrackingError, "native issue parent"):
            trace.reconcile(client, self.source, True, {"operations": []})
        self.assertEqual(client.writes, [])

    def test_unmerged_historical_feature_fails_before_mutations(self):
        client = ReadOnlyInventory(unmerged=True)
        with self.assertRaisesRegex(trace.tracking.TrackingError, "unmerged"):
            trace.reconcile(client, self.source, True, {"operations": []})
        self.assertEqual(client.writes, [])

    def test_github_default_status_capitalization_keeps_existing_option(self):
        client = ReadOnlyInventory()
        project = client.project()
        for option in project["fields"][0]["options"]:
            if option["name"] == "In progress":
                option["name"] = "In Progress"
        client.project = lambda: project
        trace.reconcile(client, self.source, False, {"operations": []})
        self.assertEqual(client.writes, [])

    def test_dry_run_is_read_only(self):
        client, report = ReadOnlyInventory(), {"operations": []}
        trace.reconcile(client, self.source, False, report)
        self.assertEqual(client.writes, [])
        self.assertEqual(report["planned"]["task_parents"], 35)

    def test_membership_receipt_names_missing_public_references_beyond_first_page(self):
        expected = {f'issue-{n}': {'kind': 'issue', 'number': n} for n in range(1, 104)}
        observed = [{'id': f'private-item-{n}', 'content': {'id': f'issue-{n}', 'number': n,
                     '__typename': 'Issue', 'body': 'PRIVATE OWNER NOTE'}, 'isArchived': n == 5}
                    for n in range(1, 103)]
        report = trace.membership_report(expected, observed)
        self.assertEqual(report['expected_total'], 103)
        self.assertEqual(report['matched_total'], 102)
        self.assertEqual(report['missing'], [{'kind': 'issue', 'number': 103,
                                             'same_reference_with_different_identity': False}])
        self.assertEqual(report['archived_managed_total'], 1)
        self.assertNotIn('PRIVATE', json.dumps(report))
        self.assertNotIn('private-item', json.dumps(report))
        observed.append({'content': {'id': 'changed-node', '__typename': 'Issue', 'number': 103}})
        self.assertTrue(trace.membership_report(expected, observed)['missing'][0]['same_reference_with_different_identity'])

    def test_confirmed_addition_receives_bounded_read_refresh_without_repeating_writes(self):
        class Client:
            def __init__(self):
                self.reads = 0
            def connection(self, project_id, name, selection):
                self.reads += 1
                return [] if self.reads < 3 else [{'content': {'id': 'new-node'}}]
        client, report = Client(), {}
        expected = {'new-node': {'kind': 'pull_request', 'number': 101}}
        with patch.object(trace.time, 'sleep') as sleep:
            self.assertEqual(trace.verify_membership(client, 'project', expected, {'new-node'}, report), {'new-node'})
            self.assertEqual(sleep.call_count, 2)
        self.assertEqual(report['membership_readback']['missing'], [])
        self.assertEqual(report['membership_readback_attempts'], 3)

    def test_missing_existing_member_and_api_error_fail_without_retry(self):
        class Client:
            def __init__(self):
                self.reads = 0
            def connection(self, *args):
                self.reads += 1
                return []
        client = Client()
        with patch.object(trace.time, 'sleep') as sleep:
            with self.assertRaises(trace.tracking.TrackingError):
                trace.verify_membership(client, 'project', {'existing': {'kind': 'issue', 'number': 5}}, set(), {})
            self.assertEqual(client.reads, 1)
            sleep.assert_not_called()
        def denied(*args):
            raise trace.tracking.TrackingError('Original denied-read fixture')
        client.connection = denied
        with self.assertRaisesRegex(trace.tracking.TrackingError, 'denied-read'):
            trace.verify_membership(client, 'project', {'new': {'kind': 'issue', 'number': 99}}, {'new'}, {})

    def test_apply_adds_missing_pr_verifies_membership_and_preserves_broad_statuses(self):
        data = copy.deepcopy(self.source)
        data['features'] = [dict(data['features'][0], pull_requests=[8], state='implemented')]
        data['reviewed_statuses'], data['reviewed_completions'] = {}, []
        feature = data['features'][0]
        class Client:
            def __init__(self):
                self.writes = []
                self.issues = {n: {'number': n, 'node_id': f'issue-{n}', 'body': '',
                                  'state': 'open', 'milestone': {'number': 1}}
                               for n in (*range(1, 8), *range(12, 47))}
                self.issues[47] = {'number': 47, 'node_id': 'issue-47', 'state': 'closed',
                    'body': '<!-- ctr2wup:feature:' + feature['id'] + ' -->',
                    'milestone': {'number': feature['milestone']},
                    'labels': [{'name': 'feature'}, {'name': 'work-item'}]}
                self.pr = {'number': 8, 'node_id': 'pr-8', 'state': 'closed', 'merged_at': '2026-10-01',
                           'milestone': {'number': feature['milestone']},
                           'base': {'ref': 'main', 'repo': {'full_name': trace.REPO}}}
                self.items = [{'id': 'item-' + issue['node_id'], 'content': {'id': issue['node_id']},
                               'status': {'name': 'Done' if n == 47 else 'Blocked'}}
                              for n, issue in self.issues.items()]
            def api(self, path, method='GET', payload=None):
                if method != 'GET':
                    raise AssertionError('No issue, milestone or label mutation is needed')
                if '/issues?' in path:
                    return list(self.issues.values())
                if '/milestones?' in path:
                    return [{'number': n} for n in range(1, 8)]
                if '/labels?' in path:
                    return [{'name': 'feature'}, {'name': 'work-item'}]
                if '/pulls?' in path:
                    return [self.pr]
                if '/pulls/' in path:
                    return self.pr
                number = int(path.rsplit('/', 1)[1])
                return self.pr if number == 8 else self.issues[number]
            def project(self):
                return {'id': 'project', 'fields': [{'id': 'status', 'name': 'Status',
                    'options': [{'name': name, 'id': name} for name in trace.tracking.STATUSES[:8]]}]}
            def connection(self, *args):
                return copy.deepcopy(self.items)
            def graphql(self, query, variables):
                if query.startswith('mutation'):
                    payload = variables['input']
                    self.writes.append(copy.deepcopy(payload))
                    if 'contentId' in payload:
                        row = {'id': 'item-' + payload['contentId'], 'content': {'id': payload['contentId']}, 'status': None}
                        self.items.append(row)
                        return {'addProjectV2ItemById': {'item': {'id': row['id']}}}
                    row = next(row for row in self.items if row['id'] == payload['itemId'])
                    row['status'] = {'name': payload['value']['singleSelectOptionId']}
                    return {'updateProjectV2ItemFieldValue': {'projectV2Item': {'id': row['id']}}}
                number = int(variables['id'].split('-')[1])
                parent = feature['parent_issue'] if number == 47 else 1 + (number - 12) // 5
                return {'node': {'parent': {'id': f'issue-{parent}', 'number': parent},
                    'closedByPullRequestsReferences': {'nodes': [{'number': 8}] if number == 47 else [],
                                                     'pageInfo': {'hasNextPage': False}}}}
        client, report = Client(), {'operations': []}
        trace.reconcile(client, data, True, report)
        self.assertEqual(report['stage'], 'verified')
        self.assertEqual(report['verified']['project_members'], 44)
        self.assertEqual(report['membership_readback']['missing'], [])
        self.assertEqual(len(client.writes), 2)
        self.assertTrue(all(row['status']['name'] == 'Blocked' for row in client.items if
                            row['content']['id'] not in ('pr-8', 'issue-47')))

    def test_untracked_repository_pr_fails_before_writes(self):
        client = ReadOnlyInventory()
        original = client.api
        client.api = lambda path, method="GET", payload=None: [{"number": 999}] if "/pulls?" in path else original(path, method, payload)
        with self.assertRaisesRegex(trace.tracking.TrackingError, "missing primary feature"):
            trace.reconcile(client, self.source, True, {"operations": []})
        self.assertEqual(client.writes, [])

    def draft_inventory(self, closed=False, ambiguous=False):
        client = ReadOnlyInventory()
        original_api, original_graphql = client.api, client.graphql
        def api(path, method="GET", payload=None):
            result = original_api(path, method, payload)
            if "/pulls?" in path:
                result.append({"number": 999, "state": "open"})
            elif "/issues?" in path:
                result.append({"number": 76, "node_id": "feature-76", "state": "closed" if closed else "open",
                               "body": "<!-- ctr2wup:feature:pica-arithmetic -->"})
                if ambiguous:
                    result.append({"number": 77, "node_id": "feature-77", "state": "open",
                                   "body": "<!-- ctr2wup:feature:pica-tev -->"})
            return result
        def graphql(query, variables):
            if "closingIssuesReferences" in query:
                nodes = [{"id": "feature-76"}] + ([{"id": "feature-77"}] if ambiguous else [])
                return {"repository": {"pullRequest": {"closingIssuesReferences": {"nodes": nodes, "pageInfo": {"hasNextPage": False}}}}}
            return original_graphql(query, variables)
        client.api, client.graphql = api, graphql
        return client

    def test_new_draft_uses_known_native_feature_without_loading_topic_branch(self):
        client, report = self.draft_inventory(), {"operations": []}
        before = copy.deepcopy(self.source)
        trace.reconcile(client, self.source, False, report)
        self.assertEqual(report["discovered_native_prs"], [{"pull_request": 999, "feature": "pica-arithmetic"}])
        self.assertEqual(report["planned"]["pull_requests"], sum(len(r["pull_requests"]) for r in self.source["features"]) + 1)
        self.assertEqual(self.source, before)
        self.assertEqual(client.writes, [])

    def test_new_draft_cannot_extend_closed_or_ambiguous_feature(self):
        for client in (self.draft_inventory(closed=True), self.draft_inventory(ambiguous=True)):
            with self.assertRaises(trace.tracking.TrackingError):
                trace.reconcile(client, self.source, True, {"operations": []})
            self.assertEqual(client.writes, [])


if __name__ == "__main__":
    unittest.main()
