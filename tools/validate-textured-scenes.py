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
import math
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
    tev: tuple = ()
    textured: bool = True


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
    return cases+tev_cases()


TEV_OPERATIONS = (0, 1, 2, 3, 4, 5, 8, 9)
RGB_OPERANDS = (0, 1, 2, 3, 4, 5, 8, 9, 12, 13)


def tev_stage(rgb=(3, 0, 0), alpha=None, operation=(0, 0), operands=((0, 0, 0), (0, 0, 0)),
              constant=(0, 0, 0, 0), scale=(0, 0)):
    alpha = rgb if alpha is None else alpha
    return (sum(value << (4*i+16*side) for side, group in enumerate((rgb, alpha)) for i, value in enumerate(group)),
            sum(value << (4*i+(12 if side else 0)) for side, group in enumerate(operands) for i, value in enumerate(group)),
            operation[0] | operation[1] << 16,
            sum(value << (8*i) for i, value in enumerate(constant)), scale[0] | scale[1] << 16)


def tev_chain():
    return (tev_stage(operation=(1, 4), rgb=(3, 14, 0), alpha=(14, 3, 14),
                      constant=(179, 73, 233, 101)),
            tev_stage(rgb=(15, 14, 0), operation=(2, 5), constant=(37, 151, 19, 33)),
            tev_stage(rgb=(15, 3, 14), operation=(4, 8), constant=(91, 211, 47, 83),
                      operands=((0, 1, 2), (0, 2, 1))),
            tev_stage(rgb=(15, 14, 0), operation=(3, 1), constant=(149, 67, 241, 177), scale=(1, 0)),
            tev_stage(rgb=(15, 14, 3), operation=(9, 4), constant=(83, 47, 181, 119)),
            tev_stage(rgb=(15, 14, 3), operation=(8, 2), constant=(113, 199, 59, 17)))


def tev_cases():
    result = []
    for index, op in enumerate(TEV_OPERATIONS):
        stage = tev_stage(rgb=(3, 14, 14), operation=(op, TEV_OPERATIONS[-1-index]),
                          constant=(113, 197, 61, 149))
        for linear in (False, True):
            result.append(Case(f'tev-operation-{op}-filter-{int(linear)}', linear=linear, tev=(stage,)))
    for index, operand in enumerate(RGB_OPERANDS):
        stage = tev_stage(rgb=(3, 14, 14), operation=(4, 4), constant=(37, 199, 83, 157),
                          operands=((operand, RGB_OPERANDS[(index+3)%10], RGB_OPERANDS[(index+6)%10]),
                                    (index%8, (index+3)%8, (index+5)%8)))
        result.append(Case(f'tev-operands-{operand}', tev=(stage,)))
    for rgb in range(3):
        for alpha in range(3):
            result.append(Case(f'tev-scale-{rgb}-{alpha}', tev=(tev_stage(rgb=(3, 14, 14),
                operation=(1, 3), constant=(71, 181, 139, 149), scale=(rgb, alpha)),)))
    result.extend((Case('tev-six-stage-chain', tev=tev_chain()),
                   Case('tev-six-stage-chain-linear', tev=tev_chain(), linear=True, index_bytes=2),
                   Case('tev-six-stage-chain-etc-alpha', tev=tev_chain(), format=13, wrap=(2, 3)),
                   Case('tev-untextured-constant', textured=False,
                        tev=(tev_stage(rgb=(14, 0, 0), constant=(19, 83, 171, 47)),)),
                   Case('tev-previous-alpha-swizzle', tev=(tev_stage(), tev_stage(rgb=(15, 15, 15),
                        operation=(4, 5), operands=((2, 5, 12), (0, 7, 0)))))))
    for error in ('dot3', 'texture1', 'operand', 'scale', 'buffer', 'previous', 'constant'):
        result.append(Case('late-tev-'+error, tev=tev_chain()[:5] if error == 'constant' else tev_chain(), late_error=error))
    return result


def case_stages(case):
    stages = list(case.tev or (tev_stage(rgb=(0, 3, 0) if case.modulate else (3, 0, 0),
                                        operation=(1, 1) if case.modulate else (0, 0)),))
    return stages+[tev_stage(rgb=(15, 0, 0))]*(6-len(stages))


def tev_reference(stages, primary, texture):
    """Independent rational byte-domain TEV; all inputs snapshot the prior stage."""
    previous = (0, 0, 0, 0)
    formulas = {0:lambda a,b,c:a, 1:lambda a,b,c:a*b/255,
                2:lambda a,b,c:a+b, 3:lambda a,b,c:a+b-128,
                4:lambda a,b,c:(a*c+b*(255-c))/255, 5:lambda a,b,c:a-b,
                8:lambda a,b,c:a*b/255+c, 9:lambda a,b,c:min(F(255),a+b)*c/255}
    for stage, row in enumerate(stages):
        sources, operands, functions, constant, scales = row
        if sources & 0x000f000f == 0x000f000f and not operands and not functions and not scales:
            continue
        colors = {0:primary, 3:texture, 14:tuple((constant >> (8*i)) & 255 for i in range(4)), 15:previous}
        next_color = []
        for lane in range(4):
            side = int(lane == 3)
            operation = functions >> (16*side) & 15
            values = []
            count = 1 if operation == 0 else 3 if operation in (4, 8, 9) else 2
            for arg in range(count):
                source = sources >> (16*side+4*arg) & 15
                if source == 15 and stage == 0: raise ValueError('Stage-zero Previous unsupported')
                modifier = operands >> ((12 if side else 0)+4*arg) & (7 if side else 15)
                selected = (3, 0, 1, 2)[modifier//2] if side else {0:lane,2:3,4:0,8:1,12:2}[modifier & ~1]
                value = F(colors[source][selected]);values.append(255-value if modifier & 1 else value)
            value = formulas[operation](*(values+[F(0)]*(3-len(values))))
            integer = max(0, min(255, value.numerator//value.denominator))
            next_color.append(min(255, integer*(1 << (scales >> (16*side) & 3))))
        previous = tuple(next_color)
    return previous


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
    shade_cache = {}
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
                if case.tev:
                    if color not in shade_cache:
                        shade_cache[color] = bytes(tev_reference(case_stages(case), tuple(255*v for v in case.color), color))
                    color = shade_cache[color]
                elif case.modulate:
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
    command(0x080, int(case.textured)); command(0x082, width << 16 | height)
    params = case.wrap[0] << 12 | case.wrap[1] << 8 | (32 if case.format >= 12 else 0) | (6 if case.linear else 0)
    command(0x083, params); command(0x084, 0); command(0x085, 0x1a000000 >> 3)
    command(0x08e, case.format); command(0x08f, 0); command(0x0e0, 0); command(0x0fd, 0)
    for index, (base, row) in enumerate(zip((0xc0, 0xc8, 0xd0, 0xd8, 0xf0, 0xf8), case_stages(case))):
        for offset, value in enumerate(row):
            if not (case.late_error == 'constant' and index == 5 and offset == 3):
                command(base+offset, value)
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
        elif case.late_error == 'dot3': command(0xfa, 6)
        elif case.late_error == 'texture1': command(0xf8, 0x40004)
        elif case.late_error == 'operand': command(0xf9, 6)
        elif case.late_error == 'scale': command(0xfc, 3)
        elif case.late_error == 'buffer': command(0xe0, 1 << 8)
        elif case.late_error == 'previous': command(0xc0, 0xf000f)
        elif case.late_error == 'constant': command(0xf8, 0xe000e)
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
    names = ('ctr_scene.c', 'ctr_tev.c', 'ctr_shader.c', 'ctr_vertex.c', 'ctr_pica.c', 'ctr_texture.c')
    sources = [ROOT/'tests/textured-scenes/reference_probe.c']+[ROOT/'runtime/core'/name for name in names]
    flags = ['-std=c11', '-O2', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-I'+str(ROOT/'runtime/core')]
    if sanitize: flags += ['-O1', '-fno-omit-frame-pointer', '-fsanitize=address,undefined']
    subprocess.run(shlex.split(cc)+flags+list(map(str, sources))+['-lm', '-o', str(binary)], check=True)


def binary32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def decode_fragment_program(row, primary, texel):
    """Independent ISA decoder with binary32 rounding after every scalar ALU.

    Decaf e6c528a20a41c34e0f9eb91dd3da40f119db2dee supplies field facts.
    A supplied normalized texel stands in for SAMPLE_LZ; this is not GPU execution.
    """
    program = bytes.fromhex(row['hex'])
    textured = row.get('textured', True)
    if not 256 <= len(program) <= 8192 or len(program) % 256 or row['gprs'] != 9:
        raise ValueError('Unexpected fragment storage/register allocation')
    count = 2 if textured else 1
    if row['interpolators'] != count or row['spi_ps_in_control_0'] != 0x14000000 | count or row['inputs'] != [0x100,0x101][:count]:
        raise ValueError('Unexpected fragment color/UV interpolation semantics')
    uniforms = row.get('uniforms', [])
    if len(uniforms) != 32: raise ValueError('Missing fragment constants')
    constants = [struct.unpack('<f', struct.pack('<I', value))[0] for value in uniforms]
    if not all(math.isfinite(value) for value in constants): raise ValueError('Nonfinite fragment constants')
    registers = {(0, lane):binary32(value) for lane, value in enumerate(primary)}
    used = set()
    def words(at, count):
        if at < 0 or at+count*4 > len(program): raise ValueError('Fragment clause outside program')
        byte_range = set(range(at, at+count*4))
        if used & byte_range: raise ValueError('Overlapping fragment instructions')
        used.update(byte_range)
        return struct.unpack_from('<'+'I'*count, program, at)
    def source(selector, channel, negative):
        if 256 <= selector < 264: value = constants[(selector-256)*4+channel]
        elif (selector, channel) in registers: value = registers[(selector, channel)]
        else: raise ValueError('Undefined fragment source lane')
        return -value if negative else value
    at, samples, instructions = 0, 0, 0
    while at < 128:
        word0, word1 = words(at, 2);at += 8
        opcode = word1 >> 23 & 127
        if opcode == 1:
            if word1 != 0x80800000 or word0 >> 22: raise ValueError('Unsupported fragment TEX control flow')
            a,b,c,padding = words(word0*8, 4);samples += 1
            if (a & 31) != 0x13 or a >> 8 & 255 or c >> 15 & 31:
                raise ValueError('Expected SAMPLE_LZ at resource/sampler zero')
            src,dest = a >> 16 & 127,b & 127
            source_selectors = [(c >> bit) & 7 for bit in (20,23,26,29)]
            destinations = [(b >> bit) & 7 for bit in (9,12,15,18)]
            if src != 1 or dest != 2 or source_selectors != [0,1,4,5]:
                raise ValueError('Expected original UV source R1.xy01 and sample R2')
            if destinations != [0,1,2,3] or b >> 28 != 15:
                raise ValueError('Expected normalized RGBA texture fetch')
            if a & 0xff80ffe0 or b & 0x0fe00180 or c & 0x00007fff or padding:
                raise ValueError('Unsupported texture offsets/relative/LOD mode')
            for lane,value in enumerate(texel):registers[(2,lane)] = binary32(value/255)
        elif word1 >> 26 & 15 == 8:
            length = (word1 >> 18 & 127)+1
            if not word1 >> 31 or word1 & 0x4203ffff or word0 >> 22:
                raise ValueError('Unsupported fragment ALU control flow')
            for index in range(length):
                a,b = words(word0*8+index*8,2);operation = b >> 7 & 2047
                if not a >> 31 or a & 0x7c401200 or b & 0x9000006f or b >> 18 & 7:
                    raise ValueError('Unsupported grouped/modified fragment ALU')
                if operation not in (0,2,3,4,20,25) or not b & 16:
                    raise ValueError('Unsupported fragment scalar opcode')
                x = source(a & 511,a >> 10 & 3,bool(a & (1 << 12)))
                if operation in (20,25):value = math.floor(x) if operation == 20 else x
                else:
                    y = source(a >> 13 & 511,a >> 23 & 3,bool(a & (1 << 25)))
                    value = {0:lambda:x+y,2:lambda:x*y,3:lambda:max(x,y),4:lambda:min(x,y)}[operation]()
                dest,channel = b >> 21 & 127,b >> 29 & 3
                if dest >= row['gprs']:raise ValueError('Fragment destination exceeds GPR allocation')
                registers[(dest,channel)] = binary32(value);instructions += 1
        elif opcode == 0x28:
            dest = word0 >> 15 & 127
            if word0 & 0xffc07fff or dest not in (3,7) or word1 != 0x94200688:
                raise ValueError('Expected complete PIX0 export and end')
            if samples != int(textured):raise ValueError('Wrong number of base-level texture samples')
            if any(value for index,value in enumerate(program) if index not in used):
                raise ValueError('Nonzero unused fragment program bytes')
            values = tuple(source(dest,lane,False) for lane in range(4))
            # All stage results are integer bytes. UNORM export is well away
            # from half-byte ties; model its final rounding independently.
            color = tuple(F(max(0,min(255,math.floor(value*255+0.5))),255) for value in values)
            return {'color':color,'linear_color':values,'samples':samples,'alu_instructions':instructions}
        else:raise ValueError('Unsupported fragment control flow')
    raise ValueError('Missing fragment program end')


def validate_fragment_programs(path):
    capture = json.loads(path.read_text());rows = capture.get('programs', [])
    if [row.get('mode') for row in rows[:2]] != ['replace','modulate']:
        raise ValueError('Require actual replace and modulate PS captures')
    cases = 0;max_instructions = 0
    texels = ((0,255,127,0),(255,0,1,255),(17,89,233,53),(128,129,130,131),
              (1,254,128,127),(255,255,255,255),(0,0,0,0),(127,128,129,254))
    for row in rows:
        for mask in range(8):
            primary = tuple((mask >> channel) & 1 for channel in range(3))+(1,)
            for texel in texels:
                result = decode_fragment_program(row,primary,texel)
                expected = tev_reference(row['stages'],tuple(255*v for v in primary),texel)
                if result['color'] != tuple(F(v,255) for v in expected):
                    raise ValueError(f"{row['mode']}: decoded fragment bytes differ: {result['color']} != {expected}")
                max_instructions = max(max_instructions,result['alu_instructions']);cases += 1
    # Exhaust every bounded integer numerator, including exact multiples of
    # 255. Binary32 reciprocal multiplication must never cross a floor boundary.
    reciprocal = binary32(1/255)
    for value in range(65026):
        if math.floor(binary32(value*reciprocal)) != value//255:
            raise ValueError('Fragment reciprocal does not preserve integer division')
    return {'programs':len(rows),'arithmetic_cases':cases,'integer_division_cases':65026,
            'max_alu_instructions':max_instructions,'source_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
            'capture':'actual native-adapter PS bytes and constants bound through the host GX2 API model',
            'reference_pin':'e6c528a20a41c34e0f9eb91dd3da40f119db2dee',
            'console_verified':False,'gpu_execution':False}


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
            'tev_stages':case_stages(case), 'textured':case.textured,
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
