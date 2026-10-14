#!/usr/bin/env python3
"""Replace verified legacy documentation pages with canonical-site redirects.

Run only on trusted main after the canonical deployment succeeds. History,
unknown files and owner-edited articles are preserved; never force a branch.
"""
import argparse
import hashlib
import html
import importlib.util
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import tempfile
from urllib.parse import quote
from urllib.request import urlopen

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('legacy_docs_publisher', ROOT / 'tools/publish-docs-site.py')
publisher = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(publisher)
SOURCE = publisher.SOURCE
LEGACY = 'Jdiller-86/CTR-2-WUP-docs'
CANONICAL = 'https://jdiller-86.github.io/CTR-2-WUP/'
MARKER = 'ctr2wup-canonical-redirect-v1'


def redirect(target):
    if not target.startswith(CANONICAL):
        raise publisher.PublishError('Redirect target must use the canonical project site.')
    script = 'location.replace(' + json.dumps(target) + '+location.search+location.hash);'
    return ('<!doctype html><html lang="en"><meta charset="utf-8">'
            '<!-- ' + MARKER + ' --><title>CTR2WUP documentation moved</title>'
            '<link rel="canonical" href="' + html.escape(target, quote=True) + '">'
            '<meta http-equiv="refresh" content="0;url=' + html.escape(target, quote=True) + '">'
            '<p>CTR2WUP documentation moved to <a href="' + html.escape(target, quote=True) + '">the project site</a>.</p>'
            '<script>' + script + '</script></html>').encode()


def plan(files):
    manifest = files.get('SHA256SUMS.txt', b'').decode()
    expected = {}
    for line in manifest.splitlines():
        digest, name = line.split('  ', 1)
        if not re.fullmatch('[0-9a-f]{64}', digest) or name in expected:
            raise publisher.PublishError('Legacy manifest identities are invalid.')
        expected[name] = digest
    if 'site/index.html' not in expected:
        raise publisher.PublishError('Legacy generated site provenance is missing.')
    changes = {}
    for name, digest in expected.items():
        path = PurePosixPath(name)
        if path.is_absolute() or '..' in path.parts or '\\' in name:
            raise publisher.PublishError('Legacy manifest path is unsafe.')
        if not name.startswith('site/') or path.suffix != '.html':
            continue
        data = files.get(name)
        if data is None or hashlib.sha256(data).hexdigest() != digest:
            if data is not None and MARKER.encode() in data:
                continue
            raise publisher.PublishError('A legacy article has owner edits; preserve it for review.')
        relative = path.relative_to('site')
        route = relative.parent.as_posix() + '/' if relative.name == 'index.html' else relative.as_posix()
        if route == './':
            route = ''
        if relative.name.startswith('CTR2WUP-Documentation-') or relative.name == '404.html':
            route = ''
        changes[name] = redirect(CANONICAL + quote(route, safe='/'))
    changes['site/progress/index.html'] = redirect(CANONICAL + 'progress/')
    # Old JSON/chart consumers must not retain a misleading available snapshot.
    changes['site/progress/progress.json'] = (json.dumps({'schema_version': 1, 'state': 'unavailable',
        'canonical_url': CANONICAL + 'progress/progress.json',
        'message': 'This endpoint moved; read the canonical Project snapshot.'}, indent=2) + '\n').encode()
    changes['site/progress/gates.svg'] = ('<svg xmlns="http://www.w3.org/2000/svg" width="640" height="48">'
        '<a href="' + CANONICAL + 'progress/"><text x="12" y="30">CTR2WUP progress moved to the canonical project site</text></a></svg>').encode()
    readme = files.get('README.md', b'').decode()
    if MARKER not in readme:
        changes['README.md'] = ('<!-- ' + MARKER + ' -->\n# Documentation moved\n\n'
            'Source, maintained articles, live Project aggregates and Pages now share\n'
            '[Jdiller-86/CTR-2-WUP](https://github.com/' + SOURCE + ') and\n'
            '[the canonical site](' + CANONICAL + ').\n\n'
            'This repository retains migration redirects and historical generated files.\n'
            'The text below is an archived earlier export; do not maintain it independently.\n\n---\n\n' + readme).encode()
    return changes


def migrate(client, apply=False):
    source = client.api('repos/' + SOURCE)
    legacy = client.api('repos/' + LEGACY)
    if (source.get('full_name') != SOURCE or source.get('private') is not False
            or legacy.get('full_name') != LEGACY or legacy.get('private') is not False
            or legacy.get('default_branch') != 'main' or legacy.get('archived') is not False):
        raise publisher.PublishError('Preserve unexpected source/legacy repository identities or settings.')
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    with urlopen(CANONICAL + 'documentation-provenance.json', timeout=30) as response:
        provenance = json.load(response)
    with urlopen(CANONICAL + 'progress/progress.json', timeout=30) as response:
        progress = json.load(response)
    if (provenance.get('source_commit') != commit or provenance.get('application_source_bundled') is not False
            or progress.get('state') != 'available'):
        raise publisher.PublishError('Verify the current canonical deployment and live Project snapshot before migration.')
    base = client.api('repos/' + LEGACY + '/git/ref/heads/main')['object']['sha']
    with tempfile.TemporaryDirectory(prefix='ctr2wup-legacy-') as temporary:
        checkout = Path(temporary) / 'legacy'
        result = subprocess.run(['git', 'clone', '--depth=1', '--no-tags', 'https://github.com/' + LEGACY + '.git', str(checkout)],
                                capture_output=True, text=True, timeout=120)
        if result.returncode or subprocess.check_output(['git', '-C', str(checkout), 'rev-parse', 'HEAD'], text=True).strip() != base:
            raise publisher.PublishError('Legacy checkout changed or could not be read; retry from fresh state.')
        files = {}
        total = 0
        for path in checkout.rglob('*'):
            if '.git' in path.relative_to(checkout).parts:
                continue
            if path.is_symlink():
                raise publisher.PublishError('Preserve unexpected legacy symlinks for review.')
            if path.is_file():
                total += path.stat().st_size
                if total > 64 * 1024 * 1024:
                    raise publisher.PublishError('Legacy files exceed the bounded migration budget.')
                files[path.relative_to(checkout).as_posix()] = path.read_bytes()
        changes = plan(files)
    report = {'ok': True, 'applied': apply, 'source_commit': commit, 'legacy_repository': LEGACY,
              'canonical_url': CANONICAL, 'legacy_base': base, 'changed_paths': sorted(changes),
              'history_preserved': True, 'legacy_archived': False, 'redirect_deployment_verified': False}
    if apply:
        tree = client.api('repos/' + LEGACY + '/git/commits/' + base)['tree']['sha']
        built = client.api('repos/' + LEGACY + '/git/trees', 'POST', {'base_tree': tree, 'tree': [
            {'path': name, 'mode': '100644', 'type': 'blob', 'content': data.decode()} for name, data in changes.items()]})
        created = client.api('repos/' + LEGACY + '/git/commits', 'POST', {'tree': built['sha'], 'parents': [base],
                            'message': 'Redirect documentation to the canonical CTR-2-WUP site'})
        client.api('repos/' + LEGACY + '/git/refs/heads/main', 'PATCH', {'sha': created['sha'], 'force': False})
        report['commit'] = created['sha']
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        report = migrate(publisher.GitHub(), args.apply)
    except (publisher.PublishError, OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        report = {'ok': False, 'error': str(error) if isinstance(error, publisher.PublishError) else
                  'Legacy migration could not be verified; preserve existing files and review the operation.'}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))
    return 0 if report['ok'] else 2


if __name__ == '__main__':
    raise SystemExit(main())
