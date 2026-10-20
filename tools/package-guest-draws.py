#!/usr/bin/env python3
"""Append a source-matched original draw diagnostic to a verified prerelease."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct
import zipfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('draw_release', ROOT / 'tools/package-development-release.py')
release = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release)
NAMES = ('ctr2wup-libctru-draw.rpx', 'ctr2wup-libctru-draw.wuhb')
GX2_NAMES = ('ctr2wup-gx2-guest.rpx', 'ctr2wup-gx2-guest.wuhb')
TEXTURED_NAMES = ('ctr2wup-textured-guest.rpx', 'ctr2wup-textured-guest.wuhb')
FRAMES = ('frame-0-top.ppm', 'frame-0-bottom.ppm')


def png_preview(rgb, width, height):
    """Losslessly preview the exact checked PPM pixels without image dependencies."""
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    rows = b''.join(b'\0' + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


def package(folder, report_path, application, sdk_receipt, backend='software'):
    if backend not in ('software', 'gx2', 'textured'):
        raise ValueError('Choose the explicit software, gx2 or textured backend')
    textured, gpu = backend == 'textured', backend != 'software'
    binary_names = {'software': NAMES, 'gx2': GX2_NAMES, 'textured': TEXTURED_NAMES}[backend]
    binary_folder = application / ('wiiu-gx2' if gpu else 'wiiu')
    manifest = release.read_json(folder / 'manifest.json')
    version, commit = manifest.get('version', ''), manifest.get('source_commit', '')
    if (not re.fullmatch(r'\d+\.\d+\.\d+', version) or not re.fullmatch('[0-9a-f]{40}', commit)
            or manifest.get('original_fixtures_only') is not True):
        raise ValueError('Require an original verified release identity')
    rows = manifest.get('artifacts', [])
    names = tuple(row.get('name', '') for row in rows)
    if (not names or len(set(names)) != len(names)
            or any(not name or Path(name).name != name or '\\' in name for name in names)):
        raise ValueError('Release artifacts must have unique local names')
    release.check_records(rows, names, folder)
    source_name = f'CTR2WUP-source-{version}.zip'
    if source_name not in names:
        raise ValueError('Require matching source and dependency notices')
    source = release.regular(folder / source_name)
    release.validate_zip(source, source=True)
    report = release.read_json(report_path)
    apps = report.get('applications', [])
    if (report.get('ok') is not True or report.get('version') != version
            or report.get('source_commit') != commit or report.get('source_dirty') is not False
            or report.get('libctru_pin') != release.LIBCTRU or len(apps) != 1
            or apps[0].get('application') != ('textured' if textured else 'draw')):
        raise ValueError('Require the clean source-matched original guest report')
    app = apps[0]
    guest = app.get('guest', {})
    if (guest.get('state') != 'exited' or guest.get('fault') != 'ok' or guest.get('fallback') != 0
            or ('CTR2WUP compiled textured PICA PASS' if textured else 'CTR2WUP compiled PICA draws PASS') not in guest.get('messages', [])
            or guest.get('pica_render') != ({'draws': 6, 'triangles': 6, 'fragments': 145770, 'shader_instructions': 72} if textured else {'draws': 4, 'triangles': 4, 'fragments': 97200, 'shader_instructions': 36})
            or (textured and 'CTR2WUP compiled D16 depth PASS' not in guest.get('messages', []))
            or guest.get('gx') != {'commands': 6, 'dma': 0, 'fills': 2, 'transfers': 2, 'copies': 0, 'cache': 0, 'pica': 2}
            or app.get('generation', {}).get('input_identity', {}).get('program_id') != '000400000ff00f0c'):
        raise ValueError('Require the exercised original guest and exact rendering counters')
    if gpu:
        shader_check = report.get('gx2_shader_validation', {})
        profile = {'backend': 'gx2', 'native_cross_build': True,
                   'software_frame_checks': True, 'gpu_hardware_verified': False}
        if textured:profile['textured'] = True
        if (app.get('gx2_guest') != profile
                or app.get('independent_frame_checks_backend') != 'software'
                or shader_check.get('ok') is not True
                or shader_check.get('source_commit') != commit
                or shader_check.get('source_dirty') is not False
                or shader_check.get('console_verified') is not False):
            raise ValueError('Require the distinct cross-built GX2 profile and portable shader validation; software pixels do not prove GPU output')
    release.check_records(app.get('wiiu_gx2_artifacts' if gpu else 'wiiu_artifacts'), binary_names, binary_folder)
    frames = app.get('frames', [])
    checks = app.get('independent_frame_checks', [])
    if (len(frames) != 2 or {r.get('name') for r in frames} != set(FRAMES)
            or len(checks) != 2 or {r.get('screen') for r in checks} != {'top', 'bottom'}):
        raise ValueError('Require both original frames and independent exact pixel checks')
    entries = {}
    for name in binary_names:
        path = release.regular(binary_folder / name)
        release.validate_binary(path)
        entries[('rpx/' if name.endswith('.rpx') else 'sd-card/wiiu/apps/') + name] = path.read_bytes()
    for screen, width in (('top', 400), ('bottom', 320)):
        name = f'frame-0-{screen}.ppm'
        path = release.regular(application / name, 1024 * 1024)
        data = path.read_bytes()
        header = f'P6\n{width} 240\n255\n'.encode()
        frame = next(r for r in frames if r['name'] == name)
        check = next(r for r in checks if r['screen'] == screen)
        if (not data.startswith(header) or len(data) != len(header) + width * 240 * 3
                or frame.get('sha256') != release.sha256(path) or check.get('sha256') != frame['sha256']
                or check.get('exact_match') is not True or check.get('width') != width
                or check.get('height') != 240 or check.get('pixels') != width * 240):
            raise ValueError('Original frame bytes differ from their exact checked receipt')
        entries['reference/' + name] = data
        if textured:
            entries[f'images/textured-guest-{screen}.png'] = png_preview(data[len(header):], width, 240)
        else:
            image = f'guest-draw-{screen}.png'
            entries['images/' + image] = (ROOT / 'docs/images' / image).read_bytes()
    sdk = release.regular(sdk_receipt, 4096)
    if sdk.read_text().strip() != release.SDK_IMAGE:
        raise ValueError('Require the pinned SDK receipt')
    guide = {'software': 'guest-draws.md', 'gx2': 'gx2-guest.md', 'textured': 'textured-guest.md'}[backend]
    entries.update({'START-HERE.md': release.render_guide(ROOT/'tools/templates'/guide, version, commit),
                    'build-reports/original-draws.json': report_path.read_bytes(),
                    'sdk-image-digest.txt': sdk.read_bytes(),
                    'source-and-notices/' + source_name: source.read_bytes(),
                    'LICENSE': (ROOT / 'LICENSE').read_bytes(),
                    'THIRD_PARTY_NOTICES': (ROOT / 'THIRD_PARTY_NOTICES').read_bytes()})
    receipt = {'render_backend': 'gx2' if gpu else 'software', 'textured': textured, 'native_cross_build': True,
               'gpu_hardware_verified': False, 'reference_frames_backend': 'software',
               'version': version, 'source_commit': commit, 'original_fixtures_only': True,
               'console_verified': False, 'installable_wup': False,
               'files': [{'path': n, 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
                         for n, data in sorted(entries.items())]}
    entries['manifest.json'] = (json.dumps(receipt, indent=2) + '\n').encode()
    entries['SHA256SUMS.txt'] = ''.join(hashlib.sha256(data).hexdigest() + '  ' + n + '\n'
                                       for n, data in sorted(entries.items())).encode()
    label = {'software': 'guest-draws', 'gx2': 'gx2-guest', 'textured': 'textured-guest'}[backend]
    name = f'CTR2WUP-{label}-{version}.zip'
    path = folder / name
    if path.exists() or name in names:
        raise ValueError('Preserve an existing Guest Draws, GX2 Guest or Textured Guest kit')
    with zipfile.ZipFile(path, 'x', compression=zipfile.ZIP_DEFLATED) as archive:
        for n, data in sorted(entries.items()):
            archive.writestr(n, data)
    with zipfile.ZipFile(path) as archive:
        if archive.testzip() or any(archive.read(n) != data for n, data in entries.items()):
            raise ValueError('Guest Draws kit failed byte/CRC verification')
    record = {'name': name, 'size': path.stat().st_size, 'sha256': release.sha256(path)}
    manifest['artifacts'].append(record)
    (folder / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    notes = folder / 'release-notes.md'
    scope = ('The GX2 profile has been cross-built; exact reference pixels were checked with the software renderer. GPU hardware acceptance is pending.'
             if gpu else 'The software-rendered original guest passed exact reference pixel checks.')
    title = {'software': 'Guest Draws', 'gx2': 'GX2 Guest', 'textured': 'Textured Guest'}[backend]
    notes.write_text(notes.read_text() + f'\n## Original {title}\n\n{name} includes the compiled original guest, reference images, source/SDK identity and notices. {scope}\n')
    records = manifest['artifacts'] + [{'name': 'manifest.json'}, {'name': 'release-notes.md'}]
    (folder / 'SHA256SUMS.txt').write_text(''.join(release.sha256(folder / row['name']) + '  ' + row['name'] + '\n'
                                               for row in sorted(records, key=lambda row: row['name'])))
    return {'ok': True, 'source_commit': commit, **record}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('release-dir', 'draw-report', 'application-dir', 'sdk-receipt'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--backend', choices=('software', 'gx2', 'textured'), default='software',
                        help='Select a matching, explicitly identified native profile')
    args = parser.parse_args()
    try:
        result = package(args.release_dir, args.draw_report, args.application_dir, args.sdk_receipt, args.backend)
    except (ValueError, OSError, KeyError, TypeError, zipfile.BadZipFile) as error:
        parser.exit(1, f'Guest Draws packaging failed: {error}\n')
    print(json.dumps(result))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
