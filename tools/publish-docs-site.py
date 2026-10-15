#!/usr/bin/env python3
"""Stage only the verified documentation archive for GitHub Pages deployment."""
import argparse
import os
import tempfile
import hashlib
import importlib.util
import json
from pathlib import Path, PurePosixPath
import re
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = 'Jdiller-86/CTR-2-WUP'
TARGET = SOURCE
spec = importlib.util.spec_from_file_location('docs_package', ROOT / 'tools/package-docs-site.py')
docs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(docs)


class PublishError(RuntimeError):
    pass


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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--source-commit', required=True)
    parser.add_argument('--stage', type=Path, required=True)
    args = parser.parse_args()
    try:
        result = stage(args.input, args.source_commit, args.stage)
    except (PublishError, OSError, ValueError, KeyError, TypeError, zipfile.BadZipFile):
        parser.exit(2, 'Documentation archive/staging validation failed.\n')
    print(json.dumps(result))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
