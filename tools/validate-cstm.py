#!/usr/bin/env python3
"""Compare original relocated CSTM block/SEEK fixtures with rational PCM."""
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


def pcm(encoded, count, coefficients, history):
    h1, h2 = history
    output = []
    for n in range(count):
        frame, lane = divmod(n, 14)
        predictor, scale = divmod(encoded[frame*8], 16)
        packed = encoded[frame*8+1+lane//2]
        nibble = packed//16 if lane%2 == 0 else packed%16
        if nibble >= 8:
            nibble -= 16
        a, b = coefficients[predictor*2:predictor*2+2]
        exact = Fraction(nibble*2**scale)+Fraction(a*h1+b*h2, 2048)+Fraction(1, 2)
        value = max(-32768, min(32767, exact.numerator//exact.denominator))
        output.append(value)
        h2, h1 = h1, value
    return output, (h1, h2)


def fixture(index, rng):
    channels = 1+index%2
    blocks = 1+index//2%5
    full_samples = (1+index//10%8)*14
    final_samples = 1+rng.randrange(full_samples)
    full_bytes = full_samples//14*8
    frames, tail = divmod(final_samples, 14)
    minimum = frames*8+(1+(tail+1)//2 if tail else 0)
    final_bytes = minimum if index%3 else (final_samples+13)//14*8
    final_stride = (final_bytes+3)//4*4
    samples = (blocks-1)*full_samples+final_samples
    rate = [8000, 32000, 44100, 48000, 96000][index%5]
    sizes = [768, (8+blocks*channels*4+31)//32*32,
             (32+index%3*32+((blocks-1)*full_bytes+final_stride)*channels+31)//32*32]
    positions = [0]*3
    order = list(range(3))
    rng.shuffle(order)
    end = 64
    for section in order:
        end += rng.randrange(3)*32
        positions[section] = end
        end += sizes[section]
    data = bytearray(end)

    def w(at, fmt, *values):
        struct.pack_into('<'+fmt, data, at, *values)

    def ref(at, kind, base, dest):
        w(at, 'HHI', kind, 0, dest-base)

    data[:4] = b'CSTM'
    w(4, 'HHIIHH', 0xfeff, 64, 0x02000000, end, 3, 0)
    for i, section in enumerate(reversed(order)):
        w(20+i*12, 'HHII', 0x4000+section, 0, positions[section], sizes[section])
    info, seek, audio = positions
    for at, size, magic in zip(positions, sizes, (b'INFO', b'SEEK', b'DATA')):
        data[at:at+4] = magic
        w(at+4, 'I', size)
    shift = index%3*8
    stream, table = info+64+shift, info+160+shift
    ref(info+8, 0x4100, info+8, stream)
    ref(info+24, 0x0101, info+8, table)
    w(table, 'I', channels)
    w(stream, 'BBBB11I', 2, 0, channels, 0, rate, 0, samples, blocks, full_bytes,
      full_samples, final_bytes, final_samples, final_stride, 4, full_samples)
    start = audio+32+index%3*32
    ref(stream+48, 0x1f00, audio+8, start)
    if index%2:
        tracks, track, indices = info+208, info+480, info+512
        ref(info+16, 0x0101, info+8, tracks)
        w(tracks, 'I', 1)
        ref(tracks+4, 0x4101, tracks, track)
        w(track, 'BBH', 127, 64, 0)
        ref(track+4, 0x0100, track, indices)
        w(indices, 'I', channels)
        data[indices+4:indices+4+channels] = bytes(range(channels))
    else:
        w(info+16, 'HHI', 0, 0, 0xffffffff)
    expected = bytearray(struct.pack('<4I', samples, channels, rate, blocks))
    for channel in range(channels):
        record, adpcm = info+256+channel*16, info+320+channel*64
        ref(table+4+channel*8, 0x4102, table, record)
        ref(record, 0x0300, record, adpcm)
        coefficients = [rng.randrange(-4096, 4097) for _ in range(16)]
        w(adpcm, '16h', *coefficients)
        history = (rng.randrange(-32768, 32768), rng.randrange(-32768, 32768))
        w(adpcm+34, '2h', *history)
        for block in range(blocks):
            count = final_samples if block == blocks-1 else full_samples
            nbytes = final_bytes if block == blocks-1 else full_bytes
            stride = final_stride if block == blocks-1 else full_bytes
            at = start+block*full_bytes*channels+channel*stride
            encoded = bytearray(rng.getrandbits(8) for _ in range(nbytes))
            for frame in range((count+13)//14):
                encoded[frame*8] = rng.randrange(128)
            if block == 0:
                data[adpcm+32] = encoded[0]
            data[at:at+nbytes] = encoded
            # Poison channel padding: a consumer must use the payload byte count.
            data[at+nbytes:at+stride] = b'\xff'*(stride-nbytes)
            w(seek+8+(block*channels+channel)*4, '2h', *history)
            decoded, history = pcm(encoded, count, coefficients, history)
            expected += struct.pack('<II2h', block*full_samples, count, *history)
            expected += struct.pack('<'+'h'*count, *decoded)
    return bytes(data), expected, samples*channels


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='cc')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    rng = random.Random(0x4353544d)
    cases = [fixture(i, rng) for i in range(240)]
    vectors = bytearray(b'CTM1'+struct.pack('<I', len(cases)))
    expected = bytearray(struct.pack('<I', len(cases)))
    for data, result, _ in cases:
        vectors += struct.pack('<I', len(data))+data
        expected += result
    source = args.output.resolve()/'original-streams.bin'
    source.write_bytes(vectors)
    modes = ['normal', 'sanitized'] if args.sanitize else ['normal']
    for mode in modes:
        binary = args.output.resolve()/('reference-'+mode+('.exe' if os.name == 'nt' else ''))
        command = [args.cc, '-std=c11', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
                   '-Iruntime/core', 'runtime/core/ctr_cstm.c', 'runtime/core/ctr_dsp_adpcm.c',
                   'tests/cstm/reference_driver.c', '-o', str(binary)]
        if mode == 'sanitized':
            command += ['-O1', '-fno-omit-frame-pointer', '-fsanitize=address,undefined']
        subprocess.run(command, cwd=ROOT, check=True)
        output = args.output.resolve()/(mode+'.pcm-results')
        subprocess.run([str(binary), str(source), str(output)], check=True,
                       env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=1',
                            'UBSAN_OPTIONS': 'halt_on_error=1:print_stacktrace=1'})
        if output.read_bytes() != expected:
            raise ValueError('CSTM metadata, channel PCM/history or seek differs from independent reference')
    report = dict(ok=True, cases=len(cases), samples=sum(row[2] for row in cases),
                  mono_stereo=True, block_counts=[1, 2, 3, 4, 5], relocated_sections=True,
                  partial_padded_final_blocks=True, checkpoint_and_continuation_match=True,
                  exact_rational_pcm_and_histories=True, modes=modes, compiler=args.cc,
                  source_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                  source_dirty=bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True)),
                  original_vectors_sha256=hashlib.sha256(vectors).hexdigest(),
                  pcm_results_sha256=hashlib.sha256(expected).hexdigest(),
                  guest_dsp_or_console_acceptance=False)
    (args.output/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, sort_keys=True))


if __name__ == '__main__':
    main()
