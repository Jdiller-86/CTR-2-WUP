#!/usr/bin/env python3
"""Independently decode Latte code and compare original exact-rational shaders."""
import argparse
from dataclasses import dataclass, field
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import shlex
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
ISA_PIN = 'e6c528a20a41c34e0f9eb91dd3da40f119db2dee'
WUT_PIN = '937f0d6901f5e2a72672154a674ec47b63e2f417'
WUT_SHA256 = '8bcd1c507fdd44476aceac9baf848260356f84c9bb9fbdc90c4b743ac8c75d0a'
REFERENCE_PINS = {
    'isa': {'repository': 'https://github.com/decaf-emu/decaf-emu', 'commit': ISA_PIN,
            'files': ['src/libgpu/latte/latte_instructions.h',
                      'src/libgpu/latte/latte_instructions_def.inl'],
            'sha256': ['f0615523d54f5740c24a272ece8695f29733ccfff3ccb60c7ffd041317dd3d12',
                       '04b7836fb8a5cc3d15049c7c92cb5cfa31ed12374e51f5fe323c3ebe76e3353f']},
    'wut': {'repository': 'https://github.com/devkitPro/wut', 'commit': WUT_PIN,
            'file': 'samples/cmake/content/pos_col_shader.gsh', 'sha256': WUT_SHA256},
}


def binary32(value, negative_zero=False):
    if not value:
        return 0x80000000 if negative_zero else 0
    number = float(value)
    if Fraction(number) != value:
        raise ValueError('Oracle corpus requires exact binary32 results')
    raw = struct.unpack('<I', struct.pack('<f', number))[0]
    if rational(raw) != value:
        raise ValueError('Oracle corpus requires exact binary32 results')
    return raw


def rational(raw):
    exponent, mantissa = (raw >> 23) & 255, raw & 0x7fffff
    if exponent == 255:
        raise ValueError('Nonfinite value outside the exact corpus')
    result = Fraction(mantissa if not exponent else (1 << 23) + mantissa)
    result *= Fraction(2) ** ((exponent or 1) - 150)
    return -result if raw & 0x80000000 else result


def arithmetic(op, a, b):
    av, bv = rational(a), rational(b)
    if op == 'MOV':
        return a
    if op == 'ADD':
        result = av + bv
        sign = bool((a & b) & 0x80000000) if not av and not bv else False
    elif op == 'MUL':
        result = av * bv
        sign = bool((a ^ b) & 0x80000000)
    else:
        raise ValueError('Unsupported oracle operation')
    return binary32(result, sign)


@dataclass(frozen=True)
class Instruction:
    op: str
    dest: int = 0
    a: int = 0
    b: int = 0
    swizzle_a: tuple = (0, 1, 2, 3)
    swizzle_b: tuple = (0, 1, 2, 3)
    negate_a: bool = False
    negate_b: bool = False
    mask: int = 15


def default_inputs():
    return [[binary32(Fraction((register + 1) * (lane + 1), 4))
             for lane in range(4)] for register in range(16)]


@dataclass
class Case:
    name: str
    instructions: list
    entry: int = 0
    inputs: list = field(default_factory=default_inputs)
    uniforms: dict = field(default_factory=dict)
    output_mask: int = 3
    maps: tuple = (0x03020100, 0x0b0a0908)
    enabled: int = 65535
    expected_compile: str = 'ok'
    expected_cpu: str = 'ok'
    change: str = ''

    def payload(self):
        code, descriptors, table = {}, {}, {}
        for index, instruction in enumerate(self.instructions, self.entry):
            if instruction.op in ('END', 'NOP'):
                code[index] = {'END': 0x88000000, 'NOP': 0x84000000}[instruction.op]
                continue
            # PICA descriptors number lanes from the most significant selector.
            mask = sum(8 >> lane for lane in range(4) if instruction.mask & (1 << lane))
            descriptor = mask | (int(instruction.negate_a) << 4) | (int(instruction.negate_b) << 13)
            descriptor |= sum(selector << (11 - 2 * lane) for lane, selector in enumerate(instruction.swizzle_a))
            descriptor |= sum(selector << (20 - 2 * lane) for lane, selector in enumerate(instruction.swizzle_b))
            slot = table.setdefault(descriptor, len(table))
            descriptors[slot] = descriptor
            code[index] = ({'MOV': 19, 'ADD': 0, 'MUL': 8}[instruction.op] << 26 |
                           instruction.dest << 21 | instruction.a << 12 |
                           instruction.b << 7 | slot)
        uniforms = dict(self.uniforms)
        if self.change == 'unsupported': code[self.entry + 2] = 1 << 26
        if self.change == 'relative': code[self.entry + 2] |= 1 << 19
        if self.change == 'undefined-code': del code[self.entry + 2]
        if self.change == 'undefined-descriptor': descriptors.clear()
        if self.change == 'descriptor-high-bit': descriptors[0] |= 1 << 31
        if self.change == 'end-reserved': code[max(code)] |= 1
        if self.change == 'missing-end':
            code = {at: 0x84000000 for at in range(512)}
        if self.change == 'uniform-undefined': uniforms[95] = (7, uniforms[95][1])
        if self.change == 'uniform-numeric':
            uniforms[95] = (15, [0x7fc00000] * 4)
        lines = [f'{self.entry} {len(code)} {len(descriptors)} {len(uniforms)} '
                 f'{self.output_mask} {len(self.maps)} {self.enabled}',
                 ' '.join(f'{word:x}' for word in self.maps + (0,) * (8 - len(self.maps))),
                 ' '.join(f'{word:x}' for register in self.inputs for word in register)]
        lines += [f'{at} {word:x}' for at, word in sorted(code.items())]
        lines += [f'{at} {word:x}' for at, word in sorted(descriptors.items())]
        lines += [f'{at} {mask} ' + ' '.join(f'{word:x}' for word in values)
                  for at, (mask, values) in sorted(uniforms.items())]
        return '\n'.join(lines) + '\n'

    def oracle(self):
        temporaries, outputs, written = {}, [[0] * 4 for _ in range(16)], [0] * 16
        input_mask, uniforms = 0, [0] * 96

        def source(register, lane, negate):
            nonlocal input_mask
            if register < 16:
                input_mask |= 1 << register
                raw = self.inputs[register][lane]
            elif register < 32:
                raw = temporaries[register - 16, lane]
            else:
                uniforms[register - 32] |= 1 << lane
                raw = self.uniforms[register - 32][1][lane]
            return raw ^ (0x80000000 if negate else 0)

        executed = 0
        for instruction in self.instructions:
            executed += 1
            if instruction.op == 'END': break
            if instruction.op == 'NOP': continue
            values = {}
            for lane in range(4):
                if not instruction.mask & (1 << lane): continue
                a = source(instruction.a, instruction.swizzle_a[lane], instruction.negate_a)
                b = (source(instruction.b, instruction.swizzle_b[lane], instruction.negate_b)
                     if instruction.op != 'MOV' else 0)
                values[lane] = arithmetic(instruction.op, a, b)
            # DSL vector assignment evaluates every source before any store.
            for lane, raw in values.items():
                if instruction.dest < 16:
                    outputs[instruction.dest][lane] = raw
                    written[instruction.dest] |= 1 << lane
                else:
                    temporaries[instruction.dest - 16, lane] = raw
        semantics, mapping_index = {}, 0
        for register in range(16):
            if not self.output_mask & (1 << register): continue
            mapping = self.maps[mapping_index]; mapping_index += 1
            for lane in range(4):
                semantic = mapping >> (lane * 8) & 31
                if semantic != 31: semantics[semantic] = outputs[register][lane]
        result = {'outputs': [word for values in outputs for word in values],
                'written': written, 'input_mask': input_mask, 'uniform_used': uniforms,
                'position': [semantics[lane] for lane in range(4)],
                'color': [semantics[lane] for lane in range(8, 12)], 'instructions': executed}
        result['texcoord_mask'] = 0
        for unit, (u, v) in enumerate(((12, 13), (14, 15), (22, 23))):
            if u in semantics or v in semantics:
                result['texcoord'+str(unit)] = [semantics[u], semantics[v], 0, 0x3f800000]
                result['texcoord_mask'] |= 1 << unit
        return result


def cases():
    base = [Instruction('MOV', 0, 0), Instruction('MOV', 1, 1)]
    end = Instruction('END')
    yield Case('identity', base + [end])
    for op in ('MOV', 'ADD', 'MUL'):
        for operand in range(1 if op == 'MOV' else 2):
            for permutation in range(256):
                selectors = tuple(permutation >> (lane * 2) & 3 for lane in range(4))
                kw = {'swizzle_a' if operand == 0 else 'swizzle_b': selectors}
                yield Case(f'{op}-swizzle-{operand}-{permutation}',
                           base + [Instruction(op, 0, 2, 3, **kw), end])
        for signs in range(4):
            for mask in range(16):
                yield Case(f'{op}-mask-{mask}-signs-{signs}', base + [
                    Instruction(op, 0, 2, 3, (3, 2, 1, 0), (1, 3, 0, 2),
                                bool(signs & 1), bool(signs & 2), mask), end])
    for register in range(16):
        yield Case(f'input-{register}', base + [Instruction('MOV', 0, register), end])
        for op in ('MOV', 'ADD', 'MUL'):
            yield Case(f'in-place-{op}-{register}', base + [Instruction('MOV', 16 + register, 2),
                       Instruction(op, 16 + register, 16 + register, 16 + register,
                                   (3, 2, 1, 0), (2, 0, 3, 1)),
                       Instruction('MOV', 0, 16 + register), end])
    for register in range(96):
        values = [binary32(Fraction((register + 1) * (lane + 1), 8)) for lane in range(4)]
        yield Case(f'uniform-{register}', base + [Instruction('ADD', 0, register + 32, 2), end],
                   uniforms={register: (15, values)})
    # Nonzero entry and the final legal code word must retain actual END semantics.
    yield Case('entry-509-last-511', base + [end], entry=509)
    yield Case('nop-before-end', base + [Instruction('NOP'), end])
    # Long programs cross multiple ALU clause boundaries, then reach END at 511.
    for length in (11, 12, 17, 128, 511):
        yield Case(f'clause-boundaries-{length}',
                   base + [Instruction('ADD', 16, 2, 3)] * (length - 2) + [end])
    yield Case('permuted-output-registers', [Instruction('MOV', 3, 0), Instruction('MOV', 6, 1), end],
               output_mask=(1 << 3) | (1 << 6), maps=(0x00030201, 0x080b0a09))
    yield Case('split-output-semantics', [Instruction('MOV', 2, 0), Instruction('MOV', 4, 1),
               Instruction('MOV', 7, 2), end], output_mask=(1 << 2) | (1 << 4) | (1 << 7),
               maps=(0x1f1f0100, 0x09081f1f, 0x0b0a0302))
    yield Case('zero-mask-does-not-read-undefined-temp', base + [Instruction('ADD', 0, 31, 31, mask=0), end])
    zeros = default_inputs(); zeros[2] = [0, 0x80000000, 0, 0x80000000]
    zeros[3] = [0x80000000, 0x80000000, binary32(Fraction(-1)), binary32(Fraction(-1))]
    for op in ('MOV', 'ADD', 'MUL'):
        yield Case(f'signed-zero-{op}', base + [Instruction(op, 0, 2, 3), end], inputs=zeros)
    bad = base + [Instruction('MOV', 0, 2), end]
    for change, status, cpu in (
            ('unsupported', 'unsupported-instruction-or-state', 'unsupported-instruction-or-state'),
            ('relative', 'unsupported-instruction-or-state', 'unsupported-instruction-or-state'),
            ('undefined-code', 'undefined-source-or-semantic', 'undefined-source-or-code'),
            ('undefined-descriptor', 'undefined-source-or-semantic', 'undefined-source-or-code'),
            ('descriptor-high-bit', 'unsupported-instruction-or-state', 'unsupported-instruction-or-state'),
            ('end-reserved', 'unsupported-instruction-or-state', 'unsupported-instruction-or-state'),
            ('missing-end', 'missing-end', 'missing-end')):
        yield Case(f'reject-{change}', bad, change=change, expected_compile=status, expected_cpu=cpu)
    for maps, status in (((0x03020100, 0x0b0a0900), 'undefined-source-or-semantic'),
                         ((0x03020100, 0x1f0a0908), 'undefined-source-or-semantic'),
                         ((0x03020100, 0x0b0a0904), 'unsupported-instruction-or-state'),
                         ((0x83020100, 0x0b0a0908), 'unsupported-instruction-or-state')):
        yield Case(f'reject-map-{maps[0]:x}-{maps[1]:x}', base + [end], maps=maps, expected_compile=status)
    yield Case('reject-entry-range', [], entry=512, expected_compile='range', expected_cpu='range')
    yield Case('reject-output-count', base + [end], maps=(0x03020100,), expected_compile='unsupported-instruction-or-state')
    yield Case('reject-temp-read', base + [Instruction('ADD', 0, 16, 16), end],
               expected_compile='undefined-source-or-semantic', expected_cpu='undefined-source-or-code')
    uniforms = {95: (15, [binary32(Fraction(1))] * 4)}
    for change, status, cpu in (('uniform-undefined', 'undefined-source-or-semantic', 'undefined-source-or-code'),
                                 ('uniform-numeric', 'unsupported-numeric-value', 'unsupported-numeric-value')):
        yield Case(f'reject-{change}', base + [Instruction('MOV', 0, 127), end], uniforms=uniforms,
                   change=change, expected_compile=status, expected_cpu=cpu)
    numeric = default_inputs(); numeric[2][0] = 0x3f800001
    yield Case('numeric-preflight-retained', bad, inputs=numeric, expected_cpu='unsupported-numeric-value')
    yield from texture_cases()
    yield from multicoordinate_cases()


def texture_cases():
    """Original UV programs exercise export layout, independently of sampling."""
    end = Instruction('END')
    base = [Instruction('MOV', 0, 0), Instruction('MOV', 1, 1), Instruction('MOV', 2, 2)]
    maps = (0x03020100, 0x0b0a0908, 0x1f1f0d0c)
    yield Case('texcoord0-identity', base + [end], output_mask=7, maps=maps)
    for u in range(4):
        for v in range(4):
            if u == v: continue
            mapping = 0x1f1f1f1f
            mapping = mapping & ~(255 << (u * 8)) | 12 << (u * 8)
            mapping = mapping & ~(255 << (v * 8)) | 13 << (v * 8)
            yield Case(f'texcoord0-source-lanes-{u}-{v}', base + [end], output_mask=7,
                       maps=maps[:2] + (mapping,))
    # Packed output tables follow the mask's register order, not semantic order.
    yield Case('texcoord0-sparse-output', [Instruction('MOV', 3, 0), Instruction('MOV', 6, 1),
               Instruction('MOV', 15, 2), end], output_mask=(1 << 3) | (1 << 6) | (1 << 15),
               maps=maps[:2] + (0x0c1f1f0d,))
    yield Case('texcoord0-interleaved-semantics', base + [end], output_mask=7,
               maps=(0x0d0c0100, 0x09080302, 0x1f1f0b0a))
    for op in ('MOV', 'ADD', 'MUL'):
        for signs in range(4):
            yield Case(f'texcoord0-{op}-signs-{signs}', base + [
                Instruction('MOV', 16, 2),
                Instruction(op, 16, 16, 3, (3, 1, 2, 0), (2, 0, 3, 1),
                            bool(signs & 1), bool(signs & 2)),
                Instruction('MOV', 2, 16), end], output_mask=7, maps=maps)
    # Boundary coordinates and signed zero remain exact-rational shader inputs;
    # scene sampling limits are enforced by the scene contract, not the encoder.
    for raw in (0, 0x80000000, binary32(Fraction(-16)), binary32(Fraction(16)),
                binary32(Fraction(-1, 16)), binary32(Fraction(255, 16)),
                binary32(Fraction(32))):
        inputs = default_inputs(); inputs[2] = [raw, raw ^ 0x80000000, 0, 0x3f800000]
        yield Case(f'texcoord0-numeric-{raw:08x}', base + [end], inputs=inputs,
                   output_mask=7, maps=maps)
    values = [binary32(Fraction(-1, 4)), binary32(Fraction(3, 4)), 0, 0x3f800000]
    yield Case('texcoord0-uniform-95', base + [Instruction('MOV', 2, 127), end],
               uniforms={95: (15, values)}, output_mask=7, maps=maps)
    yield Case('texcoord0-entry-508-last-511', base + [end], entry=508, output_mask=7, maps=maps)
    yield Case('texcoord0-clause-boundaries-511', base + [Instruction('ADD', 16, 2, 3)] * 508 + [end],
               output_mask=7, maps=maps)
    for mapping, status in ((0x1f1f1f0c, 'undefined-source-or-semantic'),
                            (0x1f1f1f0d, 'undefined-source-or-semantic'),
                            (0x1f1f0c0c, 'undefined-source-or-semantic'),
                            (0x1f0e0d0c, 'undefined-source-or-semantic')):
        yield Case(f'reject-texcoord0-map-{mapping:08x}', base + [end], output_mask=7,
                   maps=maps[:2] + (mapping,), expected_compile=status)
    yield Case('reject-texcoord0-unwritten-output', base[:2] + [end], output_mask=7,
               maps=maps, expected_compile='undefined-source-or-semantic')
    yield Case('reject-texcoord0-unwritten-lane', base[:2] + [Instruction('MOV', 2, 2, mask=1), end],
               output_mask=7, maps=maps, expected_compile='undefined-source-or-semantic')
    yield Case('reject-texcoord0-missing-color', base + [end], output_mask=7,
               maps=(maps[0], 0x1f0a0908, maps[2]), expected_compile='undefined-source-or-semantic')


def multicoordinate_cases():
    """Separate exact UV inputs make semantic and dense-export mixups visible."""
    end = Instruction('END')
    coordinates = (0x1f1f0d0c, 0x1f1f0f0e, 0x1f1f1716)
    for mask in range(2, 8):
        inputs = default_inputs()
        for unit in range(3):
            inputs[2+unit] = [binary32(Fraction(2*unit-3, 4)),
                              binary32(Fraction(7-unit, 8)), 0, 0x3f800000]
        units = [unit for unit in range(3) if mask & (1 << unit)]
        instructions = [Instruction('MOV', 0, 0), Instruction('MOV', 1, 1)]
        instructions += [Instruction('MOV', 2+rank, 2+unit) for rank, unit in enumerate(units)]
        maps = (0x03020100, 0x0b0a0908)+tuple(coordinates[unit] for unit in units)
        yield Case(f'multicoord-mask-{mask}', instructions+[end], inputs=inputs,
                   output_mask=(1 << len(maps))-1, maps=maps)
        # Reverse physical output order while retaining semantic UV ordering.
        yield Case(f'multicoord-mask-{mask}-reverse-output',
                   instructions[:2]+[Instruction('MOV', 2+rank, 2+unit)
                                     for rank, unit in enumerate(reversed(units))]+[end],
                   inputs=inputs, output_mask=(1 << len(maps))-1,
                   maps=maps[:2]+tuple(coordinates[unit] for unit in reversed(units)))
    base = [Instruction('MOV', 0, 0), Instruction('MOV', 1, 1), Instruction('MOV', 2, 2), end]
    for unit, pair in enumerate(((12, 13), (14, 15), (22, 23))):
        for lane in pair:
            yield Case(f'multicoord-reject-unit-{unit}-unpaired-{lane}', base,
                       output_mask=7, maps=(0x03020100, 0x0b0a0908, 0x1f1f1f00 | lane),
                       expected_compile='undefined-source-or-semantic')
    for semantic in (16, 17, 18, 21, 24, 30):
        yield Case(f'multicoord-reject-semantic-{semantic}', base,
                   output_mask=7, maps=(0x03020100, 0x0b0a0908, 0x1f1f1f00 | semantic),
                   expected_compile='unsupported-instruction-or-state')


def decode(program, inputs, uniforms=None, gprs=128):
    """Execute the documented scalar OP2/CF/export subset, without compiler helpers."""
    if not program or len(program) % 8 or len(program) > 65536:
        raise ValueError('Invalid program span')
    registers = dict(inputs); uniforms = uniforms or {}
    exports, clauses, export_opcodes, alu_count, at, fetches = [], [], [], 0, 0, 0
    read_inputs, read_uniforms, highest = 0, [0] * 96, 0

    def source(selector, channel, negate):
        nonlocal read_inputs, highest
        if selector < 128:
            if selector >= gprs: raise ValueError('GPR read exceeds allocation')
            if 1 <= selector <= 16: read_inputs |= 1 << (selector - 1)
            highest = max(highest, selector)
            if (selector, channel) not in registers: raise ValueError('Undefined GPR read')
            value = registers[selector, channel]
        elif 256 <= selector < 352:
            index = selector - 256
            read_uniforms[index] |= 1 << channel
            if (index, channel) not in uniforms: raise ValueError('Undefined constant-file read')
            value = uniforms[index, channel]
        elif selector in (248, 249):
            value = 0 if selector == 248 else 0x3f800000
        else:
            raise ValueError('Unsupported source selector')
        return value ^ (0x80000000 if negate else 0)

    ended = False
    while at < len(program):
        word0, word1 = struct.unpack_from('<II', program, at)
        at += 8
        if word1 >> 28 & 3 == 2:
            if word1 >> 26 & 15 != 8: raise ValueError('Unsupported ALU control flow')
            if word0 >> 22 or word1 & 0x4203ffff: raise ValueError('Unexpected cache or ALU clause mode')
            begin, count = (word0 & 0x3fffff) * 8, (word1 >> 18 & 127) + 1
            if begin < at or begin + count * 8 > len(program): raise ValueError('ALU clause outside program')
            for old_begin, old_end in clauses:
                if begin < old_end and old_begin < begin + count * 8: raise ValueError('Overlapping ALU clauses')
            clauses.append((begin, begin + count * 8))
            for offset in range(begin, begin + count * 8, 8):
                a, b = struct.unpack_from('<II', program, offset)
                if not a >> 31 or a & 0x7c400200: raise ValueError('Unexpected grouped/relative/predicated ALU')
                if b & 0x9000006f or b >> 18 & 7: raise ValueError('Unexpected ALU destination/modifier')
                opcode = b >> 7 & 2047
                if opcode not in (0, 2, 25): raise ValueError('Unsupported ALU opcode')
                if not b & 16: raise ValueError('Missing ALU write mask')
                dest, channel = b >> 21 & 127, b >> 29 & 3
                if dest >= gprs: raise ValueError('GPR write exceeds allocation')
                highest = max(highest, dest)
                av = source(a & 511, a >> 10 & 3, bool(a & 4096))
                bv = source(a >> 13 & 511, a >> 23 & 3, bool(a & (1 << 25))) if opcode != 25 else 0
                registers[dest, channel] = arithmetic({0: 'ADD', 2: 'MUL', 25: 'MOV'}[opcode], av, bv)
                alu_count += 1
        else:
            opcode = word1 >> 23 & 127
            if opcode == 19:
                if at != 8 or word0 or word1 != 19 << 23: raise ValueError('Invalid fetch-shader call')
                fetches += 1
            elif opcode in (39, 40):
                if word0 & 0xffc00000 or word1 & 0x405ff000: raise ValueError('Unsupported export addressing/mode')
                kind, base, register = word0 >> 13 & 3, word0 & 8191, word0 >> 15 & 127
                if register >= gprs: raise ValueError('Export exceeds GPR allocation')
                values = []
                selectors = [(word1 >> (lane * 3)) & 7 for lane in range(4)]
                for channel in selectors:
                    if channel < 4:
                        if (register, channel) not in registers: raise ValueError('Undefined export lane')
                        values.append(registers[register, channel])
                    elif channel in (4, 5): values.append(0 if channel == 4 else 0x3f800000)
                    else: raise ValueError('Unwritten export lane')
                exports.append({'type': kind, 'base': base, 'gpr': register,
                                'selectors': selectors, 'values': values, 'barrier': bool(word1 >> 31)})
                export_opcodes.append(opcode)
                if word1 & (1 << 21):
                    if opcode != 40: raise ValueError('Program terminates with unfinished export')
                    ended = True; break
            else:
                raise ValueError('Unsupported control-flow instruction')
    if not ended or fetches != 1: raise ValueError('Missing program termination/fetch')
    destinations = [(item['type'], item['base']) for item in exports]
    if not 2 <= len(exports) <= 5 or destinations != [(1, 60)]+[(2, index) for index in range(len(exports)-1)]:
        raise ValueError('Expected POS0, PARAM0 and dense PARAM1..3 exports')
    expected_opcodes = [40]+[39]*(len(exports)-2)+[40]
    if export_opcodes != expected_opcodes: raise ValueError('Incorrect export completion sequence')
    coordinate_registers = []
    for export in exports[2:]:
        if export['selectors'] != [0, 1, 4, 5]:
            raise ValueError('Expected texture coordinate xy01 export')
        coordinate_registers.append(export['gpr'])
    if any(not 53 <= register <= 55 for register in coordinate_registers) or coordinate_registers != sorted(set(coordinate_registers)):
        raise ValueError('Expected unique UV0/1/2 export registers')
    if any(item['barrier'] for item in exports[1:]): raise ValueError('Unexpected parameter export barrier')
    if not exports[0]['barrier']: raise ValueError('Position export lacks completion barrier')
    if any(begin < at for begin, _ in clauses): raise ValueError('ALU overlaps control flow')
    data_begin = min((begin for begin, _ in clauses), default=len(program))
    if any(program[at:data_begin]): raise ValueError('Nonzero unreferenced control-flow padding')
    if clauses:
        data_end = max(end for _, end in clauses)
        if len(program) - data_end >= 256 or any(program[data_end:]):
            raise ValueError('Invalid shader alignment padding')
    return {'exports': exports, 'alu_instructions': alu_count, 'clauses': len(clauses),
            'input_mask': read_inputs, 'uniform_used': read_uniforms, 'highest_gpr': highest}


def compile_probe(output, cc='cc', sanitize=False):
    binary = output / 'gx2-shader-reference'
    flags = ['-std=c11', '-O2', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-I' + str(ROOT / 'runtime/core')]
    if sanitize: flags += ['-O1', '-fno-omit-frame-pointer', '-fsanitize=address,undefined']
    command = shlex.split(cc) + flags + [str(ROOT / 'tests/gx2-shader/reference_driver.c'),
              str(ROOT / 'runtime/core/ctr_shader.c'), str(ROOT / 'runtime/core/ctr_gx2_shader.c'), '-o', str(binary)]
    subprocess.run(command, check=True)
    return binary


def validate(binary, corpus=None):
    corpus = list(cases()) if corpus is None else list(corpus)
    payload = ''.join(case.payload() for case in corpus).encode()
    process = subprocess.run([str(binary)], input=payload, stdout=subprocess.PIPE, check=True)
    lines = process.stdout.splitlines()
    if len(lines) != len(corpus): raise ValueError('Probe returned an incomplete original corpus')
    accepted = rejected = preflight_rejected = total_alu = total_clauses = 0
    samples = []
    for case, line in zip(corpus, lines):
        result = json.loads(line)
        if result['compile'] != case.expected_compile or result['cpu'] != case.expected_cpu or not result['rollback']:
            raise ValueError(f'{case.name}: status/atomicity differs: {result["compile"]}, {result["cpu"]}')
        if result['compile'] != 'ok': rejected += 1; continue
        accepted += 1
        if result['cpu'] != 'ok': preflight_rejected += 1; continue
        expected = case.oracle()
        coordinate_mask = expected['texcoord_mask']
        textured = bool(coordinate_mask & 1)
        if (result['texcoord0'] != textured or result['texcoord_mask'] != coordinate_mask or
                result['gprs'] != 53+coordinate_mask.bit_length()):
            raise ValueError(f'{case.name}: texture export metadata/allocation differs')
        registers = {(register + 1, lane): raw for register, values in enumerate(case.inputs)
                     if case.enabled & (1 << register) for lane, raw in enumerate(values)}
        uniforms = {(register, lane): raw for register, (mask, values) in case.uniforms.items()
                    for lane, raw in enumerate(values) if mask & (1 << lane)}
        decoded = decode(bytes.fromhex(result['bytes']), registers, uniforms, result['gprs'])
        if result['outputs'] != expected['outputs'] or result['written'] != expected['written']:
            raise ValueError(f'{case.name}: original PICA CPU output differs from rational oracle')
        if decoded['exports'][0]['values'] != expected['position'] or decoded['exports'][1]['values'] != expected['color']:
            raise ValueError(f'{case.name}: independently decoded Latte exports differ from rational oracle')
        if len(decoded['exports']) != 2+coordinate_mask.bit_count():
            raise ValueError(f'{case.name}: independently decoded texture export count differs')
        units = [unit for unit in range(3) if coordinate_mask & (1 << unit)]
        for export, unit in zip(decoded['exports'][2:], units):
            if export['gpr'] != 53+unit or export['values'] != expected['texcoord'+str(unit)]:
                raise ValueError(f'{case.name}: independently decoded texcoord{unit} differs from rational oracle')
        for key in ('input_mask', 'uniform_used'):
            if result[key] != expected[key] or decoded[key] != expected[key]:
                raise ValueError(f'{case.name}: decoded {key} differs from original source use')
        if result['alu_instructions'] != decoded['alu_instructions'] or result['pica_instructions'] != expected['instructions']:
            raise ValueError(f'{case.name}: instruction counts differ')
        total_alu += decoded['alu_instructions']; total_clauses += decoded['clauses']
        if case.name in ('identity', 'in-place-ADD-15', 'uniform-95', 'permuted-output-registers',
                         'split-output-semantics', 'clause-boundaries-511', 'texcoord0-identity',
                         'texcoord0-sparse-output', 'texcoord0-uniform-95', 'texcoord0-clause-boundaries-511'):
            samples.append({'name': case.name, 'position_bits': expected['position'], 'color_bits': expected['color'],
                            'clauses': decoded['clauses'], 'alu_instructions': decoded['alu_instructions'],
                            'program_sha256': hashlib.sha256(bytes.fromhex(result['bytes'])).hexdigest()})
            if textured: samples[-1]['texcoord0_bits'] = expected['texcoord0']
    return {'ok': True, 'cases': len(corpus), 'accepted': accepted, 'rejected': rejected,
            'numeric_preflight_rejected': preflight_rejected, 'decoded_alu_instructions': total_alu,
            'decoded_clauses': total_clauses, 'input_sha256': hashlib.sha256(payload).hexdigest(),
            'output_sha256': hashlib.sha256(process.stdout).hexdigest(), 'decoded_samples': samples,
            'oracle': 'independent exact-rational vector semantics and public Latte ISA decoder',
            'reference_pins': REFERENCE_PINS, 'original_inputs_only': True,
            'hardware_rounding_verified': False, 'console_verified': False}


def check_wut_reference(path):
    path = path / 'samples/cmake/content/pos_col_shader.gsh' if path.is_dir() else path
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != WUT_SHA256: raise ValueError('WUT reference differs from pinned bytes')
    if len(data) < 32 or data[:4] != b'Gfx2': raise ValueError('Invalid GFD header')
    offset, vertex = 32, None
    while offset < len(data):
        if offset + 32 > len(data): raise ValueError('Truncated GFD block')
        magic, header, major, minor, kind, length, _, _ = struct.unpack_from('>8I', data, offset)
        if magic != 0x424c4b7b or header != 32 or major != 1 or minor != 0: raise ValueError('Invalid GFD block')
        end = offset + header + length
        if end > len(data): raise ValueError('GFD payload outside file')
        if kind == 5:
            if vertex is not None: raise ValueError('Ambiguous vertex reference')
            vertex = data[offset + header:end]
        offset = end
    if vertex is None: raise ValueError('WUT reference lacks vertex code')
    values = default_inputs()
    registers = {(register + 1, lane): raw for register, values in enumerate(values) for lane, raw in enumerate(values)}
    decoded = decode(vertex, registers)
    if decoded['alu_instructions'] or decoded['exports'][0]['gpr'] != 2 or decoded['exports'][1]['gpr'] != 1:
        raise ValueError('Pinned WUT reference exports differ from its published assembly')
    return {'ok': True, 'sha256': WUT_SHA256, 'position_gpr': 2, 'color_gpr': 1,
            'exports': decoded['exports'], 'bundled': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cc', default='cc')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--wut-reference', type=Path)
    args = parser.parse_args()
    output = args.output.resolve(); output.mkdir(parents=True, exist_ok=False)
    binary = compile_probe(output, args.cc, args.sanitize)
    report = validate(binary)
    if args.wut_reference: report['wut_reference'] = check_wut_reference(args.wut_reference)
    report.update(source_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                  source_dirty=bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True)),
                  sanitizer=bool(args.sanitize))
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
