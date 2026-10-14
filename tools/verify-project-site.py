#!/usr/bin/env python3
"""Read fixed public Pages endpoints and record deployment identity and freshness."""
import argparse
from datetime import datetime, timezone
import importlib.util
import json
from pathlib import Path
import subprocess
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[1]
CANONICAL = 'https://jdiller-86.github.io/CTR-2-WUP/'
LEGACY = 'https://jdiller-86.github.io/CTR-2-WUP-docs/'
MARKER = b'ctr2wup-canonical-redirect-v1'
SPEC = importlib.util.spec_from_file_location('site_progress', ROOT / 'tools/publish-project-progress.py')
progress = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(progress)


def fetch(url):
    with urlopen(Request(url, headers={'Cache-Control': 'no-cache', 'User-Agent': 'CTR2WUP-site-verification'}), timeout=30) as response:
        if response.status != 200 or response.geturl() != url:
            raise ValueError('Public endpoint identity or HTTP status changed')
        data = response.read(8 * 1024 * 1024 + 1)
    if len(data) > 8 * 1024 * 1024:
        raise ValueError('Public endpoint exceeds the verification budget')
    return data


def verify(commit, get=fetch, legacy=False, now=None):
    provenance = json.loads(get(CANONICAL + 'documentation-provenance.json'))
    if (provenance.get('source_commit') != commit or provenance.get('ok') is not True
            or provenance.get('public_repository') != 'Jdiller-86/CTR-2-WUP'
            or provenance.get('application_source_bundled') is not False
            or provenance.get('private_payloads_bundled') is not False):
        raise ValueError('Published documentation does not match the checked source identity')
    for route in ('', 'docs/first-title/', 'progress/'):
        if b'<html' not in get(CANONICAL + route).lower():
            raise ValueError('Canonical article or dashboard is missing')
    data = json.loads(get(CANONICAL + 'progress/progress.json'))
    allowed = {'schema_version', 'state', 'project_url', 'generated_at', 'last_success_at', 'stale_after_seconds'}
    allowed |= {'full_gates', 'work_items', 'milestones'} if data.get('state') == 'available' else {'message'}
    if set(data) != allowed or data.get('state') not in ('available', 'unavailable'):
        raise ValueError('Published Project fields exceed the privacy allowlist')
    progress.validate_public(data)
    if data.get('project_url') != progress.EXPORT.PROJECT_URL:
        raise ValueError('Published Project identity differs')
    checked_at = now or datetime.now(timezone.utc)
    generated = datetime.strptime(data['generated_at'], '%Y-%m-%dT%H:%M:%SZ').replace(tzinfo=timezone.utc)
    if data['state'] == 'available' and not -300 <= (checked_at - generated).total_seconds() <= 7200:
        raise ValueError('Published Project snapshot is stale or from the future')
    report = {'ok': True, 'source_commit': commit, 'canonical_url': CANONICAL,
              'project_state': data['state'], 'project_generated_at': data['generated_at'],
              'verified_at': checked_at.strftime('%Y-%m-%dT%H:%M:%SZ'), 'legacy_verified': False}
    if data['state'] == 'available':
        report['full_gates'] = data['full_gates']
        report['milestone_statuses'] = [row['status'] for row in data['milestones']]
    if legacy:
        if data['state'] != 'available':
            raise ValueError('Legacy migration requires an available canonical Project snapshot')
        for route in ('', 'docs/first-title/', 'progress/'):
            document = get(LEGACY + route)
            target = CANONICAL + route
            if MARKER not in document or target.encode() not in document or b'location.search+location.hash' not in document:
                raise ValueError('Legacy article does not preserve its canonical route and fragment')
        moved = json.loads(get(LEGACY + 'progress/progress.json'))
        if moved != {'schema_version': 1, 'state': 'unavailable',
                     'canonical_url': CANONICAL + 'progress/progress.json',
                     'message': 'This endpoint moved; read the canonical Project snapshot.'}:
            raise ValueError('Legacy progress endpoint still exposes an independent snapshot')
        report['legacy_verified'] = True
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--legacy', action='store_true')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
        report = verify(commit, legacy=args.legacy)
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError):
        report = {'ok': False, 'error': 'Public site identity, content or Project freshness did not verify; inspect the deployment.'}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))
    return 0 if report['ok'] else 2


if __name__ == '__main__':
    raise SystemExit(main())
