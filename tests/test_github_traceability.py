"""Prevent premature gate acceptance and destructive hierarchy reconciliation."""
import copy
import importlib.util
import json
from pathlib import Path
import unittest

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
        return [{"number": n, "node_id": f"issue-{n}", "body": ""} for n in (*range(1, 8), *range(12, 47))]

    def project(self):
        return {"id": "project-2", "fields": [{"name": "Status", "options": [{"name": n, "id": n} for n in trace.tracking.STATUSES[:8]]}]}

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
