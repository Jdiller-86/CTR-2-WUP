#!/usr/bin/env python3
"""Package the source-bound original Audio Streams finite-playback hardware round."""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import subprocess
import tempfile
import wave
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('audio_round_release', ROOT/'tools/package-development-release.py')
release = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release)
VERSION = '0.27.0'
APPLICATION = 'native-audio-streams'
NAMES = ('ctr2wup-audio-stream-check.rpx', 'ctr2wup-audio-stream-check.wuhb')
MAX_TOTAL = 24 * 1024 * 1024
REFERENCE_FLAGS = ('original_inputs_only', 'independent_reference', 'exact_rational_adpcm',
                   'exact_linear_resample_q15_mix', 'exact_voice_position_phase_controls_counters')
REFERENCE_RATES = {8000, 11025, 22050, 32000, 44100, 48000, 64000, 88200, 96000}
SCENES = {'left', 'right', 'mix', 'rate', 'pause', 'seek', 'repeat'}


def identity() -> str:
    commit = release.git(ROOT, 'rev-parse', 'HEAD')
    if not re.fullmatch('[0-9a-f]{40}', commit) or release.git(ROOT, 'status', '--porcelain'):
        raise ValueError('Audio delivery requires a clean committed source')
    return commit


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def leaf_name(name: str, suffix: str) -> bool:
    return (isinstance(name, str) and re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{0,100}', name) is not None
            and name.endswith(suffix) and '..' not in name)


def exact_string_set(value, expected: set) -> bool:
    return (isinstance(value, list) and len(value) == len(expected)
            and all(type(item) in (str, int) for item in value) and set(value) == expected)


def json_bytes(path: Path) -> tuple[dict, bytes]:
    data = release.regular(path, 2*1024*1024).read_bytes()
    value = json.loads(data)
    if not isinstance(value, dict):
        raise ValueError('Expected an evidence JSON object')
    return value, data


def validate_inputs(app: Path, reference_report: Path, sdk_receipt: Path) -> tuple[str, dict[str, bytes]]:
    commit = identity()
    native, native_bytes = json_bytes(app/'build-report.json')
    if (native.get('ok') is not True or native.get('target') != 'audio-stream-check'
            or native.get('profile') != APPLICATION or native.get('runtime_version') != VERSION
            or native.get('source_commit') != commit or native.get('source_dirty') is not False
            or native.get('input_identity') is not None):
        raise ValueError('Require the clean source-matched original Audio Streams native build report')
    records = release.check_records(native.get('artifacts'), NAMES, app)
    reference, reference_bytes = json_bytes(reference_report)
    if (reference.get('ok') is not True or reference.get('version') != VERSION
            or reference.get('source_commit') != commit or reference.get('source_dirty') is not False
            or any(reference.get(name) is not True for name in REFERENCE_FLAGS)
            or type(reference.get('cases')) is not int or reference['cases'] < 240
            or type(reference.get('output_frames')) is not int or reference['output_frames'] <= 0
            or not exact_string_set(reference.get('sample_rates'), REFERENCE_RATES)
            or not exact_string_set(reference.get('modes'), {'normal', 'sanitized'})
            or any(not isinstance(reference.get(name), str) or not re.fullmatch('[0-9a-f]{64}', reference[name])
                   for name in ('original_vectors_sha256', 'pcm_results_sha256'))):
        raise ValueError('Require clean source-matched independent audio decode/resample/mix/controls evidence in normal and sanitized modes')
    sdk_bytes = release.regular(sdk_receipt, 4096).read_bytes()
    if sdk_bytes.decode('utf-8').strip() != release.SDK_IMAGE:
        raise ValueError('Require the pinned SDK image receipt')
    entries = {'build-reports/native-audio-streams.json': native_bytes,
               'build-reports/independent-audio-reference.json': reference_bytes,
               'sdk-image-digest.txt': sdk_bytes}
    for name in NAMES:
        path = app/name
        release.validate_binary(path)
        data = release.regular(path).read_bytes()
        expected = next(row for row in records if row['name'] == name)
        if len(data) != expected['size'] or digest(data) != expected['sha256']:
            raise ValueError('Native artifact changed after receipt verification')
        entries[('sd-card/wiiu/apps/' if name.endswith('.wuhb') else 'rpx/')+name] = data
    scenes = reference.get('guided_scenes')
    if (not isinstance(scenes, list) or len(scenes) != len(SCENES)
            or any(not isinstance(row, dict) or not isinstance(row.get('id'), str) for row in scenes)
            or {row['id'] for row in scenes} != SCENES):
        raise ValueError('Require all seven distinct original guided Audio Streams scenes')
    metadata_path = reference_report.parent/'guided-scenes.json'
    metadata, metadata_bytes = json_bytes(metadata_path)
    if (metadata.get('version') != VERSION or metadata.get('output_rate') != 48000
            or metadata.get('source_commit') != commit or metadata.get('source_dirty') is not False
            or metadata.get('original_inputs_only') is not True or metadata.get('independent_reference') is not True
            or metadata.get('scenes') != scenes):
        raise ValueError('Guided scene metadata must match the exact source-bound independent report')
    entries['reference/guided-scenes.json'] = metadata_bytes
    for row in scenes:
        if (not leaf_name(row.get('wav_file'), '.wav') or row['wav_file'] != 'scene-'+row['id']+'-48k.wav'
                or any(not isinstance(row.get(name), str) or not re.fullmatch('[0-9a-f]{64}', row[name])
                       for name in ('wav_sha256', 'pcm_sha256'))
                or type(row.get('output_frames')) is not int or not 0 < row['output_frames'] <= 4*48000
                or row.get('native_pause_or_console_verified') is not False):
            raise ValueError('Reference WAVE filenames must be safe explicit local filenames with SHA-256 receipts')
        name = row['wav_file']
        target = 'reference/'+name
        if target in entries:
            raise ValueError('Duplicate reference WAVE filename')
        data = release.regular(reference_report.parent/name, 4*1024*1024).read_bytes()
        if digest(data) != row['wav_sha256']:
            raise ValueError('Original reference WAVE bytes differ from independent evidence')
        try:
            with wave.open(io.BytesIO(data), 'rb') as stream:
                if (stream.getnchannels() != 2 or stream.getsampwidth() != 2 or stream.getframerate() != 48000
                        or stream.getnframes() != row['output_frames'] or stream.getcomptype() != 'NONE'):
                    raise ValueError('Reference WAVE must be exact finite stereo PCM16 at 48000 Hz')
                pcm = stream.readframes(row['output_frames']+1)
        except (wave.Error, EOFError) as error:
            raise ValueError('Malformed reference WAVE') from error
        if len(pcm) != row['output_frames']*4 or digest(pcm) != row['pcm_sha256']:
            raise ValueError('Reference WAVE PCM length/hash differs from exact independent output')
        entries[target] = data
    return commit, entries


def sdk_entries(folder: Path) -> dict[str, bytes]:
    if folder.is_symlink() or not folder.is_dir():
        raise ValueError('Require the real SDK redistribution-notice directory')
    entries = {}
    # Only this explicitly selected notice directory is traversed. Application
    # and reference folders are never globbed into the public archive.
    def visit(directory):
        for path in sorted(directory.iterdir()):
            info = path.lstat()
            if stat.S_ISLNK(info.st_mode):
                raise ValueError('SDK notice symlinks cannot be published')
            if stat.S_ISDIR(info.st_mode):
                visit(path)
            elif stat.S_ISREG(info.st_mode):
                relative = path.relative_to(folder).as_posix()
                if (any(part.lower() in {'private', '.git', '.env', 'artifacts', '__pycache__'}
                        for part in PurePosixPath(relative).parts)
                        or PurePosixPath(relative).suffix.lower() in release.FORBIDDEN):
                    raise ValueError('Unexpected private/generated SDK notice path')
                entries['licenses/sdk/'+relative] = release.regular(path, 2*1024*1024).read_bytes()
            else:
                raise ValueError('SDK notices must be regular files')
            if len(entries) > 256 or sum(map(len, entries.values())) > 8*1024*1024:
                raise ValueError('SDK redistribution notices exceed bounded input limits')
    visit(folder)
    if not entries:
        raise ValueError('Require SDK redistribution notices')
    return entries


def checked_source(commit: str) -> bytes:
    data = subprocess.check_output(['git', 'archive', '--format=zip', '--prefix=CTR2WUP/', commit], cwd=ROOT)
    # Reuse the release source inventory/privacy guard, including original binary
    # fixture exceptions. A temporary file is not a public artifact.
    with tempfile.TemporaryDirectory(prefix='ctr2wup-audio-source-') as temporary:
        path = Path(temporary)/'source.zip'
        path.write_bytes(data)
        release.validate_zip(path, source=True)
    return data


def package(app: Path, reference_report: Path, sdk_receipt: Path, sdk_licenses: Path, output: Path) -> dict:
    commit, entries = validate_inputs(app, reference_report, sdk_receipt)
    output = Path(os.path.abspath(output))
    if any(path.is_symlink() for path in (output, *output.parents)):
        raise ValueError('Package output cannot traverse symlinks')
    if output.exists():
        raise FileExistsError('Preserve the existing Audio Streams package output')
    entries['source-and-notices/CTR2WUP-source-'+VERSION+'.zip'] = checked_source(commit)
    for target, path in {
        'collect-audio-results.py': ROOT/'tools/collect-audio-results.py',
        'LICENSE': ROOT/'LICENSE', 'THIRD_PARTY_NOTICES': ROOT/'THIRD_PARTY_NOTICES',
    }.items():
        entries[target] = release.regular(path, 4*1024*1024).read_bytes()
    entries['START-HERE.md'] = release.render_guide(ROOT/'tools/templates/audio-streams.md', VERSION, commit)
    entries.update(sdk_entries(sdk_licenses))
    if identity() != commit:
        raise ValueError('Source changed before publication')
    if sum(map(len, entries.values())) > MAX_TOTAL:
        raise ValueError('Audio Streams package exceeds 24 MiB')
    manifest = {'schema_version': 1, 'kind': 'ctr2wup-audio-round', 'version': VERSION,
                'source_commit': commit, 'source_dirty': False,
                'review_state': 'source-bound-hardware-evaluation',
                'applications': [APPLICATION], 'original_inputs_only': True,
                'console_verified': False, 'finite_prerender': True,
                'guest_dsp_accepted': False, 'installable_wup': False,
                'files': [{'path': name, 'size': len(data), 'sha256': digest(data)}
                          for name, data in sorted(entries.items())]}
    entries['manifest.json'] = (json.dumps(manifest, indent=2)+'\n').encode('utf-8')
    entries['SHA256SUMS.txt'] = ''.join(digest(data)+'  '+name+'\n' for name, data in sorted(entries.items())).encode('utf-8')
    output.mkdir(parents=True, exist_ok=False)
    stage = output/'stage'
    stage.mkdir()
    for name, data in entries.items():
        path = stage/name
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open('xb') as stream:
            stream.write(data)
    archive_path = output/('CTR2WUP-audio-round-'+VERSION+'.zip')
    with zipfile.ZipFile(archive_path, 'x', compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(entries.items()):
            archive.writestr(name, data)
    with zipfile.ZipFile(archive_path) as archive:
        if (archive.testzip() is not None or set(archive.namelist()) != set(entries)
                or len(archive.namelist()) != len(entries)
                or any(archive.read(name) != data for name, data in entries.items())):
            raise ValueError('Audio Streams package CRC/exact byte readback failed')
    report = {'ok': True, 'kind': 'ctr2wup-audio-round', 'version': VERSION,
              'source_commit': commit, 'source_dirty': False, 'archive': archive_path.name,
              'sha256': release.sha256(archive_path), 'bytes': archive_path.stat().st_size,
              'files': len(entries), 'zip_crc_verified': True, 'exact_byte_readback': True,
              'console_verified': False, 'finite_prerender': True, 'guest_dsp_accepted': False}
    (output/'package-report.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('app', 'reference-report', 'sdk-receipt', 'sdk-licenses', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(package(args.app, args.reference_report, args.sdk_receipt, args.sdk_licenses, args.output)))


if __name__ == '__main__':
    main()
