#!/usr/bin/env python3
"""Publish public issue tracking from Project #2; update its known status fields.

Native GitHub issues and the Project are the only tracking authority. Credentials
stay in gh's environment. Arbitrary Project notes, draft items and other/private
repositories never enter the public snapshot or operation receipts.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import subprocess
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = 'Jdiller-86/CTR-2-WUP'
REPO = REPOSITORY
PROJECT_URL = 'https://github.com/users/Jdiller-86/projects/2'
STATUSES = ('Planned', 'In progress', 'Awaiting evidence', 'Blocked',
            'Ready for review', 'Done', 'Closed', 'Accepted')
WAITING = {'Console testing': 'Wii U console results',
           'Owned game input': 'Owned decrypted executable and assets',
           'Windows testing': 'Native Windows conversion validation'}
FIELDS = '''nodes { __typename
  ... on ProjectV2Field { id name dataType }
  ... on ProjectV2SingleSelectField { id name dataType options { id name } }
  ... on ProjectV2IterationField { id name dataType }
} pageInfo { hasNextPage endCursor }'''
ITEMS = '''nodes { id isArchived
  content { __typename ... on Issue { id number title url body state
    repository { nameWithOwner isPrivate }
    milestone { number title url }
    parent { number repository { nameWithOwner } }
    labels(first:100) { nodes { name } pageInfo { hasNextPage } }
    closedByPullRequestsReferences(first:10) { nodes { number url state
      repository { nameWithOwner isPrivate } } }
    blockedBy(first:100) { nodes { number repository { nameWithOwner } }
      pageInfo { hasNextPage } }
  } }
  status:fieldValueByName(name:"Status") {
    __typename ... on ProjectV2ItemFieldSingleSelectValue { optionId name }
  }
  waiting:fieldValueByName(name:"Waiting resource") {
    __typename ... on ProjectV2ItemFieldTextValue { text }
  }
  workType:fieldValueByName(name:"Work type") {
    __typename ... on ProjectV2ItemFieldSingleSelectValue { optionId name }
  }
} pageInfo { hasNextPage endCursor }'''


class TrackingError(RuntimeError):
    """Only fixed, credential-free messages may cross this boundary."""


class GitHub:
    def __init__(self, env=None):
        self.env = env

    def api(self, path, method='GET', payload=None):
        command = ['gh', 'api', path, '--method', method]
        if payload is not None:
            command += ['--input', '-']
        try:
            result = subprocess.run(command,
                input=json.dumps(payload) if payload is not None else None,
                capture_output=True, text=True, encoding='utf-8', timeout=60,
                env=self.env)
        except (OSError, subprocess.TimeoutExpired) as error:
            raise TrackingError('GitHub request could not complete.') from error
        if result.returncode:
            raise TrackingError('GitHub access failed; check the workflow credential permissions.')
        try:
            return json.loads(result.stdout) if result.stdout.strip() else None
        except ValueError as error:
            raise TrackingError('GitHub returned invalid JSON.') from error

    def graphql(self, query, variables):
        response = self.api('graphql', 'POST', {'query': query, 'variables': variables})
        if not isinstance(response, dict) or response.get('errors') or not isinstance(response.get('data'), dict):
            raise TrackingError('GitHub Project operation failed.')
        return response['data']

    def mutate(self, name, input_type, payload):
        return self.graphql('mutation($input:' + input_type + '!) { ' + name +
                            '(input:$input) { clientMutationId } }', {'input': payload})

    def add_item(self, project_id, issue_id):
        data = self.graphql('''mutation($input:AddProjectV2ItemByIdInput!) {
          addProjectV2ItemById(input:$input) { item {
            __typename id isArchived project { id }
            content { __typename ... on Issue { id number url state
              repository { nameWithOwner isPrivate } } }
          } }
        }''', {'input': {'projectId': project_id, 'contentId': issue_id}})
        result = data.get('addProjectV2ItemById')
        return result.get('item') if isinstance(result, dict) else None

    def connection(self, project_id, name, selection):
        archived = ',archivedStates:[ARCHIVED,NOT_ARCHIVED]' if name == 'items' else ''
        query = ('query($id:ID!,$after:String) { node(id:$id) { ... on ProjectV2 { '
                 + name + '(first:100,after:$after' + archived + ') { ' + selection + ' } } } }')
        rows, after, cursors, identities = [], None, set(), set()
        for _ in range(20):
            node = self.graphql(query, {'id': project_id, 'after': after}).get('node')
            page = node.get(name) if isinstance(node, dict) else None
            if not isinstance(page, dict) or not isinstance(page.get('nodes'), list):
                raise TrackingError('Project pagination returned an invalid connection.')
            batch = page['nodes']
            if len(batch) > 100 or any(not isinstance(row, dict) or not isinstance(row.get('id'), str) for row in batch):
                raise TrackingError('Project pagination returned an invalid item.')
            for row in batch:
                if row['id'] in identities:
                    raise TrackingError('Project pagination repeated an item.')
                identities.add(row['id'])
            rows.extend(batch)
            info = page.get('pageInfo')
            if not isinstance(info, dict) or type(info.get('hasNextPage')) is not bool:
                raise TrackingError('Project pagination returned invalid page information.')
            if not info['hasNextPage']:
                return rows
            after = info.get('endCursor')
            if not isinstance(after, str) or not after or after in cursors:
                raise TrackingError('Project pagination stalled.')
            cursors.add(after)
        raise TrackingError('Project exceeds the 2,000-item snapshot budget.')

    def project(self):
        data = self.graphql('''query { user(login:"Jdiller-86") {
          projectV2(number:2) { id number url closed readme shortDescription owner { ... on User { login } } }
        } repository(owner:"Jdiller-86",name:"CTR-2-WUP") { nameWithOwner isPrivate } }''', {})
        repository = data.get('repository') or {}
        project = (data.get('user') or {}).get('projectV2')
        if (repository.get('nameWithOwner') != REPOSITORY or repository.get('isPrivate') is not False
                or not isinstance(project, dict) or project.get('url') != PROJECT_URL
                or project.get('number') != 2 or project.get('closed') is not False
                or project.get('owner', {}).get('login', '').casefold() != 'jdiller-86'
                or not isinstance(project.get('id'), str)):
            raise TrackingError('Public repository and Project identity could not be verified.')
        project['fields'] = self.connection(project['id'], 'fields', FIELDS)
        project['items'] = self.connection(project['id'], 'items', ITEMS)
        return project


def fields(project):
    selected = {}
    for field in project.get('fields', []):
        name = field.get('name')
        if name not in ('Status', 'Waiting resource', 'Work type'):
            continue
        if name in selected or not isinstance(field.get('id'), str):
            raise TrackingError('Project tracking fields are ambiguous.')
        selected[name] = field
    if selected.get('Status', {}).get('dataType') != 'SINGLE_SELECT':
        raise TrackingError('Project Status field is unavailable.')
    if 'Waiting resource' in selected and selected['Waiting resource'].get('dataType') != 'TEXT':
        raise TrackingError('Project Waiting resource field has an unsupported type.')
    if 'Work type' in selected and selected['Work type'].get('dataType') != 'SINGLE_SELECT':
        raise TrackingError('Project Work type field has an unsupported type.')
    for name in ('Status', 'Work type'):
        if name not in selected:
            continue
        options = selected[name].get('options', [])
        if (any(not isinstance(option.get('id'), str) or not isinstance(option.get('name'), str)
                for option in options)
                or len({option['id'] for option in options}) != len(options)
                or len({option['name'].casefold() for option in options}) != len(options)):
            raise TrackingError('Project tracking field options are ambiguous.')
    return selected


def status_value(row, selected):
    value = row.get('status')
    if value is None:
        return 'Unassigned'
    options = {option['id']: option['name'] for option in selected['Status']['options']}
    if (not isinstance(value, dict)
            or value.get('__typename') != 'ProjectV2ItemFieldSingleSelectValue'
            or options.get(value.get('optionId')) != value.get('name')):
        raise TrackingError('Project Status identity changed during the read.')
    name = value['name'].casefold()
    if name in ('todo', 'to do', 'backlog'):
        return 'Planned'
    return next((known for known in STATUSES if known.casefold() == name), 'Other')


def waiting_value(row):
    value = row.get('waiting')
    if value is None:
        return []
    if (not isinstance(value, dict) or value.get('__typename') != 'ProjectV2ItemFieldTextValue'
            or not isinstance(value.get('text'), str)):
        raise TrackingError('Project Waiting resource value has an unsupported type.')
    # Free text can contain private notes; publish only exact known categories.
    known = {raw.casefold(): public for public, raw in WAITING.items()}
    known.update({public.casefold(): public for public in WAITING})
    return sorted({known[part.strip().casefold()] for part in value['text'].split(';')
                   if part.strip().casefold() in known})


def public_issue(row):
    issue = row.get('content') or {}
    repository = issue.get('repository') or {}
    return (issue if issue.get('__typename') == 'Issue'
            and repository.get('nameWithOwner') == REPOSITORY
            and repository.get('isPrivate') is False else None)


def milestone_number(issue):
    if issue.get('number') in range(1, 8):
        return issue['number']
    match = re.match(r'^M([1-7])(?:\s*:|\b)', (issue.get('milestone') or {}).get('title', ''))
    return int(match[1]) if match else None


def references(issue, name):
    connection = issue.get(name) or {}
    if connection.get('pageInfo', {}).get('hasNextPage'):
        raise TrackingError('Issue tracking references exceed the snapshot budget.')
    return sorted({node['number'] for node in connection.get('nodes', [])
                   if node.get('repository', {}).get('nameWithOwner') == REPOSITORY
                   and type(node.get('number')) is int and node['number'] > 0})


def body_details(body):
    body = body if isinstance(body, str) else ''
    checks = re.findall(r'^\s*[-*]\s+\[([ xX])\]\s+.+', body, re.MULTILINE)
    clean = re.sub(r'<!--.*?-->', '', body, flags=re.DOTALL)
    paragraph = next((part.strip() for part in re.split(r'\n\s*\n', clean)
                      if part.strip() and not part.lstrip().startswith('#')), '')
    summary = re.sub(r'\[([^\]]+)\]\([^)]+\)', r'\1', paragraph)
    summary = re.sub(r'<[^>]*>|[`*_#]', '', summary)
    summary = re.sub(r'\s+', ' ', summary).strip()[:320]
    evidence, seen = [], set()
    for text, url in re.findall(r'\[([^\]\n]+)\]\((https://[^\s)]+)\)', clean):
        parsed = urlparse(url)
        allowed = ((parsed.netloc == 'github.com' and parsed.path.startswith('/' + REPOSITORY + '/')
                    and any('/' + section + '/' in parsed.path for section in ('issues', 'pull', 'actions')))
                   or (parsed.netloc == 'jdiller-86.github.io' and parsed.path.startswith('/CTR-2-WUP/')))
        if allowed and url not in seen:
            evidence.append({'text': text[:120], 'url': url})
            seen.add(url)
    return summary, {'done': sum(check.lower() == 'x' for check in checks), 'total': len(checks)}, evidence[:20]


def implementation_links(issue):
    result = []
    for pull_request in (issue.get('closedByPullRequestsReferences') or {}).get('nodes', []):
        repository = pull_request.get('repository') or {}
        number = pull_request.get('number')
        url = pull_request.get('url')
        state = pull_request.get('state')
        if (repository.get('nameWithOwner') == REPOSITORY and repository.get('isPrivate') is False
                and type(number) is int and number > 0
                and url == f'https://github.com/{REPOSITORY}/pull/{number}'
                and state in ('OPEN', 'CLOSED', 'MERGED')):
            result.append({'text': f'PR #{number} ({state.lower()})', 'url': url})
    return result


def snapshot(project, source_commit, now=None):
    if not re.fullmatch(r'[0-9a-f]{40}', source_commit):
        raise TrackingError('Snapshot source identity must be a full commit SHA.')
    selected = fields(project)
    items, numbers = [], set()
    for row in project.get('items', []):
        issue = public_issue(row)
        if issue is None:
            continue
        number = issue.get('number')
        if type(number) is not int or number <= 0 or number in numbers:
            raise TrackingError('Public issue membership is invalid or duplicated.')
        numbers.add(number)
        if (issue.get('state') not in ('OPEN', 'CLOSED')
                or issue.get('url') != f'https://github.com/{REPOSITORY}/issues/{number}'
                or not isinstance(issue.get('title'), str)
                or type(row.get('isArchived')) is not bool):
            raise TrackingError('Public issue identity is invalid.')
        label_data = issue.get('labels') or {}
        if label_data.get('pageInfo', {}).get('hasNextPage'):
            raise TrackingError('Issue labels exceed the snapshot budget.')
        labels = sorted({node['name'] for node in label_data.get('nodes', []) if isinstance(node.get('name'), str)})
        if number == 121 and 'duplicate' in labels:
            continue
        parent = issue.get('parent') or {}
        parent_number = parent.get('number') if parent.get('repository', {}).get('nameWithOwner') == REPOSITORY else None
        summary, acceptance, evidence = body_details(issue.get('body'))
        urls = {item['url'] for item in evidence}
        evidence += [item for item in implementation_links(issue) if item['url'] not in urls]
        work_type = (row.get('workType') or {}).get('name') or ''
        if not isinstance(work_type, str):
            raise TrackingError('Project Work type value has an unsupported type.')
        work_type = next((known for known in ('Milestone', 'Task', 'Feature')
                          if known.casefold() == work_type.casefold()),
                         'Milestone' if number in range(1, 8) else 'Feature' if 'feature' in labels else 'Task')
        items.append({'number': number, 'title': issue['title'], 'url': issue['url'],
            'state': issue['state'], 'status': status_value(row, selected),
            'milestone': milestone_number(issue), 'parent': parent_number, 'labels': labels,
            'blocked_by': references(issue, 'blockedBy'), 'summary': summary,
            'acceptance': acceptance, 'evidence': evidence, 'archived': row['isArchived'],
            'work_type': work_type, 'waiting': waiting_value(row)})
    items.sort(key=lambda item: item['number'])
    gates = {item['number']: item for item in items if item['number'] in range(1, 8)}
    if set(gates) != set(range(1, 8)):
        raise TrackingError('Seven milestone gate issues must remain in the Project.')
    milestones = [{'number': n, 'title': gates[n]['title'], 'url': gates[n]['url'],
        'status': gates[n]['status'],
        'accepted': gates[n]['status'] == 'Accepted' and gates[n]['state'] == 'CLOSED',
        'issues': [item['number'] for item in items if item['milestone'] == n and item['number'] != n],
        'depends_on': [number for number in gates[n]['blocked_by'] if number in range(1, 8)]}
        for n in range(1, 8)]
    return {**snapshot_base(source_commit, now), 'state': 'available',
            'milestones': milestones, 'items': items}


def snapshot_base(source_commit, now=None):
    now = now or datetime.now(timezone.utc)
    return {'schema_version': 2, 'project_url': PROJECT_URL, 'repository': REPOSITORY,
        'generated_at': now.astimezone(timezone.utc).isoformat(timespec='seconds').replace('+00:00', 'Z'),
        'source_commit': source_commit, 'stale_after_seconds': 7200}


def write_snapshot(client, output, source_commit):
    try:
        result = snapshot(client.project(), source_commit)
    except (TrackingError, ValueError, TypeError, KeyError, AttributeError):
        # Error details and raw API responses must never become public payloads.
        result = {**snapshot_base(source_commit), 'state': 'unavailable',
                  'error_code': 'project_unavailable', 'milestones': [], 'items': []}
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    return result


def trusted_main(client):
    def git(*args):
        result = subprocess.run(['git', *args], cwd=ROOT, capture_output=True, text=True, timeout=30)
        if result.returncode:
            raise TrackingError('Trusted-main checkout could not be verified.')
        return result.stdout.strip()
    if git('status', '--porcelain', '--untracked-files=normal'):
        raise TrackingError('Project updates require a clean checkout.')
    commit = git('rev-parse', 'HEAD')
    remote = client.api(f'repos/{REPOSITORY}/commits/main')
    if not re.fullmatch(r'[0-9a-f]{40}', commit) or not isinstance(remote, dict) or remote.get('sha') != commit:
        raise TrackingError('Project updates require the current immutable trusted-main commit.')
    return commit


def update_item(client, project, issue_number, status=None, waiting=None, apply=False):
    if type(issue_number) is not int or issue_number < 1:
        raise TrackingError('A positive repository issue number is required.')
    selected = fields(project)
    rows = [row for row in project['items'] if (public_issue(row) or {}).get('number') == issue_number]
    if len(rows) > 1:
        raise TrackingError('Manual updates require unambiguous public issue membership.')
    adding = not rows
    if adding:
        issue = client.api(f'repos/{REPOSITORY}/issues/{issue_number}')
        if (not isinstance(issue, dict) or 'pull_request' in issue
                or issue.get('number') != issue_number or issue.get('state') not in ('open', 'closed')
                or issue.get('repository_url') != f'https://api.github.com/repos/{REPOSITORY}'
                or issue.get('html_url') != f'https://github.com/{REPOSITORY}/issues/{issue_number}'
                or not isinstance(issue.get('node_id'), str) or not issue['node_id']):
            raise TrackingError('Only an existing issue in the public repository can be added.')
        issue = {'id': issue['node_id'], 'state': issue['state'].upper()}
        row = None
    else:
        row, issue = rows[0], public_issue(rows[0])
    if status is None and waiting is None:
        raise TrackingError('Choose a status or waiting-resource change.')
    if status == 'Accepted' and (issue_number not in range(1, 8) or issue['state'] != 'CLOSED'):
        raise TrackingError('Accepted requires a closed milestone gate issue.')
    if status == 'Done' and issue_number in range(1, 8):
        raise TrackingError('Milestone gates use Accepted after their full scope is reviewed.')
    option = None
    if status is not None:
        if status not in STATUSES:
            raise TrackingError('Unsupported status.')
        options = [value for value in selected['Status']['options']
                   if value['name'].casefold() == status.casefold()]
        if not options and status == 'Planned':
            options = [value for value in selected['Status']['options']
                       if value['name'].casefold() in ('todo', 'to do', 'backlog')]
        if len(options) != 1:
            raise TrackingError('Selected status option is unavailable or ambiguous.')
        option = options[0]
    if waiting is not None and (any(value not in WAITING for value in waiting) or 'Waiting resource' not in selected):
        raise TrackingError('Selected waiting resource is unavailable.')
    receipt = {'repository': REPOSITORY, 'project_url': PROJECT_URL, 'issue': issue_number,
               'status': status, 'waiting': waiting, 'will_add': adding,
               'will_unarchive': bool(row and row.get('isArchived')), 'applied': False}
    if not apply:
        return receipt
    trusted_main(client)
    if adding:
        row = client.add_item(project['id'], issue['id'])
        content = row.get('content') if isinstance(row, dict) else None
        actual = (public_issue(row) if isinstance(content, dict)
                  and isinstance(content.get('repository'), dict) else None)
        # A mutation acknowledgement alone proves no membership. Bind field
        # updates to the exact returned public issue item; the final full
        # connection read still has to observe that same identity and values.
        if (not isinstance(row, dict) or row.get('__typename') != 'ProjectV2Item'
                or not isinstance(row.get('id'), str) or not row['id']
                or type(row.get('isArchived')) is not bool
                or not isinstance(row.get('project'), dict) or row['project'].get('id') != project['id']
                or actual is None or actual.get('id') != issue['id']
                or type(actual.get('number')) is not int or actual['number'] != issue_number
                or actual.get('url') != f'https://github.com/{REPOSITORY}/issues/{issue_number}'
                or actual.get('state') != issue['state']):
            raise TrackingError('Project issue addition could not be verified.')
        receipt['will_unarchive'] = row['isArchived']
    if row.get('isArchived'):
        client.mutate('unarchiveProjectV2Item', 'UnarchiveProjectV2ItemInput', {
            'projectId': project['id'], 'itemId': row['id']})
    if option:
        client.mutate('updateProjectV2ItemFieldValue', 'UpdateProjectV2ItemFieldValueInput', {
            'projectId': project['id'], 'itemId': row['id'], 'fieldId': selected['Status']['id'],
            'value': {'singleSelectOptionId': option['id']}})
    if waiting is not None:
        payload = {'projectId': project['id'], 'itemId': row['id'], 'fieldId': selected['Waiting resource']['id']}
        if waiting:
            client.mutate('updateProjectV2ItemFieldValue', 'UpdateProjectV2ItemFieldValueInput', {
                **payload, 'value': {'text': '; '.join(WAITING[value] for value in sorted(set(waiting)))}})
        else:
            client.mutate('clearProjectV2ItemFieldValue', 'ClearProjectV2ItemFieldValueInput', payload)
    refreshed = client.project()
    refreshed_fields = fields(refreshed)
    if any(refreshed_fields.get(name, {}).get('id') != definition['id']
           for name, definition in selected.items()):
        raise TrackingError('Project tracking fields changed during the update.')
    rows = [item for item in refreshed['items']
            if (public_issue(item) or {}).get('number') == issue_number]
    if (refreshed.get('id') != project['id'] or len(rows) != 1
            or rows[0].get('id') != row['id'] or rows[0].get('isArchived') is not False
            or (public_issue(rows[0]) or {}).get('id') != issue['id']):
        raise TrackingError('Project update readback did not match the requested issue membership.')
    actual = rows[0]
    if status == 'Accepted' and (public_issue(actual) or {}).get('state') != 'CLOSED':
        raise TrackingError('Accepted requires a closed milestone gate issue at readback.')
    if option and (status_value(actual, refreshed_fields) != status
                   or (actual.get('status') or {}).get('optionId') != option['id']):
        raise TrackingError('Project update readback did not match the requested status.')
    if waiting is not None:
        value = actual.get('waiting')
        text = (value or {}).get('text', '')
        if not isinstance(text, str) or waiting_value(actual) != sorted(set(waiting)):
            raise TrackingError('Project update readback did not match the requested waiting resource.')
        expected = {WAITING[name].casefold() for name in waiting}
        observed = {part.strip().casefold() for part in text.split(';') if part.strip()}
        if observed != expected:
            raise TrackingError('Project update readback did not match the requested waiting resource.')
    receipt['applied'] = True
    receipt['verified'] = True
    return receipt


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    read = commands.add_parser('snapshot')
    read.add_argument('--output', type=Path, required=True)
    read.add_argument('--source-commit', required=True)
    update = commands.add_parser('update')
    update.add_argument('--issue', type=int, required=True)
    update.add_argument('--status', choices=STATUSES)
    update.add_argument('--waiting', choices=(*WAITING, 'None'), action='append')
    update.add_argument('--apply', action='store_true')
    update.add_argument('--output', type=Path)
    args = parser.parse_args(argv)
    client = GitHub()
    try:
        if args.command == 'snapshot':
            if not re.fullmatch(r'[0-9a-f]{40}', args.source_commit):
                parser.error('--source-commit requires a full commit SHA')
            result = write_snapshot(client, args.output, args.source_commit)
            print(json.dumps({'state': result['state'], 'items': len(result['items'])}))
        else:
            waiting = args.waiting
            if waiting and 'None' in waiting:
                if len(waiting) != 1:
                    parser.error('--waiting None cannot be combined with another resource')
                waiting = []
            result = update_item(client, client.project(), args.issue, args.status, waiting, args.apply)
            if args.output:
                args.output.parent.mkdir(parents=True, exist_ok=True)
                args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
            print(json.dumps(result))
    except TrackingError as error:
        print(json.dumps({'ok': False, 'error': str(error)}))
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
