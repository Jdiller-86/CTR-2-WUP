#!/usr/bin/env python3
"""Publish the verified docs-only export to the authorized public Pages repo.

Dry-run by default. Run only from trusted main with normal gh authentication.
Never copy the application checkout, alter its visibility or force a branch.
"""
import argparse
import base64
import hashlib
import importlib.util
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = 'Jdiller-86/CTR-2-WUP'
TARGET = 'Jdiller-86/CTR-2-WUP-docs'
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
            if row.filename not in ('README.md', 'SHA256SUMS.txt', 'documentation-provenance.json', '.github/workflows/pages.yml'):
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
    if entries.get('.github/workflows/pages.yml') != docs.PAGES_WORKFLOW.encode():
        raise PublishError('The public Pages workflow differs from its reviewed template.')
    if json.loads(entries.get('documentation-provenance.json', b'{}')).get('source_commit') != commit:
        raise PublishError('Embedded documentation provenance differs from the reviewed source.')
    if 'site/index.html' not in entries or 'site/search/search_index.json' not in entries:
        raise PublishError('Rendered searchable documentation is missing.')
    expected = ''.join(hashlib.sha256(data).hexdigest() + '  ' + name + '\n'
                       for name, data in sorted(entries.items()) if name != 'SHA256SUMS.txt').encode()
    if entries.get('SHA256SUMS.txt') != expected:
        raise PublishError('Bundled file checksums do not match the documentation.')
    return report, entries


def publish(folder, commit, client, apply=False, create=False):
    report, entries = bundle(folder, commit)  # Validate everything before any API mutation.
    source = client.api('repos/' + SOURCE)
    if source.get('full_name') != SOURCE or source.get('private') is not True:
        raise PublishError('The application repository must remain private.')
    target = client.api('repos/' + TARGET, missing=True)
    result = {'ok': True, 'applied': apply, 'repository': TARGET, 'source_commit': commit,
              'documentation_only': True, 'application_visibility_changed': False,
              'files': len(entries), 'repository_created': False, 'pages_deployment_verified': False}
    if target is None:
        if not create:
            raise PublishError('The authorized docs-only repository does not yet exist; use explicit --create.')
        if client.api('user').get('login') != 'Jdiller-86':
            raise PublishError('Only the authorized owner may create this docs repository.')
        if not apply:
            result['would_create_repository'] = True
            return result
        target = client.api('user/repos', 'POST', {'name': 'CTR-2-WUP-docs', 'private': False,
                             'auto_init': True, 'description': 'Documentation and aggregate progress for CTR2WUP. Application source remains private.'})
        result['repository_created'] = True
        if target.get('default_branch') != 'main':
            branch = target.get('default_branch', '')
            if not re.fullmatch('[A-Za-z0-9_-]+', branch):
                raise PublishError('New docs repository has an unsupported initial branch.')
            client.api('repos/' + TARGET + '/branches/' + branch + '/rename', 'POST', {'new_name': 'main'})
            target = client.api('repos/' + TARGET)
    if target.get('full_name') != TARGET or target.get('private') is not False or target.get('default_branch') != 'main':
        raise PublishError('Preserve and review the existing docs repository identity/visibility/branch.')
    if not apply:
        return result
    pages = client.api('repos/' + TARGET + '/pages', missing=True)
    if pages is None:
        client.api('repos/' + TARGET + '/pages', 'POST', {'build_type': 'workflow'})
    elif pages.get('build_type') != 'workflow':
        client.api('repos/' + TARGET + '/pages', 'PUT', {'build_type': 'workflow'})
    base = client.api('repos/' + TARGET + '/git/ref/heads/main')['object']['sha']
    tree = client.api('repos/' + TARGET + '/git/commits/' + base)['tree']['sha']
    prior = client.api('repos/' + TARGET + '/git/trees/' + tree + '?recursive=1')
    if prior.get('truncated'):
        raise PublishError('Docs tree exceeds the bounded publication budget.')
    old = {row['path']: row['sha'] for row in prior['tree'] if row['type'] == 'blob'}
    changes = []
    for path, data in entries.items():
        blob_sha = hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()
        if old.get(path) == blob_sha:
            continue
        try:
            record = {'content': data.decode('utf-8')}
        except UnicodeDecodeError:
            blob = client.api('repos/' + TARGET + '/git/blobs', 'POST', {'encoding': 'base64', 'content': base64.b64encode(data).decode('ascii')})
            record = {'sha': blob['sha']}
        changes.append({'path': path, 'mode': '100644', 'type': 'blob', **record})
    if changes:
        built = client.api('repos/' + TARGET + '/git/trees', 'POST', {'base_tree': tree, 'tree': changes})
        created = client.api('repos/' + TARGET + '/git/commits', 'POST', {'tree': built['sha'], 'parents': [base],
                             'message': 'Publish verified CTR2WUP documentation ' + report['version']})
        client.api('repos/' + TARGET + '/git/refs/heads/main', 'PATCH', {'sha': created['sha'], 'force': False})
        result['commit'] = created['sha']
    result['changed_files'] = len(changes)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--source-commit', required=True)
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--create', action='store_true')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    try:
        result = publish(args.input, args.source_commit, GitHub(), args.apply, args.create)
    except (PublishError, OSError, ValueError, KeyError, TypeError, subprocess.TimeoutExpired, zipfile.BadZipFile) as error:
        result = {'ok': False, 'error': str(error) if isinstance(error, PublishError) else 'Documentation publication could not be verified; review Actions receipt.'}
    if args.output:
        args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))
    return 0 if result['ok'] else 2


if __name__ == '__main__':
    raise SystemExit(main())
