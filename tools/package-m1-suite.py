#!/usr/bin/env python3
"""Bundle exact-source M1 diagnostics; an unavailable WUP stays explicitly blocked."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import re
import zipfile

ROOT = Path(__file__).resolve().parents[1]
VERSION = '0.26.0'
NAMES = ('ctr2wup-m1-check.rpx', 'ctr2wup-m1-check.wuhb')
MAX_TOTAL = 24*1024*1024


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


def package(native, audio_kit, output):
    audio = helpers()
    commit = audio.identity()
    release = audio.release
    report = release.read_json(native/'build-report.json')
    if (report.get('ok') is not True or report.get('target') != 'm1-check'
            or report.get('profile') != 'native-m1' or report.get('runtime_version') != VERSION
            or report.get('source_commit') != commit or report.get('source_dirty') is not False
            or report.get('input_identity') is not None):
        raise ValueError('Require a clean exact-source M1 native build')
    release.check_records(report.get('artifacts'), NAMES, native)
    manifest = release.read_json(audio_kit/'manifest.json')
    if (manifest.get('kind') != 'ctr2wup-audio-round' or manifest.get('version') != VERSION
            or manifest.get('source_commit') != commit or manifest.get('source_dirty') is not False
            or manifest.get('original_inputs_only') is not True):
        raise ValueError('Require the exact-source original Audio Streams kit')
    records = manifest.get('files')
    if not isinstance(records, list) or not 10 <= len(records) <= 512:
        raise ValueError('Audio kit inventory exceeds its bounds')
    entries = {}
    for row in records:
        name = row.get('path') if isinstance(row, dict) else None
        if not safe_name(name) or name in entries or name in ('manifest.json', 'SHA256SUMS.txt'):
            raise ValueError('Unsafe or repeated audio kit path')
        data = release.regular(audio_kit/name, MAX_TOTAL).read_bytes()
        if type(row.get('size')) is not int or len(data) != row['size'] or audio.digest(data) != row.get('sha256'):
            raise ValueError('Audio kit payload differs from its receipt')
        entries[name] = data
        if sum(map(len, entries.values())) > MAX_TOTAL:
            raise ValueError('M1 suite exceeds 24 MiB')
    source_name = 'source-and-notices/CTR2WUP-source-'+VERSION+'.zip'
    if entries.get(source_name) != audio.checked_source(commit):
        raise ValueError('Audio source archive differs from the actual source commit')
    required = {'START-HERE.md', 'LICENSE', 'THIRD_PARTY_NOTICES',
                'sd-card/wiiu/apps/ctr2wup-audio-stream-check.wuhb',
                'rpx/ctr2wup-audio-stream-check.rpx', 'build-reports/native-audio-streams.json',
                'build-reports/independent-audio-reference.json', source_name}
    # Reports have stable semantic checks in the audio packager. Require the
    # core files independently here; all retained records are hashed above.
    missing = required-set(entries)
    if missing:
        raise ValueError('Audio kit is missing required source/build/native evidence: '+', '.join(sorted(missing)))
    native_audio=json.loads(entries['build-reports/native-audio-streams.json'])
    reference=json.loads(entries['build-reports/independent-audio-reference.json'])
    if (native_audio.get('ok') is not True or native_audio.get('source_commit') != commit
            or native_audio.get('source_dirty') is not False or native_audio.get('profile') != 'native-audio-streams'
            or reference.get('ok') is not True or reference.get('source_commit') != commit
            or reference.get('source_dirty') is not False
            or any(reference.get(k) is not True for k in audio.REFERENCE_FLAGS)
            or set(reference.get('modes',[])) != {'normal','sanitized'}
            or type(reference.get('cases')) is not int or reference['cases'] < 240
            or entries.get('sdk-image-digest.txt',b'').decode().strip() != release.SDK_IMAGE):
        raise ValueError('Audio build/reference/SDK receipts are incomplete or from another source')
    entries['AUDIO-STREAMS.md'] = entries.pop('START-HERE.md')+(
        '\n\nIn this combined M1 kit, the audio manifest is `audio-manifest.json`. '
        'Keep it with Audio Streams returns. If using the optional collector, '
        'select it with `--manifest audio-manifest.json`; manual FTP ZIP return remains valid.\n').encode()
    entries['audio-manifest.json'] = (json.dumps(manifest, indent=2)+'\n').encode()
    for name in NAMES:
        release.validate_binary(native/name)
        data = release.regular(native/name).read_bytes()
        record = next(r for r in report['artifacts'] if r['name'] == name)
        if audio.digest(data) != record['sha256']:
            raise ValueError('M1 native binary changed during packaging')
        entries[('sd-card/wiiu/apps/' if name.endswith('.wuhb') else 'rpx/')+name] = data
    entries['build-reports/m1-native.json'] = release.regular(native/'build-report.json').read_bytes()
    entries['START-HERE.md'] = audio.immutable_guide(release.regular(ROOT/'docs/m1-acceptance.md').read_bytes(), commit)
    entries['review-m1-results.py'] = release.regular(ROOT/'tools/review-m1-results.py').read_bytes()
    entries['KIT-STATUS.md'] = (
        '# M1 acceptance kit '+VERSION+'\n\nSource: `'+commit+'`\n\n'
        '- Ready: M1 Check full native guide, ten-minute stability, SD persistence/recovery, fresh Minus/HOME runs.\n'
        '- Included: optional Audio Streams listening/control test and reference WAVs.\n'
        '- Not available: an installable WUP. Skip the installed-title section of START-HERE and return the other results.\n'
        '- Full M1 remains open until a verified original WUP passes WUP Installer GX2 installation and two Menu launches/exits.\n\n'
        'Copy sd-card/ contents by FTP binary mode; open CTR2WUP M1 Check '+VERSION+'. '
        'Follow START-HERE.md. Keep manifest.json and both named TXT files for every launch. '
        'No PC command is needed.\n').encode()
    m1 = dict(schema_version=1, kind='ctr2wup-m1-acceptance',
              suite_id='m1-native-'+VERSION+'-'+commit[:12], version=VERSION,
              source_commit=commit, source_dirty=False, native_app='native-m1',
              original_inputs_only=True, console_verified=False,
              full_milestone_acceptance=False,
              recovery=dict(available=True, protocol='scratch-v1'),
              package=dict(available=False, blockers=[
                  'A real source-matched original WUP, local packaging material/provenance, collision review and WUP Installer GX2 acceptance are not yet available.']),
              files=[dict(path=n, size=len(d), sha256=audio.digest(d)) for n,d in sorted(entries.items())])
    entries['manifest.json'] = (json.dumps(m1, indent=2)+'\n').encode()
    entries['SHA256SUMS.txt'] = ''.join(audio.digest(d)+'  '+n+'\n' for n,d in sorted(entries.items())).encode()
    output = Path(os.path.abspath(output))
    if output.exists() or any(p.is_symlink() for p in (output, *output.parents)):
        raise ValueError('Preserve existing output and avoid symlink destinations')
    if audio.identity() != commit or sum(map(len, entries.values())) > MAX_TOTAL:
        raise ValueError('Source changed or suite exceeded its size bound')
    stage = output/'stage'
    stage.mkdir(parents=True)
    for name,data in entries.items():
        dest=stage/name;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data)
    archive=output/('CTR2WUP-M1-acceptance-'+VERSION+'.zip')
    with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED) as z:
        for name,data in sorted(entries.items()):z.writestr(name,data)
    with zipfile.ZipFile(archive) as z:
        if z.testzip() or set(z.namelist()) != set(entries) or any(z.read(n)!=d for n,d in entries.items()):
            raise ValueError('M1 archive byte/CRC verification failed')
    result=dict(ok=True,version=VERSION,source_commit=commit,archive=archive.name,
                bytes=archive.stat().st_size,sha256=audio.digest(archive.read_bytes()),files=len(entries),
                console_verified=False,installed_wup_available=False,full_milestone_acceptance=False)
    (output/'package-report.json').write_text(json.dumps(result,indent=2)+'\n')
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('native','audio-kit','output'):parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args()
    print(json.dumps(package(args.native,args.audio_kit,args.output)))


if __name__ == '__main__':main()
