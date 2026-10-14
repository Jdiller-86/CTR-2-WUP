#!/usr/bin/env python3
"""Run fixed GitHub management operations without a hosted Actions runner.

Uses only the securely configured CTR2WUP_GITHUB_MANAGEMENT_TOKEN binding.
Dry-run is the default. Apply requires a clean checkout of current remote main.
All command output and receipts stay in a fresh ignored artifacts directory.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('management_tracking', ROOT / 'tools/export-github-project.py')
tracking = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(tracking)
SECRET = 'CTR2WUP_GITHUB_MANAGEMENT_TOKEN'
DOCS = 'Jdiller-86/CTR-2-WUP-docs'


def credential_environment(environ):
    if not environ.get(SECRET):
        raise tracking.TrackingError('Configure the secure CTR2WUP_GITHUB_MANAGEMENT_TOKEN environment binding; do not put a token in arguments or chat.')
    env = dict(environ)
    env['GH_TOKEN'] = env[SECRET]
    env['GH_PROMPT_DISABLED'] = '1'
    return env


def preflight(client, source, report, operation='preflight'):
    report.update(ok=False, stage='source_repository', writes_attempted=False,
                  project_read_verified=False, project_write_verified=False,
                  docs_write_permission_verified=False)
    repository = client.api('repos/' + tracking.REPOSITORY)
    if (not isinstance(repository, dict) or repository.get('full_name') != tracking.REPOSITORY
            or repository.get('private') is not True or repository.get('default_branch') != 'main'
            or repository.get('permissions', {}).get('push') is not True):
        raise tracking.TrackingError('Private source identity and repository write access could not be verified.')
    if operation in ('preflight', 'traceability', 'review', 'progress'):
        report['stage'] = 'project'
        project = client.project()
        tracking.collect(project, source)
        report['project_read_verified'] = True
    if operation in ('preflight', 'progress', 'docs'):
        report['stage'] = 'public_docs_repository'
        docs = client.api('repos/' + DOCS)
        if (not isinstance(docs, dict) or docs.get('full_name') != DOCS or docs.get('private') is not False
                or docs.get('default_branch') != 'main' or docs.get('permissions', {}).get('push') is not True):
            raise tracking.TrackingError('Authorized public docs identity and repository write access could not be verified.')
        report['docs_write_permission_verified'] = True
    report.update(ok=True, stage='ready', repository_write_permission_verified=True,
                  required_project_permission='project read/write; read access alone does not establish mutation access')
    return report


def git(*args):
    result = subprocess.run(['git', *args], cwd=ROOT, capture_output=True, text=True, timeout=30)
    if result.returncode:
        raise tracking.TrackingError('Local source identity could not be verified.')
    return result.stdout.strip()


def trusted_source(client):
    sha = git('rev-parse', 'HEAD')
    if (not re.fullmatch('[0-9a-f]{40}', sha) or git('branch', '--show-current') != 'main'
            or git('status', '--porcelain', '--untracked-files=normal')):
        raise tracking.TrackingError('Apply requires clean trusted main; commit/review branch changes first.')
    remote = client.api('repos/' + tracking.REPOSITORY + '/commits/main')
    if not isinstance(remote, dict) or remote.get('sha') != sha:
        raise tracking.TrackingError('Local main is not the current remote main; refresh it without discarding changes.')
    return sha


def commands(args, output, sha):
    apply = ['--apply'] if args.apply else []
    result = []
    if args.operation == 'traceability':
        result.append(('traceability', 'reconcile-github-traceability.py', ['--output', str(output / 'traceability.json'), *apply]))
    elif args.operation == 'evidence':
        result.append(('evidence', 'sync-github-progress.py', ['--output', str(output / 'evidence.json'), *apply]))
    elif args.operation == 'review':
        if not args.review:
            raise tracking.TrackingError('Select a versioned --review identifier.')
        result.append(('review', 'review-github-project.py', ['--review', args.review, '--output', str(output / 'review.json'), *apply]))
    elif args.operation == 'progress':
        result.append(('project', 'export-github-project.py', ['--output', str(output / 'progress'), '--report', str(output / 'project.json'), *apply]))
        if args.apply:
            result.append(('publication', 'publish-project-progress.py', ['--input', str(output / 'progress'), '--apply']))
    elif args.operation == 'docs':
        if args.input is None:
            raise tracking.TrackingError('Select the checked docs-only publication --input directory.')
        result.append(('docs', 'publish-docs-site.py', ['--input', str(args.input), '--source-commit', sha, '--output', str(output / 'docs.json'), *apply]))
    return result


def output_directory(path):
    resolved = path.resolve()
    if not resolved.is_relative_to(ROOT / 'artifacts') or resolved == ROOT / 'artifacts' or path.exists():
        raise tracking.TrackingError('Use a fresh output directory under ignored artifacts/.')
    resolved.mkdir(parents=True)
    return resolved


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation', choices=('preflight', 'traceability', 'evidence', 'review', 'progress', 'docs'))
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--review')
    parser.add_argument('--input', type=Path)
    parser.add_argument('--output', type=Path, default=ROOT / 'artifacts' / ('github-management-' + datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')))
    args = parser.parse_args(argv)
    report = {'ok': False, 'operation': args.operation, 'applied': args.apply, 'operations_completed': []}
    output = None
    try:
        output = output_directory(args.output)
        env = credential_environment(os.environ)
        client = tracking.GitHub(env=env)
        access = {}
        try:
            preflight(client, json.loads((ROOT / 'docs/milestone-status.json').read_text()), access, args.operation)
        finally:
            (output / 'access.json').write_text(json.dumps(access, indent=2) + '\n', encoding='utf-8')
        report['stage'] = 'source'
        sha = trusted_source(client) if args.apply else git('rev-parse', 'HEAD')
        report['source_commit'] = sha
        for name, script, arguments in commands(args, output, sha):
            report['stage'] = name
            # Fixed allowlisted scripts, no shell interpolation or credential argument.
            with (output / (name + '.log')).open('w', encoding='utf-8') as log:
                result = subprocess.run([sys.executable, str(ROOT / 'tools' / script), *arguments],
                                        cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=1800)
            if result.returncode:
                raise tracking.TrackingError('Management operation failed; inspect its private receipt before retrying. Partial writes may have occurred.')
            report['operations_completed'].append(name)
        report.update(ok=True, stage='complete')
    except (tracking.TrackingError, OSError, ValueError, subprocess.TimeoutExpired) as error:
        report['error'] = str(error) if isinstance(error, tracking.TrackingError) else 'Management inputs or execution could not be verified.'
    if output is not None:
        (output / 'receipt.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report))
    return 0 if report['ok'] else 2


if __name__ == '__main__':
    raise SystemExit(main())
