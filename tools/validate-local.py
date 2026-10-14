#!/usr/bin/env python3
"""Record actual local validation for one clean commit, without GitHub credentials.

The scoped maintenance profile checks Python management/documentation changes.
The runtime profile also runs all C/sanitizer suites and native RPX/WUHB builds.
This is local evidence, never a fabricated hosted check or console result.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import platform
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
MAINTENANCE_TOOLS = {
    'manage-github.py', 'validate-local.py', 'review-github-project.py',
    'reconcile-github-traceability.py', 'sync-github-progress.py', 'sync-github-status.py',
    'export-github-project.py', 'publish-project-progress.py', 'setup-github-project.py',
    'flesh-github-project.py', 'configure-github-repository.py', 'build-docs-site.py',
    'package-docs-site.py', 'publish-docs-site.py',
}
MAINTENANCE_FILES = {'AGENTS.md', 'CONTRIBUTING.md', 'README.md', 'STATUS.md', 'CHANGELOG.md', 'mkdocs.yml'}


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT, text=True, timeout=30).strip()


def profile(paths):
    if any(path.startswith('.github/workflows/') for path in paths):
        raise ValueError('Workflow changes require pinned actionlint and hosted review; this local route does not waive them.')
    def maintenance(path):
        return (path in MAINTENANCE_FILES or path.startswith('docs/')
                or path in {'tools/' + name for name in MAINTENANCE_TOOLS}
                or path.startswith(('tests/test_github_', 'tests/test_docs_', 'tests/test_documentation')) and path.endswith('.py'))
    return 'maintenance' if paths and all(maintenance(path) for path in paths) else 'runtime'


def validate(output, base='origin/main', runner=subprocess.run):
    if git('status', '--porcelain', '--untracked-files=normal'):
        raise ValueError('Local validation requires a clean committed checkout.')
    sha = git('rev-parse', 'HEAD')
    base_sha = git('rev-parse', '--verify', base + '^{commit}')
    paths = git('diff', '--name-only', base_sha + '...HEAD').splitlines()
    selected = profile(paths)
    resolved = output.resolve()
    if not resolved.is_relative_to(ROOT / 'artifacts') or resolved == ROOT / 'artifacts' or output.exists():
        raise ValueError('Use a fresh output directory under ignored artifacts/.')
    resolved.mkdir(parents=True)
    report = {'schema_version': 1, 'ok': False, 'source_commit': sha, 'base_commit': base_sha,
              'profile': selected, 'platform': platform.system(), 'python': platform.python_version(),
              'generated_at': datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'),
              'changed_paths': paths, 'checks': [], 'hosted_ci': False, 'console_acceptance': False,
              'unverified': ['native Windows', 'Python versions other than this interpreter', 'physical Wii U']}
    checks = [('python', ['make', 'check-python', 'PYTHON=' + sys.executable])]
    if selected == 'runtime':
        checks = [('required-local', ['make', 'check', 'PYTHON=' + sys.executable]), ('native-build', ['make', 'prototype'])]
    checks += [('docs', [sys.executable, 'tools/build-docs-site.py', '--output', str(resolved / 'docs')])]
    try:
        for name, command in checks:
            path = resolved / (name + '.log')
            with path.open('w', encoding='utf-8') as log:
                result = runner(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=1800)
            report['checks'].append({'name': name, 'command': command, 'returncode': result.returncode,
                                     'log': path.name, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
            if result.returncode:
                break
        report['ok'] = (len(report['checks']) == len(checks) and all(row['returncode'] == 0 for row in report['checks'])
                        and git('rev-parse', 'HEAD') == sha and not git('status', '--porcelain', '--untracked-files=normal'))
    except (OSError, subprocess.TimeoutExpired):
        report['error'] = 'Local execution did not complete; no passing validation is claimed.'
    finally:
        (resolved / 'validation.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', default='origin/main')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        report = validate(args.output, args.base)
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        report = {'ok': False, 'error': str(error) if isinstance(error, ValueError) else 'Local source identity could not be verified.'}
    print(json.dumps(report))
    return 0 if report['ok'] else 2


if __name__ == '__main__':
    raise SystemExit(main())
