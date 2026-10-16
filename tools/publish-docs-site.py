#!/usr/bin/env python3
"""Stage only the verified documentation archive for GitHub Pages deployment."""
import argparse
from datetime import datetime, timezone
import os
import tempfile
import hashlib
import importlib.util
import json
from pathlib import Path, PurePosixPath
from urllib.parse import urlsplit
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



def public_project_snapshot(path, commit):
    """Keep only the public issue snapshot contract; unavailable data is explicit."""
    base = {'schema_version': 2, 'project_url': 'https://github.com/users/Jdiller-86/projects/2',
            'repository': SOURCE, 'source_commit': commit, 'stale_after_seconds': 7200,
            'generated_at': datetime.now(timezone.utc).isoformat(timespec='seconds').replace('+00:00', 'Z')}
    unavailable = {**base, 'state': 'unavailable', 'error_code': 'project_unavailable',
                   'milestones': [], 'items': []}

    def bounded_text(value, limit):
        if not isinstance(value, str) or len(value) > limit:
            raise ValueError('Invalid public text')
        return value

    def number(value, maximum=1000000):
        if type(value) is not int or not 0 < value <= maximum:
            raise ValueError('Invalid public issue reference')
        return value

    def values(value, maximum=2000):
        if not isinstance(value, list) or len(value) > maximum:
            raise ValueError('Invalid public list')
        return value

    def status(value):
        if value not in ('Planned', 'In progress', 'Awaiting evidence', 'Blocked',
                         'Ready for review', 'Done', 'Closed', 'Accepted', 'Unassigned', 'Other'):
            raise ValueError('Invalid public status')
        return value

    def evidence(value):
        url = bounded_text(value['url'], 2000)
        parsed = urlsplit(url)
        if (parsed.scheme != 'https' or parsed.username or parsed.password
                or not ((parsed.netloc == 'github.com' and parsed.path.startswith('/' + SOURCE + '/'))
                        or (parsed.netloc == 'jdiller-86.github.io' and parsed.path.startswith('/CTR-2-WUP/')))):
            raise ValueError('Invalid public evidence link')
        return {'text': bounded_text(value['text'], 300), 'url': url}

    try:
        data = json.loads(docs.read_file(path, 2 * 1024 * 1024))
        if not isinstance(data, dict):
            raise ValueError('Invalid public snapshot')
        if any(data.get(key) != value for key, value in base.items() if key != 'generated_at'):
            raise ValueError('Invalid public snapshot identity')
        generated = datetime.fromisoformat(bounded_text(data['generated_at'], 100).replace('Z', '+00:00'))
        if generated.tzinfo is None or (generated - datetime.now(timezone.utc)).total_seconds() > 300:
            raise ValueError('Invalid public snapshot timestamp')
        base['generated_at'] = data['generated_at']
        if data.get('state') == 'unavailable':
            return {**unavailable, 'generated_at': base['generated_at']}
        if data.get('state') != 'available':
            raise ValueError('Invalid public snapshot state')
        items, ids = [], set()
        for row in values(data['items']):
            identifier = number(row['number'])
            if identifier in ids or row['url'] != f'https://github.com/{SOURCE}/issues/{identifier}':
                raise ValueError('Invalid public issue identity')
            ids.add(identifier)
            milestone = None if row['milestone'] is None else number(row['milestone'], 7)
            parent = None if row['parent'] is None else number(row['parent'])
            if row['state'] not in ('OPEN', 'CLOSED') or type(row['archived']) is not bool:
                raise ValueError('Invalid public issue state')
            acceptance = row['acceptance']
            done, total = acceptance['done'], acceptance['total']
            if type(done) is not int or type(total) is not int or not 0 <= done <= total <= 10000:
                raise ValueError('Invalid public acceptance counts')
            work_type = row['work_type']
            if work_type not in ('Milestone', 'Task', 'Feature'):
                raise ValueError('Invalid public work type')
            items.append({'number': identifier, 'title': bounded_text(row['title'], 300),
                'url': row['url'], 'state': row['state'], 'status': status(row['status']),
                'milestone': milestone, 'parent': parent,
                'labels': [bounded_text(label, 100) for label in values(row['labels'], 100)],
                'blocked_by': [number(identifier) for identifier in values(row['blocked_by'])],
                'summary': bounded_text(row['summary'], 3000), 'acceptance': {'done': done, 'total': total},
                'evidence': [evidence(value) for value in values(row['evidence'], 100)],
                'work_type': work_type, 'archived': row['archived'],
                'waiting': [value for value in values(row['waiting'], 10)
                            if value in ('Console testing', 'Owned game input', 'Windows testing')]})
        milestones, positions = [], set()
        gates = {item['number']: item for item in items if item['number'] in range(1, 8)}
        for row in values(data['milestones'], 7):
            identifier = number(row['number'], 7)
            if identifier in positions or row['url'] != f'https://github.com/{SOURCE}/issues/{identifier}':
                raise ValueError('Invalid public milestone identity')
            positions.add(identifier)
            gate = gates.get(identifier)
            if (not gate or type(row['accepted']) is not bool
                    or row['accepted'] != (row['status'] == 'Accepted' and gate['state'] == 'CLOSED')
                    or row['status'] != gate['status']):
                raise ValueError('Invalid milestone acceptance')
            milestones.append({'number': identifier, 'title': bounded_text(row['title'], 180),
                'url': row['url'], 'status': status(row['status']), 'accepted': row['accepted'],
                'issues': [number(value) for value in values(row['issues'])],
                'depends_on': [number(value, 7) for value in values(row['depends_on'], 7)]})
        if positions != set(range(1, 8)):
            raise ValueError('Seven public milestone gates are required')
        return {**base, 'state': 'available', 'milestones': milestones, 'items': items}
    except (OSError, ValueError, KeyError, TypeError, RecursionError):
        return unavailable


def stage_progress(output, path, commit):
    """The live graph is online-only and never enters the offline docs bundle."""
    progress = output / 'progress'
    progress.mkdir()
    dashboard = docs.read_file(ROOT / 'tools/project-dashboard.html', 128 * 1024)
    snapshot = public_project_snapshot(path, commit)
    (progress / 'index.html').write_bytes(dashboard)
    (progress / 'progress.json').write_text(json.dumps(snapshot, indent=2) + '\n', encoding='utf-8')
    return snapshot['state']


def stage(folder, commit, output, project_snapshot=None):
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
    project_state = stage_progress(output, project_snapshot, commit) if project_snapshot is not None else None
    return {'ok': True, 'source_commit': commit, 'stage': str(output),
            'project_snapshot_state': project_state,
            'documentation_only': True, 'pages_deployment_verified': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--source-commit', required=True)
    parser.add_argument('--stage', type=Path, required=True)
    parser.add_argument('--project-snapshot', type=Path, help='Optional online-only public Project snapshot')
    args = parser.parse_args()
    try:
        result = stage(args.input, args.source_commit, args.stage, args.project_snapshot)
    except (PublishError, OSError, ValueError, KeyError, TypeError, zipfile.BadZipFile):
        parser.exit(2, 'Documentation archive/staging validation failed.\n')
    print(json.dumps(result))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
