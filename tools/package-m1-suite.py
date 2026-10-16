#!/usr/bin/env python3
"""Build one focused install-check handoff, never a repeated native test bundle."""
import argparse
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import re
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
VERSION = '0.27.0'
NAMES = ('ctr2wup-install-check.rpx', 'ctr2wup-install-check.wuhb')
MAX_TOTAL = 24 * 1024 * 1024
INSTALL_FOLDER = 'CTR2WUP-M1'
EXPECTED_TITLE_ID = '0005000043543257'
EXPECTED_TITLE_NAME = 'CTR2WUP Install Check'


def helpers():
    spec = importlib.util.spec_from_file_location('m1_audio_package', ROOT/'tools/package-audio-round.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def safe_name(name):
    if not isinstance(name, str) or not name or '\\' in name:
        return False
    p = PurePosixPath(name)
    return (not p.is_absolute() and p.as_posix() == name
            and all(part not in ('', '.', '..') for part in p.parts)
            and not any(':' in part or part.lower() in ('private', '.git', '.env') for part in p.parts))


def native_receipt(native, commit, audio):
    """Bind the installed program to a clean build; do not ship another app."""
    release = audio.release
    report = release.read_json(native/'build-report.json')
    if (report.get('ok') is not True or report.get('target') != 'install-check'
            or report.get('profile') != 'native-install' or report.get('runtime_version') != VERSION
            or report.get('source_commit') != commit or report.get('source_dirty') is not False
            or report.get('input_identity') is not None):
        raise ValueError('Require a clean exact-source install-check native build')
    witness = report.get('install_check')
    if (not isinstance(witness, dict)
            or witness.get('storage_protocol') != 'snapshot-v2'
            or witness.get('session_protocol') != 'install-v2'
            or witness.get('production_snapshot_adapter') is not True
            or witness.get('cross_launch_recovery') is not True
            or witness.get('process_session_trace') is not True
            or witness.get('full_native_guide') is not False
            or witness.get('log_directory') != 'ctr2wup/install-check/'+commit
            or witness.get('console_verified') is not False
            or witness.get('full_milestone_acceptance') is not False):
        raise ValueError('Require the production snapshot-v2 install-check build witness')
    release.check_records(report.get('artifacts'), NAMES, native)
    for name in NAMES:
        release.validate_binary(native/name)
    return report


def provenance(audio_kit, sdk_licenses, sdk_receipt, commit, audio):
    """Keep redistribution material in support; no extra app, guide or WAVs."""
    entries = {
        'support/LICENSE': audio.release.regular(ROOT/'LICENSE', 4*1024*1024).read_bytes(),
        'support/THIRD_PARTY_NOTICES': audio.release.regular(ROOT/'THIRD_PARTY_NOTICES', 4*1024*1024).read_bytes(),
        'support/CTR2WUP-source-'+VERSION+'.zip': audio.checked_source(commit),
    }
    if audio_kit is None:
        if sdk_licenses is None or sdk_receipt is None:
            raise ValueError('Provide SDK redistribution notices and pinned receipt, directly or through the source-matched audio kit')
        receipt = audio.release.regular(sdk_receipt, 4096).read_bytes()
        if receipt.decode().strip() != audio.release.SDK_IMAGE:
            raise ValueError('Require the pinned SDK receipt')
        entries['support/sdk-image-digest.txt'] = receipt
        entries.update(('support/'+name, data) for name, data in audio.sdk_entries(sdk_licenses).items())
        return entries
    manifest = audio.release.read_json(audio_kit/'manifest.json')
    if (manifest.get('kind') != 'ctr2wup-audio-round' or manifest.get('version') != VERSION
            or manifest.get('source_commit') != commit or manifest.get('source_dirty') is not False
            or manifest.get('original_inputs_only') is not True):
        raise ValueError('Require the exact-source original Audio Streams provenance')
    records = manifest.get('files')
    if not isinstance(records, list) or not 10 <= len(records) <= 512:
        raise ValueError('Audio provenance inventory exceeds its bounds')
    allowed = {'START-HERE.md', 'collect-audio-results.py', 'LICENSE', 'THIRD_PARTY_NOTICES',
               'sdk-image-digest.txt', 'source-and-notices/CTR2WUP-source-'+VERSION+'.zip',
               'rpx/ctr2wup-audio-stream-check.rpx', 'sd-card/wiiu/apps/ctr2wup-audio-stream-check.wuhb',
               'build-reports/native-audio-streams.json', 'build-reports/independent-audio-reference.json',
               'reference/guided-scenes.json'}
    allowed.update('reference/scene-'+scene+'-48k.wav' for scene in audio.SCENES)
    seen = set()
    total = 0
    notices = 0
    for row in records:
        name = row.get('path') if isinstance(row, dict) else None
        if not safe_name(name) or name in seen:
            raise ValueError('Unsafe or repeated audio provenance path')
        seen.add(name)
        sdk_notice = name.startswith('licenses/sdk/') and PurePosixPath(name).suffix.lower() not in audio.release.FORBIDDEN
        if name not in allowed and not sdk_notice:
            raise ValueError('Unexpected audio provenance payload')
        data = audio.release.regular(audio_kit/name, MAX_TOTAL).read_bytes()
        if type(row.get('size')) is not int or len(data) != row['size'] or audio.digest(data) != row.get('sha256'):
            raise ValueError('Audio provenance differs from its receipt')
        total += len(data)
        if total > MAX_TOTAL:
            raise ValueError('Audio provenance exceeds 24 MiB')
        if name == 'source-and-notices/CTR2WUP-source-'+VERSION+'.zip' and data != entries['support/CTR2WUP-source-'+VERSION+'.zip']:
            raise ValueError('Audio source archive differs from the actual source commit')
        if sdk_notice:
            entries['support/'+name] = data
            notices += 1
        elif name.startswith('build-reports/') or name == 'sdk-image-digest.txt':
            entries['support/'+name] = data
    if not notices:
        raise ValueError('Audio provenance has no SDK redistribution notices')
    if entries.get('support/sdk-image-digest.txt', b'').decode().strip() != audio.release.SDK_IMAGE:
        raise ValueError('Require the pinned SDK receipt')
    return entries


def install_payload(installed, commit, native, audio):
    """Independently validate the exact, explicitly public original payload."""
    from host.package_integrity import verify_package

    report = audio.release.read_json(installed/'report.json')
    if (report.get('ok') is not True or report.get('kind') != 'ctr2wup-install-candidate'
            or report.get('source_commit') != commit or report.get('source_dirty') is not False
            or report.get('original_inputs_only') is not True or report.get('install_test_ready') is not True
            or report.get('profile') != 'wiiu_plaintext_homebrew'
            or report.get('requires_signature_patches') is not True
            or report.get('title_name') != EXPECTED_TITLE_NAME
            or not isinstance(report.get('title_id'), str)
            or report['title_id'] != EXPECTED_TITLE_ID):
        raise ValueError('Require a clean source-bound original install candidate')
    if report.get('rpx_sha256') != next(r['sha256'] for r in native['artifacts'] if r['name'].endswith('.rpx')):
        raise ValueError('Installed payload RPX differs from its clean native build')
    verified = verify_package(installed, None)
    files = verified.get('files')
    rpx = [row for row in files if isinstance(row, dict) and row.get('path') == 'code/ctr2wup.rpx'] if isinstance(files, list) else []
    if (verified.get('ok') is not True or verified.get('title_id') != report['title_id']
            or len(rpx) != 1 or rpx[0].get('sha256') != report['rpx_sha256']):
        raise ValueError('Independent install-package validation failed')
    records = report.get('files')
    if not isinstance(records, list) or not 4 <= len(records) <= 256:
        raise ValueError('Install payload inventory exceeds its bounds')
    entries = {}
    seen = set()
    for row in records:
        name = row.get('path') if isinstance(row, dict) else None
        if (not isinstance(name, str) or not re.fullmatch(r'(?:title\.(?:tmd|tik|cert)|[0-9a-f]{8}\.(?:app|h3))', name)
                or name in seen):
            raise ValueError('Unexpected or repeated install payload path')
        seen.add(name)
        data = audio.release.regular(installed/name, MAX_TOTAL).read_bytes()
        if type(row.get('size')) is not int or len(data) != row['size'] or audio.digest(data) != row.get('sha256'):
            raise ValueError('Install payload differs from its receipt')
        entries['sd-card/install/'+INSTALL_FOLDER+'/'+name] = data
        if sum(map(len, entries.values())) > MAX_TOTAL:
            raise ValueError('Install payload exceeds 24 MiB')
    if {p.name for p in installed.iterdir()} != seen | {'report.json'}:
        raise ValueError('Unexpected files in the public install candidate')
    entries['support/install-package.json'] = audio.release.regular(installed/'report.json').read_bytes()
    entries['support/independent-package-validation.json'] = (json.dumps(verified, indent=2)+'\n').encode()
    return report, entries


def package(native, audio_kit, output, installed=None, sdk_licenses=None, sdk_receipt=None):
    if installed is None:
        raise ValueError('No install payload; do not publish a kit that asks for accepted tests again')
    audio = helpers()
    commit = audio.identity()
    report = native_receipt(native, commit, audio)
    candidate, entries = install_payload(installed, commit, report, audio)
    entries.update(provenance(audio_kit, sdk_licenses, sdk_receipt, commit, audio))
    entries['support/build-report.json'] = audio.release.regular(native/'build-report.json').read_bytes()
    entries['START-HERE.md'] = audio.release.render_guide(
        ROOT/'tools/templates/install-check.md', VERSION, commit)
    log_directory = 'ctr2wup/install-check/'+commit
    m1 = dict(schema_version=1, kind='ctr2wup-m1-acceptance',
              acceptance_profile='m1-completion-v2',
              suite_id='m1-completion-'+VERSION+'-'+commit[:12], version=VERSION,
              source_commit=commit, source_dirty=False, native_app='native-install',
              original_inputs_only=True, console_verified=False, full_milestone_acceptance=False,
              scope='installed-title-and-production-snapshot-recovery', prior_native_evidence_carried_forward=True,
              recovery=dict(available=True, protocol='snapshot-v2'),
              install_check=dict(full_native_guide=False, storage_protocol='snapshot-v2',
                                 session_protocol='install-v2', log_directory=log_directory),
              package=dict(available=True, source_commit=commit, title_name=candidate['title_name'],
                           title_id=candidate['title_id'], install_test_ready=True,
                           requires_signature_patches=True, console_verified=False),
              files=[dict(path=n, size=len(d), sha256=audio.digest(d)) for n, d in sorted(entries.items())])
    manifest = (json.dumps(m1, indent=2)+'\n').encode()
    entries['support/manifest.json'] = manifest
    # FTP copies identity into the dedicated log directory; the user returns
    # that whole folder instead of finding a separate manifest or pairing files.
    entries['sd-card/'+log_directory+'/kit.json'] = manifest
    entries['support/SHA256SUMS.txt'] = ''.join(audio.digest(d)+'  '+n+'\n' for n, d in sorted(entries.items())).encode()
    output = Path(os.path.abspath(output))
    if output.exists() or any(p.is_symlink() for p in (output, *output.parents)):
        raise ValueError('Preserve existing output and avoid symlink destinations')
    if audio.identity() != commit or sum(map(len, entries.values())) > MAX_TOTAL:
        raise ValueError('Source changed or kit exceeded its size bound')
    stage = output/'stage'
    stage.mkdir(parents=True)
    for name, data in entries.items():
        dest = stage/name
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(data)
    archive = output/('CTR2WUP-install-check-'+VERSION+'.zip')
    with zipfile.ZipFile(archive, 'x', compression=zipfile.ZIP_DEFLATED) as z:
        for name, data in sorted(entries.items()):
            z.writestr(name, data)
    with zipfile.ZipFile(archive) as z:
        if z.testzip() or set(z.namelist()) != set(entries) or any(z.read(n) != d for n, d in entries.items()):
            raise ValueError('Install-check archive byte/CRC verification failed')
    result = dict(ok=True, version=VERSION, source_commit=commit, archive=archive.name,
                  bytes=archive.stat().st_size, sha256=audio.digest(archive.read_bytes()), files=len(entries),
                  console_verified=False, installed_wup_available=True, full_milestone_acceptance=False)
    (output/'package-report.json').write_text(json.dumps(result, indent=2)+'\n')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('native', 'installed', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    for name in ('audio-kit', 'sdk-licenses', 'sdk-receipt'):
        parser.add_argument('--'+name, type=Path)
    args = parser.parse_args()
    print(json.dumps(package(args.native, args.audio_kit, args.output, args.installed, args.sdk_licenses, args.sdk_receipt)))


if __name__ == '__main__':
    main()
