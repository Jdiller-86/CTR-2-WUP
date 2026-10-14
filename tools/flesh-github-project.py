#!/usr/bin/env python3
"""Enrich the selected existing Project; preserve all owner notes and statuses.

Dry-run by default. This never creates a Project, changes visibility, closes
issues or accepts a gate. Base issue population uses setup-github-project.py.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("project_setup", ROOT / "tools/setup-github-project.py")
setup = importlib.util.module_from_spec(spec)
spec.loader.exec_module(setup)
BEGIN = "<!-- ctr2wup:project-overview:begin -->"
END = "<!-- ctr2wup:project-overview:end -->"
FIELDS = {
    "Work type": [("Milestone", "PURPLE"), ("Task", "BLUE")],
    "Priority": [("P0 · Critical", "RED"), ("P1 · High", "ORANGE"),
                 ("P2 · Normal", "BLUE"), ("P3 · Low", "GRAY")],
}
VIEWS = {
    "Delivery gates": "label:milestone",
    "Implementation work": "label:work-item -status:Done,Closed",
    "Evidence handoff": 'status:"Awaiting evidence",Blocked',
    "Pull requests": "is:pr",
}


def overview(previous: str, content: str) -> str:
    """Only replace the marked section; retain unrelated private notes verbatim."""
    generated = BEGIN + "\n" + content.rstrip() + "\n" + END
    if BEGIN in previous or END in previous:
        if (previous.count(BEGIN) != 1 or previous.count(END) != 1
                or previous.index(BEGIN) >= previous.index(END)):
            raise setup.SetupError("Project overview boundaries changed; preserve and review")
        merged = previous[:previous.index(BEGIN)] + generated + previous[previous.index(END) + len(END):]
    else:
        merged = previous + ("\n\n" if previous else "") + generated
    if len(merged) > 60000:
        raise setup.SetupError("Project README exceeds the bounded metadata budget")
    return merged


def additions(field, choices):
    prior = [dict(row) for row in field.get("options", [])] if field else []
    names = {row["name"] for row in prior}
    return prior + [{"name": name, "color": color, "description": ""}
                    for name, color in choices if name not in names]


def enrich(client, context, report, content, apply=False):
    project = context["project"]
    if not project:
        raise setup.SetupError("The owner-selected existing Project must be accessible; no creation fallback")
    fields, items, views = setup.validate_project(project)
    # Validate the complete enrichment before starting any mutation.
    for name in FIELDS:
        if name in fields and fields[name]["dataType"] != "SINGLE_SELECT":
            raise setup.SetupError("Existing " + name + " field has a different type; preserve and review")
    new_readme = overview(project.get("readme") or "", content)
    operations = []
    if new_readme != (project.get("readme") or ""):
        operations.append({"kind": "update-managed-project-overview"})
    for name, choices in FIELDS.items():
        field = fields.get(name)
        if not field or len(additions(field, choices)) != len(field["options"]):
            operations.append({"kind": "ensure-select-field", "name": name})
    for name, query in VIEWS.items():
        if name not in views:
            operations.append({"kind": "create-filtered-view", "name": name, "filter": query})
    report["enrichment_operations"] = operations
    if not apply:
        return
    # Base population and enrichment both retain nonempty manual values.
    setup.apply(client, context, report)
    live = client.snapshot(project["id"])
    fields, items, views = setup.validate_project(live)
    refreshed = overview(live.get("readme") or "", content)
    if refreshed != (live.get("readme") or ""):
        client.mutate("updateProjectV2", "UpdateProjectV2Input",
                      {"projectId": project["id"], "readme": refreshed}, "projectV2 { id }")
        report["completed_operations"].append({"kind": "update-managed-project-overview"})
    for name, choices in FIELDS.items():
        field = fields.get(name)
        options = additions(field, choices)
        if not field:
            client.mutate("createProjectV2Field", "CreateProjectV2FieldInput",
                          {"projectId": project["id"], "name": name, "dataType": "SINGLE_SELECT",
                           "singleSelectOptions": options},
                          "projectV2Field { ... on ProjectV2SingleSelectField { id } }")
        elif len(options) != len(field["options"]):
            client.mutate("updateProjectV2Field", "UpdateProjectV2FieldInput",
                          {"fieldId": field["id"], "singleSelectOptions": options},
                          "projectV2Field { ... on ProjectV2SingleSelectField { id } }")
        else:
            continue
        report["completed_operations"].append({"kind": "ensure-select-field", "name": name})
    live = client.snapshot(project["id"])
    fields, items, views = setup.validate_project(live)
    visible = [fields[name]["id"] for name in
               ("Title", "Status", "Milestone", "Work type", "Priority", "Waiting resource") if name in fields]
    for name, query in VIEWS.items():
        if name in views:  # Existing filters/layout/fields belong to the owner.
            continue
        created = client.mutate("createProjectV2View", "CreateProjectV2ViewInput",
                                {"projectId": project["id"], "name": name, "layout": "TABLE_LAYOUT",
                                 "configuration": {"visibleFieldIds": visible}}, "projectV2View { id }")
        report["completed_operations"].append({"kind": "create-view", "name": name})
        client.mutate("updateProjectV2View", "UpdateProjectV2ViewInput",
                      {"viewId": created["projectV2View"]["id"], "filter": query}, "projectV2View { id }")
        report["completed_operations"].append({"kind": "filter-new-view", "name": name})
    for tracker in setup.managed_items(context):
        # Re-read before each item so newly entered owner values are retained.
        live = client.snapshot(project["id"])
        fields, items, _ = setup.validate_project(live)
        item = items[tracker["issue"]["node_id"]]
        for name, label in (("Work type", "Milestone" if tracker["planned"]["kind"] == "milestone" else "Task"),
                            ("Priority", "P2 · Normal")):
            # Extra fields are read by a separate item query to avoid treating an
            # omitted selection as an empty value.
            query = '''query($id:ID!,$name:String!) { node(id:$id) { ... on ProjectV2Item {
              fieldValueByName(name:$name) { ... on ProjectV2ItemFieldSingleSelectValue { optionId } }
            } } }'''
            node = client.graphql(query, {"id": item["id"], "name": name}).get("node")
            if not node or "fieldValueByName" not in node:
                raise setup.SetupError("Cannot verify current extra field value; preserve and review")
            if node["fieldValueByName"]:
                continue
            option = next(row["id"] for row in fields[name]["options"] if row["name"] == label)
            client.mutate("updateProjectV2ItemFieldValue", "UpdateProjectV2ItemFieldValueInput",
                          {"projectId": project["id"], "itemId": item["id"], "fieldId": fields[name]["id"],
                           "value": {"singleSelectOptionId": option}}, "projectV2Item { id }")
            report["completed_operations"].append({"kind": "initialize-empty-extra-field",
                                                   "item": tracker["planned"]["id"], "field": name})
    live = client.snapshot(project["id"])
    if overview(live.get("readme") or "", content) != (live.get("readme") or ""):
        raise setup.SetupError("Managed Project overview did not verify after update; preserve and review")
    report["managed_overview_verified"] = True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apply", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    report, client = {"ok": False, "applied": False, "completed_operations": []}, setup.GitHub()
    try:
        settings = json.loads((ROOT / ".github/repository-settings.json").read_text())
        if not settings["project_requested"].get("existing_url"):
            raise setup.SetupError("An explicit existing Project URL is required")
        state = json.loads((ROOT / "docs/milestone-status.json").read_text())
        report = setup.base_report(settings, state)
        context = setup.preflight(client, settings, state, report)
        setup.plan(context, report)
        enrich(client, context, report, (ROOT / "docs/github-project-overview.md").read_text(), args.apply)
        report.update(ok=True, applied=args.apply)
    except (setup.SetupError, ValueError, KeyError, OSError, StopIteration) as error:
        report.update(ok=False, error=str(error), partial_changes=bool(report["completed_operations"]),
                      changes_possible=client.write_attempted)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 0 if report["ok"] else 2


if __name__ == "__main__":
    sys.exit(main())
