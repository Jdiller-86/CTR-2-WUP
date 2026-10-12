#!/usr/bin/env python3
"""Synchronize milestone scope, criteria and evidence; Project #2 owns status.

Dry-run by default. --apply creates missing labels/milestones/managed issues and
updates only their managed section. All source/remote ambiguities are checked
before writes. Existing issue states, comments, manual body regions, labels and
selected statuses are preserved. Work completion never accepts a milestone.
Uses normal gh authentication; tokens never enter arguments or reports.
"""
import argparse
import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BEGIN = '<!-- ctr2wup:managed:start -->'
END = '<!-- ctr2wup:managed:end -->'
MILESTONE_BEGIN = '<!-- ctr2wup:milestone-description:start -->'
MILESTONE_END = '<!-- ctr2wup:milestone-description:end -->'
COLORS = {'milestone': '5319e7', 'work-item': '0e8a16', 'console-testing': '1d76db',
          'needs-console': 'fbca04', 'needs-input': 'd93f0b', 'needs-windows': 'c2e0c6',
          'area:cpu': '1d76db', 'area:runtime': '0052cc', 'area:graphics': 'bfd4f2', 'area:host': '0e8a16',
          'area:docs': '0075ca', 'area:github': '24292e',
          'p0': 'b60205', 'p1': 'd93f0b', 'p2': 'fbca04', 'p3': 'c5def5',
          'status:planned': 'c5def5', 'status:in-progress': '1d76db', 'status:awaiting-evidence': 'fbca04',
          'status:blocked': 'd93f0b', 'status:ready-for-review': '0e8a16', 'status:done': '6f42c1', 'status:closed': '6a737d',
          'status:accepted': '238636'}
DESCRIPTIONS = {'milestone': 'Milestone overview and full acceptance gate; separate work issues feed this tracker.',
                'work-item': 'Actionable milestone work with scope, prerequisites, acceptance criteria and evidence.',
                'area:docs': 'Documentation, project explanation and evidence records.',
                'area:github': 'GitHub issues, pull requests, Actions and contributor automation.',
                'p0': 'Urgent maintainer triage for a demonstrated critical release, security or data-loss blocker.',
                'p1': 'High priority milestone work or a demonstrated widespread regression.',
                'p2': 'Normal planned work; priority alone does not establish a verified bug.',
                'p3': 'Lower priority improvement or follow-up that can wait.',
                'status:planned': 'Planned work that has not started.',
                'status:in-progress': 'Implementation is underway; acceptance is not implied.',
                'status:awaiting-evidence': 'Implemented work awaiting the specified runtime or resource evidence.',
                'status:blocked': 'Work awaiting a prerequisite or external resource.',
                'status:ready-for-review': 'Ready for review after the required checks and tracked prerequisites.',
                'status:done': 'Maintainer-confirmed completed work; full milestone acceptance remains separate.',
                'status:closed': 'Closed work that is not being represented as accepted implementation.',
                'status:accepted': 'Explicit full-gate acceptance mirrored from the authoritative GitHub Project.'}
STATES = {'planned', 'in_progress', 'awaiting_evidence', 'blocked'}
WAITING = {'needs-console', 'needs-input', 'needs-windows'}
ROLE_RE = re.compile(r'<!-- ctr2wup:(milestone|work-item):([a-z0-9-]+) -->')
PARENT_RE = re.compile(r'<!-- ctr2wup:parent-milestone:(m[1-7]) -->')


def api(path, method='GET', payload=None):
    command = ['gh', 'api', path, '--method', method]
    if payload is not None:
        command += ['--input', '-']
    result = subprocess.run(command, input=json.dumps(payload) if payload is not None else None,
                            capture_output=True, text=True, encoding='utf-8', timeout=60)
    if result.returncode:
        raise RuntimeError('GitHub ' + method + ' ' + path + ' failed: ' + result.stderr.strip())
    return json.loads(result.stdout) if result.stdout.strip() else None


def pages(path, request=None):
    request = request or api
    items = []
    for page in range(1, 11):
        data = request(path + ('&' if '?' in path else '?') + 'per_page=100&page=' + str(page))
        if not isinstance(data, list):
            raise RuntimeError('Unexpected GitHub list response')
        items.extend(data)
        if len(data) < 100:
            return items
    raise RuntimeError('GitHub list exceeds supported 1,000-entry budget')


def text(value, name, limit=8192):
    if not isinstance(value, str) or not value.strip() or len(value) > limit or '<!-- ctr2wup:' in value:
        raise ValueError('Invalid source text: ' + name)


def strings(value, name, nonempty=False):
    if not isinstance(value, list) or (nonempty and not value) or len(value) > 100:
        raise ValueError('Invalid source list: ' + name)
    for item in value:
        text(item, name)
    if len(set(value)) != len(value):
        raise ValueError('Duplicate source entries: ' + name)


def validate_state(state):
    milestones = state.get('milestones', [])
    if (state.get('schema_version') != 1
            or [m.get('id') for m in milestones] != ['m' + str(i) for i in range(1, 8)]
            or type(state.get('full_milestones_accepted')) is not int
            or not 0 <= state['full_milestones_accepted'] <= 7
            or type(state.get('approximate_implementation_percent')) is not int
            or not 0 <= state['approximate_implementation_percent'] <= 100):
        raise ValueError('Review tracker schema/acceptance policy before synchronization')
    text(state.get('version'), 'version', 64)
    text(state.get('updated'), 'updated', 64)
    titles, work = set(), {}
    for milestone in milestones:
        for name in ('title', 'summary'):
            text(milestone.get(name), milestone['id'] + '.' + name, 256 if name == 'title' else 8192)
        if milestone['title'] in titles:
            raise ValueError('Duplicate milestone title')
        titles.add(milestone['title'])
        if milestone.get('area') not in COLORS or not milestone['area'].startswith('area:'):
            raise ValueError('Unknown milestone area: ' + milestone['id'])
        for name in ('waiting_labels', 'verified_foundations', 'acceptance_remaining'):
            strings(milestone.get(name), milestone['id'] + '.' + name)
        if set(milestone['waiting_labels']) - WAITING or milestone.get('state', 'planned') not in STATES:
            raise ValueError('Unknown milestone state/waiting label: ' + milestone['id'])
        items = milestone.get('work_items', [])
        if not isinstance(items, list) or len(items) > 100:
            raise ValueError('Invalid work_items: ' + milestone['id'])
        for item in items:
            identifier = item.get('id', '')
            if (not isinstance(identifier, str) or len(identifier) > 96
                    or not re.fullmatch(milestone['id'] + r'-[a-z0-9]+(?:-[a-z0-9]+)*', identifier)
                    or identifier in work):
                raise ValueError('Duplicate/invalid work ID or parent prefix: ' + str(identifier))
            for name in ('title', 'summary'):
                text(item.get(name), identifier + '.' + name, 256 if name == 'title' else 8192)
            for name in ('acceptance_criteria', 'depends_on', 'waiting_labels'):
                strings(item.get(name), identifier + '.' + name, name == 'acceptance_criteria')
            if item.get('state') not in STATES or set(item['waiting_labels']) - WAITING:
                raise ValueError('Unknown work state/waiting label: ' + identifier)
            if item.get('priority', 'p2') not in {'p0', 'p1', 'p2', 'p3'}:
                raise ValueError('Unknown work priority: ' + identifier)
            if 'evidence' in item:
                strings(item['evidence'], identifier + '.evidence')
            work[identifier] = (milestone, item)
    if len(work) > 300:
        raise ValueError('Work issues exceed the 300-item synchronization budget')
    known = set(work) | {m['id'] for m in milestones}
    for identifier, (_, item) in work.items():
        if set(item['depends_on']) - known or identifier in item['depends_on']:
            raise ValueError('Unknown/self prerequisite: ' + identifier)
    active, visited = set(), set()
    def visit(identifier):
        if identifier in active:
            raise ValueError('Cyclic work prerequisites: ' + identifier)
        if identifier in visited:
            return
        active.add(identifier)
        for dependency in work[identifier][1]['depends_on']:
            if dependency in work:
                visit(dependency)
        active.remove(identifier)
        visited.add(identifier)
    for identifier in work:
        visit(identifier)
    return work


def reference(identifier, refs, title=None):
    issue = refs.get(identifier)
    if issue:
        return '[#' + str(issue['number']) + '](' + issue['url'] + ')' + (' — ' + title if title else '')
    return chr(96) + identifier + chr(96) + (' — ' + title if title else '') + ' (issue number pending creation)'


def milestone_description(repo, state, milestone, refs):
    def brief(value, limit):
        return value if len(value) <= limit else value[:limit - 1].rstrip() + '…'
    criteria = milestone['acceptance_remaining']
    content = [MILESTONE_BEGIN, '**Goal:** ' + brief(milestone['summary'], 220), '',
               '**Tracking authority:** [Project #2](https://github.com/users/Jdiller-86/projects/2)'
               + ' · **Focused work:** ' + str(len(milestone.get('work_items', []))) + ' issues.', '',
               '**Remaining full acceptance:**']
    content += ['- ' + brief(criterion, 150) for criterion in criteria[:3]]
    if len(criteria) > 3:
        content.append('- Further criteria are in the full checklist below.')
    content += ['', '**Overview:** ' + reference(milestone['id'], refs),
                '[Full checklist](https://github.com/' + repo + '/blob/main/docs/milestones.md#' + milestone['id'] + ')', '',
                'Source v' + state['version'] + '. Task completion alone does not accept this gate.', MILESTONE_END]
    result = '\n'.join(content)
    if len(result) > 1600:
        raise ValueError('Generated milestone description exceeds its readable metadata budget')
    return result


def replace_milestone_description(old, generated, old_summary):
    old = old or ''
    if not isinstance(old, str):
        raise ValueError('Invalid remote milestone description')
    if MILESTONE_BEGIN in old or MILESTONE_END in old:
        if (old.count(MILESTONE_BEGIN) != 1 or old.count(MILESTONE_END) != 1
                or old.index(MILESTONE_BEGIN) >= old.index(MILESTONE_END)):
            raise RuntimeError('Managed milestone description boundaries changed; preserve and review')
        result = old[:old.index(MILESTONE_BEGIN)] + generated + old[old.index(MILESTONE_END) + len(MILESTONE_END):]
    elif not old or old == old_summary:
        result = generated
    else:
        result = old + '\n\n' + generated  # Retain all unrecognized/manual text verbatim.
    if len(result) > 65536:
        raise ValueError('Merged milestone description exceeds the bounded metadata budget')
    return result


def managed(content):
    result = '\n'.join([BEGIN, *content, END])
    if len(result) > 60000:
        raise ValueError('Generated issue section exceeds the bounded body budget')
    return result


def body(repo, state, milestone, refs=None):
    refs = refs or {}
    base = 'https://github.com/' + repo
    content = ['## Current evidence', '', milestone['summary'], '',
               'Evidence updated ' + state['updated'] + ' for **v' + state['version'] + '**. '
               '[Project #2](https://github.com/users/Jdiller-86/projects/2) owns current status, waiting resources '
               'and explicit full-gate acceptance. This source refresh never changes them.', '',
               '### Implemented foundations', '']
    content += ['- [x] ' + item for item in milestone['verified_foundations']]
    content += ['', '### Remaining acceptance', ''] + ['- [ ] ' + item for item in milestone['acceptance_remaining']]
    if milestone.get('work_items'):
        content += ['', '### Actionable work issues', '']
        content += ['- ' + reference(item['id'], refs, item['title']) for item in milestone['work_items']]
        content += ['', 'These separate issues feed this milestone. Closing a work issue never automatically accepts the full gate or closes this overview.']
    content += ['', '[Current status](' + base + '/blob/main/STATUS.md) · [Acceptance checklist]('
                + base + '/blob/main/docs/milestones.md) · [Documentation](' + base + '/blob/main/docs/README.md)', '',
                'This scope and evidence section is generated from versioned requirements/evidence files. '
                'Their historical planning states are not applied to tracking. Keep evidence in comments or the maintainer notes. '
                'Desktop builds and CI never establish console/game acceptance. Do not attach ROMs, keys, saves or extracted commercial assets.']
    return '<!-- ctr2wup:milestone:' + milestone['id'] + ' -->\n\n' + managed(content) + '\n\n## Maintainer notes\n\n'


def checked_criteria(old):
    section = re.search(r'### Acceptance criteria\n(.*?)(?=\n### |' + re.escape(END) + ')', old, re.S)
    if not section:
        return set()
    return {match.group(1) for match in re.finditer(r'^- \[[xX]\] (.+)$', section.group(1), re.M)}


def work_body(repo, state, milestone, item, refs, old=''):
    checked = checked_criteria(old)
    content = ['## Work scope', '', item['summary'], '',
               '**Milestone overview:** ' + reference(milestone['id'], refs, milestone['title']), '',
               '**Tracking authority:** [Project #2](https://github.com/users/Jdiller-86/projects/2) · **Evidence source:** v'
               + state['version'] + ' (' + state['updated'] + ')', '',
               '### Context', '', milestone['summary'], '', '### Prerequisites', '']
    content += ['- ' + reference(dependency, refs) for dependency in item['depends_on']] or ['- No tracked prerequisite.']
    content += ['', '### Acceptance criteria', '']
    content += ['- [' + ('x' if criterion in checked else ' ') + '] ' + criterion for criterion in item['acceptance_criteria']]
    content += ['', '### Evidence', '']
    content += ['- ' + value for value in item.get('evidence', [])] or [
        '- Record reproducible original-fixture/build/test results, source version and relevant logs in comments.']
    if 'needs-console' in item['waiting_labels']:
        content += ['- Console criteria require actual Wii U reports and the firmware/loader/installer versions; desktop or cross-build success does not satisfy them.']
    content += ['- Keep ROMs, decryption keys, saves and extracted commercial assets private.', '',
                'Acceptance checkmarks already recorded for unchanged criteria are preserved. This issue stays open until a maintainer closes it; '
                'its completion never accepts or closes the milestone overview. Keep other manual notes outside this generated section.']
    return ('<!-- ctr2wup:work-item:' + item['id'] + ' -->\n<!-- ctr2wup:parent-milestone:' + milestone['id']
            + ' -->\n\n' + managed(content) + '\n\n## Maintainer notes\n\n')


def replacement(old, generated):
    if old.count(BEGIN) != 1 or old.count(END) != 1 or old.index(BEGIN) >= old.index(END):
        raise RuntimeError('Managed issue boundaries changed; preserve and review')
    section = generated[generated.index(BEGIN):generated.index(END) + len(END)]
    result = old[:old.index(BEGIN)] + section + old[old.index(END) + len(END):]
    if len(result) > 65536:
        raise ValueError('Merged issue body exceeds the GitHub size budget; preserve manual content')
    return result


def label_names(issue):
    return [label['name'] if isinstance(label, dict) else label for label in issue.get('labels', [])]


def validate_identity(old, role, identifier, parent=None):
    replacement(old, managed([]))
    identities = [match.groups() for match in ROLE_RE.finditer(old)]
    mentions = old.count('<!-- ctr2wup:milestone:') + old.count('<!-- ctr2wup:work-item:')
    if identities != [(role, identifier)] or mentions != 1:
        raise RuntimeError('Ambiguous managed identity: ' + identifier)
    marker = '<!-- ctr2wup:' + role + ':' + identifier + ' -->'
    if old.index(BEGIN) <= old.index(marker) <= old.index(END):
        raise RuntimeError('Identity marker inside generated content: ' + identifier)
    if parent:
        if PARENT_RE.findall(old) != [parent] or old.count('<!-- ctr2wup:parent-milestone:') != 1:
            raise RuntimeError('Ambiguous work-item parent marker: ' + identifier)
        marker = '<!-- ctr2wup:parent-milestone:' + parent + ' -->'
        if old.index(BEGIN) <= old.index(marker) <= old.index(END):
            raise RuntimeError('Parent marker inside generated content: ' + identifier)


def desired_labels(milestone, item=None, issue=None):
    source = item or milestone
    labels = ['work-item' if item else 'milestone', milestone['area']]
    existing = label_names(issue or {})
    if item and not any(re.fullmatch(r'p\d+', label.casefold()) for label in existing):
        labels.append(item.get('priority', 'p2'))
    return labels


def synchronize(repo, state, apply=False, request=None, report=None):
    request = request or api
    report = report if report is not None else {}
    report.update(ok=False, applied=False, version=state.get('version'), repository=repo,
                  operations=[], completed_operations=[], writes_attempted=False)
    if not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', repo):
        raise ValueError('Require owner/repository')
    work = validate_state(state)
    prefix = 'repos/' + repo
    labels = {item['name'].casefold() for item in pages(prefix + '/labels', request)}
    milestone_rows = pages(prefix + '/milestones?state=all', request)
    remote_milestones = {}
    for row in milestone_rows:
        if row['title'] in remote_milestones:
            raise RuntimeError('Duplicate remote milestone title; review before writes: ' + row['title'])
        remote_milestones[row['title']] = row
    rows = [issue for issue in pages(prefix + '/issues?state=all', request) if 'pull_request' not in issue]
    existing = {}
    for issue in rows:
        old = issue.get('body') or ''
        found = list(ROLE_RE.finditer(old))
        if len(found) != old.count('<!-- ctr2wup:milestone:') + old.count('<!-- ctr2wup:work-item:'):
            raise RuntimeError('Malformed managed identity marker in issue ' + str(issue['number']))
        if len(found) > 1:
            raise RuntimeError('Multiple managed identities in issue ' + str(issue['number']))
        if found:
            role, identifier = found[0].groups()
            key = (role, identifier)
            if key in existing:
                raise RuntimeError('Duplicate managed marker: ' + role + ':' + identifier)
            existing[key] = issue
    known = [('milestone', m['id'], m, None) for m in state['milestones']]
    known += [('work-item', identifier, milestone, item) for identifier, (milestone, item) in work.items()]
    refs = {}
    for role, identifier, milestone, item in known:
        issue = existing.get((role, identifier))
        if not issue:
            continue
        old = issue.get('body') or ''
        validate_identity(old, role, identifier, milestone['id'] if item else None)
        target = remote_milestones.get(milestone['title'])
        association = issue.get('milestone')
        if association and (target is None or association.get('number') != target['number'] or association.get('title') != milestone['title']):
            raise RuntimeError('Ambiguous existing milestone association: ' + identifier)
        refs[identifier] = {'number': issue['number'], 'url': 'https://github.com/' + repo + '/issues/' + str(issue['number'])}
    report['unmanaged_existing_ids_preserved'] = [identifier for role, identifier in existing if
                                                 (role == 'work-item' and identifier not in work) or
                                                 (role == 'milestone' and identifier not in {m['id'] for m in state['milestones']})]
    for name in COLORS:
        if name not in labels:
            report['operations'].append({'kind': 'label', 'name': name, 'method': 'POST', 'path': prefix + '/labels'})
    for milestone in state['milestones']:
        remote = remote_milestones.get(milestone['title'])
        generated = milestone_description(repo, state, milestone, refs)
        updated = replace_milestone_description(remote.get('description') if remote else '', generated, milestone['summary'])
        if remote is None:
            report['operations'].append({'kind': 'milestone', 'id': milestone['id'], 'title': milestone['title'],
                                         'description_preview': generated})
        elif updated != (remote.get('description') or ''):
            report['operations'].append({'kind': 'milestone-description', 'id': milestone['id'], 'number': remote['number'],
                                         'update_fields': ['description'], 'managed_description_preview': generated})
    for role, identifier, milestone, item in known:
        issue = existing.get((role, identifier))
        generated = work_body(repo, state, milestone, item, refs, (issue.get('body') or '') if issue else '') if item else body(repo, state, milestone, refs)
        entry = {'kind': 'work-item' if item else 'tracker', 'id': identifier, 'existing': bool(issue),
                 'parent': milestone['id'] if item else None, 'title': item['title'] if item else milestone['title']}
        if issue:
            entry.update(refs[identifier])
            fields = []
            if replacement(issue.get('body') or '', generated) != (issue.get('body') or ''):
                fields.append('body')
            if issue.get('milestone') is None:
                fields.append('milestone')
            missing_labels = [name for name in desired_labels(milestone, item, issue)
                              if name.casefold() not in {label.casefold() for label in label_names(issue)}]
            if fields or missing_labels:
                entry.update(update_fields=fields, add_labels=missing_labels,
                             managed_body_preview=generated[generated.index(BEGIN):generated.index(END) + len(END)])
        else:
            entry.update(create=True, labels=desired_labels(milestone, item), body_preview=generated)
        report['operations'].append(entry)
    def mutate(kind, path, payload, method='POST', identifier=None):
        report['writes_attempted'] = True
        result = request(path, method, payload)
        report['completed_operations'].append({'kind': kind, 'id': identifier, 'method': method, 'path': path})
        return result
    if apply:
        for name, color in COLORS.items():
            if name not in labels:
                mutate('label', prefix + '/labels', {'name': name, 'color': color,
                    'description': DESCRIPTIONS.get(name, 'CTR2WUP ' + name + ' tracking')})
        for milestone in state['milestones']:
            if milestone['title'] not in remote_milestones:
                remote_milestones[milestone['title']] = mutate('milestone', prefix + '/milestones',
                    {'title': milestone['title'], 'description': milestone_description(repo, state, milestone, refs)}, identifier=milestone['id'])
        for role, identifier, milestone, item in known:
            if (role, identifier) not in existing:
                generated = work_body(repo, state, milestone, item, refs) if item else body(repo, state, milestone, refs)
                issue = mutate('work-item' if item else 'tracker', prefix + '/issues',
                    {'title': item['title'] if item else milestone['title'], 'body': generated,
                     'labels': desired_labels(milestone, item), 'milestone': remote_milestones[milestone['title']]['number']}, identifier=identifier)
                existing[(role, identifier)] = issue
                refs[identifier] = {'number': issue['number'], 'url': 'https://github.com/' + repo + '/issues/' + str(issue['number'])}
        for role, identifier, milestone, item in known:
            issue = request(prefix + '/issues/' + str(existing[(role, identifier)]['number']))
            validate_identity(issue.get('body') or '', role, identifier, milestone['id'] if item else None)
            association = issue.get('milestone')
            target = remote_milestones[milestone['title']]
            if association and (association.get('number') != target['number'] or association.get('title') != milestone['title']):
                raise RuntimeError('Milestone association changed since preflight: ' + identifier)
            existing[(role, identifier)] = issue
            old = issue.get('body') or ''
            generated = work_body(repo, state, milestone, item, refs, old) if item else body(repo, state, milestone, refs)
            updated = replacement(old, generated)
            payload = {}
            if updated != old:
                payload['body'] = updated
            if issue.get('milestone') is None:
                payload['milestone'] = remote_milestones[milestone['title']]['number']
            if payload:
                result = mutate('work-update' if item else 'issue-update', prefix + '/issues/' + str(issue['number']),
                                payload, 'PATCH', identifier)
                if result:
                    issue.update(result)
            missing = [name for name in desired_labels(milestone, item, issue)
                       if name.casefold() not in {label.casefold() for label in label_names(issue)}]
            if missing:
                mutate('issue-labels', prefix + '/issues/' + str(issue['number']) + '/labels', {'labels': missing}, identifier=identifier)
        for milestone in state['milestones']:
            target = remote_milestones[milestone['title']]
            remote = request(prefix + '/milestones/' + str(target['number']))
            if remote['title'] != milestone['title']:
                raise RuntimeError('Milestone identity changed since preflight: ' + milestone['id'])
            generated = milestone_description(repo, state, milestone, refs)
            updated = replace_milestone_description(remote.get('description'), generated, milestone['summary'])
            if updated != (remote.get('description') or ''):
                mutate('milestone-description', prefix + '/milestones/' + str(target['number']),
                       {'description': updated}, 'PATCH', milestone['id'])
    report.update(ok=True, applied=apply, tracker_count=7, work_item_count=len(work), references=refs,
                  milestone_acceptance_inferred=False, issue_states_changed=False)
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', default='Jdiller-86/CTR-2-WUP')
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args(argv)
    report = {'ok': False, 'applied': False, 'completed_operations': [], 'writes_attempted': False}
    try:
        state = json.loads((ROOT / 'docs/milestone-status.json').read_text())
        synchronize(args.repo, state, args.apply, report=report)
    except (ValueError, RuntimeError, KeyError, OSError, subprocess.TimeoutExpired) as error:
        report.update(ok=False, error=str(error), partial_changes=bool(report['completed_operations']),
                      changes_possible=report['writes_attempted'])
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))
    return 0 if report['ok'] else 2


if __name__ == '__main__':
    raise SystemExit(main())
