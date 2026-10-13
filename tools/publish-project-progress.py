#!/usr/bin/env python3
"""Atomically publish only three allowlisted progress files to the public docs repo.

The docs repository and its main branch must already exist. This never creates
repositories, changes visibility, configures Pages, or publishes application code.
Uses gh's normal environment authentication; credentials never enter reports.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import re

SPEC = importlib.util.spec_from_file_location('project_export', Path(__file__).with_name('export-github-project.py'))
EXPORT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EXPORT)
TARGET = 'Jdiller-86/CTR-2-WUP-docs'
FILES = ('index.html', 'progress.json', 'gates.svg')


def publish(directory, client, apply=False):
    if not directory.is_dir() or sorted(p.name for p in directory.iterdir()) != sorted(FILES):
        raise EXPORT.TrackingError('Only the three allowlisted public progress files may be published.')
    contents = {}
    for name in FILES:
        path = directory / name
        if path.is_symlink() or not path.is_file() or path.stat().st_size > 256 * 1024:
            raise EXPORT.TrackingError('Public progress inputs exceed the regular-file size policy.')
        contents[name] = path.read_text(encoding='utf-8')
    # An arbitrary input JSON must not become a path for disclosing private data.
    data = json.loads(contents['progress.json'])
    expected_top = {'schema_version', 'state', 'project_url', 'generated_at', 'last_success_at', 'stale_after_seconds'}
    if data.get('state') == 'available':
        expected_top |= {'full_gates', 'work_items', 'milestones'}
    elif data.get('state') == 'unavailable':
        expected_top |= {'message'}
    else:
        raise EXPORT.TrackingError('Public progress state is invalid.')
    if set(data) != expected_top or data.get('project_url') != EXPORT.PROJECT_URL:
        raise EXPORT.TrackingError('Public JSON contains fields outside the privacy allowlist.')
    if contents['index.html'] != Path(__file__).with_name('project-progress-dashboard.html').read_text(encoding='utf-8'):
        raise EXPORT.TrackingError('Dashboard HTML differs from the reviewed original template.')
    validate_public(data)
    if contents['gates.svg'] != EXPORT.gate_svg(data):
        raise EXPORT.TrackingError('Gate SVG differs from the fixed public aggregate rendering.')
    repository = client.api('repos/' + TARGET)
    if not isinstance(repository, dict) or repository.get('full_name') != TARGET or repository.get('private') is not False or repository.get('default_branch') != 'main':
        raise EXPORT.TrackingError('The authorized docs-only repository must exist, be public, and use main.')
    base = client.api('repos/' + TARGET + '/git/ref/heads/main')
    sha = base.get('object', {}).get('sha') if isinstance(base, dict) else None
    if not isinstance(sha, str) or not re.fullmatch('[a-f0-9]{40}', sha):
        raise EXPORT.TrackingError('The docs main branch identity cannot be verified.')
    commit = client.api('repos/' + TARGET + '/git/commits/' + sha)
    tree = commit.get('tree', {}).get('sha') if isinstance(commit, dict) else None
    if not isinstance(tree, str) or not re.fullmatch('[a-f0-9]{40}', tree):
        raise EXPORT.TrackingError('The docs tree identity cannot be verified.')
    result = {'ok': True, 'applied': apply, 'repository': TARGET,
              'paths': ['progress/' + name for name in FILES], 'application_source_published': False}
    if apply:
        built = client.api('repos/' + TARGET + '/git/trees', 'POST', {'base_tree': tree, 'tree': [
            {'path': 'progress/' + name, 'mode': '100644', 'type': 'blob', 'content': contents[name]} for name in FILES]})
        created = client.api('repos/' + TARGET + '/git/commits', 'POST', {
            'message': 'Refresh public aggregate from private Project #2', 'tree': built['sha'], 'parents': [sha]})
        # Never force-update; concurrent documentation publishing causes a safe
        # rejection and requires a fresh run, preserving the other commit.
        client.api('repos/' + TARGET + '/git/refs/heads/main', 'PATCH', {'sha': created['sha'], 'force': False})
        result['commit'] = created['sha']
    return result


def validate_public(data):
    if data.get('schema_version') != 1 or data.get('stale_after_seconds') != 7200:
        raise EXPORT.TrackingError('Public progress schema changed.')
    if not isinstance(data.get('generated_at'), str) or not re.fullmatch(r'\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ', data['generated_at']):
        raise EXPORT.TrackingError('Public progress timestamp is invalid.')
    if data['state'] == 'unavailable':
        if data != EXPORT.unavailable(data['generated_at']):
            raise EXPORT.TrackingError('Unavailable snapshot does not match its fixed public message.')
        return
    source = json.loads((EXPORT.ROOT / 'docs/milestone-status.json').read_text(encoding='utf-8'))
    milestones, _ = EXPORT.catalog(source)
    public_rows = data.get('milestones')
    if not isinstance(public_rows, list) or len(public_rows) != 7:
        raise EXPORT.TrackingError('Public milestone totals are invalid.')
    total = {status: 0 for status in EXPORT.STATUSES}
    accepted = 0
    for index, (row, milestone) in enumerate(zip(public_rows, milestones), 1):
        if (not isinstance(row, dict) or set(row) != {'position', 'title', 'status', 'accepted', 'work_total', 'status_counts', 'waiting_resources'}
                or row['position'] != index or row['title'] != milestone['title'] or row['status'] not in EXPORT.STATUSES
                or type(row['accepted']) is not bool or row['accepted'] != (row['status'] == 'Accepted') or row['work_total'] != 5):
            raise EXPORT.TrackingError('Public milestone fields exceed the privacy/identity allowlist.')
        for values, names in ((row['status_counts'], EXPORT.STATUSES), (row['waiting_resources'], EXPORT.PUBLIC_RESOURCES)):
            if not isinstance(values, dict) or set(values) != set(names) or any(type(v) is not int or not 0 <= v <= 5 for v in values.values()):
                raise EXPORT.TrackingError('Public counts contain unexpected names or values.')
        if sum(row['status_counts'].values()) != 5:
            raise EXPORT.TrackingError('Public work counts are inconsistent.')
        if row['status_counts']['Accepted'] != 0:
            raise EXPORT.TrackingError('Accepted is reserved for milestone overviews.')
        for status in EXPORT.STATUSES:
            total[status] += row['status_counts'][status]
        accepted += int(row['accepted'])
    if (data['last_success_at'] != data['generated_at'] or data['full_gates'] != {'accepted': accepted, 'total': 7}
            or data['work_items'] != {'total': 35, 'status_counts': total}):
        raise EXPORT.TrackingError('Public aggregate totals are inconsistent.')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args(argv)
    try:
        result = publish(args.input, EXPORT.GitHub(), args.apply)
    except (EXPORT.TrackingError, OSError, ValueError, KeyError) as error:
        result = {'ok': False, 'applied': False, 'error': str(error) if isinstance(error, EXPORT.TrackingError) else 'Public publication inputs could not be validated.'}
    print(json.dumps(result))
    return 0 if result['ok'] else 2


if __name__ == '__main__':
    raise SystemExit(main())
