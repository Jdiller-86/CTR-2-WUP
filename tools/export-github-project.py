#!/usr/bin/env python3
"""Read the selected private Project; export public aggregates, optionally mirror labels.

Status, Waiting resource and full-gate Accepted values come only from Project #2.
Source JSON supplies stable managed identities and already-public milestone names.
No private titles, bodies, issue numbers, item IDs or resource notes are exported.
Unknown/unassigned values are visible; missing/ambiguous managed items fail closed.
GitHub credentials are consumed by gh from its environment, never by arguments.
"""
from __future__ import annotations

import argparse
from collections import Counter
from datetime import datetime, timezone
import json
from html import escape
from pathlib import Path
import re
import subprocess
from urllib.parse import quote

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = 'Jdiller-86/CTR-2-WUP'
PROJECT_URL = 'https://github.com/users/Jdiller-86/projects/2'
STATUSES = ('Planned', 'In progress', 'Awaiting evidence', 'Blocked',
            'Ready for review', 'Done', 'Closed', 'Accepted', 'Unassigned', 'Other')
STATUS_LABELS = dict(zip(STATUSES[:8], (
    'status:planned', 'status:in-progress', 'status:awaiting-evidence', 'status:blocked',
    'status:ready-for-review', 'status:done', 'status:closed', 'status:accepted')))
RESOURCE_NAMES = {
    'Wii U console results': 'Console testing',
    'Owned decrypted executable and assets': 'Owned game input',
    'Native Windows conversion validation': 'Windows testing',
}
PUBLIC_RESOURCES = ('Console testing', 'Owned game input', 'Windows testing', 'Other resource')
RESOURCE_LABELS = {'Console testing': 'needs-console', 'Owned game input': 'needs-input',
                   'Windows testing': 'needs-windows'}
MARKER = re.compile(r'<!-- ctr2wup:(milestone|work-item):([a-z0-9-]+) -->')
PARENT = re.compile(r'<!-- ctr2wup:parent-milestone:(m[1-7]) -->')

PROJECT_QUERY = '''query($login:String!,$number:Int!) {
  repositoryOwner(login:$login) { login
    ... on User { projectV2(number:$number) { id number url closed owner { ... on User { login } } } }
  }
}'''
FIELDS = '''nodes { __typename
  ... on ProjectV2Field { id name dataType }
  ... on ProjectV2SingleSelectField { id name dataType options { id name } }
} pageInfo { hasNextPage endCursor }'''
ITEMS = '''nodes { id isArchived
  content { __typename ... on Issue { id number body repository { nameWithOwner } } }
  status:fieldValueByName(name:"Status") {
    __typename ... on ProjectV2ItemFieldSingleSelectValue { optionId name }
  }
  waiting:fieldValueByName(name:"Waiting resource") {
    __typename ... on ProjectV2ItemFieldTextValue { text }
  }
} pageInfo { hasNextPage endCursor }'''


class TrackingError(RuntimeError):
    """An error safe to put in a public status snapshot."""


class GitHub:
    def __init__(self, env=None):
        self.env = env

    def api(self, path, method='GET', payload=None):
        args = ['gh', 'api', path, '--method', method]
        if payload is not None:
            args += ['--input', '-']
        try:
            result = subprocess.run(args, input=json.dumps(payload) if payload is not None else None,
                                    capture_output=True, text=True, encoding='utf-8', timeout=60,
                                    env=getattr(self, 'env', None))
        except (OSError, subprocess.TimeoutExpired) as error:
            raise TrackingError('GitHub request could not complete; review workflow access.') from error
        if result.returncode:
            # stderr can contain private URLs, identities or request diagnostics.
            raise TrackingError('GitHub access/request failed; review the configured credential permissions.')
        try:
            return json.loads(result.stdout) if result.stdout.strip() else None
        except ValueError as error:
            raise TrackingError('GitHub returned invalid JSON.') from error

    def graphql(self, query, variables):
        response = self.api('graphql', 'POST', {'query': query, 'variables': variables})
        if not isinstance(response, dict) or response.get('errors') or not isinstance(response.get('data'), dict):
            raise TrackingError('Project read failed; no current tracking state was published.')
        return response['data']

    def connection(self, project_id, name, selection):
        query = ('query($id:ID!,$after:String) { node(id:$id) { ... on ProjectV2 { '
                 + name + '(first:100,after:$after) { ' + selection + ' } } } }')
        rows, after, cursors, ids = [], None, set(), set()
        for _ in range(10):
            node = self.graphql(query, {'id': project_id, 'after': after}).get('node')
            connection = node.get(name) if isinstance(node, dict) else None
            if not isinstance(connection, dict) or not isinstance(connection.get('nodes'), list):
                raise TrackingError('Project pagination returned an invalid connection.')
            batch = connection['nodes']
            if len(batch) > 100 or any(not isinstance(row, dict) or not isinstance(row.get('id'), str) for row in batch):
                raise TrackingError('Project pagination exceeded its page budget or returned an invalid item.')
            for row in batch:
                if row['id'] in ids:
                    raise TrackingError('Project pagination repeated an item.')
                ids.add(row['id'])
            rows.extend(batch)
            page = connection.get('pageInfo')
            if not isinstance(page, dict) or type(page.get('hasNextPage')) is not bool:
                raise TrackingError('Project pagination returned invalid page information.')
            if not page['hasNextPage']:
                return rows
            after = page.get('endCursor')
            if not isinstance(after, str) or not after or after in cursors:
                raise TrackingError('Project pagination stalled or repeated a cursor.')
            cursors.add(after)
        raise TrackingError('Project pagination exceeds the 1,000-item review budget.')

    def project(self):
        owner = self.graphql(PROJECT_QUERY, {'login': 'Jdiller-86', 'number': 2}).get('repositoryOwner')
        project = owner.get('projectV2') if isinstance(owner, dict) else None
        if (not isinstance(project, dict) or project.get('url') != PROJECT_URL
                or project.get('number') != 2 or owner.get('login', '').casefold() != 'jdiller-86'
                or project.get('owner', {}).get('login', '').casefold() != 'jdiller-86'
                or project.get('closed') is not False or not isinstance(project.get('id'), str)):
            raise TrackingError('The selected Project #2 identity/read access could not be verified.')
        project['fields'] = self.connection(project['id'], 'fields', FIELDS)
        project['items'] = self.connection(project['id'], 'items', ITEMS)
        return project


def catalog(source):
    """Requirements identity, never historical source status, is used here."""
    milestones = source.get('milestones')
    if not isinstance(milestones, list) or [m.get('id') for m in milestones] != [f'm{i}' for i in range(1, 8)]:
        raise TrackingError('Milestone identity schema changed; review the public export policy.')
    identities, titles = {}, set()
    for milestone in milestones:
        title = milestone.get('title')
        if not isinstance(title, str) or not title.startswith(milestone['id'].upper() + ': ') or len(title) > 120 or title in titles:
            raise TrackingError('Public milestone names are invalid or ambiguous.')
        titles.add(title)
        identities[('milestone', milestone['id'])] = milestone['id']
        items = milestone.get('work_items')
        if not isinstance(items, list) or len(items) != 5:
            raise TrackingError('The 35-task identity schema changed; review tracking migration first.')
        for item in items:
            identifier = item.get('id')
            key = ('work-item', identifier)
            if (not isinstance(identifier, str) or not re.fullmatch(milestone['id'] + r'-[a-z0-9]+(?:-[a-z0-9]+)*', identifier)
                    or key in identities):
                raise TrackingError('Managed task identities are invalid or ambiguous.')
            identities[key] = milestone['id']
    return milestones, identities


def status_value(row, options):
    value = row.get('status')
    if value is None:
        return 'Unassigned'
    if not isinstance(value, dict) or value.get('__typename') != 'ProjectV2ItemFieldSingleSelectValue':
        raise TrackingError('Project Status values use an unsupported field type.')
    name = options.get(value.get('optionId'))
    if name is None or value.get('name') != name:
        raise TrackingError('Project Status option identity changed during the read.')
    if name.casefold() == 'todo':
        return 'Planned'  # GitHub's default option; preserve its name and ID.
    return next((canonical for canonical in STATUSES[:8] if canonical.casefold() == name.casefold()), 'Other')


def resources(row):
    value = row.get('waiting')
    if value is None:
        return []
    if not isinstance(value, dict) or value.get('__typename') != 'ProjectV2ItemFieldTextValue' or not isinstance(value.get('text'), str):
        raise TrackingError('Project Waiting resource values use an unsupported field type.')
    text = value['text'].strip()
    if not text:
        return []
    # Never publish a user's free-form resource notes, even partially.
    return sorted({RESOURCE_NAMES.get(part.strip(), 'Other resource') for part in text.split(';')})


def counts(rows):
    counter = Counter(row['status'] for row in rows)
    return {name: counter[name] for name in STATUSES}


def collect(project, source):
    milestones, expected = catalog(source)
    fields = project.get('fields', [])
    selected = {}
    for name, kind in [('Status', 'ProjectV2SingleSelectField'), ('Waiting resource', 'ProjectV2Field')]:
        matches = [field for field in fields if field.get('name') == name]
        if len(matches) != 1 or matches[0].get('__typename') != kind:
            raise TrackingError('Project tracking fields are missing, duplicate or use unsupported types.')
        selected[name] = matches[0]
    if selected['Status'].get('dataType') != 'SINGLE_SELECT' or selected['Waiting resource'].get('dataType') != 'TEXT':
        raise TrackingError('Project tracking field data types changed.')
    options = {}
    for option in selected['Status'].get('options', []):
        if not isinstance(option.get('id'), str) or not isinstance(option.get('name'), str) or option['id'] in options:
            raise TrackingError('Project Status option definitions are ambiguous.')
        options[option['id']] = option['name']
    if len({name.casefold() for name in options.values()}) != len(options):
        raise TrackingError('Project Status option names are ambiguous.')
    managed, item_ids, issue_ids, issue_numbers = {}, set(), set(), set()
    for row in project.get('items', []):
        content = row.get('content') or {}
        if content.get('__typename') != 'Issue' or content.get('repository', {}).get('nameWithOwner') != REPOSITORY:
            continue  # Other repositories, drafts and PRs are not exported or changed.
        body = content.get('body') or ''
        if not isinstance(body, str):
            raise TrackingError('Managed issue identity cannot be read.')
        found = MARKER.findall(body)
        mentions = body.count('<!-- ctr2wup:milestone:') + body.count('<!-- ctr2wup:work-item:')
        if len(found) != mentions or len(found) > 1:
            raise TrackingError('Managed issue identity markers are malformed or ambiguous.')
        if not found:
            continue
        key = found[0]
        if key not in expected:
            continue  # Retired/unmanaged issues stay private and unchanged.
        if key in managed:
            raise TrackingError('Project contains duplicate managed identities.')
        if key[0] == 'work-item' and (PARENT.findall(body) != [expected[key]] or body.count('<!-- ctr2wup:parent-milestone:') != 1):
            raise TrackingError('Managed task parent identity is ambiguous.')
        if (type(content.get('number')) is not int or content['number'] < 1 or not isinstance(content.get('id'), str)
                or not isinstance(row.get('id'), str)):
            raise TrackingError('Managed issue or Project item identity is invalid.')
        if row.get('isArchived') is not False:
            raise TrackingError('A managed Project item is archived; restore it before exporting current totals.')
        if row['id'] in item_ids or content['id'] in issue_ids or content['number'] in issue_numbers:
            raise TrackingError('Managed Project item or issue content identities are duplicated.')
        item_ids.add(row['id'])
        issue_ids.add(content['id'])
        issue_numbers.add(content['number'])
        status = status_value(row, options)
        if key[0] == 'work-item' and status == 'Accepted':
            raise TrackingError('Accepted is reserved for full milestone overviews; use Done for completed work items.')
        managed[key] = {'item_id': row['id'], 'issue_id': content['id'], 'number': content['number'],
                        'role': key[0], 'parent': expected[key], 'status': status,
                        'raw_status': row.get('status'),
                        'resources': resources(row)}
    if set(managed) != set(expected):
        raise TrackingError('Project does not contain all 7 overview and 35 managed work items; current totals are unavailable.')
    return milestones, managed


def aggregate(milestones, managed, now):
    work = [row for row in managed.values() if row['role'] == 'work-item']
    accepted = sum(managed[('milestone', milestone['id'])]['status'] == 'Accepted' for milestone in milestones)
    result = {
        'schema_version': 1, 'state': 'available', 'project_url': PROJECT_URL,
        'generated_at': now, 'last_success_at': now, 'stale_after_seconds': 7200,
        'full_gates': {'accepted': accepted, 'total': 7},
        'work_items': {'total': len(work), 'status_counts': counts(work)},
        'milestones': [],
    }
    for position, milestone in enumerate(milestones, 1):
        overview = managed[('milestone', milestone['id'])]
        tasks = [row for row in work if row['parent'] == milestone['id']]
        waiting = Counter(resource for row in tasks for resource in row['resources'])
        result['milestones'].append({
            'position': position, 'title': milestone['title'], 'status': overview['status'],
            'accepted': overview['status'] == 'Accepted', 'work_total': len(tasks),
            'status_counts': counts(tasks), 'waiting_resources': {name: waiting[name] for name in PUBLIC_RESOURCES},
        })
    return result


def review_result_collection(client, source, apply=False):
    """Explicit one-task evidence review; never run by scheduled source sync."""
    evidence = source.get('console_result_collection_review', {})
    if evidence.get('reviewed') is not True or evidence.get('task_id') != 'm1-results':
        raise TrackingError('No reviewed physical result-collection record is present.')
    project = client.project()
    _, managed = collect(project, source)
    row = managed[('work-item', 'm1-results')]
    if row['status'] == 'Done' and not row['resources']:
        return {'ok': True, 'changed': False, 'full_gate_accepted': False}
    if row['status'] not in ('Planned', 'In progress', 'Awaiting evidence', 'Ready for review', 'Done') or any(resource != 'Console testing' for resource in row['resources']):
        raise TrackingError('Preserve a blocked/custom result-collection status or resource for review.')
    fields = {field['name']: field for field in project['fields']}
    choices = [option['id'] for option in fields['Status']['options'] if option['name'] == 'Done']
    if len(choices) != 1:
        raise TrackingError('The Done option cannot be identified uniquely.')
    if not apply:
        return {'ok': True, 'changed': False, 'would_set': 'Done', 'full_gate_accepted': False}
    query = '''query($id:ID!) { node(id:$id) { ... on ProjectV2Item {
      isArchived content { ... on Issue { id } }
      status:fieldValueByName(name:"Status") { __typename ... on ProjectV2ItemFieldSingleSelectValue { optionId name } }
      waiting:fieldValueByName(name:"Waiting resource") { __typename ... on ProjectV2ItemFieldTextValue { text } }
    } } }'''
    fresh = client.graphql(query, {'id': row['item_id']}).get('node')
    if (not fresh or fresh.get('isArchived') is not False or fresh.get('content', {}).get('id') != row['issue_id']
            or fresh.get('status') != row['raw_status'] or resources(fresh) != row['resources']):
        raise TrackingError('Result-collection item changed during review; no update applied.')
    mutation = '''mutation($input:UpdateProjectV2ItemFieldValueInput!) {
      updateProjectV2ItemFieldValue(input:$input) { projectV2Item { id } }
    }'''
    if row['status'] != 'Done':
        client.graphql(mutation, {'input': {'projectId': project['id'], 'itemId': row['item_id'],
                        'fieldId': fields['Status']['id'], 'value': {'singleSelectOptionId': choices[0]}}})
    if row['resources']:
        current = client.graphql(query, {'id': row['item_id']}).get('node')
        if (not current or current.get('isArchived') is not False
                or current.get('content', {}).get('id') != row['issue_id']
                or (current.get('status') or {}).get('optionId') != choices[0]
                or resources(current) != row['resources']):
            raise TrackingError('Result status was updated, but its resource changed; preserve it for review.')
        mutation = '''mutation($input:ClearProjectV2ItemFieldValueInput!) {
          clearProjectV2ItemFieldValue(input:$input) { projectV2Item { id } }
        }'''
        client.graphql(mutation, {'input': {'projectId': project['id'], 'itemId': row['item_id'],
                        'fieldId': fields['Waiting resource']['id']}})
    final = client.graphql(query, {'id': row['item_id']}).get('node')
    if not final or (final.get('status') or {}).get('optionId') != choices[0] or resources(final):
        raise TrackingError('Result-collection review was not readable after the update; verify its current board values.')
    return {'ok': True, 'changed': True, 'task': 'm1-results', 'full_gate_accepted': False}


def unavailable(now):
    # A failed request must never silently display invented zero-progress totals.
    return {'schema_version': 1, 'state': 'unavailable', 'project_url': PROJECT_URL,
            'generated_at': now, 'last_success_at': None, 'stale_after_seconds': 7200,
            'message': 'Live Project tracking is unavailable. Review the workflow access and open Project #2.'}


def mirror_labels(client, managed, apply=False, report=None):
    """Mirror owned status/resource labels; preserve every other label and all state."""
    result = report if report is not None else {}
    result.update(applied=apply, changed_issues=0, unmapped_items=0,
                  unmapped_status_items=0, unmapped_resource_items=0, operations_completed=0,
                  writes_attempted=False)
    actions = []
    known = {label.casefold() for label in STATUS_LABELS.values()}
    for row in managed.values():
        desired = STATUS_LABELS.get(row['status'])
        unknown_resource = 'Other resource' in row['resources']
        if desired is None or unknown_resource:
            result['unmapped_items'] += 1
        if desired is None:
            result['unmapped_status_items'] += 1
        if unknown_resource:
            result['unmapped_resource_items'] += 1
        route = f'repos/{REPOSITORY}/issues/{row["number"]}'
        issue = client.api(route)
        if not isinstance(issue, dict) or issue.get('node_id') != row['issue_id'] or 'pull_request' in issue:
            raise TrackingError('Issue identity changed before status mirroring.')
        labels = issue.get('labels')
        if not isinstance(labels, list) or any(not isinstance(label, dict) or not isinstance(label.get('name'), str) for label in labels):
            raise TrackingError('Issue labels cannot be safely mirrored.')
        names = [label['name'] for label in labels]
        desired_names = [desired] if desired is not None else []
        desired_names += [RESOURCE_LABELS[resource] for resource in row['resources'] if resource in RESOURCE_LABELS]
        desired_folded = {name.casefold() for name in desired_names}
        removes = [name for name in names if ((desired is not None and name.casefold() in known)
                   or name.casefold() in RESOURCE_LABELS.values()) and name.casefold() not in desired_folded]
        adds = [name for name in desired_names if name.casefold() not in {old.casefold() for old in names}]
        if removes or adds:
            actions.append((row, route, removes, adds))
    result['changed_issues'] = len(actions)
    if not apply:
        return result
    # Preflight every issue before the first write. Labels are appended/removed
    # individually so concurrent unrelated manual labels are never replaced.
    for row, route, removes, adds in actions:
        query = '''query($id:ID!) { node(id:$id) { ... on ProjectV2Item {
          content { ... on Issue { id } }
          status:fieldValueByName(name:"Status") {
            __typename ... on ProjectV2ItemFieldSingleSelectValue { optionId name }
          }
          waiting:fieldValueByName(name:"Waiting resource") {
            __typename ... on ProjectV2ItemFieldTextValue { text }
          }
        } } }'''
        fresh = client.graphql(query, {'id': row['item_id']}).get('node')
        if (not fresh or fresh.get('content', {}).get('id') != row['issue_id']
                or fresh.get('status') != row['raw_status'] or resources(fresh) != row['resources']):
            raise TrackingError('Project status/resource changed during mirroring; retry a fresh Project read.')
        if adds:
            result['writes_attempted'] = True
            client.api(route + '/labels', 'POST', {'labels': adds})
            result['operations_completed'] += 1
        for name in removes:
            result['writes_attempted'] = True
            client.api(route + '/labels/' + quote(name, safe=''), 'DELETE')
            result['operations_completed'] += 1
    return result


def gate_svg(public):
    # GitHub Markdown displays this server-generated chart without JavaScript.
    # Every bar comes from the current Project fields, never the source seeds.
    parts = ['<svg xmlns="http://www.w3.org/2000/svg" width="800" height="390" viewBox="0 0 800 390" role="img" aria-labelledby="title desc">',
             '<title id="title">CTR2WUP Project progress</title>',
             '<desc id="desc">Work-item statuses from Project #2. Completed tasks do not accept a full milestone.</desc>',
             '<rect width="800" height="390" rx="8" fill="#0d1117"/>',
             '<g font-family="Verdana,sans-serif" fill="#e6edf3">',
             '<text x="24" y="34" font-size="18">CTR2WUP development</text>']
    if public['state'] != 'available':
        parts += ['<text x="24" y="80" font-size="14">Project tracking unavailable; no current totals published.</text>',
                  '<text x="24" y="112" font-size="12">Open the dashboard to review freshness and access status.</text>']
    else:
        groups = [('Planned', '#8b949e', ('Planned',)), ('Active', '#58a6ff', ('In progress', 'Ready for review')),
                  ('Awaiting evidence', '#d29922', ('Awaiting evidence',)), ('Blocked', '#f85149', ('Blocked',)),
                  ('Done', '#3fb950', ('Done',)), ('Other', '#bc8cff', ('Closed', 'Unassigned', 'Other'))]
        accepted = public['full_gates']['accepted']
        parts.append(f'<text x="24" y="60" font-size="12">{accepted}/7 full milestones accepted · 35 tracked work items</text>')
        for index, (label, color, _) in enumerate(groups):
            x = 24 + index * 126
            parts += [f'<rect x="{x}" y="79" width="10" height="10" fill="{color}"/>',
                      f'<text x="{x+15}" y="89" font-size="10">{label}</text>']
        for index, row in enumerate(public['milestones']):
            y = 126 + index * 30
            label = escape(row['title'])
            parts.append(f'<text x="24" y="{y}" font-size="11">{label}</text>')
            x = 362
            for group, color, statuses in groups:
                count = sum(row['status_counts'][status] for status in statuses)
                if count:
                    width = count * 64
                    parts.append(f'<rect x="{x}" y="{y-14}" width="{width}" height="20" fill="{color}"><title>{label}: {group} {count}/5</title></rect>')
                    x += width
            state = 'Accepted' if row['accepted'] else escape(row['status'])
            parts.append(f'<text x="696" y="{y}" font-size="9">{state}</text>')
        parts.append('<text x="24" y="350" font-size="11">Bars show task status. Milestone acceptance requires the complete reviewed gate.</text>')
    stamp = escape(public['generated_at'].replace('T', ' ').replace('Z', ' UTC'))
    parts += [f'<text x="24" y="374" font-size="10" fill="#8b949e">Project snapshot: {stamp} · Open the dashboard for current freshness.</text>', '</g></svg>\n']
    return ''.join(parts)


def write_site(output, public):
    if output.exists():
        raise TrackingError('Use a fresh output directory for the allowlisted public export.')
    output.mkdir(parents=True)
    (output / 'progress.json').write_text(json.dumps(public, indent=2) + '\n', encoding='utf-8')
    template = ROOT / 'tools/project-progress-dashboard.html'
    (output / 'index.html').write_text(template.read_text(encoding='utf-8'), encoding='utf-8')
    (output / 'gates.svg').write_text(gate_svg(public), encoding='utf-8')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True, help='Fresh directory for public assets only')
    parser.add_argument('--apply', action='store_true', help='Mirror Project status to owned issue labels')
    parser.add_argument('--report', type=Path, help='Private operation receipt (no issue contents)')
    args = parser.parse_args(argv)
    now = datetime.now(timezone.utc).isoformat(timespec='seconds').replace('+00:00', 'Z')
    report = {'ok': False, 'project_url': PROJECT_URL, 'status_source': 'GitHub Project #2',
              'project_writes': False, 'issue_states_changed': False}
    public = unavailable(now)
    try:
        source = json.loads((ROOT / 'docs/milestone-status.json').read_text(encoding='utf-8'))
        client = GitHub()
        milestones, managed = collect(client.project(), source)
        # Do not turn an issue-label mirror failure into invented Project totals.
        public = aggregate(milestones, managed, now)
        report['mirror'] = {}
        mirror_labels(client, managed, args.apply, report['mirror'])
        report['ok'] = True
    except (TrackingError, ValueError, KeyError, OSError) as error:
        report['error'] = str(error) if isinstance(error, TrackingError) else 'Tracking schema/local input could not be validated.'
        report['partial_changes'] = bool(report.get('mirror', {}).get('operations_completed'))
        report['changes_possible'] = bool(report.get('mirror', {}).get('writes_attempted'))
    try:
        write_site(args.output, public)
    except (TrackingError, OSError) as error:
        report['ok'] = False
        report['error'] = str(error) if isinstance(error, TrackingError) else 'Public export could not be written.'
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report))
    return 0 if report['ok'] else 2


if __name__ == '__main__':
    raise SystemExit(main())
