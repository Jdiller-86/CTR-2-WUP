#!/usr/bin/env python3
"""Read fixed public Pages endpoints and record deployment identity and freshness."""
import argparse
from datetime import datetime, timezone
import importlib.util
import json
from pathlib import Path
import subprocess
import time
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[1]
CANONICAL = 'https://jdiller-86.github.io/CTR-2-WUP/'
LEGACY = 'https://jdiller-86.github.io/CTR-2-WUP-docs/'
MARKER = b'ctr2wup-canonical-redirect-v1'
SPEC = importlib.util.spec_from_file_location('site_progress', ROOT / 'tools/publish-project-progress.py')
progress = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(progress)


class VerificationError(ValueError):
    """Only fixed public-safe messages/codes are retained in receipts."""
    def __init__(self, code, message, retryable=False):
        super().__init__(message)
        self.code, self.retryable = code, retryable


def fetch(url):
    with urlopen(Request(url, headers={'Cache-Control': 'no-cache', 'User-Agent': 'CTR2WUP-site-verification'}), timeout=30) as response:
        if response.status != 200 or response.geturl() != url:
            raise VerificationError('endpoint_identity', 'Public endpoint identity or HTTP status changed')
        data = response.read(8 * 1024 * 1024 + 1)
    if len(data) > 8 * 1024 * 1024:
        raise VerificationError('endpoint_budget', 'Public endpoint exceeds the verification budget')
    return data


def verify(commit, get=fetch, legacy=False, now=None):
    provenance = json.loads(get(CANONICAL + 'documentation-provenance.json'))
    if (not isinstance(provenance, dict) or provenance.get('ok') is not True
            or provenance.get('public_repository') != 'Jdiller-86/CTR-2-WUP'
            or provenance.get('application_source_bundled') is not False
            or provenance.get('private_payloads_bundled') is not False):
        raise VerificationError('provenance_policy', 'Published documentation provenance violates the checked privacy policy')
    if provenance.get('source_commit') != commit:
        raise VerificationError('source_identity', 'Published documentation does not match the checked source identity', True)
    for route in ('', 'docs/first-title/', 'progress/'):
        if b'<html' not in get(CANONICAL + route).lower():
            raise VerificationError('article_missing', 'Canonical article or dashboard is missing', True)
    data = json.loads(get(CANONICAL + 'progress/progress.json'))
    if not isinstance(data, dict):
        raise VerificationError('project_schema', 'Published Project data does not match the reviewed schema')
    allowed = {'schema_version', 'state', 'project_url', 'generated_at', 'last_success_at', 'stale_after_seconds'}
    allowed |= {'full_gates', 'work_items', 'milestones'} if data.get('state') == 'available' else {'message'}
    if set(data) != allowed or data.get('state') not in ('available', 'unavailable'):
        raise VerificationError('project_privacy', 'Published Project fields exceed the privacy allowlist')
    try:
        progress.validate_public(data)
    except progress.EXPORT.TrackingError:
        raise VerificationError('project_schema', 'Published Project data does not match the reviewed schema') from None
    if data.get('project_url') != progress.EXPORT.PROJECT_URL:
        raise VerificationError('project_identity', 'Published Project identity differs')
    checked_at = now or datetime.now(timezone.utc)
    generated = datetime.strptime(data['generated_at'], '%Y-%m-%dT%H:%M:%SZ').replace(tzinfo=timezone.utc)
    if data['state'] == 'available' and not -300 <= (checked_at - generated).total_seconds() <= 7200:
        raise VerificationError('project_freshness', 'Published Project snapshot is stale or from the future', True)
    report = {'ok': True, 'source_commit': commit, 'canonical_url': CANONICAL,
              'project_state': data['state'], 'project_generated_at': data['generated_at'],
              'verified_at': checked_at.strftime('%Y-%m-%dT%H:%M:%SZ'), 'legacy_verified': False}
    if data['state'] == 'available':
        report['full_gates'] = data['full_gates']
        report['milestone_statuses'] = [row['status'] for row in data['milestones']]
    if legacy:
        if data['state'] != 'available':
            raise VerificationError('legacy_unavailable', 'Legacy migration requires an available canonical Project snapshot')
        for route in ('', 'docs/first-title/', 'progress/'):
            document = get(LEGACY + route)
            target = CANONICAL + route
            if MARKER not in document or target.encode() not in document or b'location.search+location.hash' not in document:
                raise VerificationError('legacy_route', 'Legacy article does not preserve its canonical route and fragment')
        moved = json.loads(get(LEGACY + 'progress/progress.json'))
        if moved != {'schema_version': 1, 'state': 'unavailable',
                     'canonical_url': CANONICAL + 'progress/progress.json',
                     'message': 'This endpoint moved; read the canonical Project snapshot.'}:
            raise VerificationError('legacy_snapshot', 'Legacy progress endpoint still exposes an independent snapshot')
        report['legacy_verified'] = True
    return report


def verify_until_ready(commit, *, attempts=6, delay=10, get=fetch, wait=time.sleep, legacy=False, now=None):
    """Retry propagation/HTTP outages, retaining all original acceptance checks."""
    if not 1 <= attempts <= 12 or not 0 <= delay <= 30:
        raise ValueError('Verification retry budget is outside the supported bounds')
    failures = []
    for attempt in range(1, attempts+1):
        try:
            report = verify(commit, get, legacy=legacy, now=now)
            return dict(report, attempts=attempt, retry_failures=failures)
        except VerificationError as error:
            code, message, retryable = error.code, str(error), error.retryable
        except HTTPError as error:
            code, message = 'http_'+str(error.code), 'Public endpoint returned an HTTP error'
            retryable = error.code in (404, 408, 429, 500, 502, 503, 504)
        except (URLError, TimeoutError, ConnectionError):
            code, message, retryable = 'http_transport', 'Public endpoint transport failed', True
        except (OSError, ValueError, KeyError, TypeError):
            code, message, retryable = 'invalid_public_data', 'Public endpoint data could not be validated', False
        failures.append({'attempt': attempt, 'code': code, 'retryable': retryable})
        if not retryable or attempt == attempts:
            return {'ok': False, 'source_commit': commit, 'canonical_url': CANONICAL,
                    'error': message, 'error_code': code, 'attempts': attempt,
                    'retry_failures': failures}
        wait(delay)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--legacy', action='store_true')
    parser.add_argument('--attempts', type=int, default=6, choices=range(1, 13))
    parser.add_argument('--retry-delay', type=int, default=10, choices=range(31))
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
        report = verify_until_ready(commit, legacy=args.legacy, attempts=args.attempts, delay=args.retry_delay)
    except (OSError, subprocess.SubprocessError):
        report = {'ok': False, 'error_code': 'local_source', 'error': 'Checked source identity could not be read'}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))
    return 0 if report['ok'] else 2


if __name__ == '__main__':
    raise SystemExit(main())
