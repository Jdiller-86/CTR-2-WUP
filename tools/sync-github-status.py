#!/usr/bin/env python3
"""Reconcile bounded issue/PR status labels; dry-run unless --apply.

Only GitHub API JSON is consumed. No contributor code, bodies, artifacts, branch
names or titles are executed. Labels describe work-item state, never console or
milestone acceptance. GitHub has no branch labels: the report maps branches to
their associated PR/check status without creating Git tags or changing refs.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
from urllib.parse import quote

STATES = {
    'planned': 'status:planned',
    'in_progress': 'status:in-progress',
    'awaiting_evidence': 'status:awaiting-evidence',
    'blocked': 'status:blocked',
    'ready_for_review': 'status:ready-for-review',
    'done': 'status:done',
    'closed': 'status:closed',
}
MANAGED = frozenset(STATES.values())
REQUIRED = ('Required CI', 'Documentation build')
FAILURES = frozenset(('failure', 'cancelled', 'timed_out', 'action_required', 'startup_failure', 'stale'))
OWN_CHECK = 'Synchronize work-item statuses'


class StatusError(RuntimeError):
    pass


class GitHub:
    def request(self, path, method='GET', payload=None):
        command = ['gh', 'api', path, '--method', method]
        if payload is not None:
            command += ['--input', '-']
        result = subprocess.run(command, input=json.dumps(payload) if payload is not None else None,
                                capture_output=True, text=True, encoding='utf-8')
        if result.returncode:
            raise StatusError('GitHub ' + method + ' ' + path + ' failed: ' + result.stderr.strip())
        try:
            return json.loads(result.stdout) if result.stdout.strip() else None
        except json.JSONDecodeError as error:
            raise StatusError('GitHub returned invalid JSON') from error


def pages(client, path, field=None):
    result = []
    for page in range(1, 11):
        response = client.request(path + ('&' if '?' in path else '?') + 'per_page=100&page=' + str(page))
        items = response.get(field) if field and isinstance(response, dict) else response
        if not isinstance(items, list):
            raise StatusError('GitHub returned an unexpected list: ' + path)
        result.extend(items)
        if len(items) < 100:
            return result
    raise StatusError('GitHub list exceeds the 1,000-item budget: ' + path)


def number(value):
    if type(value) is not int or not 1 <= value <= 2**31 - 1:
        raise StatusError('Require a positive issue/PR number')
    return value


def workflow_run_id(value):
    # GitHub Actions run IDs already exceed the issue-number range. Keep each
    # API identifier's own bound instead of accepting arbitrary route text.
    if type(value) is not int or not 1 <= value <= 2**63 - 1:
        raise StatusError('Require a positive 64-bit Actions run ID')
    return value


def sha(value):
    if not isinstance(value, str) or not re.fullmatch('[0-9a-f]{40}', value):
        raise StatusError('GitHub returned an invalid commit SHA')
    return value


def label_names(item):
    labels = item.get('labels', [])
    if not isinstance(labels, list):
        raise StatusError('GitHub returned invalid labels')
    return {label['name'] for label in labels if isinstance(label, dict) and isinstance(label.get('name'), str)}


def status_names(item):
    """GitHub label identity is case-insensitive; retain wire spelling elsewhere."""
    return {name.casefold() for name in label_names(item)} & MANAGED


def latest_checks(checks):
    latest = {}
    for check in checks:
        name = check.get('name')
        if (not isinstance(name, str) or name == OWN_CHECK or
                (check.get('app') or {}).get('slug') != 'github-actions' or type(check.get('id')) is not int):
            continue
        if name not in latest or check['id'] > latest[name]['id']:
            latest[name] = check
    return latest


def check_state(checks, required=REQUIRED):
    latest = latest_checks(checks)
    failed = sorted(name for name, check in latest.items()
                    if check.get('status') == 'completed' and check.get('conclusion') in FAILURES)
    if failed:
        return 'blocked', 'Latest checks failed or were cancelled: ' + ', '.join(failed)
    missing = [name for name in required if name not in latest]
    unfinished = [name for name in required if name in latest and latest[name].get('status') != 'completed']
    nonpassing = [name for name in required if name in latest and latest[name].get('status') == 'completed'
                  and latest[name].get('conclusion') != 'success']
    if nonpassing:
        return 'blocked', 'Required checks did not pass: ' + ', '.join(nonpassing)
    if missing or unfinished:
        parts = []
        if missing:
            parts.append('Missing required checks: ' + ', '.join(missing))
        if unfinished:
            parts.append('Required checks pending: ' + ', '.join(unfinished))
        return 'in_progress', '; '.join(parts)
    return 'ready_for_review', 'All required current-commit checks passed; review remains a maintainer decision'


def issue_state(issue, explicit=None, action=None):
    if issue.get('state') == 'closed':
        if issue.get('state_reason') == 'not_planned':
            return 'closed', 'Issue closed without completion; no acceptance is inferred'
        return 'done', 'Issue task closed; no milestone or console acceptance is inferred'
    if explicit is not None:
        return explicit, 'Explicit maintainer status selection'
    if action == 'reopened':
        return 'planned', 'Issue reopened; its task is pending again'
    current = status_names(issue)
    if len(current) == 1:
        state = next(state for state, label in STATES.items() if label in current)
        return state, 'Preserved ordinary issue status selected by a maintainer'
    if len(current) > 1:
        return None, 'Multiple issue statuses need an explicit maintainer label selection'
    return 'planned', 'New or status-less issue; awaiting task triage'


def pr_state(pr, checks, explicit=None):
    if pr.get('merged_at'):
        return 'done', 'PR merged; implementation landed, without milestone or console acceptance'
    if pr.get('state') == 'closed':
        return 'closed', 'PR closed without merge; implementation was not accepted'
    if explicit is not None:
        return explicit, 'Explicit maintainer status selection'
    if pr.get('draft'):
        return 'in_progress', 'Draft PR; not ready for review'
    if STATES['awaiting_evidence'] in status_names(pr):
        return 'awaiting_evidence', 'Maintainer evidence hold; CI cannot remove this hold'
    return check_state(checks)


def item_identity(item, is_pr):
    identity = (item.get('state'), item.get('state_reason'))
    if is_pr:
        identity += (item.get('draft'), item.get('merged_at'), item.get('head', {}).get('sha'),
                     item.get('merge_commit_sha'))
    return identity


def replace_status(client, prefix, item, desired, apply, operations):
    """Only add/delete managed names; never replace the complete label list."""
    current = {name for name in label_names(item) if name.casefold() in MANAGED}
    label = STATES[desired]
    path = prefix + '/issues/' + str(number(item['number'])) + '/labels'
    if label not in {name.casefold() for name in current}:
        operations.append({'method': 'POST', 'path': path, 'labels': [label]})
        if apply:
            client.request(path, 'POST', {'labels': [label]})
    for stale in sorted(name for name in current if name.casefold() != label):
        endpoint = path + '/' + quote(stale, safe='')
        operations.append({'method': 'DELETE', 'path': endpoint})
        if apply:
            client.request(endpoint, 'DELETE')


def commit_checks(client, prefix, commit):
    return pages(client, prefix + '/commits/' + sha(commit) + '/check-runs?filter=all', 'check_runs')


def pr_checks(client, prefix, pr):
    head = sha(pr['head']['sha'])
    commits = {head}
    if pr.get('merge_commit_sha'):
        merge = sha(pr['merge_commit_sha'])
        if merge != head:
            # GitHub may briefly retain a previous merge SHA after a new push.
            # Its successful checks cannot establish readiness for this head.
            detail = client.request(prefix + '/commits/' + merge)
            parents = detail.get('parents', []) if isinstance(detail, dict) else []
            if isinstance(parents, list) and any(isinstance(parent, dict) and parent.get('sha') == head for parent in parents):
                commits.add(merge)
    return [check for commit in sorted(commits) for check in commit_checks(client, prefix, commit)]


def event_scope(client, prefix, event, open_prs):
    """Event JSON selects numeric IDs only; API snapshots supply every decision."""
    action = event.get('action')
    explicit = None
    label = event.get('label', {}).get('name')
    label = label.casefold() if isinstance(label, str) else None
    sender = event.get('sender', {})
    if action == 'labeled' and label in MANAGED and sender.get('type') != 'Bot':
        explicit = next(state for state, name in STATES.items() if name == label)
    if 'pull_request' in event:
        return [number(event['pull_request']['number'])], explicit, action
    if 'issue' in event:
        return [number(event['issue']['number'])], explicit, action
    if 'workflow_run' in event:
        run_id = workflow_run_id(event['workflow_run'].get('id'))
        run = client.request(prefix + '/actions/runs/' + str(run_id))
        if run.get('name') not in ('CI', 'Documentation'):
            return [], None, action
        run_sha = sha(run.get('head_sha'))
        associated = {number(pr['number']) for pr in run.get('pull_requests', [])}
        associated.update(number(pr['number']) for pr in open_prs if pr.get('head', {}).get('sha') == run_sha
                          or pr.get('merge_commit_sha') == run_sha)
        # Workflow IDs from a different repository cannot select write targets.
        known = {number(pr['number']) for pr in open_prs}
        return sorted(associated & known), None, action
    return None, None, action


def reconcile(client, repo, apply=False, selected=None, explicit=None, event=None):
    if (not re.fullmatch('[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', repo) or
            any(part in ('.', '..') for part in repo.split('/'))):
        raise StatusError('Require owner/repository')
    if explicit not in (None, *STATES):
        raise StatusError('Unknown status state')
    if explicit is not None and selected is None:
        raise StatusError('Explicit status requires one --number target')
    prefix = 'repos/' + repo
    metadata = client.request(prefix)
    default_branch = metadata.get('default_branch')
    if not isinstance(default_branch, str) or not default_branch:
        raise StatusError('GitHub returned no default branch')
    main = client.request(prefix + '/branches/' + quote(default_branch, safe=''))
    main_sha = sha(main['commit']['sha'])
    open_prs = pages(client, prefix + '/pulls?state=open')
    action = None
    if event is not None:
        targets, event_explicit, action = event_scope(client, prefix, event, open_prs)
        if selected is not None:
            raise StatusError('Use an event or a numeric scope, not both')
        explicit = event_explicit
    elif selected is not None:
        targets = [number(selected)]
    else:
        targets = [number(item['number']) for item in pages(client, prefix + '/issues?state=all')]
    operations, results, branches = [], [], []
    snapshots = {}

    def snapshot(target):
        if target not in snapshots:
            issue = client.request(prefix + '/issues/' + str(target))
            is_pr = 'pull_request' in issue
            item = client.request(prefix + '/pulls/' + str(target)) if is_pr else issue
            checks = pr_checks(client, prefix, item) if is_pr and item.get('state') == 'open' else []
            snapshots[target] = (item, is_pr, checks)
        return snapshots[target]

    for target in targets or []:
        item, is_pr, checks = snapshot(target)
        if not is_pr and re.search(r'<!-- ctr2wup:(milestone|work-item):[a-z0-9-]+ -->', item.get('body') or ''):
            results.append({'number': target, 'kind': 'issue', 'state': None,
                            'label': None, 'reason': 'Project #2 owns managed roadmap status; Project synchronization mirrors its labels',
                            'skipped': 'Update the Project Status field rather than a separate issue-label state'})
            continue
        state, reason = pr_state(item, checks, explicit) if is_pr else issue_state(item, explicit, action)
        outcome = {'number': target, 'kind': 'pull_request' if is_pr else 'issue', 'state': state,
                   'label': STATES[state] if state else None, 'reason': reason}
        if state is not None:
            # Re-read before writes: do not publish old results after new commits,
            # draft conversion, closure, or a competing manual status selection.
            current = client.request(prefix + ('/pulls/' if is_pr else '/issues/') + str(target)) if apply else item
            if item_identity(current, is_pr) != item_identity(item, is_pr) or status_names(current) != status_names(item):
                outcome['skipped'] = 'Work item changed during reconciliation; next event will retry'
                # Do not show obsolete checks as passing for a freshly pushed
                # branch. The follow-up event will collect its current checks.
                snapshots[target] = (current, is_pr, [])
            else:
                replace_status(client, prefix, current, state, apply, operations)
        results.append(outcome)
    for pr in open_prs:
        target = number(pr['number'])
        item, _, checks = snapshot(target)
        derived, reason = pr_state(item, checks)
        matched = next((row for row in results if row['number'] == target), None)
        state = matched['state'] if matched and not matched.get('skipped') else derived
        head_repo = item.get('head', {}).get('repo') or {}
        branches.append({'repository': head_repo.get('full_name'),
                         'branch': item.get('head', {}).get('ref'), 'sha': sha(item['head']['sha']),
                         'pull_request': target, 'state': state,
                         'reason': matched['reason'] if matched and not matched.get('skipped') else reason,
                         'checks': [{'name': name, 'status': check.get('status'), 'conclusion': check.get('conclusion')}
                                    for name, check in sorted(latest_checks(checks).items())]})
    main_checks = commit_checks(client, prefix, main_sha)
    main_state, main_reason = check_state(main_checks)
    return {'schema_version': 1, 'ok': True, 'applied': apply, 'repository': repo,
            'required_checks': list(REQUIRED), 'operations': operations, 'items': results,
            'branches': branches, 'default_branch': {'branch': default_branch, 'sha': main_sha,
                                                   'state': main_state, 'reason': main_reason},
            'scope': 'Labels apply to issues/PRs. Branch status is reported through associated PRs/checks; no refs or Git tags change.',
            'acceptance': 'done means task closure or merged implementation, never full milestone or console acceptance.'}


def markdown(report):
    def cell(value):
        return str(value).replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;').replace('|', '&#124;').replace('\n', ' ')
    lines = ['## Work-item and branch status', '', report['scope'], '', report['acceptance'], '',
             '| Branch | PR/check scope | State | Reason |', '| --- | --- | --- | --- |']
    main = report['default_branch']
    lines.append('| ' + ' | '.join(cell(v) for v in (main['branch'], 'Default branch checks', main['state'], main['reason'])) + ' |')
    for branch in report['branches']:
        lines.append('| ' + ' | '.join(cell(v) for v in (branch['branch'], '#' + str(branch['pull_request']), branch['state'], branch['reason'])) + ' |')
    lines += ['', 'Mutations: ' + str(len(report['operations'])) + (' applied.' if report['applied'] else ' proposed; dry-run.'), '']
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', default='Jdiller-86/CTR-2-WUP')
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--number', type=int)
    parser.add_argument('--status', choices=tuple(STATES))
    parser.add_argument('--event', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--summary', type=Path)
    args = parser.parse_args()
    event = json.loads(args.event.read_text(encoding='utf-8')) if args.event else None
    report = reconcile(GitHub(), args.repo, args.apply, args.number, args.status, event)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    summary = args.summary or (Path(os.environ['GITHUB_STEP_SUMMARY']) if os.environ.get('GITHUB_STEP_SUMMARY') else None)
    if summary:
        with summary.open('a', encoding='utf-8') as stream:
            stream.write(markdown(report))
    print(json.dumps(report))


if __name__ == '__main__':
    main()
