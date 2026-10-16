#!/usr/bin/env python3
"""Verify the deployed documentation matches this checkout; retry Pages propagation."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import time
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[1]
CANONICAL = 'https://jdiller-86.github.io/CTR-2-WUP/'


class VerificationError(ValueError):
    def __init__(self, code, message, retryable=False):
        super().__init__(message)
        self.code, self.retryable = code, retryable


def fetch(url):
    with urlopen(Request(url, headers={'Cache-Control': 'no-cache', 'User-Agent': 'CTR2WUP-docs-verification'}), timeout=30) as response:
        if response.status != 200 or response.geturl() != url:
            raise VerificationError('endpoint_identity', 'Unexpected documentation endpoint identity/status')
        data = response.read(8 * 1024 * 1024 + 1)
    if len(data) > 8 * 1024 * 1024:
        raise VerificationError('endpoint_budget', 'Documentation endpoint exceeds the size limit')
    return data


def verify(commit, get=fetch, *, with_project=False):
    provenance = json.loads(get(CANONICAL + 'documentation-provenance.json'))
    if (not isinstance(provenance, dict) or provenance.get('ok') is not True
            or provenance.get('public_repository') != 'Jdiller-86/CTR-2-WUP'
            or provenance.get('application_source_bundled') is not False
            or provenance.get('private_payloads_bundled') is not False):
        raise VerificationError('provenance_policy', 'Published documentation violates the checked publication policy')
    if provenance.get('source_commit') != commit:
        raise VerificationError('source_identity', 'Published documentation does not match this source commit', True)
    for route in ('', 'docs/getting-started/'):
        if b'<html' not in get(CANONICAL + route).lower():
            raise VerificationError('article_missing', 'Documentation page is missing', True)
    result = {'ok': True, 'source_commit': commit, 'canonical_url': CANONICAL}
    if with_project:
        if b'Project progress' not in get(CANONICAL + 'progress/'):
            raise VerificationError('project_page', 'Project graph page is missing', True)
        project = json.loads(get(CANONICAL + 'progress/progress.json'))
        if (not isinstance(project, dict) or project.get('schema_version') != 2
                or project.get('repository') != 'Jdiller-86/CTR-2-WUP'
                or project.get('project_url') != 'https://github.com/users/Jdiller-86/projects/2'
                or project.get('state') not in ('available', 'unavailable')
                or project.get('stale_after_seconds') != 7200):
            raise VerificationError('project_policy', 'Published Project snapshot identity is invalid')
        if project.get('source_commit') != commit:
            raise VerificationError('project_source', 'Published Project snapshot does not match this source commit', True)
        generated = datetime.fromisoformat(project['generated_at'].replace('Z', '+00:00'))
        if generated.tzinfo is None:
            raise VerificationError('project_timestamp', 'Published Project snapshot timestamp lacks a timezone')
        age = (datetime.now(timezone.utc) - generated).total_seconds()
        if age < -300:
            raise VerificationError('project_timestamp', 'Published Project snapshot timestamp is in the future')
        result.update(project_snapshot_state=project['state'], project_generated_at=project['generated_at'],
                      project_snapshot_stale=age > project['stale_after_seconds'])
    return result


def verify_until_ready(commit, *, attempts=6, delay=10, get=fetch, wait=time.sleep, with_project=False):
    if not 1 <= attempts <= 12 or not 0 <= delay <= 30:
        raise ValueError('Verification retry budget is outside the supported bounds')
    failures = []
    for attempt in range(1, attempts + 1):
        try:
            return dict(verify(commit, get, with_project=with_project), attempts=attempt, retry_failures=failures)
        except VerificationError as error:
            code, message, retryable = error.code, str(error), error.retryable
        except HTTPError as error:
            code, message = 'http_' + str(error.code), 'Documentation returned an HTTP error'
            retryable = error.code in (404, 408, 429, 500, 502, 503, 504)
        except (URLError, TimeoutError, ConnectionError):
            code, message, retryable = 'http_transport', 'Documentation transport failed', True
        except (OSError, ValueError, KeyError, TypeError):
            code, message, retryable = 'invalid_public_data', 'Documentation provenance is invalid', False
        failures.append({'attempt': attempt, 'code': code, 'retryable': retryable})
        if not retryable or attempt == attempts:
            return {'ok': False, 'source_commit': commit, 'canonical_url': CANONICAL,
                    'error': message, 'error_code': code, 'attempts': attempt, 'retry_failures': failures}
        wait(delay)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--with-project', action='store_true', help='Verify the online graph; unavailable tracking is permitted')
    args = parser.parse_args()
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    report = verify_until_ready(commit, with_project=args.with_project)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))
    return 0 if report['ok'] else 2


if __name__ == '__main__':
    raise SystemExit(main())
