#!/usr/bin/env python3
"""Compare original DSP ADPCM vectors with independent rational arithmetic."""
import argparse
from fractions import Fraction
import hashlib
import json
import os
from pathlib import Path
import random
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PAIRS = [(0, 0), (2048, 0), (1024, 0), (1536, -512), (4096, -2048),
         (-32768, 32767), (8191, 17), (-1024, -1024)]


def reference(data, coefficients, first, count, h1, h2):
    output = []
    for sample in range(first, first+count):
        frame, lane = divmod(sample, 14)
        predictor, scale = divmod(data[frame*8], 16)
        packed = data[frame*8+1+lane//2]
        nibble = packed//16 if lane%2 == 0 else packed%16
        signed = nibble if nibble<8 else nibble-16
        c0, c1 = coefficients[predictor*2:predictor*2+2]
        value = Fraction(signed*2**scale)+Fraction(c0*h1+c1*h2, 2048)+Fraction(1, 2)
        rounded = value.numerator//value.denominator
        decoded = max(-32768, min(32767, rounded))
        output.append(decoded);h2, h1 = h1, decoded
    return output, h1, h2


def fixtures():
    coefficients = [v for pair in PAIRS for v in pair]
    histories = [-32768, -32767, -2048, -1, 0, 1, 2047, 32767]
    rows = []
    for predictor in range(8):
        for scale in range(16):
            for nibble in range(16):
                first = (scale+nibble)%14;count = 42-first
                data = bytes([predictor*16+scale]+[nibble*17]*7)*3
                rows.append((data, coefficients, first, count, histories[nibble%8], histories[scale%8]))
    rng = random.Random(0x435452)
    for _ in range(4096):
        data = bytearray(rng.getrandbits(8) for _ in range(32))
        for frame in range(4):data[frame*8] = rng.randrange(128)
        first = rng.randrange(14);count = rng.randrange(1, 57-first)
        end = first+count-1;needed = end//14*8+2+(end%14)//2
        rows.append((bytes(data[:needed]), [rng.randrange(-32768, 32768) for _ in range(16)],
                     first, count, rng.randrange(-32768, 32768), rng.randrange(-32768, 32768)))
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='cc');parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--output', type=Path, required=True);args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    rows = fixtures();vector = bytearray(b'CAD1'+struct.pack('<I', len(rows)));expected = bytearray(struct.pack('<I', len(rows)))
    samples = 0
    for data, coefficients, first, count, h1, h2 in rows:
        vector += struct.pack('<III16h2h', len(data), first, count, *coefficients, h1, h2)+data
        decoded, last1, last2 = reference(data, coefficients, first, count, h1, h2)
        expected += struct.pack('<II2h', count, first+count, last1, last2)+struct.pack('<'+'h'*count, *decoded)
        samples += count
    vectors = args.output.resolve()/'original-vectors.bin';vectors.write_bytes(vector)
    sources = ['runtime/core/ctr_dsp_adpcm.c', 'tests/dsp-adpcm/reference_driver.c']
    modes = ['normal', 'sanitized'] if args.sanitize else ['normal']
    for mode in modes:
        binary = args.output.resolve()/('reference-'+mode+('.exe' if os.name=='nt' else ''))
        command = [args.cc, '-std=c11', '-O2', '-g', '-Wall', '-Wextra', '-Werror', '-Iruntime/core', *sources, '-o', str(binary)]
        if mode == 'sanitized':command += ['-O1', '-fno-omit-frame-pointer', '-fsanitize=address,undefined']
        subprocess.run(command, cwd=ROOT, check=True)
        output = args.output.resolve()/(mode+'.pcm-results')
        subprocess.run([str(binary), str(vectors), str(output)], check=True, env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=1', 'UBSAN_OPTIONS': 'halt_on_error=1:print_stacktrace=1'})
        if output.read_bytes()!=expected:raise ValueError('PCM sample, history, seek or continuation differs from rational reference')
    report = {'ok': True, 'cases': len(rows), 'samples': samples, 'all_predictors_scales_nibbles': True,
              'exact_samples_histories': True, 'stream_and_seek_match': True, 'modes': modes,
              'original_vectors_sha256': hashlib.sha256(vector).hexdigest(), 'reference_sha256': hashlib.sha256(expected).hexdigest(),
              'compiler': subprocess.check_output([args.cc, '--version'], text=True).splitlines()[0],
              'source_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
              'source_dirty': bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT)),
              'console_verified': False, 'guest_audio_implemented': False}
    (args.output/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report))


if __name__ == '__main__':main()
