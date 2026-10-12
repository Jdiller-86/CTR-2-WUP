#!/usr/bin/env python3
"""Compare original PICA vectors with pinned libctru construction and C/Python.

The external command builder is compiled unmodified, only into a test binary.
This establishes command bytes, inspection and a narrow no-draw state contract;
it does not establish shader/draw rendering, GX2 execution or console behavior.
"""

import argparse
import hashlib
import json
from pathlib import Path
import shlex
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from host import __version__
from renderer.pica import CommandError, decode_commands
from tests.pica_reference.vectors import (
    BuilderVector, builder_vectors, expected_layout, parameter_values, raw_record,
)

PIN = '9b55eda44cf80b971503991e0c78f9bc8fe50425'
MAX_WRITES = 65536
MAX_BYTES = 4 * 1024 * 1024
STATE_REGISTERS = (0x41, 0x42, 0x43, 0x44, 0x65, 0x66, 0x67, 0x68)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def rows_digest(rows) -> str:
    return digest(json.dumps(rows, separators=(',', ':')).encode())


def merge(previous: int, value: int, mask: int) -> int:
    """Independent byte-select reference, avoiding the production bitmask formula."""
    before = previous.to_bytes(4, 'little')
    after = value.to_bytes(4, 'little')
    return int.from_bytes(bytes(after[i] if mask & (1 << i) else before[i]
                                for i in range(4)), 'little')


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--libctru-source', type=Path,
                        default=ROOT.parent / 'ctr2wup-deps/libctru')
    parser.add_argument('--cc', default='cc')
    parser.add_argument('--cc-prefix', default='')
    parser.add_argument('--run-prefix', default='')
    parser.add_argument('--static', action='store_true')
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    vectors_dir = output / 'vectors'
    vectors_dir.mkdir()
    checks = []
    rows = []
    report = {
        'ok': False, 'version': __version__, 'checks': checks, 'vectors': rows,
        'libctru_reference_pin': None,
        'external_builder_embedded_in_runtime': False,
        'external_gpu_execution': False, 'pica_draws_executed': False,
        'gx2_submission_executed': False, 'console_verified': False,
        'powerpc_execution': 'emulated Linux' if args.static else None,
        'limits': {'register_identifiers': 1024, 'max_extra_parameters': 255,
                   'max_record_parameters': 256, 'max_writes': MAX_WRITES,
                   'max_bytes': MAX_BYTES},
        'scope': [
            'Unmodified libctru GPUCMD_Add/Split command construction only.',
            'All parameter data is original and generated deterministically.',
            'Libctru splits larger API arrays into records of at most 256 parameters.',
            'Nonzero padding tolerance is the inspector contract, not a hardware claim.',
            'Raw viewport/scissor state and canonical finalization produce no pixels.',
        ],
    }

    def run(name, command, *, data=None, preserve_output=False):
        result = subprocess.run(command, input=data, capture_output=True)
        checks.append({'name': name, 'exit_code': result.returncode})
        if preserve_output or result.returncode:
            (output / (name + '.log')).write_bytes(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError(name + ' failed; see preserved log')
        return result.stdout

    try:
        source = args.libctru_source.resolve()
        pin = run('dependency-pin', ['git', '-C', str(source), 'rev-parse', 'HEAD']).decode().strip()
        if pin != PIN or run('dependency-clean', ['git', '-C', str(source), 'status', '--porcelain']):
            raise RuntimeError('Require the clean audited libctru pin')
        readme = (source / 'README.md').read_text()
        permission = readme.split('# License', 1)[1].strip()
        notice = ROOT / 'docs/licenses/libctru.txt'
        if permission not in notice.read_text():
            raise RuntimeError('Retained libctru notice does not match audited permission')
        report['libctru_reference_pin'] = pin
        paths = ('libctru/source/gpu/gpu.c', 'libctru/include/3ds/gpu/gpu.h',
                 'libctru/include/3ds/gpu/registers.h', 'README.md')
        report['external_reference'] = {
            'repository': 'https://github.com/devkitPro/libctru', 'pin': pin,
            'license': 'zlib-style permission in upstream README.md',
            'retained_notice': 'docs/licenses/libctru.txt',
            'notice_sha256': digest(notice.read_bytes()),
            'files': [{'path': name, 'sha256': digest((source / name).read_bytes())}
                      for name in paths],
            'compiled_source': 'libctru/source/gpu/gpu.c',
            'copied_into_runtime': False,
        }
        flags = ['-std=c11', '-O2', '-Wall', '-Wextra', '-Werror']
        if args.static:
            flags += ['-static', '-mcpu=750']
        if args.sanitize:
            flags += ['-O1', '-g', '-fno-omit-frame-pointer', '-fsanitize=address,undefined']
        prefix = shlex.split(args.cc_prefix) + [args.cc, *flags]
        reference = output / 'libctru-command-builder'
        native = output / 'native-inspector'
        compiler = run('compiler-version', shlex.split(args.cc_prefix) + [args.cc, '--version'],
                       preserve_output=True)
        report['compiler'] = compiler.decode(errors='replace').splitlines()[0]
        run('reference-build', prefix + ['-I' + str(source / 'libctru/include'),
            str(source / 'libctru/source/gpu/gpu.c'),
            str(ROOT / 'tests/pica_reference/libctru_builder.c'), '-o', str(reference)],
            preserve_output=True)
        run('native-build', prefix + ['-I' + str(ROOT / 'runtime/core'),
            str(ROOT / 'runtime/core/ctr_pica.c'),
            str(ROOT / 'tests/pica_reference/native_inspector.c'), '-o', str(native)],
            preserve_output=True)
        execute = shlex.split(args.run_prefix)

        def native_result(name, data, limit=MAX_WRITES, capacity=MAX_WRITES):
            result = json.loads(run(name, execute + [str(native), str(limit), str(capacity)], data=data))
            if not result['output_unchanged_on_error'] or not result['state_unchanged_on_error']:
                raise RuntimeError(name + ' changed output/state on failure')
            return result

        def check_stream(name, data, expected=None, *, valid=True, limit=MAX_WRITES,
                         native_error=None, state_expected=None, provenance='original-raw'):
            (vectors_dir / (name + '.bin')).write_bytes(data)
            try:
                host_writes = decode_commands(data, max_writes=limit)
                host_rows = [[w.byte_offset, w.register, w.byte_mask, w.value] for w in host_writes]
                host_ok = True
            except CommandError:
                host_writes = []
                host_rows = []
                host_ok = False
            actual = native_result(name + '-native', data, limit)
            if host_ok != valid or (actual['result'] == 'ok') != valid:
                raise RuntimeError(name + ' host/C accept/reject mismatch')
            if native_error is not None and actual['result'] != native_error:
                raise RuntimeError(name + ' C returned unexpected error ' + actual['result'])
            if valid:
                if expected is not None and host_rows != expected:
                    raise RuntimeError(name + ' host differed from independent layout')
                if actual['writes'] != host_rows or actual['write_count'] != len(host_rows):
                    raise RuntimeError(name + ' C/Python writes differ')
                merged = [merge(0x12345678, w.value, w.byte_mask) for w in host_writes]
                if actual['merged'] != merged or any(w.merge(0x12345678) != value
                                                     for w, value in zip(host_writes, merged)):
                    raise RuntimeError(name + ' byte-select merge differs')
                counted = native_result(name + '-count-only', data, limit, 'count-only')
                if counted['result'] != 'ok' or counted['write_count'] != len(host_rows) or counted['writes']:
                    raise RuntimeError(name + ' count-only inspection differs')
                if host_rows:
                    short = native_result(name + '-capacity', data, limit, len(host_rows) - 1)
                    if short['result'] != 'output-capacity' or not short['output_unchanged_on_error']:
                        raise RuntimeError(name + ' short output buffer was not rejected transactionally')
            if state_expected is not None:
                for key, value in state_expected.items():
                    if actual[key] != value:
                        raise RuntimeError(name + ' state differs at ' + key)
            row = {'name': name, 'provenance': provenance, 'bytes': len(data),
                   'stream_sha256': digest(data), 'expected_valid': valid,
                   'host_native_match': True, 'native_result': actual['result'],
                   'native_state_result': actual['state_result'],
                   'transactional_failure_verified': True}
            if valid:
                row.update(writes=len(host_rows), write_rows_sha256=rows_digest(host_rows),
                           byte_mask_merge_match=True, count_only_match=True)
            rows.append(row)
            return actual

        for vector in builder_vectors():
            expected_data, expected_writes = expected_layout(vector)
            actual_data = run(vector.name + '-reference', execute + [str(reference),
                str(vector.header), str(vector.api_count), str(int(vector.finalize))], data=vector.source)
            if actual_data != expected_data:
                raise RuntimeError(vector.name + ' pinned builder differs from independent layout')
            row_state = None
            if vector.finalize:
                value = vector.parameters[-1]
                finalizers = sum(write[1] == 0x10 for write in expected_writes)
                row_state = {'state_result': 'ok', 'state_committed_lists': 1,
                             'state_register_writes': len(expected_writes),
                             'state_finalizations': finalizers,
                             'state_registers': [value, 0, 0, 0, 0, 0, 0, 0]}
            check_stream(vector.name, actual_data, expected_writes, state_expected=row_state,
                         provenance='original-parameters-through-pinned-libctru')
            rows[-1].update(reference_builder_match=True,
                            parameters_sha256=digest(vector.source),
                            producer_api_count=vector.api_count)

        # These streams test inspection boundaries independently of the oracle.
        for reg, grouped, values in (
            (0x3FF, False, (0xD4C3B2A1,)),
            (0x3FF, False, parameter_values(256, 91)),
            (0x2FF, True, parameter_values(3, 92)),
            (0x300, True, parameter_values(256, 93)),
        ):
            name = f'raw-bank-{reg:03x}-count-{len(values)}-group-{int(grouped)}'
            expected = [[0, reg + i if grouped else reg, 15, value]
                        for i, value in enumerate(values)]
            check_stream(name, raw_record(reg, 15, values, grouped=grouped), expected)
        padded = raw_record(0x41, 5, (0x01020304, 0x11223344), padding=0xDEADBEEF)
        check_stream('raw-nonzero-padding', padded,
                     [[0, 0x41, 5, 0x01020304], [0, 0x41, 5, 0x11223344]])
        check_stream('empty', b'', [], state_expected={'state_result': 'finalization'})
        one = raw_record(0x41, 15, (0xD4C3B2A1,))
        failures = [
            ('misaligned-one-byte', b'x', 'alignment'),
            ('misaligned-word', one[:4], 'alignment'),
            ('missing-padding', padded[:-4], 'alignment'),
            ('truncated-record', struct.pack('<II', 1, 0x002F0041), 'truncated'),
            ('truncated-after-prefix', one + struct.pack('<II', 1, 0x002F0041), 'truncated'),
            ('register-out-of-bank', raw_record(0x400, 15, (1,)), 'register-range'),
            ('register-high-identifier', raw_record(0xFFFF, 15, (1,)), 'register-range'),
            ('grouped-bank-overflow', raw_record(0x3FF, 15, (1, 2), grouped=True), 'register-range'),
        ]
        for bit in (28, 29, 30):
            failures.append((f'reserved-header-bit-{bit}',
                             struct.pack('<II', 0xD4C3B2A1, (1 << bit) | 0xF0041), 'header'))
        for name, data, error in failures:
            check_stream(name, data, valid=False, native_error=error)
        check_stream('zero-write-budget', one, valid=False, limit=0,
                     native_error='write-or-byte-limit')
        check_stream('global-budget-overflow', one, valid=False, limit=MAX_WRITES + 1,
                     native_error='write-or-byte-limit')
        budget = raw_record(0x41, 15, parameter_values(256, 94))
        check_stream('exact-write-budget', budget,
                     [[0, 0x41, 15, value] for value in parameter_values(256, 94)], limit=256)
        check_stream('record-write-budget-overflow', budget, valid=False, limit=255,
                     native_error='write-or-byte-limit')
        check_stream('byte-limit-overflow', one * ((MAX_BYTES + 8) // 8), valid=False,
                     native_error='write-or-byte-limit')

        # Original state-only command lists use actual libctru record construction.
        commands = [
            BuilderVector('state-viewport', 0x41, 15, True,
                          (0x3F800000, 0x3C23D70A, 0x43700000, 0x3B888889), 4),
            BuilderVector('state-mask', 0x41, 5, False, (0xAA55CC33, 0x55667788), 2),
            BuilderVector('state-scissor', 0x65, 15, True,
                          (1, 0x00100020, 0x00EF018F, 0x00040008), 4),
        ]
        state_data = b''
        state_rows = []
        state = {reg: 0 for reg in STATE_REGISTERS}
        for vector in commands:
            expected_data, expected_rows = expected_layout(vector)
            built = run(vector.name + '-reference', execute + [str(reference), str(vector.header),
                        str(vector.api_count), '0'], data=vector.source)
            if built != expected_data:
                raise RuntimeError('state builder differs')
            for offset, reg, mask, value in expected_rows:
                state_rows.append([offset + len(state_data), reg, mask, value])
                state[reg] = merge(state[reg], value, mask)
            state_data += built
        state_data += struct.pack('<II', 0x12345678, 0x000F0010)
        state_rows.append([len(state_data) - 8, 0x10, 15, 0x12345678])
        if len(state_data) % 16:
            state_data += struct.pack('<II', 0x12345678, 0x000F0010)
            state_rows.append([len(state_data) - 8, 0x10, 15, 0x12345678])
        finals = sum(row[1] == 0x10 for row in state_rows)
        state_expected = {'state_result': 'ok', 'state_committed_lists': 1,
                          'state_register_writes': len(state_rows), 'state_finalizations': finals,
                          'state_registers': [state[reg] for reg in STATE_REGISTERS]}
        check_stream('state-viewport-mask-scissor-finalize', state_data, state_rows,
                     state_expected=state_expected,
                     provenance='original-state-through-pinned-libctru-plus-canonical-finalize')
        # Structurally valid trigger registers remain visibly unsupported by state execution.
        for reg, label in ((0x22E, 'draw-arrays'), (0x22F, 'draw-elements'),
                           (0x2CC, 'vertex-shader-upload'), (0x2D6, 'operand-upload')):
            data = raw_record(reg, 0, (0,)) + struct.pack('<II', 0x12345678, 0x000F0010)
            check_stream('unsupported-' + label + '-zero-mask', data,
                         [[0, reg, 0, 0], [8, 0x10, 15, 0x12345678]],
                         state_expected={'state_result': 'unsupported-register',
                                         'state_committed_lists': 0, 'state_register_writes': 0})
        report['ok'] = True
        report['totals'] = {'vectors': len(rows),
                            'builder_vectors': sum(bool(row.get('reference_builder_match')) for row in rows),
                            'valid_streams': sum(row['expected_valid'] for row in rows),
                            'rejected_streams': sum(not row['expected_valid'] for row in rows),
                            'inspected_writes': sum(row.get('writes', 0) for row in rows)}
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps({'ok': report['ok'], 'vectors': len(rows),
                          'report': str(output / 'report.json')}))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
