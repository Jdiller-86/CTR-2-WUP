#!/usr/bin/env python3
"""Original exact-rational ADD/MUL oracle, not a hardware rounding reference."""
import argparse
from fractions import Fraction
import hashlib
import json
import math
from pathlib import Path
import random
import shlex
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def number(raw):
    return struct.unpack('<f', struct.pack('<I', raw))[0]


def exact_normal(value):
    value = abs(value)
    if value == 0:
        return True
    if not Fraction(1, 2**62) <= value <= (2-Fraction(1, 65536))*2**63:
        return False
    exponent = value.numerator.bit_length()-value.denominator.bit_length()
    power = Fraction(2)**exponent
    if value < power:
        power /= 2
    return (value/power*65536).denominator == 1


def oracle(op, a, b):
    values = [number(raw) for raw in (a, b)]
    if not all(math.isfinite(value) for value in values):
        return None
    av, bv = map(Fraction, values)
    if not exact_normal(av) or not exact_normal(bv):
        return None
    result = av*bv if op else av+bv
    if not exact_normal(result):
        return None
    if result == 0:
        return ((a ^ b) if op else (a & b) if av == bv == 0 else 0) & 0x80000000
    return bits(float(result))


def vectors():
    # Every normal exponent pair, both signs, exact/inexact cancellation and
    # products. Fractions below independently decide representability.
    for ae in range(-62, 64):
        for be in range(-62, 64):
            for signs in range(4):
                a = bits(math.ldexp(-1.0 if signs & 1 else 1.0, ae))
                b = bits(math.ldexp(-1.0 if signs & 2 else 1.0, be))
                for op in (0, 8):
                    yield op, a, b
    edge = [0, 0x80000000, 1, 0x80000001, 0x7f800000, 0xff800000, 0x7fc00000,
            bits(2**-63), bits(2**-62), bits(2**63), 0x5f7fff80,
            bits(1), bits(-1), bits(2**-16), bits(2**-17), bits(2**-18),
            bits(1+2**-16), bits(1-2**-17), bits(1+2**-23)]
    for a in edge:
        for b in edge:
            for op in (0, 8):
                yield op, a, b
    rng = random.Random(0xADD008)
    for _ in range(20000):
        a, b = [bits(math.ldexp((1+rng.randrange(65536)/65536)*rng.choice((-1, 1)),
                               rng.randrange(-62, 64))) for _ in range(2)]
        for op in (0, 8):
            yield op, a, b


def validate(binary):
    cases = list(vectors())
    payload = ''.join(f'{op} {a:08x} {b:08x}\n' for op, a, b in cases).encode()
    result = subprocess.run([str(binary)], input=payload, stdout=subprocess.PIPE, check=True)
    lines = result.stdout.decode().splitlines()
    if len(lines) != len(cases):
        raise ValueError('Arithmetic probe returned an incomplete case set')
    accepted = rejected = 0
    for case, line in zip(cases, lines):
        expected = oracle(*case)
        status, actual = line.split()
        if expected is None:
            if status != 'unsupported-numeric-value' or int(actual, 16) != 0xa5a5a5a5:
                raise ValueError(f'Numeric rejection/atomicity differs for original vector {case}')
            rejected += 1
        else:
            if status != 'ok' or int(actual, 16) != expected:
                raise ValueError(f'Exact result differs for original vector {case}')
            accepted += 1
    return {'ok': True, 'cases': len(cases), 'accepted': accepted, 'rejected': rejected,
            'input_sha256': hashlib.sha256(payload).hexdigest(),
            'output_sha256': hashlib.sha256(result.stdout).hexdigest(),
            'oracle': 'independent arbitrary-precision rational arithmetic',
            'original_inputs_only': True, 'hardware_rounding_verified': False,
            'console_verified': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cc', default='cc')
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    output = args.output.resolve();output.mkdir(parents=True, exist_ok=False)
    flags = ['-std=c11', '-O2', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-I'+str(ROOT/'runtime/core')]
    if args.sanitize:
        flags += ['-O1', '-fno-omit-frame-pointer', '-fsanitize=address,undefined']
    binary = output/'arithmetic-probe'
    command = shlex.split(args.cc)+flags+[str(ROOT/'tests/scenes/arithmetic_probe.c'),
                str(ROOT/'runtime/core/ctr_shader.c'), '-o', str(binary)]
    subprocess.run(command, check=True)
    report = validate(binary)
    report.update(source_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                  source_dirty=bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True)),
                  command=command)
    (output/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
