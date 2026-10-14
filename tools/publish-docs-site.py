#!/usr/bin/env python3
"""Stage checked documentation or dispatch Pages for the canonical public source.

Dry-run by default. Run only from trusted main with normal gh authentication.
Only validated site files are staged. Never write generated files into source.
"""
import argparse
import os
import tempfile
import hashlib
import importlib.util
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = 'Jdiller-86/CTR-2-WUP'
TARGET = SOURCE
spec = importlib.util.spec_from_file_location('docs_package', ROOT / 'tools/package-docs-site.py')
docs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(docs)


class PublishError(RuntimeError):
    pass


class GitHub:
    def api(self, path, method='GET', payload=None, missing=False):
        command = ['gh', 'api', path, '--method', method]
        if payload is not None:
            command += ['--input', '-']
        result = subprocess.run(command, input=json.dumps(payload) if payload is not None else None,
                                capture_output=True, text=True, encoding='utf-8', timeout=60)
        if result.returncode == 0 and not result.stdout.strip():
            return None  # Successful 204 responses, such as alert enablement.
        try:
            response = json.loads(result.stdout)
        except ValueError:
            raise PublishError('GitHub returned no usable JSON; review Actions access.') from None
        if result.returncode:
            if missing and isinstance(response, dict) and str(response.get('status')) == '404':
                return None
            raise PublishError('GitHub denied or could not complete the docs operation; review Actions access.')
        return response


def bundle(folder, commit):
    report = json.loads(docs.read_file(folder / 'publication-report.json', 2 * 1024 * 1024))
    for key in ('ok', 'zip_crc_verified', 'archive_contains_documentation_only', 'all_maintained_article_pages_retained'):
        if report.get(key) is not True:
            raise PublishError('Require a verified docs-only publication report.')
    if (not re.fullmatch('[0-9a-f]{40}', commit) or report.get('source_commit') != commit
            or report.get('public_repository') != TARGET
            or report.get('application_source_bundled') is not False
            or report.get('private_payloads_bundled') is not False):
        raise PublishError('Documentation source identity/privacy contract does not match.')
    version = report.get('version', '')
    if not re.fullmatch(r'\d+\.\d+\.\d+', version):
        raise PublishError('Documentation version is invalid.')
    name = 'CTR2WUP-Public-Docs-' + version + '.zip'
    if report.get('public_bundle') != name:
        raise PublishError('Unexpected documentation bundle name.')
    archive_bytes = docs.read_file(folder / name, 64 * 1024 * 1024)
    records = [row for row in report.get('artifacts', []) if row.get('name') == name]
    if len(records) != 1 or records[0].get('sha256') != hashlib.sha256(archive_bytes).hexdigest():
        raise PublishError('Documentation archive checksum differs from the reviewed report.')
    entries = {}
    with zipfile.ZipFile(folder / name) as archive:
        infos = archive.infolist()
        if len(infos) > 20000 or sum(row.file_size for row in infos) > 64 * 1024 * 1024 or archive.testzip():
            raise PublishError('Documentation archive exceeds bounds or has invalid CRCs.')
        for row in infos:
            path = PurePosixPath(row.filename)
            if (row.filename != path.as_posix() or path.is_absolute() or '..' in path.parts
                    or '\\' in row.filename or row.filename in entries or row.is_dir()
                    or (row.external_attr >> 16) & 0o170000 == 0o120000):
                raise PublishError('Documentation archive contains unsafe or duplicate paths.')
            if row.filename not in ('README.md', 'SHA256SUMS.txt', 'documentation-provenance.json'):
                relative = path.relative_to('site') if path.parts[0] == 'site' else None
                if (relative is None or len(path.parts) < 2
                        or any(part.lower() in {'private', '.git', '.github', '.env', 'artifacts', 'build', 'obj', 'bin'} for part in relative.parts)
                        or path.suffix.lower() in docs.FORBIDDEN
                        or path.suffix.lower() not in docs.STATIC_SUFFIXES and path.name not in docs.TEXT_LICENSES):
                    raise PublishError('Documentation archive contains non-documentation input.')
            data = archive.read(row)
            if data.startswith((b'\x7fELF', b'MZ', b'WUHB')):
                raise PublishError('Executable payload cannot be published as documentation.')
            entries[row.filename] = data
    if json.loads(entries.get('documentation-provenance.json', b'{}')).get('source_commit') != commit:
        raise PublishError('Embedded documentation provenance differs from the reviewed source.')
    if 'site/index.html' not in entries or 'site/search/search_index.json' not in entries:
        raise PublishError('Rendered searchable documentation is missing.')
    expected = ''.join(hashlib.sha256(data).hexdigest() + '  ' + name + '\n'
                       for name, data in sorted(entries.items()) if name != 'SHA256SUMS.txt').encode()
    if entries.get('SHA256SUMS.txt') != expected:
        raise PublishError('Bundled file checksums do not match the documentation.')
    return report, entries


def stage(folder, commit, output):
    report, entries = bundle(folder, commit)
    output = output.resolve()
    artifacts = (ROOT / 'artifacts').resolve()
    if not output.is_relative_to(artifacts) or output == artifacts or output.exists():
        raise PublishError('Stage into a fresh directory under ignored artifacts/.')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.pages-', dir=output.parent) as temporary:
        site = Path(temporary) / 'site'
        for name, data in entries.items():
            if name.startswith('site/'):
                target = Path(temporary) / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
        (site / 'documentation-provenance.json').write_bytes(entries['documentation-provenance.json'])
        os.replace(site, output)
    return {'ok': True, 'source_commit': commit, 'stage': str(output),
            'documentation_only': True, 'pages_deployment_verified': False}


def publish(folder, commit, client, apply=False, create=False):
    report, entries = bundle(folder, commit)
    if create:
        raise PublishError('The canonical source repository must already exist; creation is unsupported.')
    source = client.api('repos/' + SOURCE)
    if (not isinstance(source, dict) or source.get('full_name') != SOURCE
            or source.get('private') is not False or source.get('default_branch') != 'main'):
        raise PublishError('The authorized source must be public and use main; preserve unexpected settings.')
    result = {'ok': True, 'applied': apply, 'repository': SOURCE, 'source_commit': commit,
              'documentation_only': True, 'application_visibility_changed': False,
              'files': len(entries), 'pages_deployment_verified': False,
              'workflow_dispatched': False}
    if apply:
        current = client.api('repos/' + SOURCE + '/commits/main')
        if not isinstance(current, dict) or current.get('sha') != commit:
            raise PublishError('Dispatch requires the current trusted main commit.')
        client.api('repos/' + SOURCE + '/actions/workflows/publish-docs.yml/dispatches',
                   'POST', {'ref': 'main'})
        result['workflow_dispatched'] = True
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--source-commit', required=True)
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--create', action='store_true', help=argparse.SUPPRESS)
    parser.add_argument('--stage', type=Path, help='Stage checked site files locally; no API calls')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    try:
        if args.stage is not None:
            if args.apply or args.create:
                raise PublishError('Local staging cannot be combined with API apply/create.')
            result = stage(args.input, args.source_commit, args.stage)
        else:
            result = publish(args.input, args.source_commit, GitHub(), args.apply, args.create)
    except (PublishError, OSError, ValueError, KeyError, TypeError, subprocess.TimeoutExpired, zipfile.BadZipFile) as error:
        result = {'ok': False, 'error': str(error) if isinstance(error, PublishError) else 'Documentation publication could not be verified; review Actions receipt.'}
    if args.output:
        args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))
    return 0 if result['ok'] else 2


if __name__ == '__main__':
    raise SystemExit(main())
