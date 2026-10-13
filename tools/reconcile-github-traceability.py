#!/usr/bin/env python3
"""Reconcile reviewed feature/PR relationships using the trusted Actions credential.

Dry-run by default. Only catalogued issues, PRs and Project #2 are changed.
Full milestone acceptance and owner notes are never inferred from PR merges.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("tracking", ROOT / "tools/export-github-project.py")
tracking = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tracking)
REPO = tracking.REPOSITORY
FEATURE = re.compile(r"<!-- ctr2wup:feature:([a-z0-9-]+) -->")
DETAIL = '''query($id:ID!) { node(id:$id) { ... on Issue {
  id number state parent { id number }
  closedByPullRequestsReferences(first:100,includeClosedPrs:true) { nodes { id number } pageInfo { hasNextPage } }
} } }'''


def catalog(data):
    if data.get("schema_version") != 1 or data.get("repository") != REPO:
        raise tracking.TrackingError("Traceability catalog identity is invalid.")
    rows, ids, prs = data.get("features"), set(), set()
    if not isinstance(rows, list) or not 1 <= len(rows) <= 100:
        raise tracking.TrackingError("Feature catalog exceeds the reviewed budget.")
    reviewed = data.get("reviewed_statuses", {})
    if not isinstance(reviewed, dict) or any(
        not isinstance(number, str) or not number.isdecimal() or int(number) not in (*range(1, 8), *range(12, 47))
        or value not in ("Planned", "In progress", "Awaiting evidence", "Blocked", "Ready for review")
        for number, value in reviewed.items()
    ):
        raise tracking.TrackingError("Reviewed broad task status cannot accept or complete a gate.")
    for row in rows:
        key = row.get("id")
        if not isinstance(key, str) or not re.fullmatch(r"[a-z0-9]+(?:-[a-z0-9]+)*", key) or key in ids:
            raise tracking.TrackingError("Feature identities are invalid or repeated.")
        ids.add(key)
        m, parent = row.get("milestone"), row.get("parent_issue")
        if type(m) is not int or not 1 <= m <= 7 or type(parent) is not int:
            raise tracking.TrackingError("Feature requires a milestone and parent issue.")
        if parent != m and parent not in range(12 + (m - 1) * 5, 17 + (m - 1) * 5):
            raise tracking.TrackingError("Feature parent belongs to a different milestone.")
        if not isinstance(row.get("title"), str) or not 1 <= len(row["title"]) <= 160:
            raise tracking.TrackingError("Feature title is invalid.")
        for field in ("scope", "acceptance", "evidence"):
            if not isinstance(row.get(field), list) or not row[field] or any(not isinstance(s, str) or not s for s in row[field]):
                raise tracking.TrackingError("Feature scope, acceptance and evidence must be explicit.")
        if row.get("state") not in ("planned", "active", "implemented"):
            raise tracking.TrackingError("Feature state is invalid.")
        links = row.get("pull_requests")
        if not isinstance(links, list) or len(links) > 10:
            raise tracking.TrackingError("Feature PR relationship budget exceeded.")
        for number in links:
            if type(number) is not int or number <= 0 or number in prs:
                raise tracking.TrackingError("A PR must have one primary feature justification.")
            prs.add(number)
        if row["state"] != "planned" and not links:
            raise tracking.TrackingError("Active/completed features require visible PRs.")
    return rows


def body(row):
    parts = [f'<!-- ctr2wup:feature:{row["id"]} -->',
             f'Parent: #{row["parent_issue"]} · Milestone M{row["milestone"]}.',
             "This scoped feature does not accept its parent task or the full milestone."]
    for label, field in (("Scope", "scope"), ("Acceptance", "acceptance"), ("Evidence and limits", "evidence")):
        parts += [f"\n**{label}**\n", *[f"- {s}" for s in row[field]]]
    parts += ["\n**Implementation PRs**\n", *[f"- #{n}" for n in row["pull_requests"]]]
    return "\n".join(parts) + "\n"


def pages(client, path):
    rows = []
    for p in range(1, 11):
        batch = client.api(path + ("&" if "?" in path else "?") + f"per_page=100&page={p}")
        if not isinstance(batch, list):
            raise tracking.TrackingError("Traceability inventory is invalid.")
        rows += batch
        if len(batch) < 100:
            return rows
    raise tracking.TrackingError("Traceability inventory exceeds the review budget.")


def mutate(client, name, kind, value, selection):
    return client.graphql(f"mutation($input:{kind}!) {{ {name}(input:$input) {{ {selection} }} }}", {"input": value})[name]


def reconcile(client, data, apply, report):
    rows = catalog(data)
    issues = [i for i in pages(client, f"repos/{REPO}/issues?state=all") if "pull_request" not in i]
    by_number, features = {i["number"]: i for i in issues}, {}
    for issue in issues:
        matches = FEATURE.findall(issue.get("body") or "")
        if len(matches) > 1 or (matches and matches[0] in features):
            raise tracking.TrackingError("Feature issue identity is ambiguous; preserve and review.")
        if matches:
            features[matches[0]] = issue
    milestones = {m["number"]: m for m in pages(client, f"repos/{REPO}/milestones?state=all")}
    project = client.project()
    fields = {f["name"]: f for f in project["fields"]}
    status = {o["name"]: o["id"] for o in fields["Status"]["options"]}
    for required in ("Planned", "In progress", "Awaiting evidence", "Blocked", "Done"):
        if required not in status:
            raise tracking.TrackingError("Required Project status is absent; preserve existing options.")
    prs = {}
    for row in rows:
        if row["parent_issue"] not in by_number or row["milestone"] not in milestones:
            raise tracking.TrackingError("Feature parent/milestone is missing.")
        for number in row["pull_requests"]:
            pr = client.api(f"repos/{REPO}/pulls/{number}")
            if pr["base"]["ref"] != "main" or pr["base"]["repo"]["full_name"] != REPO:
                raise tracking.TrackingError("Feature PR must target this repository's main branch.")
            if row["state"] == "implemented" and not pr["merged_at"]:
                raise tracking.TrackingError("Historical completed feature has an unmerged PR.")
            prs[number] = pr
    # Preflight all native parents before writes; never steal/reparent an issue.
    edges = [(by_number[n], by_number[1 + (n - 12) // 5]) for n in range(12, 47)]
    edges += [(features[r["id"]], by_number[r["parent_issue"]]) for r in rows if r["id"] in features]
    for child, parent in edges:
        detail = client.graphql(DETAIL, {"id": child["node_id"]})["node"]
        if detail["parent"] and detail["parent"]["id"] != parent["node_id"]:
            raise tracking.TrackingError("Existing native issue parent differs; preserve and review.")
    report["planned"] = {"features": len(rows), "pull_requests": len(prs), "task_parents": 35}
    if not apply:
        return
    labels = {l["name"] for l in pages(client, f"repos/{REPO}/labels")}
    if "feature" not in labels:
        client.api(f"repos/{REPO}/labels", "POST", {"name": "feature", "color": "1D76DB", "description": "Scoped implementation with acceptance criteria, parent task and PR evidence"})
    for row in rows:
        if row["id"] not in features:
            issue = client.api(f"repos/{REPO}/issues", "POST", {"title": row["title"], "body": body(row),
                                "milestone": row["milestone"], "labels": ["feature"]})
            features[row["id"]] = issue
            report["operations"].append({"create_feature": row["id"], "issue": issue["number"]})
    # Link only scoped feature issues as closing references. Broad parents stay open.
    for row in rows:
        issue = features[row["id"]]
        detail = client.graphql(DETAIL, {"id": issue["node_id"]})["node"]
        if detail["closedByPullRequestsReferences"]["pageInfo"]["hasNextPage"]:
            raise tracking.TrackingError("Issue PR references exceed the review budget.")
        linked = {p["number"] for p in detail["closedByPullRequestsReferences"]["nodes"]}
        missing = [prs[n]["node_id"] for n in row["pull_requests"] if n not in linked]
        if missing:
            mutate(client, "addCloseIssueReferences", "AddCloseIssueReferencesInput",
                   {"issueId": issue["node_id"], "pullRequestIds": missing}, "clientMutationId")
            report["operations"].append({"link_prs": row["id"]})
        done = bool(row["pull_requests"]) and all(prs[n]["merged_at"] for n in row["pull_requests"])
        if done and issue["state"] != "closed":
            client.api(f'repos/{REPO}/issues/{issue["number"]}', "PATCH", {"state": "closed", "state_reason": "completed"})
            report["operations"].append({"complete_feature": row["id"]})
        if (issue.get("milestone") or {}).get("number") != row["milestone"]:
            client.api(f'repos/{REPO}/issues/{issue["number"]}', "PATCH", {"milestone": row["milestone"]})
        for number in row["pull_requests"]:
            if (prs[number].get("milestone") or {}).get("number") != row["milestone"]:
                client.api(f"repos/{REPO}/issues/{number}", "PATCH", {"milestone": row["milestone"]})
    edges = [(by_number[n], by_number[1 + (n - 12) // 5]) for n in range(12, 47)]
    edges += [(features[r["id"]], by_number[r["parent_issue"]]) for r in rows]
    for child, parent in edges:
        detail = client.graphql(DETAIL, {"id": child["node_id"]})["node"]
        if not detail["parent"]:
            mutate(client, "addSubIssue", "AddSubIssueInput", {"issueId": parent["node_id"], "subIssueId": child["node_id"]}, "clientMutationId")
            report["operations"].append({"set_parent": child["number"], "parent": parent["number"]})
    project = client.project()
    # Include PR content explicitly: the public aggregate reader deliberately omits it.
    selection = '''nodes { id content { ... on Issue { id } ... on PullRequest { id } }
      status:fieldValueByName(name:"Status") { ... on ProjectV2ItemFieldSingleSelectValue { name } }
    } pageInfo { hasNextPage endCursor }'''
    items = {i["content"]["id"]: i for i in client.connection(project["id"], "items", selection) if i.get("content") and i["content"].get("id")}
    desired = []
    for row in rows:
        done = bool(row["pull_requests"]) and all(prs[n]["merged_at"] for n in row["pull_requests"])
        value = "Done" if done else "In progress" if row["state"] == "active" else "Planned"
        desired.append((features[row["id"]]["node_id"], value, "Task"))
        desired += [(prs[n]["node_id"], "Done" if prs[n]["merged_at"] else "In progress", "Task") for n in row["pull_requests"]]
    # Explicit reviewed status changes; no gate is ever Accepted by this tool.
    for number, value in data.get("reviewed_statuses", {}).items():
        if value not in status or int(number) not in by_number:
            raise tracking.TrackingError("Reviewed broad task status cannot accept or complete a gate.")
        node = by_number[int(number)]["node_id"]
        # Migrate only the old empty/Todo values. Later owner edits stay authoritative.
        if node in items and (items[node].get("status") or {}).get("name") in (None, "Todo"):
            desired.append((node, value, "Milestone" if int(number) <= 7 else "Task"))
    for node, value, kind in desired:
        if node not in items:
            result = mutate(client, "addProjectV2ItemById", "AddProjectV2ItemByIdInput", {"projectId": project["id"], "contentId": node}, "item { id }")
            items[node] = {"id": result["item"]["id"], "status": None}
        item = items[node]
        previous = (item.get("status") or {}).get("name")
        if previous != value and (value == "Done" or previous in (None, "Todo", "Planned") or node in {by_number[int(n)]["node_id"] for n in data.get("reviewed_statuses", {})}):
            mutate(client, "updateProjectV2ItemFieldValue", "UpdateProjectV2ItemFieldValueInput",
                   {"projectId": project["id"], "itemId": item["id"], "fieldId": fields["Status"]["id"], "value": {"singleSelectOptionId": status[value]}}, "projectV2Item { id }")
        if "Work type" in fields:
            option = next(o["id"] for o in fields["Work type"]["options"] if o["name"] == kind)
            mutate(client, "updateProjectV2ItemFieldValue", "UpdateProjectV2ItemFieldValueInput",
                   {"projectId": project["id"], "itemId": item["id"], "fieldId": fields["Work type"]["id"], "value": {"singleSelectOptionId": option}}, "projectV2Item { id }")
    # Verify live relationships after mutations, never count attempts as successes.
    for child, parent in edges:
        detail = client.graphql(DETAIL, {"id": child["node_id"]})["node"]
        if not detail["parent"] or detail["parent"]["id"] != parent["node_id"]:
            raise tracking.TrackingError("Native hierarchy did not verify after reconciliation.")
    for row in rows:
        detail = client.graphql(DETAIL, {"id": features[row["id"]]["node_id"]})["node"]
        linked = {p["number"] for p in detail["closedByPullRequestsReferences"]["nodes"]}
        if not set(row["pull_requests"]) <= linked:
            raise tracking.TrackingError("Native PR links did not verify after reconciliation.")
    report["verified"] = {"native_parent_links": len(edges), "native_pr_links": len(prs),
                          "feature_issues": {r["id"]: features[r["id"]]["number"] for r in rows}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apply", action="store_true")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    report = {"ok": False, "applied": args.apply, "operations": []}
    try:
        reconcile(tracking.GitHub(), json.loads((ROOT / "docs/features.json").read_text()), args.apply, report)
        report["ok"] = True
    except (tracking.TrackingError, OSError, ValueError, KeyError, StopIteration) as error:
        report["error"] = str(error)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 0 if report["ok"] else 2


if __name__ == "__main__":
    sys.exit(main())
