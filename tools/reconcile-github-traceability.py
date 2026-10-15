#!/usr/bin/env python3
"""Reconcile reviewed feature/PR relationships using the trusted Actions credential.

Dry-run by default. Only catalogued issues, PRs and Project #2 are changed.
Full milestone acceptance and owner notes are never inferred from PR merges.
"""
from __future__ import annotations

import argparse
import copy
import importlib.util
import json
from pathlib import Path
import re
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("tracking", ROOT / "tools/export-github-project.py")
tracking = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tracking)
REPO = tracking.REPOSITORY
FEATURE = re.compile(r"<!-- ctr2wup:feature:([a-z0-9-]+) -->")
COMMAND_REVIEW = "<!-- ctr2wup:reviewed-command-boundary:v023 -->"
DETAIL = '''query($id:ID!) { node(id:$id) { ... on Issue {
  id number state parent { id number }
  closedByPullRequestsReferences(first:100,includeClosedPrs:true) { nodes { id number } pageInfo { hasNextPage } }
} } }'''


def catalog(data):
    if data.get("schema_version") != 1 or data.get("repository") != REPO:
        raise tracking.TrackingError("Traceability catalog identity is invalid.")
    rows, ids, prs = data.get("features"), set(), set()
    completions = data.get("reviewed_completions", [])
    if not isinstance(completions, list) or completions not in ([], ["m4-pica-commands"]):
        raise tracking.TrackingError("Only the explicitly reviewed bounded command task can complete; full gates remain open.")
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


MEMBER_SELECTION = '''nodes { id isArchived content { __typename
  ... on Issue { id number } ... on PullRequest { id number }
} status:fieldValueByName(name:"Status") {
  ... on ProjectV2ItemFieldSingleSelectValue { name }
} } pageInfo { hasNextPage endCursor }'''

# GitHub documents this edge (including archived items) on both Issue and PR:
# https://docs.github.com/en/graphql/reference/pulls#pullrequest
CONTENT_ITEM_FIELDS = '''nodes { id isArchived project { id } content { __typename
  ... on Issue { id number } ... on PullRequest { id number }
} status:fieldValueByName(name:"Status") {
  ... on ProjectV2ItemFieldSingleSelectValue { name }
} } pageInfo { hasNextPage }'''
CONTENT_MEMBERSHIPS = '''query($id:ID!) { node(id:$id) { id __typename
  ... on Issue { projectItems(first:100,includeArchived:true) { ''' + CONTENT_ITEM_FIELDS + ''' } }
  ... on PullRequest { projectItems(first:100,includeArchived:true) { ''' + CONTENT_ITEM_FIELDS + ''' } }
} }'''


def complete_membership_read(client, project_id, expected, observed, report, label):
    """Confirm a small missing set independently; never infer membership from writes.

    Large inventory loss keeps the ordinary failure path. No Project settings,
    archived flags or memberships are mutated here. Public receipts contain
    only repository references and booleans, never private Project/item IDs.
    """
    present = {(row.get('content') or {}).get('id') for row in observed}
    missing = sorted(set(expected)-present)
    if not missing or len(missing)>8:
        return observed
    added, diagnostics = [], []
    report[label] = diagnostics
    for node in missing:
        reference = expected[node]
        detail = client.graphql(CONTENT_MEMBERSHIPS, {'id': node}).get('node')
        kind = 'PullRequest' if reference['kind']=='pull_request' else 'Issue'
        connection = detail.get('projectItems') if isinstance(detail, dict) else None
        page = connection.get('pageInfo') if isinstance(connection, dict) else None
        if (not isinstance(detail, dict) or detail.get('id')!=node or detail.get('__typename')!=kind
                or not isinstance(connection, dict) or not isinstance(connection.get('nodes'), list)
                or len(connection['nodes'])>100 or not isinstance(page, dict) or page.get('hasNextPage') is not False):
            raise tracking.TrackingError('Independent Project membership identity or budget did not verify.')
        matches = []
        for row in connection['nodes']:
            if not isinstance(row, dict) or not isinstance(row.get('project'), dict):
                raise tracking.TrackingError('Independent Project membership schema did not verify.')
            if row['project'].get('id')==project_id:
                matches.append(row)
        diagnostics.append(dict(reference, selected_project_links=len(matches),
                                archived=any(row.get('isArchived') is True for row in matches)))
        if len(matches)>1:
            raise tracking.TrackingError('Managed content has ambiguous selected Project memberships; preserve and review.')
        if matches:
            row=matches[0];content=row.get('content');status=row.get('status')
            if (not isinstance(row.get('id'), str) or type(row.get('isArchived')) is not bool
                    or not isinstance(content, dict) or content.get('id')!=node
                    or content.get('__typename')!=kind or content.get('number')!=reference['number']
                    or status is not None and (not isinstance(status, dict) or not isinstance(status.get('name'), str))):
                raise tracking.TrackingError('Independent Project content identity did not verify.')
            if row['isArchived']:
                raise tracking.TrackingError('Managed content is archived in the selected Project; preserve and review.')
            added.append(row)
    return [*observed, *added]


def membership_report(expected, observed):
    """Expose only repository-public references, never Project notes or identities."""
    members = {row['content']['id'] for row in observed if row.get('content') and row['content'].get('id')}
    references = {(row['content'].get('__typename'), row['content'].get('number'))
                  for row in observed if row.get('content')}
    missing = [{**reference, 'same_reference_with_different_identity':
                ('PullRequest' if reference['kind'] == 'pull_request' else 'Issue', reference['number']) in references}
               for node, reference in expected.items() if node not in members]
    return {'expected_total': len(expected), 'observed_total': len(observed),
            'matched_total': len(set(expected) & members),
            'missing': sorted(missing, key=lambda row: (row['kind'], row['number'])),
            'archived_managed_total': sum(bool(row.get('isArchived')) for row in observed
                if (row.get('content') or {}).get('id') in expected)}


def verify_membership(client, project_id, expected, added, report):
    """Refresh only confirmed additions awaiting Project connection indexing.

    Missing pre-existing members and API errors fail immediately. Mutations are
    never repeated to turn an unverifiable operation into reported success.
    """
    for attempt in range(5):
        fresh = client.connection(project_id, 'items', MEMBER_SELECTION)
        members = {row['content']['id'] for row in fresh if row.get('content') and row['content'].get('id')}
        report['membership_readback'] = membership_report(expected, fresh)
        report['membership_readback_attempts'] = attempt + 1
        missing = set(expected) - members
        if not missing:
            return members
        if not missing <= added:
            break
        if attempt < 4:
            time.sleep(0.5 * (2 ** attempt))
    # Connection indexing may lag an acknowledged addition. An independent
    # reverse edge must prove exact active membership before it is counted.
    report['membership_connection_readback'] = report['membership_readback']
    complete = complete_membership_read(client, project_id, expected, fresh, report, 'membership_readback_direct')
    report['membership_readback'] = membership_report(expected, complete)
    if not report['membership_readback']['missing']:
        return {row['content']['id'] for row in complete if (row.get('content') or {}).get('id')}
    raise tracking.TrackingError('Project membership did not verify for every managed issue and PR.')


def reconcile(client, data, apply, report):
    report["stage"] = "preflight"
    report["changes_possible"] = False
    rows = copy.deepcopy(catalog(data))
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
    options = {o["name"].casefold(): o["id"] for o in fields["Status"]["options"]}
    status = {name: options[name.casefold()] for name in tracking.STATUSES[:8] if name.casefold() in options}
    for required in ("Planned", "In progress", "Awaiting evidence", "Blocked", "Done"):
        if required not in status:
            raise tracking.TrackingError("Required Project status is absent; preserve existing options.")
    if any(value not in status for value in data.get("reviewed_statuses", {}).values()):
        raise tracking.TrackingError("A reviewed Project status has no existing option.")
    registered = {n for row in rows for n in row["pull_requests"]}
    visible = pages(client, f"repos/{REPO}/pulls?state=all")
    by_feature_node = {issue["node_id"]: key for key, issue in features.items()}
    by_key = {row["id"]: row for row in rows}
    report["discovered_native_prs"] = []
    for pr in visible:
        if pr["number"] in registered:
            continue
        # A new draft can close an already planned feature while its ledger
        # update is still on the topic branch. Read only native relationships;
        # never load or execute that branch or arbitrary PR body instructions.
        query = '''query($number:Int!) { repository(owner:"Jdiller-86",name:"CTR-2-WUP") {
          pullRequest(number:$number) { closingIssuesReferences(first:100) {
            nodes { id } pageInfo { hasNextPage }
          } }
        } }'''
        connection = client.graphql(query, {"number": pr["number"]})["repository"]["pullRequest"]["closingIssuesReferences"]
        candidates = {by_feature_node[node["id"]] for node in connection["nodes"]
                      if node["id"] in by_feature_node and by_feature_node[node["id"]] in by_key}
        if connection["pageInfo"]["hasNextPage"] or len(candidates) != 1:
            raise tracking.TrackingError("Repository PRs are missing primary feature justification; link one known scoped feature before applying.")
        key = candidates.pop()
        if features[key]["state"] == "closed" and pr["state"] == "open":
            raise tracking.TrackingError("A new active PR cannot extend a closed feature; create or reopen its scoped issue first.")
        by_key[key]["pull_requests"].append(pr["number"])
        if by_key[key]["state"] == "planned":
            by_key[key]["state"] = "active"
        report["discovered_native_prs"].append({"pull_request": pr["number"], "feature": key})
    catalog(dict(data, features=rows))  # Keep primary ownership and link budgets bounded.
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
    review_commands = "m4-pica-commands" in data.get("reviewed_completions", [])
    if review_commands and (27 not in by_number
            or "<!-- ctr2wup:work-item:m4-pica-commands -->" not in (by_number[27].get("body") or "")
            or any(n not in prs or not prs[n]["merged_at"] for n in (48, 57))):
        raise tracking.TrackingError("Reviewed command completion requires the identified task and both merged implementation PRs.")
    # Preflight all native parents before writes; never steal/reparent an issue.
    edges = [(by_number[n], by_number[1 + (n - 12) // 5]) for n in range(12, 47)]
    edges += [(features[r["id"]], by_number[r["parent_issue"]]) for r in rows if r["id"] in features]
    for child, parent in edges:
        detail = client.graphql(DETAIL, {"id": child["node_id"]})["node"]
        if detail["parent"] and detail["parent"]["id"] != parent["node_id"]:
            raise tracking.TrackingError("Existing native issue parent differs; preserve and review.")
    report["planned"] = {"features": len(rows), "pull_requests": len(prs), "task_parents": 35}
    expected_members = {i['node_id']: {'kind': 'issue', 'number': i['number']} for i in by_number.values()
                        if i['number'] in (*range(1, 8), *range(12, 47))}
    expected_members.update({features[row['id']]['node_id']: {'kind': 'issue', 'number': features[row['id']]['number']}
                             for row in rows if row['id'] in features})
    expected_members.update({pr['node_id']: {'kind': 'pull_request', 'number': number} for number, pr in prs.items()})
    inventory = complete_membership_read(client, project['id'], expected_members,
        client.connection(project['id'], 'items', MEMBER_SELECTION), report, 'membership_preflight_direct')
    report['membership_preflight'] = membership_report(expected_members, inventory)
    if not apply:
        return
    report["changes_possible"] = True
    report["stage"] = "create-scoped-features"
    labels = {l["name"] for l in pages(client, f"repos/{REPO}/labels")}
    if "feature" not in labels:
        client.api(f"repos/{REPO}/labels", "POST", {"name": "feature", "color": "1D76DB", "description": "Scoped implementation with acceptance criteria, parent task and PR evidence"})
    for row in rows:
        if row["id"] not in features:
            issue = client.api(f"repos/{REPO}/issues", "POST", {"title": row["title"], "body": body(row),
                                "milestone": row["milestone"], "labels": ["feature", "work-item"]})
            features[row["id"]] = issue
            report["operations"].append({"create_feature": row["id"], "issue": issue["number"]})
    expected_members.update({features[row['id']]['node_id']: {'kind': 'issue', 'number': features[row['id']]['number']}
                             for row in rows})
    # Link only scoped feature issues as closing references. Broad parents stay open.
    report["stage"] = "link-feature-prs-and-milestones"
    for row in rows:
        issue = features[row["id"]]
        if not {"feature", "work-item"} <= {label["name"] for label in issue.get("labels", [])}:
            client.api(f'repos/{REPO}/issues/{issue["number"]}/labels', "POST", {"labels": ["feature", "work-item"]})
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
        report["stage"] = "native-hierarchy"
        detail = client.graphql(DETAIL, {"id": child["node_id"]})["node"]
        if not detail["parent"]:
            mutate(client, "addSubIssue", "AddSubIssueInput", {"issueId": parent["node_id"], "subIssueId": child["node_id"]}, "clientMutationId")
            report["operations"].append({"set_parent": child["number"], "parent": parent["number"]})
    project = client.project()
    # Include PR content explicitly: the public aggregate reader deliberately omits it.
    selection = MEMBER_SELECTION
    inventory = complete_membership_read(client, project['id'], expected_members,
        client.connection(project['id'], 'items', selection), report, 'membership_before_mutation_direct')
    items = {i["content"]["id"]: i for i in inventory if i.get("content") and i["content"].get("id")}
    desired = []
    if review_commands:
        issue = client.api(f"repos/{REPO}/issues/27")
        # Record this review once. A later maintainer reopening/status edit is
        # authoritative and must not be undone by replaying the catalog.
        if COMMAND_REVIEW not in (issue.get("body") or ""):
            scope = json.loads((ROOT / "docs/milestone-status.json").read_text())["milestones"][3]["work_items"][0]
            current_body = issue.get("body") or ""
            begin, end = "<!-- ctr2wup:managed:start -->", "<!-- ctr2wup:managed:end -->"
            if current_body.count(begin) != 1 or current_body.count(end) != 1 or current_body.index(begin) >= current_body.index(end):
                raise tracking.TrackingError("Command review cannot verify managed acceptance boundaries; preserve and review.")
            first, last = current_body.index(begin), current_body.index(end)
            section = current_body[first:last]
            for criterion in scope["acceptance_criteria"]:
                unchecked, checked = "- [ ] " + criterion, "- [x] " + criterion
                if section.count(unchecked) + section.count(checked) != 1:
                    raise tracking.TrackingError("Command acceptance criteria changed; preserve and review.")
                section = section.replace(unchecked, checked)
            reviewed_body = current_body[:first] + section + current_body[last:]
            mutate(client, "addCloseIssueReferences", "AddCloseIssueReferencesInput",
                   {"issueId": issue["node_id"], "pullRequestIds": [prs[n]["node_id"] for n in (48, 57)]}, "clientMutationId")
            note = ("\n\n" + COMMAND_REVIEW + "\nReviewed v0.23 bounded command acceptance: PR #48 supplies checked configuration; "
                    "PR #57 connects standard guest PICA draws to owned tiled targets with transactional state/completion. "
                    "Original compiled reference traces, 83,108 normal/sanitizer assertions and exact target/display pixels "
                    "meet this task's two criteria. Native GX2 translation, broader shader/fragment behavior and physical "
                    "M4 acceptance remain open. This review closes the bounded implementation task, not the full milestone.\n")
            client.api(f"repos/{REPO}/issues/27", "PATCH", {"body": reviewed_body + note,
                         "state": "closed", "state_reason": "completed"})
            desired.append((issue["node_id"], "Done", "Task"))
            report["operations"].append({"reviewed_task_completion": 27, "implementation_prs": [48, 57]})
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
    added_nodes = set()
    for node, value, kind in desired:
        report["stage"] = "project-membership-and-status"
        if node not in items:
            result = mutate(client, "addProjectV2ItemById", "AddProjectV2ItemByIdInput", {"projectId": project["id"], "contentId": node}, "item { id }")
            items[node] = {"id": result["item"]["id"], "status": None}
            added_nodes.add(node)
            report['operations'].append({'project_member_added': expected_members[node]})
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
    report["stage"] = "verify-live-relationships"
    for child, parent in edges:
        detail = client.graphql(DETAIL, {"id": child["node_id"]})["node"]
        if not detail["parent"] or detail["parent"]["id"] != parent["node_id"]:
            raise tracking.TrackingError("Native hierarchy did not verify after reconciliation.")
    for row in rows:
        detail = client.graphql(DETAIL, {"id": features[row["id"]]["node_id"]})["node"]
        linked = {p["number"] for p in detail["closedByPullRequestsReferences"]["nodes"]}
        if not set(row["pull_requests"]) <= linked:
            raise tracking.TrackingError("Native PR links did not verify after reconciliation.")
    verify_membership(client, project['id'], expected_members, added_nodes, report)
    for row in rows:
        issue = client.api(f'repos/{REPO}/issues/{features[row["id"]]["number"]}')
        if (issue.get("milestone") or {}).get("number") != row["milestone"]:
            raise tracking.TrackingError("Feature milestone assignment did not verify.")
        for number in row["pull_requests"]:
            pr = client.api(f"repos/{REPO}/issues/{number}")
            if (pr.get("milestone") or {}).get("number") != row["milestone"]:
                raise tracking.TrackingError("PR milestone assignment did not verify.")
    report["verified"] = {"native_parent_links": len(edges), "native_pr_links": len(prs),
                          "project_members": len(expected_members), "untracked_pull_requests": 0,
                          "feature_issues": {r["id"]: features[r["id"]]["number"] for r in rows}}
    report["stage"] = "verified"


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
