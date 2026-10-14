#!/usr/bin/env python3
"""Apply an explicitly versioned Project review once, preserving later owner edits.

Dry-run by default. A receipt in the overview issue records the reviewed plan
only after all fields read back correctly. Interrupted reviews can be retried;
completed reviews never reapply their historical status to a changed board.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('review_tracking', ROOT / 'tools/export-github-project.py')
tracking = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(tracking)
ITEM = '''query($id:ID!) { node(id:$id) { ... on ProjectV2Item {
  id isArchived content { __typename ... on Issue { id number body repository { nameWithOwner } } }
  status:fieldValueByName(name:"Status") { __typename ... on ProjectV2ItemFieldSingleSelectValue { optionId name } }
  waiting:fieldValueByName(name:"Waiting resource") { __typename ... on ProjectV2ItemFieldTextValue { text } }
} } }'''
UPDATE = '''mutation($input:UpdateProjectV2ItemFieldValueInput!) {
  updateProjectV2ItemFieldValue(input:$input) { projectV2Item { id } }
}'''
CLEAR = '''mutation($input:ClearProjectV2ItemFieldValueInput!) {
  clearProjectV2ItemFieldValue(input:$input) { projectV2Item { id } }
}'''
ACTIVE = ('Planned', 'In progress', 'Awaiting evidence', 'Blocked', 'Ready for review')


def validate(data, source, identifier):
    if (not isinstance(data, dict) or set(data) != {'schema_version', 'repository', 'project_url', 'reviews'}
            or data['schema_version'] != 1 or data['repository'] != tracking.REPOSITORY
            or data['project_url'] != tracking.PROJECT_URL or not isinstance(data['reviews'], list)
            or not 1 <= len(data['reviews']) <= 20):
        raise tracking.TrackingError('Project review catalog identity or budget changed.')
    _, identities = tracking.catalog(source)
    seen = set()
    for review in data['reviews']:
        if (not isinstance(review, dict) or set(review) != {'id', 'overview', 'reason', 'changes'}
                or not isinstance(review['id'], str) or not re.fullmatch(r'[a-z0-9]+(?:-[a-z0-9]+)*', review['id'])
                or review['id'] in seen or review['overview'] not in [f'm{i}' for i in range(1, 8)]
                or not isinstance(review['reason'], str) or not 1 <= len(review['reason']) <= 2000
                or not isinstance(review['changes'], list) or not 1 <= len(review['changes']) <= 6):
            raise tracking.TrackingError('Project review scope is malformed or repeated.')
        seen.add(review['id'])
        targets = set()
        for change in review['changes']:
            if (not isinstance(change, dict) or set(change) != {'role', 'id', 'from_status', 'to_status', 'remove_resources'}
                    or not isinstance(change['role'], str) or not isinstance(change['id'], str)):
                raise tracking.TrackingError('Project review target schema changed.')
            key = (change['role'], change['id'])
            if key not in identities or identities[key] != review['overview'] or key in targets:
                raise tracking.TrackingError('Project review contains an unknown, unrelated or duplicate target.')
            targets.add(key)
            if (not isinstance(change['from_status'], list) or not change['from_status']
                    or any(not isinstance(status, str) for status in change['from_status'])
                    or len(set(change['from_status'])) != len(change['from_status'])
                    or any(status not in ACTIVE for status in change['from_status'])
                    or change['to_status'] not in (*ACTIVE, None)
                    or change['to_status'] is not None and change['to_status'] not in change['from_status']
                    or not isinstance(change['remove_resources'], list)
                    or any(not isinstance(resource, str) for resource in change['remove_resources'])
                    or len(set(change['remove_resources'])) != len(change['remove_resources'])
                    or any(resource not in tracking.RESOURCE_NAMES for resource in change['remove_resources'])):
                raise tracking.TrackingError('Review cannot complete/accept gates or remove unrecognized resource notes.')
    matches = [review for review in data['reviews'] if review['id'] == identifier]
    if len(matches) != 1:
        raise tracking.TrackingError('Select one known versioned Project review.')
    return matches[0]


def waiting_text(row):
    value = row.get('waiting')
    if value is None:
        return None
    if not isinstance(value, dict) or value.get('__typename') != 'ProjectV2ItemFieldTextValue' or not isinstance(value.get('text'), str):
        raise tracking.TrackingError('Waiting resource is not the reviewed text field type.')
    return value['text']


def remove_resources(text, names):
    if text is None:
        return None
    # Preserve surviving free-form notes exactly, including whitespace.
    parts = text.split(';')
    kept = [part for part in parts if part.strip() not in names]
    if len(kept) == len(parts):
        return text
    result = ';'.join(kept)
    return result if result.strip() else None


def snapshot(client, row, expected_status, expected_waiting):
    fresh = client.graphql(ITEM, {'id': row['id']}).get('node')
    if (not isinstance(fresh, dict) or fresh.get('id') != row['id'] or fresh.get('isArchived') is not False
            or fresh.get('content') != row.get('content') or fresh.get('status') != expected_status
            or waiting_text(fresh) != expected_waiting):
        raise tracking.TrackingError('Project target changed during review; preserve owner edits and inspect the partial receipt.')
    return fresh


def comments(client, number):
    result = []
    for page in range(1, 11):
        batch = client.api(f'repos/{tracking.REPOSITORY}/issues/{number}/comments?per_page=100&page={page}')
        if not isinstance(batch, list) or len(batch) > 100 or any(not isinstance(row, dict) or not isinstance(row.get('body'), str) for row in batch):
            raise tracking.TrackingError('Project review comments could not be verified.')
        result.extend(batch)
        if len(batch) < 100:
            return result
    raise tracking.TrackingError('Project review comments exceed the bounded review budget.')


def review(client, source, data, identifier, apply=False, report=None):
    report = report if report is not None else {}
    report.update(ok=False, applied=apply, review=identifier, writes_attempted=False,
                  operations_completed=[], full_gate_accepted=False)
    plan = validate(data, source, identifier)
    digest = hashlib.sha256(json.dumps(plan, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
    marker = '<!-- ctr2wup:project-review:' + identifier + ' -->'
    project = client.project()
    _, managed = tracking.collect(project, source)
    overview = managed[('milestone', plan['overview'])]
    recorded = [row for row in comments(client, overview['number']) if marker in row['body']]
    if len(recorded) > 1 or (recorded and ('Plan SHA-256: ' + digest) not in recorded[0]['body']):
        raise tracking.TrackingError('Project review receipt is ambiguous or its immutable plan changed.')
    if recorded:
        report.update(ok=True, already_reviewed=True, owner_edits_preserved=True)
        return report
    fields = {field['name']: field for field in project['fields']}
    options = {option['id']: option['name'] for option in fields['Status']['options']}
    rows = {row['id']: row for row in project['items']}
    actions = []
    # Complete every identity, option, status and raw-resource preflight first.
    for change in plan['changes']:
        target = managed[(change['role'], change['id'])]
        row = rows[target['item_id']]
        if target['status'] not in change['from_status']:
            raise tracking.TrackingError('Reviewed target has a conflicting status; preserve it for an explicit new review.')
        desired_status = row['status']
        if change['to_status'] is not None:
            choices = [key for key, name in options.items() if name.casefold() == change['to_status'].casefold()]
            if len(choices) != 1:
                raise tracking.TrackingError('Reviewed Status option is not present uniquely.')
            desired_status = {'__typename': 'ProjectV2ItemFieldSingleSelectValue', 'optionId': choices[0], 'name': options[choices[0]]}
        old_waiting = waiting_text(row)
        desired_waiting = remove_resources(old_waiting, change['remove_resources'])
        actions.append((change, row, desired_status, old_waiting, desired_waiting))
    report['targets'] = [change['id'] for change, *_ in actions]
    report['already_reviewed'] = False
    if not apply:
        report.update(ok=True, changes_planned=sum(row['status'] != status or old != new for _, row, status, old, new in actions))
        return report
    # Reject a stale batch before its first mutation, then recheck each field.
    for _, row, _, old, _ in actions:
        snapshot(client, row, row['status'], old)
    for change, row, desired_status, old, new in actions:
        snapshot(client, row, row['status'], old)
        if row['status'] != desired_status:
            report['writes_attempted'] = True
            client.graphql(UPDATE, {'input': {'projectId': project['id'], 'itemId': row['id'],
                'fieldId': fields['Status']['id'], 'value': {'singleSelectOptionId': desired_status['optionId']}}})
            report['operations_completed'].append({'target': change['id'], 'field': 'Status'})
        snapshot(client, row, desired_status, old)
        if old != new:
            payload = {'projectId': project['id'], 'itemId': row['id'], 'fieldId': fields['Waiting resource']['id']}
            if new is not None:
                payload['value'] = {'text': new}
            report['writes_attempted'] = True
            client.graphql(UPDATE if new is not None else CLEAR, {'input': payload})
            report['operations_completed'].append({'target': change['id'], 'field': 'Waiting resource'})
        snapshot(client, row, desired_status, new)
    # All targets must still agree before recording the completed review.
    for _, row, status, _, new in actions:
        snapshot(client, row, status, new)
    if any(marker in row['body'] for row in comments(client, overview['number'])):
        raise tracking.TrackingError('Another review recorded this plan concurrently; inspect the board before retrying.')
    report['writes_attempted'] = True
    receipt = client.api(f'repos/{tracking.REPOSITORY}/issues/{overview["number"]}/comments', 'POST', {'body':
        marker + '\nPlan SHA-256: ' + digest + '\n\n' + plan['reason'] + '\n\n'
        'Reviewed targets: ' + ', '.join(report['targets']) + '. No full milestone accepted. '
        'This completed review is not replayed over subsequent owner edits.'})
    if not isinstance(receipt, dict) or marker not in receipt.get('body', ''):
        raise tracking.TrackingError('Fields read back, but the completed review receipt was not returned; inspect before retrying.')
    recorded = [row for row in comments(client, overview['number']) if marker in row['body']]
    if len(recorded) != 1 or 'Plan SHA-256: ' + digest not in recorded[0]['body']:
        raise tracking.TrackingError('Completed review receipt did not verify; inspect before retrying.')
    report.update(ok=True, review_recorded=True)
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--review', required=True)
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(argv)
    report = {}
    try:
        review(tracking.GitHub(), json.loads((ROOT / 'docs/milestone-status.json').read_text()),
               json.loads((ROOT / 'docs/project-reviews.json').read_text()), args.review, args.apply, report)
    except (tracking.TrackingError, OSError, ValueError, KeyError, TypeError) as error:
        report.update(ok=False, error=str(error) if isinstance(error, tracking.TrackingError) else 'Project review inputs could not be validated.')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report))
    return 0 if report.get('ok') else 2


if __name__ == '__main__':
    raise SystemExit(main())
