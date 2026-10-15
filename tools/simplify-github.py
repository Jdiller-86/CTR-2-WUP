#!/usr/bin/env python3
"""One-time, dry-run-first cleanup of this repository's former tracking system.

Keep seven milestone gates and three active tasks. Close superseded checklists
as not planned, retain their history, and archive inactive Project items. This
never accepts a milestone. Apply is restricted to clean current trusted main and
the exact plan digest from a fresh dry run. Credentials stay in gh's environment.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
REPO = 'Jdiller-86/CTR-2-WUP'
PROJECT = 'https://github.com/users/Jdiller-86/projects/2'
BASE = 'https://github.com/' + REPO + '/blob/main/'
# Fixed migration scope: newly created issues are never automatically closed.
SUPERSEDED = (13, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 26, 28, 29, 30, 31,
              32, 33, 34, 35, 36, 38, 39, 40, 41, 42, 43, 44, 45, 46, 77, 78, 79, 81)
GATES = {
    1: ('Native Wii U platform', 'area:runtime', 'Awaiting evidence',
        'Original native TV/GamePad, controls, touch, tone, ten-minute stability, normal Minus/HOME returns and save reload already have reviewed physical evidence. Do not repeat them for version changes.',
        'Review the original diagnostic WUP installation, two brief Wii U Menu launches/exits and automatic SD recovery probe from #37. Then review the combined platform record.'),
    2: ('CPU translation and performance', 'area:cpu', 'In progress',
        'Interpreter, generated-code ABI and checked memory have original reference fixtures, including emulated big-endian PowerPC.',
        'Close exercised ARM/Thumb/VFP and fallback gaps; measure representative execution, memory and scheduling costs on Espresso before claiming useful game performance.'),
    3: ('Homebrew execution runtime', 'area:runtime', 'In progress',
        'Original compiled guests exercise kernel/IPC, display, RomFS, persistence and bounded services.',
        'Complete the exercised service/module contracts and guest DSP/audio path (#25); establish a stable compiled homebrew execution route on Wii U.'),
    4: ('PICA to GX2 rendering', 'area:graphics', 'In progress',
        'Original guest draw scenes, software reference rendering, vertex shader arithmetic and checked transport are implemented.',
        'Build real GX2 vertex translation (#80), then TEV/textures/raster state and coherent resource caches. Compare original scene output and actual console costs. Software rendering does not establish GX2 parity.'),
    5: ('Super Mario 3D Land revision', 'area:runtime', 'In progress',
        'Private owned-revision intake has been received. Commercial gameplay has not been established.',
        'Derive the exact-revision requirements, implement the exercised gaps, and demonstrate boot, useful gameplay, audio, save/reload, stability and performance on Wii U.'),
    6: ('Automated ROM to WUP conversion', 'area:host', 'In progress',
        'Host inspection, preparation, staging and independent package verification exist. An original keyless plaintext/self-signed WUP candidate is ready for the focused #37 installation check.',
        'Connect the selected-game conversion stages, identity-aware rebuild/cancellation, clean-Windows conversion and installed-title save/rebuild/uninstall continuity. A diagnostic package alone does not accept this gate.'),
    7: ('Windows GUI injector', 'area:host', 'Planned',
        'The product goal is a Windows GUI that converts one owned, supported 3DS revision into a playable Wii U WUP.',
        'After the execution/conversion pipeline works, provide the Inject workflow, dependency/key setup, saved projects, rebuild and exportable diagnostics; accept the whole route on clean Windows.'),
}
TASKS = {
    25: (3, 'In progress', ['area:runtime'],
         'Complete the guest DSP/audio execution path using original probes. Keep native Audio Streams listening evidence separate from guest DSP acceptance.\n\nAcceptance: supported guest submissions produce independently checked buffers, lifecycle/failure behavior is explicit, and the relevant original guest runs on Wii U.'),
    37: (6, 'Awaiting evidence', ['area:host', 'needs-console'],
         'Review the existing original Install Check WUP with WUP Installer GX2 on signature-patched Aroma. The candidate uses original plaintext/self-signed content; no common key, production certificate or donor title is required.\n\nAcceptance: installation and two brief Wii U Menu launch/exit observations match title ID `0005000043543257`; returned automatic logs establish the SD recovery probe. Reuse the accepted native baseline. This result feeds #1 and does not accept full commercial conversion.'),
    80: (4, 'Planned', ['area:graphics'],
         'Translate the proven original PICA vertex subset into real GX2 shaders.\n\nAcceptance: independent original reference scenes establish supported arithmetic/control-flow/output parity, unsupported input fails explicitly, and the native target builds. Record real Wii U output/cost evidence only for the changed path.'),
}
KEEP = set(GATES) | set(TASKS)
PROJECT_README = ('Active work: seven milestone gates and three concrete tasks. '
                  '[Development plan](' + BASE + 'plans.md).\n\n'
                  'Closed work and pull requests are archived, with history retained. '
                  'A task finishing never accepts a full milestone. Existing hardware '
                  'evidence is reused; request only a changed path or missing criterion.')


class CleanupError(RuntimeError):
    """Safe public operation error."""


class GitHub:
    def api(self, path, method='GET', payload=None):
        command = ['gh', 'api', path, '--method', method]
        if payload is not None:
            command += ['--input', '-']
        result = subprocess.run(command, input=json.dumps(payload) if payload is not None else None,
                                capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise CleanupError('GitHub request failed; inspect credential access before retrying.')
        return json.loads(result.stdout) if result.stdout.strip() else None

    def graphql(self, query, variables):
        response = self.api('graphql', 'POST', {'query': query, 'variables': variables})
        if not isinstance(response, dict) or response.get('errors') or not isinstance(response.get('data'), dict):
            raise CleanupError('Project operation failed; partial writes may need review.')
        return response['data']

    def mutate(self, name, input_type, payload):
        return self.graphql('mutation($input:' + input_type + '!) { ' + name +
                            '(input:$input) { clientMutationId } }', {'input': payload})

    def issues(self):
        rows = []
        for page in range(1, 11):
            batch = self.api(f'repos/{REPO}/issues?state=all&per_page=100&page={page}')
            if not isinstance(batch, list):
                raise CleanupError('Issue pagination failed.')
            rows += [row for row in batch if 'pull_request' not in row]
            if len(batch) < 100:
                return {row['number']: row for row in rows}
        raise CleanupError('Issue pagination exceeds the cleanup budget.')

    def project(self):
        owner = self.graphql('query { user(login:"Jdiller-86") { projectV2(number:2) { '
                             'id url closed title shortDescription readme fields(first:30) { nodes { '
                             '... on ProjectV2SingleSelectField { id name options { id name } } } } } } }', {})['user']
        project = owner['projectV2']
        if project['url'] != PROJECT or project['closed']:
            raise CleanupError('Selected Project identity changed.')
        rows, after, seen = [], None, set()
        for _ in range(10):
            page = self.graphql('query($id:ID!,$after:String) { node(id:$id) { ... on ProjectV2 { '
                'items(first:100,after:$after) { nodes { id isArchived content { __typename '
                '... on Issue { id number state repository { nameWithOwner } } '
                '... on PullRequest { id number repository { nameWithOwner } } } '
                'status:fieldValueByName(name:"Status") { ... on ProjectV2ItemFieldSingleSelectValue { name } } } '
                'pageInfo { hasNextPage endCursor } } } } }', {'id': project['id'], 'after': after})['node']['items']
            for row in page['nodes']:
                if row['id'] in seen:
                    raise CleanupError('Project pagination repeated an item.')
                seen.add(row['id']); rows.append(row)
            if not page['pageInfo']['hasNextPage']:
                project['items'] = rows
                return project
            next_cursor = page['pageInfo']['endCursor']
            if not next_cursor or next_cursor == after:
                raise CleanupError('Project pagination stalled.')
            after = next_cursor
        raise CleanupError('Project pagination exceeds the cleanup budget.')

    def milestones(self):
        rows = self.api(f'repos/{REPO}/milestones?state=all&per_page=100')
        if not isinstance(rows, list) or len(rows) >= 100:
            raise CleanupError('Milestone list exceeds the cleanup budget.')
        return rows

    def hierarchy(self):
        selection = ' '.join(f'g{n}:issue(number:{n}) {{ id subIssues(first:100) {{ nodes {{ id number }} pageInfo {{ hasNextPage }} }} }}' for n in GATES)
        selection += ' ' + ' '.join(f't{n}:issue(number:{n}) {{ id parent {{ id number }} }}' for n in TASKS)
        return self.graphql('query { repository(owner:"Jdiller-86",name:"CTR-2-WUP") { ' + selection + ' } }', {})['repository']


def gate_body(number):
    title, _, _, foundation, remaining = GATES[number]
    return (f'Milestone M{number}: {title}. **Open; full acceptance has not been established.**\n\n'
            f'Implemented/evidence: {foundation}\n\nRemaining: {remaining}\n\n'
            f'[Development plan]({BASE}plans.md) · '
            f'[Reviewed hardware evidence]({BASE}tests/hardware-evidence.json). '
            'Earlier issue revisions and comments retain the detailed history.\n')


def plan(issues, project):
    if not KEEP <= issues.keys() or any(issues[n]['state'] != 'open' for n in KEEP):
        raise CleanupError('Expected open milestone gates/active tasks are missing; preserve the current state.')
    field = next((f for f in project['fields']['nodes'] if f.get('name') == 'Status'), None)
    if not field or not {'Planned', 'In progress', 'Awaiting evidence'} <= {o['name'] for o in field['options']}:
        raise CleanupError('Expected Project status options are missing.')
    operations = []
    milestones = {}
    for row in project['milestones']:
        match = re.match(r'M([1-7]): ', row.get('title', ''))
        if match:
            n = int(match[1])
            if n in milestones:
                raise CleanupError('Milestone names are ambiguous.')
            milestones[n] = row
    if set(milestones) != set(GATES):
        raise CleanupError('Seven repository milestone gates are required.')
    for n, row in milestones.items():
        description = (GATES[n][4] + f'\n\nScope, progress and evidence: https://github.com/{REPO}/issues/{n}. '
                       'This milestone remains open until its full gate is reviewed.')
        if row.get('description') != description:
            operations.append({'kind': 'milestone', 'number': row['number'], 'payload': {'description': description}})
    hierarchy = project['hierarchy']
    for n in GATES:
        children = hierarchy[f'g{n}']['subIssues']
        if children['pageInfo']['hasNextPage']:
            raise CleanupError('Milestone hierarchy exceeds the cleanup budget.')
        for child in children['nodes']:
            if child['number'] in range(12, 47) and child['number'] not in TASKS:
                operations.append({'kind': 'remove_parent', 'parent': hierarchy[f'g{n}']['id'], 'child': child['id']})
    for n, (parent, _, _, _) in TASKS.items():
        current = hierarchy[f't{n}']['parent']
        if current and current['number'] != parent:
            operations.append({'kind': 'remove_parent', 'parent': current['id'], 'child': issues[n]['node_id']})
        if not current or current['number'] != parent:
            operations.append({'kind': 'add_parent', 'parent': issues[parent]['node_id'], 'child': issues[n]['node_id']})
    for n in sorted(KEEP):
        if n in GATES:
            title, area, status, _, _ = GATES[n]
            desired = {'title': f'M{n}: {title}', 'body': gate_body(n), 'labels': ['milestone', area]}
        else:
            _, status, labels, text = TASKS[n]
            desired = {'body': text + f'\n\n[Development plan]({BASE}plans.md).\n', 'labels': ['work-item', *labels]}
        current = issues[n]
        change = {k: v for k, v in desired.items() if
                  (sorted(label['name'] for label in current['labels']) != sorted(v) if k == 'labels' else current.get(k) != v)}
        if change:
            operations.append({'kind': 'issue', 'number': n, 'payload': change})
    for n in SUPERSEDED:
        if n in issues and issues[n]['state'] == 'open':
            b = issues[n].get('body') or ''
            banner = ('Consolidated into the milestone overview and [development plan](' + BASE + 'plans.md). '
                      'Closing this planning checklist as superseded, not as completed or accepted. '
                      'The original scope and evidence remain below.\n\n---\n\n')
            operations.append({'kind': 'issue', 'number': n, 'payload': {
                'state': 'closed', 'state_reason': 'not_planned', 'body': banner + b,
                'labels': [l['name'] for l in issues[n]['labels'] if not l['name'].startswith('status:')]}})
    active = {}
    for item in project['items']:
        content = item.get('content') or {}
        if content.get('repository', {}).get('nameWithOwner') != REPO:
            continue
        n = content.get('number')
        if content.get('__typename') == 'Issue' and n in KEEP:
            if n in active:
                raise CleanupError('Duplicate active Project membership; review before cleanup.')
            active[n] = item
            if item['isArchived']:
                operations.append({'kind': 'unarchive', 'item': item['id']})
        elif not item['isArchived'] and (content.get('__typename') == 'PullRequest'
                or content.get('state') == 'CLOSED' or n in SUPERSEDED):
            operations.append({'kind': 'archive', 'item': item['id']})
    for n in sorted(KEEP):
        status = GATES[n][2] if n in GATES else TASKS[n][1]
        item = active.get(n)
        if item is None:
            operations.append({'kind': 'add', 'number': n, 'content': issues[n]['node_id']})
        if item is None or (item.get('status') or {}).get('name') != status:
            operations.append({'kind': 'status', 'number': n, 'item': item['id'] if item else None,
                               'field': field['id'], 'option': next(o['id'] for o in field['options'] if o['name'] == status)})
    desired_project = {'projectId': project['id'], 'title': 'CTR2WUP',
                       'shortDescription': 'Seven milestone gates and three active development tasks.', 'readme': PROJECT_README}
    if any(project.get(k) != v for k, v in desired_project.items() if k != 'projectId'):
        operations.append({'kind': 'project', 'payload': desired_project})
    return operations


def digest(operations):
    return hashlib.sha256(json.dumps(operations, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def trusted_source(client):
    def git(*args):
        return subprocess.check_output(['git', *args], cwd=ROOT, text=True, timeout=30).strip()
    sha = git('rev-parse', 'HEAD')
    if (not re.fullmatch('[0-9a-f]{40}', sha) or git('branch', '--show-current') != 'main'
            or git('status', '--porcelain', '--untracked-files=normal')
            or client.api(f'repos/{REPO}/commits/main').get('sha') != sha):
        raise CleanupError('Apply requires clean current trusted main.')
    return sha


def cleanup(client, apply=False, plan_sha256=None):
    repo = client.api('repos/' + REPO)
    if repo.get('full_name') != REPO or repo.get('private') is not False or repo.get('default_branch') != 'main':
        raise CleanupError('Expected public source repository identity changed.')
    if apply:
        trusted_source(client)
    issues, project = client.issues(), client.project()
    project['hierarchy'] = client.hierarchy()
    project['milestones'] = client.milestones()
    operations = plan(issues, project)
    plan_hash = digest(operations)
    if apply and (not plan_sha256 or plan_sha256 != plan_hash):
        raise CleanupError('Cleanup plan changed or digest is missing; inspect a fresh dry run before apply.')
    report = {'ok': True, 'applied': apply, 'plan_sha256': plan_hash,
              'active_issues': sorted(KEEP), 'milestones_accepted_by_cleanup': 0,
              'close_superseded': [o['number'] for o in operations if o['kind'] == 'issue' and o['payload'].get('state') == 'closed'],
              'archive_items': sum(o['kind'] == 'archive' for o in operations),
              'operation_count': len(operations), 'completed_operations': 0}
    added = {}
    for op in operations if apply else []:
        kind = op['kind']
        if kind == 'issue':
            client.api(f"repos/{REPO}/issues/{op['number']}", 'PATCH', op['payload'])
        elif kind == 'milestone':
            client.api(f"repos/{REPO}/milestones/{op['number']}", 'PATCH', op['payload'])
        elif kind == 'add':
            data = client.graphql('mutation($input:AddProjectV2ItemByIdInput!) { addProjectV2ItemById(input:$input) { item { id } } }',
                                  {'input': {'projectId': project['id'], 'contentId': op['content']}})
            added[op['number']] = data['addProjectV2ItemById']['item']['id']
        elif kind == 'status':
            client.mutate('updateProjectV2ItemFieldValue', 'UpdateProjectV2ItemFieldValueInput', {
                'projectId': project['id'], 'itemId': op['item'] or added[op['number']],
                'fieldId': op['field'], 'value': {'singleSelectOptionId': op['option']}})
        elif kind in ('add_parent', 'remove_parent'):
            name = 'addSubIssue' if kind == 'add_parent' else 'removeSubIssue'
            client.mutate(name, name[0].upper() + name[1:] + 'Input', {'issueId': op['parent'], 'subIssueId': op['child']})
        elif kind == 'project':
            client.mutate('updateProjectV2', 'UpdateProjectV2Input', op['payload'])
        else:
            name = 'archiveProjectV2Item' if kind == 'archive' else 'unarchiveProjectV2Item'
            client.mutate(name, name[0].upper() + name[1:] + 'Input', {'projectId': project['id'], 'itemId': op['item']})
        report['completed_operations'] += 1
    if apply:
        fresh_project = client.project()
        fresh_project['hierarchy'] = client.hierarchy()
        fresh_project['milestones'] = client.milestones()
        remaining = plan(client.issues(), fresh_project)
        if remaining:
            raise CleanupError('Cleanup readback differs; inspect a fresh dry run. Partial changes may have occurred.')
        report['readback_verified'] = True
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--plan-sha256')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        report = cleanup(GitHub(), args.apply, args.plan_sha256)
    except (CleanupError, OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        report = {'ok': False, 'applied': args.apply, 'error': str(error) if isinstance(error, CleanupError)
                  else 'Cleanup could not complete; inspect current state before retrying.'}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))
    return 0 if report['ok'] else 2


if __name__ == '__main__':
    raise SystemExit(main())
