#!/usr/bin/env python3
"""Execute original MOV/ADD/MUL scenes, compare analytic pixels and save evidence.

Optional pinned nihstro comparison checks wire encoding only. It is not a GPU
or float24 arithmetic reference. No external code enters the runtime.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import struct
import subprocess
import sys
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from host import __version__
PIN = 'f4d8659decbfe5d234f04134b5002b82dc515a44'


def png(rgba):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    # Owned scene rows start at the bottom; PNG scanlines start at the top.
    scanlines = b''.join(b'\0' + rgba[y*256:(y+1)*256] for y in reversed(range(64)))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 64, 64, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(scanlines, 9)) + chunk(b'IEND', b''))


def golden():
    # Closed-form regions of the fixture square, independent of edge evaluation.
    return b''.join(bytes((255, 0, 0, 255) if x+y < 63 else (0, 255, 0, 255))
                    if 8 <= x < 56 and 8 <= y < 56 else bytes((16, 32, 48, 255))
                    for y in range(64) for x in range(64))


def reference(directory, output, cxx):
    revision = subprocess.check_output(['git', '-C', str(directory), 'rev-parse', 'HEAD'], text=True).strip()
    dirty = subprocess.check_output(['git', '-C', str(directory), 'status', '--porcelain'], text=True)
    if revision != PIN or dirty:
        raise ValueError('Require a clean nihstro checkout at the documented pin')
    executable = output / 'reference-pack'
    subprocess.run(shlex.split(cxx) + ['-std=c++17', '-O2', '-I'+str(directory/'include'),
                   str(ROOT/'tests/scenes/reference_pack.cpp'), '-o', str(executable)], check=True)
    actual = subprocess.check_output([str(executable)])
    expected = [0x4c000000 | dest << 21 | src << 12 | ((src+dest) & 127)
                for src in range(128) for dest in range(32)]
    expected.extend(selectors << 5 | negate << 4 | mask
                    for selectors in range(256) for negate in range(2) for mask in range(16))
    expected.extend(op << 26 | dest << 21 | a << 12 | b << 7 | ((a+b+dest) & 127)
                    for op in (0, 8) for a in range(128) for b in range(32) for dest in range(32))
    expected.extend(selectors << 14 | negate << 13 | mask
                    for selectors in range(256) for negate in range(2) for mask in range(16))
    expected.extend((0x84000000, 0x88000000))
    if actual != struct.pack('<'+'I'*len(expected), *expected):
        raise ValueError('Pinned independent shader encoding comparison differs')
    (output/'nihstro-encodings.bin').write_bytes(actual)
    return {'pin': PIN, 'encoding_cases': len(expected), 'sha256': hashlib.sha256(actual).hexdigest(),
            'scope': 'instruction/operand encoding only; no external shader or GPU execution'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--nihstro-dir', type=Path)
    parser.add_argument('--cc', default='cc')
    parser.add_argument('--cxx', default='c++')
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    output = args.output.resolve();output.mkdir(parents=True, exist_ok=False)
    report = {'ok': False, 'version': __version__, 'original_fixtures_only': True,
              'source_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
              'source_dirty': bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True)),
              'console_verified': False, 'gx2_execution': False, 'guest_gsp_draws': False,
              'float24_arithmetic': 'exact ADD/MUL only; hardware rounding unverified',
              'pixel_tolerance': 0, 'scenes': []}
    try:
        binary = output/'emit-scene'
        sources = [ROOT/'tests/scenes/emit_scene.c'] + [ROOT/'runtime/core'/name
                   for name in ('ctr_scene.c', 'ctr_fragment.c', 'ctr_tev.c', 'ctr_shader.c', 'ctr_scene_fixture.c', 'ctr_vertex.c', 'ctr_pica.c', 'ctr_texture.c')]
        flags = ['-std=c11', '-O2', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-I'+str(ROOT/'runtime/core')]
        if args.sanitize:
            flags += ['-O1', '-fno-omit-frame-pointer', '-fsanitize=address,undefined']
        subprocess.run(shlex.split(args.cc)+flags+list(map(str, sources))+['-lm', '-o', str(binary)], check=True)
        expected = golden()
        for index in range(4):
            name = ('arithmetic-' if index >= 2 else '')+('u16' if index & 1 else 'u8')
            files = [output/(name+suffix) for suffix in ('.rgba', '.commands', '.resources')]
            result = json.loads(subprocess.check_output([str(binary), str(index), *map(str, files)], text=True))
            counters = {'result':'ok', 'draws':2, 'triangles':2, 'fragments':2304,
                        'shader_instructions':30 if index >= 2 else 18}
            if any(result.get(key) != value for key, value in counters.items()):
                raise ValueError('Scene execution counters differ')
            rgba = files[0].read_bytes()
            if rgba != expected:
                raise ValueError('Scene differs from independent exact analytic pixels')
            preview = output/(name+'.png');preview.write_bytes(png(rgba));files.append(preview)
            report['scenes'].append({'index_bytes': (index & 1)+1, 'arithmetic': index >= 2,
                **result, 'red_pixels':1128, 'green_pixels':1176,
                'files':[{'name': p.name, 'size':p.stat().st_size, 'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in files]})
        report['nihstro_reference'] = reference(args.nihstro_dir.resolve(), output, args.cxx) if args.nihstro_dir else None
        command = [sys.executable, str(ROOT/'tools/validate-shader-arithmetic.py'),
                   '--output', str(output/'arithmetic'), '--cc', args.cc]
        if args.sanitize:
            command.append('--sanitize')
        subprocess.run(command, check=True, stdout=subprocess.DEVNULL)
        report['arithmetic_reference'] = json.loads((output/'arithmetic/report.json').read_text())
        report['ok'] = True
    finally:
        (output/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({'ok':report['ok'], 'scenes':len(report['scenes']), 'report':str(output/'report.json'),
                      'reference':report.get('nihstro_reference')}))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
