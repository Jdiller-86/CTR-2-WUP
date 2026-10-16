#!/usr/bin/env python3
"""Add the verified two-app audio diagnostic to a checked development release."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from host import __version__
spec = importlib.util.spec_from_file_location('release_package', ROOT / 'tools/package-development-release.py')
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)
AUDIO_NAMES = ('ctr2wup-audio-check.rpx', 'ctr2wup-audio-check.wuhb')


def package(folder, audio, transfer_report, transfer_dir):
    manifest = release.read_json(folder / 'manifest.json')
    version = manifest.get('version')
    if (version != __version__ or manifest.get('original_fixtures_only') is not True
            or not re.fullmatch('[0-9a-f]{40}', manifest.get('source_commit', ''))):
        raise ValueError('Require the current verified original development release')
    rows = manifest.get('artifacts')
    if (not isinstance(rows, list) or any(not isinstance(row, dict) or not isinstance(row.get('name'), str)
            or Path(row['name']).name != row['name'] or '\\' in row['name'] for row in rows)):
        raise ValueError('Release artifact names must be regular local filenames')
    names = tuple(row['name'] for row in rows)
    if len(set(names)) != len(names):
        raise ValueError('Release artifact names must be unique')
    release.check_records(rows, names, folder)
    source_name = f'CTR2WUP-source-{version}.zip'
    source_rows = [row for row in manifest['artifacts'] if row.get('name') == source_name]
    source = release.regular(folder / source_name)
    if len(source_rows) != 1 or release.sha256(source) != source_rows[0].get('sha256'):
        raise ValueError('Matching source archive differs from its verified receipt')
    release.validate_zip(source, source=True)
    native = release.read_json(audio / 'build-report.json')
    if (native.get('ok') is not True or native.get('runtime_version') != version
            or native.get('target') != 'audio-check' or native.get('profile') != 'native-audio-check'
            or native.get('input_identity') is not None):
        raise ValueError('Require the current original native Audio Check')
    release.check_records(native['artifacts'], AUDIO_NAMES, audio)
    transfer = release.read_json(transfer_report)
    apps = transfer.get('applications', [])
    if (transfer.get('ok') is not True or transfer.get('version') != version
            or transfer.get('libctru_pin') != release.LIBCTRU or len(apps) != 1
            or apps[0].get('application') != 'transfer' or apps[0].get('guest', {}).get('state') != 'exited'
            or apps[0].get('guest', {}).get('fault') != 'ok'
            or apps[0].get('guest', {}).get('fallback') != 0
            or apps[0].get('generation', {}).get('input_identity', {}).get('program_id') != '000400000ff00f0c'):
        raise ValueError('Require the checked original compiled GX application')
    release.check_records(apps[0]['wiiu_artifacts'], release.TRANSFER_NAMES, transfer_dir)
    name = f'CTR2WUP-launch-audio-tests-{version}.zip'
    path = folder / name
    if path.exists() or any(row.get('name') == name for row in manifest['artifacts']):
        raise ValueError('Preserve an existing audio kit; choose a fresh release directory')
    entries = {}
    for directory, names in ((audio, AUDIO_NAMES), (transfer_dir, release.TRANSFER_NAMES)):
        for artifact in names:
            file = release.regular(directory / artifact)
            release.validate_binary(file)
            target = ('rpx/' if file.suffix == '.rpx' else 'sd-card/wiiu/apps/') + artifact
            entries[target] = file.read_bytes()
    entries.update({'START-HERE.md': release.render_guide(ROOT/'tools/templates/audio-check.md', version, manifest['source_commit']),
                    'build-reports/audio-check.json': (audio / 'build-report.json').read_bytes(),
                    'build-reports/compiled-gx.json': transfer_report.read_bytes(),
                    'source-and-notices/' + source_name: source.read_bytes(),
                    'LICENSE': (ROOT / 'LICENSE').read_bytes(),
                    'THIRD_PARTY_NOTICES': (ROOT / 'THIRD_PARTY_NOTICES').read_bytes()})
    receipt = {'version': version, 'source_commit': manifest['source_commit'], 'apps': 2,
               'original_fixtures_only': True, 'console_verified': False, 'installable_wup': False,
               'files': [{'path': n, 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
                         for n, data in sorted(entries.items())]}
    entries['manifest.json'] = (json.dumps(receipt, indent=2) + '\n').encode()
    entries['SHA256SUMS.txt'] = ''.join(hashlib.sha256(data).hexdigest() + '  ' + n + '\n'
                                        for n, data in sorted(entries.items())).encode()
    with zipfile.ZipFile(path, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for n, data in sorted(entries.items()):
            archive.writestr(n, data)
    with zipfile.ZipFile(path) as archive:
        if archive.testzip() or any(archive.read(n) != data for n, data in entries.items()):
            raise ValueError('Audio kit CRC/byte verification failed')
    record = {'name': name, 'size': path.stat().st_size, 'sha256': release.sha256(path)}
    manifest['artifacts'].append(record)
    (folder / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    notes = folder / 'release-notes.md'
    notes.write_text(notes.read_text() + f'\n{name}: optional Audio Check and GX Transfers diagnostics; follow the included START-HERE.md only when a changed audio/lifecycle path needs verification.\n')
    records = manifest['artifacts'] + [{'name': 'manifest.json'}, {'name': 'release-notes.md'}]
    (folder / 'SHA256SUMS.txt').write_text(''.join(release.sha256(folder / row['name']) + '  ' + row['name'] + '\n'
                                               for row in sorted(records, key=lambda row: row['name'])))
    return {'ok': True, 'source_commit': manifest['source_commit'], **record}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('release-dir', 'audio-check-dir', 'transfer-report', 'transfer-dir'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    try:
        result = package(args.release_dir, args.audio_check_dir, args.transfer_report, args.transfer_dir)
    except (ValueError, OSError, KeyError, TypeError, zipfile.BadZipFile) as error:
        parser.exit(1, f'Audio kit packaging failed: {error}\n')
    print(json.dumps(result))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
