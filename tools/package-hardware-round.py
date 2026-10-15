#!/usr/bin/env python3
"""Stage two source-matched original hardware diagnostics and a checked ZIP."""
import argparse
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('hardware_release', ROOT/'tools/package-development-release.py')
release = importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(release)
SCENE = ('ctr2wup-scene-check.rpx', 'ctr2wup-scene-check.wuhb')
DRAW = ('ctr2wup-libctru-draw.rpx', 'ctr2wup-libctru-draw.wuhb')
VERSION = '0.25.0'


def identity():
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    dirty = subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True)
    if dirty:raise ValueError('Hardware delivery requires a clean committed source')
    return commit


def validate_inputs(scene, draw_report, draw_app, sdk_receipt):
    commit = identity()
    native = release.read_json(scene/'build-report.json')
    draw = release.read_json(draw_report)
    if (native.get('ok') is not True or native.get('runtime_version') != VERSION
        or native.get('target') != 'scene-check' or native.get('source_commit') != commit
        or native.get('source_dirty') is not False):
        raise ValueError('Require the clean source-matched arithmetic Scene Check')
    release.check_records(native.get('artifacts'), SCENE, scene)
    apps = draw.get('applications', [])
    if (draw.get('ok') is not True or draw.get('version') != VERSION
        or draw.get('source_commit') != commit or draw.get('source_dirty') is not False
        or draw.get('libctru_pin') != release.LIBCTRU or len(apps) != 1 or apps[0].get('application') != 'draw'):
        raise ValueError('Require the clean source-matched original compiled draw report')
    app = apps[0];guest = app.get('guest', {})
    if (guest.get('state') != 'exited' or guest.get('fault') != 'ok' or guest.get('fallback') != 0
        or 'CTR2WUP compiled PICA draws PASS' not in guest.get('messages', [])
        or guest.get('pica_render') != {'draws': 4, 'triangles': 4, 'fragments': 97200, 'shader_instructions': 36}
        or app.get('generation', {}).get('input_identity', {}).get('program_id') != '000400000ff00f0c'):
        raise ValueError('Original draw own PASS/exit/counters/identity are incomplete')
    release.check_records(app.get('wiiu_artifacts'), DRAW, draw_app/'wiiu')
    if release.regular(sdk_receipt, 4096).read_text().strip() != release.SDK_IMAGE:
        raise ValueError('Require the pinned SDK receipt')
    for names, folder in ((SCENE, scene), (DRAW, draw_app/'wiiu')):
        for name in names:release.validate_binary(folder/name)
    entries = {}
    for names, folder in ((SCENE, scene), (DRAW, draw_app/'wiiu')):
        for name in names:
            entries[('sd-card/wiiu/apps/' if name.endswith('.wuhb') else 'rpx/')+name] = (folder/name).read_bytes()
    for screen, width in (('top', 400), ('bottom', 320)):
        name = f'frame-0-{screen}.ppm';path = draw_app/name;data = release.regular(path, 1024*1024).read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        rows = app.get('independent_frame_checks', [])
        checks = [r for r in rows if r.get('screen') == screen]
        if (len(checks) != 1 or checks[0].get('exact_match') is not True
            or checks[0].get('sha256') != digest or checks[0].get('width') != width
            or checks[0].get('height') != 240 or checks[0].get('pixels') != width*240
            or data[:len(f'P6\n{width} 240\n255\n')] != f'P6\n{width} 240\n255\n'.encode()
            or len(data) != len(f'P6\n{width} 240\n255\n')+width*240*3):
            raise ValueError('Require both independent exact original guest frames')
        entries['reference/'+name] = data
    return commit, entries


def package(scene, draw_report, draw_app, sdk_receipt, sdk_licenses, output):
    commit, entries = validate_inputs(scene, draw_report, draw_app, sdk_receipt)
    if output.exists():raise ValueError('Preserve an existing hardware package output')
    source = subprocess.check_output(['git', 'archive', '--format=zip', '--prefix=CTR2WUP/', 'HEAD'], cwd=ROOT)
    with zipfile.ZipFile(io.BytesIO(source)) as archive:
        if archive.testzip() or any(n.startswith('CTR2WUP/private/') for n in archive.namelist()):
            raise ValueError('Source archive failed privacy/CRC checks')
    entries['source-and-notices/CTR2WUP-source-'+VERSION+'.zip'] = source
    for target, path in {
        'START-HERE.md': ROOT/'docs/console-retest.md',
        'collect-hardware-results.py': ROOT/'tools/collect-hardware-results.py',
        'collect-hardware-results.cmd': ROOT/'tools/collect-hardware-results.cmd',
        'LICENSE': ROOT/'LICENSE', 'THIRD_PARTY_NOTICES': ROOT/'THIRD_PARTY_NOTICES',
        'licenses/libctru.txt': ROOT/'docs/licenses/libctru.txt',
        'reference/arithmetic-scene.png': ROOT/'docs/images/software-scene-v022.png',
        'build-reports/arithmetic-scene.json': scene/'build-report.json',
        'build-reports/compiled-draw.json': draw_report,
        'sdk-image-digest.txt': sdk_receipt,
    }.items():entries[target] = release.regular(path).read_bytes()
    # START-HERE is at the artifact root; its repository-relative links must
    # point to this immutable source instead of missing sibling documents.
    guide = entries['START-HERE.md'].decode('utf-8')
    for name in ('console-evidence.md', 'gsp-display.md#native-display-cache'):
        guide = guide.replace(']('+name+')', '](https://github.com/Jdiller-86/CTR-2-WUP/blob/'+commit+'/docs/'+name+')')
    entries['START-HERE.md'] = guide.encode('utf-8')
    licenses = sorted(p for p in sdk_licenses.rglob('*') if p.is_file())
    if not licenses:raise ValueError('Require SDK redistribution notices')
    for path in licenses:
        entries['licenses/sdk/'+path.relative_to(sdk_licenses).as_posix()] = release.regular(path, 2*1024*1024).read_bytes()
    if identity() != commit:raise ValueError('Source changed before publication')
    if sum(map(len, entries.values())) > 24*1024*1024:raise ValueError('Hardware kit exceeds its size budget')
    manifest = {'schema_version': 1, 'kind': 'ctr2wup-hardware-round', 'version': VERSION,
                'source_commit': commit, 'source_dirty': False, 'review_state': 'premerge-hardware-evaluation',
                'original_inputs_only': True, 'console_verified': False, 'installable_wup': False,
                'applications': ['arithmetic-scene', 'compiled-pica-draws'],
                'files': [{'path': n, 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
                          for n, data in sorted(entries.items())]}
    entries['manifest.json'] = (json.dumps(manifest, indent=2)+'\n').encode()
    entries['SHA256SUMS.txt'] = ''.join(hashlib.sha256(data).hexdigest()+'  '+n+'\n' for n, data in sorted(entries.items())).encode()
    output.mkdir(parents=True);stage = output/'stage';stage.mkdir()
    for name, data in entries.items():
        path = stage/name;path.parent.mkdir(parents=True, exist_ok=True);path.write_bytes(data)
    release.validate_zip(stage/('source-and-notices/CTR2WUP-source-'+VERSION+'.zip'), source=True)
    archive_path = output/('CTR2WUP-hardware-round-'+VERSION+'.zip')
    with zipfile.ZipFile(archive_path, 'x', compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(entries.items()):archive.writestr(name, data)
    with zipfile.ZipFile(archive_path) as archive:
        if archive.testzip() or any(archive.read(n) != data for n, data in entries.items()):
            raise ValueError('Hardware kit CRC/byte readback failed')
    report = {'ok': True, 'version': VERSION, 'source_commit': commit,
              'archive': archive_path.name, 'sha256': release.sha256(archive_path),
              'bytes': archive_path.stat().st_size, 'files': len(entries), 'console_verified': False}
    (output/'package-report.json').write_text(json.dumps(report, indent=2)+'\n')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('scene', 'draw-report', 'draw-app', 'sdk-receipt', 'sdk-licenses', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(package(args.scene, args.draw_report, args.draw_app, args.sdk_receipt, args.sdk_licenses, args.output)))


if __name__ == '__main__':main()
