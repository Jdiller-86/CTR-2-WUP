#!/usr/bin/env python3
"""Compare original sampled PICA scenes with exact rational pixel expectations.

This checks the owned portable path, not console execution or GPU performance.
The sampler, interpolation and original texture encoders are independent of C.
"""
import argparse
from dataclasses import dataclass, replace
from fractions import Fraction as F
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
from renderer.texture import decode_texture, required_bytes

BACKGROUND = bytes((16, 32, 48, 255))


@dataclass(frozen=True)
class Case:
    name: str
    format: int = 0
    dimensions: tuple = (8, 8)
    points: tuple = ((8, 8), (56, 8), (8, 56), (56, 56))
    uv: tuple = ((F(0), F(0)), (F(1), F(0)), (F(0), F(1)), (F(1), F(1)))
    indices: tuple = ((0, 1, 2), (1, 3, 2))
    index_bytes: int = 1
    wrap: tuple = (0, 0)
    modulate: bool = False
    color: tuple = (1, 1, 1, 1)
    scissor: tuple = (0, 0, 0, 63, 63)
    cull: int = 0
    late_error: str = ''
    linear: bool = False
    side: int = 64


def original_cases():
    cases = []
    for format in range(14):
        cases.append(Case(f'format-{format:02}-clamp', format=format,
                          dimensions=(8, 16) if format & 1 else (16, 8)))
        cases.append(Case(f'format-{format:02}-repeat', format=format,
                          dimensions=(16, 16), index_bytes=2, wrap=(2, 2),
                          uv=((F(-5, 4), F(9, 4)), (F(9, 4), F(9, 4)),
                              (F(-5, 4), F(-5, 4)), (F(9, 4), F(-5, 4)))))
        cases.append(Case(f'format-{format:02}-mirror-modulate', format=format,
                          dimensions=(16, 8), index_bytes=2, wrap=(3, 3),
                          modulate=True, color=(1, 0, 1, 1),
                          uv=((F(-9, 8), F(17, 8)), (F(25, 8), F(17, 8)),
                              (F(-9, 8), F(-15, 8)), (F(25, 8), F(-15, 8)))))
    asymmetric = Case('asymmetric-fractional-mixed-wrap', dimensions=(32, 16),
                      points=((F(17, 2), F(33, 4)), (F(111, 2), F(49, 4)),
                              (F(53, 4), F(221, 4)), (F(215, 4), F(207, 4))),
                      uv=((F(-7, 4), F(13, 8)), (F(19, 8), F(15, 8)),
                          (F(-3, 2), F(-9, 8)), (F(21, 8), F(-7, 8))), wrap=(3, 2))
    cases += [asymmetric,
              replace(asymmetric, name='inside-scissor', scissor=(3, 19, 17, 43, 46)),
              replace(asymmetric, name='outside-scissor', scissor=(1, 19, 17, 43, 46)),
              replace(asymmetric, name='reversed-winding', indices=((2, 1, 0), (2, 3, 1))),
              replace(asymmetric, name='culled-front', cull=1),
              replace(asymmetric, name='culled-back', cull=2),
              replace(asymmetric, name='nonindexed', index_bytes=0),
              Case('constant-uv-negative-integer', uv=((F(-1), F(-1)),)*4, wrap=(2, 3)),
              Case('constant-uv-clamp-upper', uv=((F(1), F(1)),)*4),
              Case('constant-uv-clamp-lower', uv=((F(-1, 65536), F(-1, 65536)),)*4),
              Case('constant-uv-mirror-odd-boundary', uv=((F(1), F(-1)),)*4, wrap=(3, 3)),
              Case('constant-uv-mirror-even-boundary', uv=((F(2), F(-2)),)*4, wrap=(3, 3)),
              Case('maximum-q16-uv', uv=((F(-16), F(16)),)*4, wrap=(2, 3)),
              Case('maximum-base-dimension', dimensions=(1024, 8)),
              Case('late-unsupported-filter', late_error='filter'),
              Case('late-truncated-binding', late_error='binding'),
              Case('late-invalid-uv', late_error='uv')]
    cases += [replace(case,name='linear-'+case.name,linear=True) for case in cases]
    cases += [Case('linear-half-up-tie',linear=True,uv=((F(1,8),F(1,16)),)*4),
              Case('linear-texel-center',linear=True,uv=((F(3,16),F(5,16)),)*4),
              Case('linear-mixed-filter',linear=True,late_error='mixed-filter'),
              Case('linear-mip-filter',linear=True,late_error='mip-filter')]
    cases.append(Case('linear-maximum-area-wide-weights',linear=True,side=512,
        dimensions=(1024,8),points=((0,0),(512,0),(0,512),(512,512)),
        uv=((F(-16),F(16)),(F(16),F(-16)),(F(1,2),F(1,2)),(F(15),F(-15))),
        wrap=(2,3),scissor=(3,253,253,255,255)))
    return cases


def morton_coordinates(size=8):
    """Recursive quadrant order, rather than the runtime's bit-shift indexing."""
    if size == 1:
        return [(0, 0)]
    half = size // 2
    child = morton_coordinates(half)
    return [(x+ox, y+oy) for ox, oy in ((0, 0), (half, 0), (0, half), (half, half))
            for x, y in child]


def original_texture(format, width, height):
    data = bytearray()
    if format >= 12:
        for ty in range(height//8):
            for tx in range(width//8):
                for by, bx in ((0, 0), (0, 1), (1, 0), (1, 1)):
                    k = (tx+3*ty+bx+5*by) % 13
                    # Individual ETC1 mode: two unequal RGB bases and both
                    # selector planes vary in X/Y. No ETC2 differential mode.
                    high = (((k+1) % 16)*16+(k+7) % 16) << 24
                    high |= (((k+4) % 16)*16+(k+10) % 16) << 16
                    high |= (((k+8) % 16)*16+(k+13) % 16) << 8
                    high |= ((k % 8) << 5) | (((k+3) % 8) << 2) | (k & 1)
                    low = sum(((x+2*y+k) & 1) << (4*x+y) |
                              (((x+y+k)//2) & 1) << (16+4*x+y)
                              for x in range(4) for y in range(4))
                    if format == 13:
                        alpha = sum(((x*3+y*5+k) % 16) << (4*(x*4+y))
                                    for x in range(4) for y in range(4))
                        data += alpha.to_bytes(8, 'little')
                    data += struct.pack('<II', low, high)
        return bytes(data)
    nibbles = []
    for ty in range(height//8):
        for tx in range(width//8):
            for dx, dy in morton_coordinates():
                x, y = tx*8+dx, ty*8+dy
                r, g, b, a = ((29*x+13*y+31) % 256, (7*x+37*y+89) % 256,
                              (43*x+3*y+149) % 256, (19*x+23*y+53) % 256)
                if format == 0: data += bytes((a, b, g, r))
                elif format == 1: data += bytes((b, g, r))
                elif format == 2: data += struct.pack('<H', ((r//8)<<11)|((g//8)<<6)|((b//8)<<1)|(a//128))
                elif format == 3: data += struct.pack('<H', ((r//8)<<11)|((g//4)<<5)|(b//8))
                elif format == 4: data += struct.pack('<H', ((r//16)<<12)|((g//16)<<8)|((b//16)<<4)|(a//16))
                elif format == 5: data += bytes((a, r))
                elif format == 6: data += bytes((g, r))
                elif format == 7: data.append(r)
                elif format == 8: data.append(a)
                elif format == 9: data.append((r//16)*16+a//16)
                else: nibbles.append((r if format == 10 else a)//16)
    if nibbles:
        data += bytes(low+16*high for low, high in zip(nibbles[::2], nibbles[1::2]))
    assert len(data) == required_bytes(format, width, height)
    return bytes(data)


def sampled_texel(coordinate, extent, wrap):
    raw = coordinate*extent
    integer = raw.numerator//raw.denominator
    if wrap == 0:
        return min(extent-1, max(0, integer))
    if wrap == 2:
        return integer % extent
    if wrap == 3:
        phase = integer % (2*extent)
        return phase if phase < extent else 2*extent-1-phase
    raise ValueError('unsupported wrap')


def barycentric(point, triangle):
    """Solve the affine basis exactly; return None for a degenerate triangle."""
    (ax, ay), (bx, by), (cx, cy) = triangle
    px, py = point
    determinant = F((bx-ax)*(cy-ay)-(by-ay)*(cx-ax))
    if not determinant:
        return None
    beta = ((px-ax)*(cy-ay)-(py-ay)*(cx-ax))/determinant
    gamma = ((bx-ax)*(py-ay)-(by-ay)*(px-ax))/determinant
    return F(1)-beta-gamma, beta, gamma


def sampled_color(rgba, width, height, u, v, wrap, linear):
    if not linear:
        x=sampled_texel(u,width,wrap[0]);y=height-1-sampled_texel(v,height,wrap[1])
        return rgba[(y*width+x)*4:(y*width+x+1)*4]
    # Rational ideal: texel centers, independently wrapped taps, one final
    # round-half-up. No runtime limbs or intermediate-rounding model.
    axes=[]
    for value,extent,mode in zip((u,v),(width,height),wrap):
        position=value*extent-F(1,2)
        base=position.numerator//position.denominator;fraction=position-base
        axes.append([(sampled_texel(F(base+i,extent),extent,mode),weight)
                     for i,weight in ((0,1-fraction),(1,fraction))])
    color=[]
    for lane in range(4):
        value=sum(wx*wy*rgba[((height-1-y)*width+x)*4+lane]
                  for x,wx in axes[0] for y,wy in axes[1])+F(1,2)
        color.append(value.numerator//value.denominator)
    return bytes(color)


def golden(case, texture):
    width, height = case.dimensions
    rgba = decode_texture(texture, case.format, width, height)
    pixels = bytearray(BACKGROUND*case.side*case.side)
    fragments = 0
    for indices in case.indices:
        points = tuple(tuple(map(F, case.points[i])) for i in indices)
        uv = tuple(case.uv[i] for i in indices)
        a, b, c = points
        determinant = (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])
        if not determinant or (case.cull == 1 and determinant > 0) or (case.cull == 2 and determinant < 0):
            continue
        if determinant < 0:
            points = points[0], points[2], points[1]
            uv = uv[0], uv[2], uv[1]
        for y in range(case.side):
            for x in range(case.side):
                mode, x1, y1, x2, y2 = case.scissor
                box = x1 <= x <= x2 and y1 <= y <= y2
                if (mode == 3 and not box) or (mode == 1 and box):
                    continue
                weights = barycentric((F(2*x+1, 2), F(2*y+1, 2)), points)
                if weights is None or any(w < 0 for w in weights):
                    continue
                # A zero barycentric weight lies on its opposite edge. The
                # published half-open fill rule owns downward/leftward edges.
                include = True
                for lane, weight in enumerate(weights):
                    start, end = points[(lane+1) % 3], points[(lane+2) % 3]
                    if weight == 0 and not (end[1] < start[1] or (end[1] == start[1] and end[0] < start[0])):
                        include = False
                if not include:
                    continue
                u, v = (sum(weight*value[axis] for weight, value in zip(weights, uv)) for axis in range(2))
                color = sampled_color(rgba,width,height,u,v,case.wrap,case.linear)
                if case.modulate:
                    color = bytes(component*mask for component, mask in zip(color, case.color))
                pixels[(y*case.side+x)*4:(y*case.side+x+1)*4] = color
                fragments += 1
    return bytes(pixels), fragments


def command_bytes(case):
    commands = []
    def command(reg, value): commands.append((value, 0xf0000 | reg))
    half=(case.side.bit_length()+61)<<16
    command(0x040, case.cull); command(0x041, half); command(0x043, half)
    mode, x1, y1, x2, y2 = case.scissor
    command(0x065, mode); command(0x066, x1 | y1 << 16); command(0x067, x2 | y2 << 16)
    command(0x068, 0)
    command(0x04f, 3); command(0x050, 0x03020100); command(0x051, 0x0b0a0908)
    command(0x052, 0x1f1f0d0c)
    command(0x200, 0x03000000); command(0x201, 0x7ff); command(0x202, 0x20020000)
    command(0x203, 0); command(0x204, 0x20); command(0x205, 0x20180000)
    command(0x228, 3); command(0x229, 0); command(0x22a, 0); command(0x25e, 0)
    command(0x2b9, 0xa0000002); command(0x2ba, 0)
    command(0x2bb, 0x76543210); command(0x2bc, 0xfedcba98); command(0x2bd, 7)
    command(0x2cb, 0)
    for instruction in (0x4c000000, 0x4c201000, 0x4c402000, 0x88000000):
        command(0x2cc, instruction)
    command(0x2bf, 1); command(0x2d5, 0); command(0x2d6, 0x36f)
    # Fixed input1 is an exact original opaque binary RGBA vector. Upload
    # float24 as three packed words; field construction is independent of C.
    raw = [0x3f0000 if component else 0 for component in case.color]
    packed = raw[0] | raw[1] << 24 | raw[2] << 48 | raw[3] << 72
    command(0x232, 1)
    for shift in (64, 32, 0): command(0x233+((64-shift)//32), (packed >> shift) & 0xffffffff)
    width, height = case.dimensions
    command(0x080, 1); command(0x082, width << 16 | height)
    params = case.wrap[0] << 12 | case.wrap[1] << 8 | (32 if case.format >= 12 else 0) | (6 if case.linear else 0)
    command(0x083, params); command(0x084, 0); command(0x085, 0x1a000000 >> 3)
    command(0x08e, case.format); command(0x08f, 0); command(0x0e0, 0); command(0x0fd, 0)
    for index, base in enumerate((0xc0, 0xc8, 0xd0, 0xd8, 0xf0, 0xf8)):
        command(base, 0x300030 if not index and case.modulate else 0x30003 if not index else 0xf000f)
        command(base+1, 0); command(base+2, 0x10001 if not index and case.modulate else 0)
        command(base+4, 0)
    geometry = bytearray()
    vertices = range(len(case.points)) if case.index_bytes else [i for triangle in case.indices for i in triangle]
    for i in vertices:
        x, y = case.points[i]
        geometry += struct.pack('<6f', float(F(x)*2/case.side-1), float(F(y)*2/case.side-1), -.5, 1,
                                *map(float, case.uv[i]))
    index_offset = len(geometry)
    if case.index_bytes:
        for triangle in case.indices:
            geometry += struct.pack('<'+('B' if case.index_bytes == 1 else 'H')*3, *triangle)
    for draw in range(len(case.indices)):
        if case.index_bytes:
            command(0x227, (0x80000000 if case.index_bytes == 2 else 0) | (index_offset+draw*3*case.index_bytes))
            command(0x22f, 1)
        else:
            command(0x22a, draw*3); command(0x22e, 1)
    if case.late_error:
        if case.late_error in ('filter','mixed-filter'): command(0x083, (params & ~6) | 2)
        elif case.late_error == 'mip-filter': command(0x083, params | (1 << 24))
        elif case.late_error == 'binding': command(0x085, (0x1a000000+8) >> 3)
        elif case.late_error == 'uv':
            # A later shader changes U beyond the exact supported Q16 bound;
            # earlier valid texture draws must still roll back atomically.
            command(0x2c0, 0x80000000)
            for component in (0, 0, 0, 17):
                command(0x2c1, struct.unpack('<I', struct.pack('<f', component))[0])
            command(0x2cb, 2); command(0x2cc, 0x4c420000)
        command(0x22f if case.index_bytes else 0x22e, 1)
    command(0x010, 0x12345678)
    if len(commands) & 1: command(0x010, 0x12345678)
    return b''.join(struct.pack('<II', *pair) for pair in commands), bytes(geometry)


def compile_probe(binary, cc='cc', sanitize=False):
    names = ('ctr_scene.c', 'ctr_shader.c', 'ctr_vertex.c', 'ctr_pica.c', 'ctr_texture.c')
    sources = [ROOT/'tests/textured-scenes/reference_probe.c']+[ROOT/'runtime/core'/name for name in names]
    flags = ['-std=c11', '-O2', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-I'+str(ROOT/'runtime/core')]
    if sanitize: flags += ['-O1', '-fno-omit-frame-pointer', '-fsanitize=address,undefined']
    subprocess.run(shlex.split(cc)+flags+list(map(str, sources))+['-lm', '-o', str(binary)], check=True)


def decode_fragment_program(row, primary, texel):
    """Decode captured Latte PS bytes using ISA facts, without encoder helpers.

    References: Decaf e6c528a20a41c34e0f9eb91dd3da40f119db2dee,
    src/libgpu/latte/latte_instructions.h and latte_instructions_def.inl.
    TEX_SAMPLE_LZ samples the independently supplied normalized RGBA texel;
    no desktop GPU execution is inferred by this small instruction oracle.
    """
    program = bytes.fromhex(row['hex'])
    if len(program) != 256 or row['gprs'] != 3 or row['interpolators'] != 2:
        raise ValueError('Unexpected fragment storage/register allocation')
    if row['spi_ps_in_control_0'] != 0x14000002 or row['inputs'] != [0x100, 0x101]:
        raise ValueError('Unexpected fragment color/UV interpolation semantics')
    registers = {(0, lane):F(component) for lane, component in enumerate(primary)}
    used = set()
    def words(at, count):
        if at < 0 or at+count*4 > len(program):
            raise ValueError('Fragment clause outside program')
        byte_range = set(range(at, at+count*4))
        if used & byte_range:
            raise ValueError('Overlapping fragment instructions')
        used.update(byte_range)
        return struct.unpack_from('<'+'I'*count, program, at)
    at, samples, instructions = 0, 0, 0
    while at < 32:
        word0, word1 = words(at, 2)
        at += 8
        opcode = word1 >> 23 & 127
        if opcode == 1:
            # TEX CF: one instruction, active/unconditional, barrier. The
            # address unit is eight bytes; a TEX occupies four words.
            if word1 != 0x80800000 or word0 >> 22:
                raise ValueError('Unsupported fragment TEX control flow')
            a, b, c, padding = words(word0*8, 4)
            samples += 1
            if (a & 31) != 0x13 or (a >> 8 & 255) or (c >> 15 & 31):
                raise ValueError('Expected SAMPLE_LZ at resource/sampler zero')
            source = a >> 16 & 127
            destination = b & 127
            source_selectors = [(c >> bit) & 7 for bit in (20, 23, 26, 29)]
            destinations = [(b >> bit) & 7 for bit in (9, 12, 15, 18)]
            if source != 1 or destination != 2 or source_selectors != [0, 1, 4, 5]:
                raise ValueError('Expected original UV source R1.xy01 and sample R2')
            if destinations != [0, 1, 2, 3] or b >> 28 != 15:
                raise ValueError('Expected normalized RGBA texture fetch')
            if a & 0xff80ffe0 or b & 0x0fe00180 or c & 0x00007fff or padding:
                raise ValueError('Unsupported texture offsets/relative/LOD mode')
            for lane, component in enumerate(texel):
                registers[(destination, lane)] = F(component, 255)
        elif word1 >> 26 & 15 == 8:
            count = (word1 >> 18 & 127)+1
            if not word1 >> 31 or word1 & 0x4203ffff or word0 >> 22:
                raise ValueError('Unsupported fragment ALU control flow')
            for index in range(count):
                a, b = words(word0*8+index*8, 2)
                # Scalar independent LAST groups, no predicates, relative
                # addressing, output modifiers, clamping or absolutes.
                if not a >> 31 or a & 0x7c401200 or b & 0x9000006f or b >> 18 & 7:
                    raise ValueError('Unsupported grouped/modified fragment ALU')
                if b >> 7 & 2047 != 2 or not b & 16:
                    raise ValueError('Expected fragment MUL_IEEE with write mask')
                sources = ((a & 511, a >> 10 & 3, bool(a & (1 << 12))),
                           (a >> 13 & 511, a >> 23 & 3, bool(a & (1 << 25))))
                values = []
                for register, channel, negative in sources:
                    if (register, channel) not in registers:
                        raise ValueError('Undefined fragment source lane')
                    value = registers[(register, channel)]
                    values.append(-value if negative else value)
                destination, channel = b >> 21 & 127, b >> 29 & 3
                if destination != 2:
                    raise ValueError('Unexpected fragment multiplication destination')
                registers[(destination, channel)] = values[0]*values[1]
                instructions += 1
        elif opcode == 0x28:
            if word0 & 0xffc07fff or (word0 >> 15 & 127) != 2:
                raise ValueError('Expected PIX0 export from sample register R2')
            if word1 != 0x94200688:
                raise ValueError('Expected complete RGBA pixel export and end')
            if samples != 1:
                raise ValueError('Expected one base-level texture sample')
            if any(byte for index, byte in enumerate(program) if index not in used):
                raise ValueError('Nonzero unused fragment program bytes')
            return {'color':tuple(registers[(2, lane)] for lane in range(4)),
                    'samples':samples, 'alu_instructions':instructions}
        else:
            raise ValueError('Unsupported fragment control flow')
    raise ValueError('Missing fragment program end')


def validate_fragment_programs(path):
    capture = json.loads(path.read_text())
    rows = capture.get('programs', [])
    if [row.get('mode') for row in rows] != ['replace', 'modulate']:
        raise ValueError('Require actual replace and modulate PS captures')
    cases = 0
    for row in rows:
        for mask in range(8):
            primary = tuple((mask >> channel) & 1 for channel in range(3))+(1,)
            for texel in ((0, 255, 127, 0), (255, 0, 1, 255), (17, 89, 233, 53), (128, 129, 130, 131)):
                result = decode_fragment_program(row, primary, texel)
                expected = tuple(F(value*(primary[lane] if row['mode'] == 'modulate' else 1), 255)
                                 for lane, value in enumerate(texel))
                if result['color'] != expected or result['alu_instructions'] != (4 if row['mode'] == 'modulate' else 0):
                    raise ValueError('Decoded fragment pixels differ from independent TEV arithmetic')
                cases += 1
    return {'programs':len(rows), 'arithmetic_cases':cases, 'source_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
            'capture':'actual native-adapter PS bytes bound through the host GX2 API model',
            'reference_pin':'e6c528a20a41c34e0f9eb91dd3da40f119db2dee',
            'console_verified':False, 'gpu_execution':False}


def execute_case(binary, output, case):
    commands, geometry = command_bytes(case)
    texture = original_texture(case.format, *case.dimensions)
    paths = [output/(case.name+suffix) for suffix in ('.commands', '.resources', '.texture', '.rgba')]
    for path, data in zip(paths, (commands, geometry, texture)):
        path.write_bytes(data)
    result = json.loads(subprocess.check_output([str(binary), *map(str, paths),str(case.side)], text=True))
    actual = paths[-1].read_bytes()
    expected, fragments = golden(case, texture)
    if case.late_error:
        if result['result'] == 'ok' or not result['state_unchanged'] or not result['pixels_unchanged']:
            raise ValueError(case.name+': late invalid state committed')
        if actual != BACKGROUND*case.side*case.side or result['draws'] != 2:
            raise ValueError(case.name+': missing staged prefix or pixel rollback')
    else:
        counters = {'result':'ok', 'draws':len(case.indices), 'triangles':len(case.indices),
                    'shader_instructions':len(case.indices)*3*4, 'fragments':fragments}
        if any(result.get(name) != value for name, value in counters.items()):
            raise ValueError(case.name+': scene execution differs: '+str(result)+', expected fragments='+str(fragments))
        if actual != expected:
            at = next(index for index, pair in enumerate(zip(actual, expected)) if pair[0] != pair[1])
            raise ValueError(f'{case.name}: independent exact pixel mismatch at ({at//4%case.side},{at//(case.side*4)}) lane{at%4}')
    return {'name':case.name, 'format':case.format, 'dimensions':case.dimensions,
            'index_bytes':case.index_bytes, 'wrap':case.wrap, 'modulate':case.modulate,'linear':case.linear,'side':case.side,
            'late_error':case.late_error or None, **result,
            'expected_rgba_sha256':hashlib.sha256(BACKGROUND*case.side*case.side if case.late_error else expected).hexdigest(),
            'files':[{'name':path.name, 'size':path.stat().st_size,
                      'sha256':hashlib.sha256(path.read_bytes()).hexdigest()} for path in paths]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cc', default='cc')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--fragment-programs', type=Path,
                        help='JSON from the native GX2 API model --fragment-programs capture')
    args = parser.parse_args()
    output = args.output.resolve(); output.mkdir(parents=True, exist_ok=False)
    report = {'ok':False, 'version':__version__, 'original_fixtures_only':True, 'sanitized':args.sanitize,
              'source_commit':subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
              'source_dirty':bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True)),
              'console_verified':False, 'gx2_execution':False, 'guest_gsp_draws':False,
              'pixel_tolerance':0, 'sampler_reference':'exact Fraction barycentric interpolation, floor, wrap and raw-memory V inversion',
              'texture_reference':'original recursive Morton encoding and independently tested renderer.texture decoder',
              'cases':[]}
    try:
        binary = output/'reference-probe'
        compile_probe(binary, args.cc, args.sanitize)
        for case in original_cases():
            report['cases'].append(execute_case(binary, output, case))
        if args.fragment_programs:
            capture = output/'gx2-fragment-programs.json'
            capture.write_bytes(args.fragment_programs.resolve().read_bytes())
            report['gx2_fragment_reference'] = validate_fragment_programs(capture)
        sources = [Path(__file__).resolve(), ROOT/'tests/textured-scenes/reference_probe.c', ROOT/'renderer/texture.py']
        report['reference_sources'] = [{'path':str(path.relative_to(ROOT)),
                                        'sha256':hashlib.sha256(path.read_bytes()).hexdigest()} for path in sources]
        report['ok'] = True
    finally:
        (output/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({'ok':report['ok'], 'cases':len(report['cases']), 'report':str(output/'report.json')}))


if __name__ == '__main__':
    main()
