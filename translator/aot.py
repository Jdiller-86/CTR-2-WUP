#!/usr/bin/env python3
"""Bounded, fail-closed ARMv6K/Thumb-to-C translator for flat test executables.

This deliberately does not claim container loading, block discovery, VFP arithmetic, or
arbitrary game compatibility. Each instruction becomes a specialized C function;
the generated path does not call the interpreter or decode instructions at run time.
"""
from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import re
import struct
import tempfile


def literal(value: int) -> str:
    return f"UINT32_C(0x{value & 0xffffffff:08x})"


def register(index: int, pc: int, thumb: bool = False) -> str:
    return literal(pc + (4 if thumb else 8)) if index == 15 else f"cpu->r[{index}]"

def multiple(rn, registers, increment, before, writeback, load, is_thumb, opcode, next_pc):
    if rn==15 or not registers or (is_thumb and not load and registers&0x8000):
        raise ValueError("unsupported multiple-register list/base")
    if writeback and registers&(1<<rn) and (load or registers&((1<<rn)-1)):
        raise ValueError("unpredictable multiple-transfer base/writeback combination")
    flags=', '.join('true' if flag else 'false' for flag in (increment,before,writeback,load,is_thumb))
    result=[f"ctr_cpu_result result = ctr_cpu_multiple(cpu, {rn}u, {literal(registers)}, {flags}, {literal(opcode)});",
            "if (result != CTR_CPU_OK) return result;"]
    if not load or not registers&0x8000:result.append(f"cpu->r[15] = {literal(next_pc)};")
    return result+["return CTR_CPU_OK;"]


def arm(insn: int, pc: int) -> list[str]:
    cond = insn >> 28
    if cond == 15:
        if insn & 0xfe000000 == 0xfa000000:
            offset=(insn&0xffffff)<<2|((insn>>23)&2)
            if offset&0x2000000:offset-=0x4000000
            return ['++cpu->instructions;',f'cpu->r[14] = {literal(pc+4)};',
                    f'ctr_cpu_branch_exchange(cpu, {literal((pc+8+offset)|1)});','return CTR_CPU_OK;']
        if insn==0xf57ff01f:
            return ['++cpu->instructions;','cpu->exclusive_valid = false;',f'cpu->r[15] = {literal(pc+4)};','return CTR_CPU_OK;']
        if insn&0xfd70f000==0xf550f000:
            return ['++cpu->instructions;',f'cpu->r[15] = {literal(pc+4)};','return CTR_CPU_OK;']
        raise ValueError("unconditional extension space is unsupported")
    next_pc = literal(pc + 4)
    body = ["++cpu->instructions;"]
    if cond != 14:
        body += [f"if (!ctr_cpu_condition(cpu, {cond}u)) {{ cpu->r[15] = {next_pc}; return CTR_CPU_OK; }}"]
    def done(statements):
        return body+statements+[f'cpu->r[15] = {next_pc};','return CTR_CPU_OK;']
    dsp=insn&0x0ff00090;kind=None;control=0
    if dsp==0x01600080:kind=0;control=(insn>>5)&3
    elif dsp==0x01200080:kind=2 if insn&0x20 else 1;control=(insn>>6)&1
    elif dsp==0x01400080:kind=3;control=(insn>>5)&3
    elif dsp in (0x07000010,0x07400010):kind=(4 if dsp==0x07000010 else 6)+((insn>>6)&1);control=(insn>>5)&1
    elif insn&0x0ff00010==0x07500010 and (insn>>6)&3 in (0,3):kind=9 if insn&0x40 else 8;control=(insn>>5)&1
    elif insn&0x0ff000f0==0x07800010:kind=10
    if kind is not None:
        rd,ra,rm,rs=(insn>>16)&15,(insn>>12)&15,insn&15,(insn>>8)&15
        if 15 in (rd,rm,rs) or (kind in (3,6,7) and (ra==15 or ra==rd)) or (kind in (1,9) and ra==15):raise ValueError('invalid DSP multiply registers')
        return done([f'ctr_cpu_result result = ctr_cpu_dsp_multiply(cpu, {kind}u, {rd}u, {ra}u, {rm}u, {rs}u, {control}u, {literal(insn)});','if (result != CTR_CPU_OK) return result;'])
    if insn&0x0fb00ff0==0x01000090:
        rn,rd,rm=(insn>>16)&15,(insn>>12)&15,insn&15
        if 15 in (rn,rd,rm) or rn in (rd,rm):raise ValueError('invalid swap registers')
        return done([f'ctr_cpu_result result = ctr_cpu_swap(cpu, {rn}u, {rd}u, {rm}u, {1 if insn&0x400000 else 4}u, {literal(insn)});','if (result != CTR_CPU_OK) return result;'])
    if insn&0x0f900fff==0x01900f9f or insn&0x0f900ff0==0x01800f90:
        rn,rd,rm=(insn>>16)&15,(insn>>12)&15,insn&15;load=bool(insn&0x100000);width=(4,8,1,2)[(insn>>21)&3]
        if 15 in (rn,rd) or (not load and (rm==15 or rd in (rn,rm))) or (width==8 and ((load and ((rd&1) or rd>=14)) or (not load and ((rm&1) or rm>=14 or rd==rm+1)))):raise ValueError('invalid exclusive registers')
        return done([f'ctr_cpu_result result = ctr_cpu_exclusive(cpu, {rn}u, {rd}u, {0 if load else rm}u, {width}u, {"true" if load else "false"}, {literal(insn)});','if (result != CTR_CPU_OK) return result;'])
    if insn&0x0f800f10==0x06000f10 and insn&0x0ff00ff0 not in (0x06500f90,0x06500ff0):
        mode,op,rd,rn,rm=(insn>>20)&7,(insn>>5)&7,(insn>>12)&15,(insn>>16)&15,insn&15
        if mode in (0,4) or op in (5,6) or 15 in (rd,rn,rm):raise ValueError('invalid parallel arithmetic encoding')
        return done([f'cpu->r[{rd}] = ctr_cpu_parallel(cpu, cpu->r[{rn}], cpu->r[{rm}], {mode}u, {op}u);'])
    if insn&0x0fa00030==0x06a00010 or insn&0x0fb00ff0==0x06a00f30:
        rd,rm=(insn>>12)&15,insn&15;unsign=bool(insn&0x400000);lanes=insn&0x0fb00ff0==0x06a00f30
        if 15 in (rd,rm):raise ValueError('saturation PC operand is unsupported')
        bits=((insn>>16)&(15 if lanes else 31))+(0 if unsign else 1);unsigned='true' if unsign else 'false'
        if lanes:
            return done([f'uint32_t source = cpu->r[{rm}];',f'uint32_t low = ctr_cpu_saturate(cpu, (uint32_t)ctr_cpu_signed_half(source, 0u), {bits}u, {unsigned});',f'uint32_t high = ctr_cpu_saturate(cpu, (uint32_t)ctr_cpu_signed_half(source, 1u), {bits}u, {unsigned});',f'cpu->r[{rd}] = (low & 65535u) | (high << 16);'])
        return done(['unsigned ignored;',f'uint32_t value = ctr_cpu_shift(cpu->r[{rm}], {2 if insn&0x40 else 0}u, {(insn>>7)&31}u, true, 0u, &ignored);',f'cpu->r[{rd}] = ctr_cpu_saturate(cpu, value, {bits}u, {unsigned});'])
    if insn&0x0ff00030==0x06800010:
        rd,rn,rm=(insn>>12)&15,(insn>>16)&15,insn&15;top=bool(insn&0x40)
        if 15 in (rd,rn,rm):raise ValueError('pack PC operand is unsupported')
        expression=f'(cpu->r[{rn}] & 0xffff0000u) | (value & 65535u)' if top else f'(cpu->r[{rn}] & 65535u) | (value & 0xffff0000u)'
        return done(['unsigned ignored;',f'uint32_t value = ctr_cpu_shift(cpu->r[{rm}], {2 if top else 0}u, {(insn>>7)&31}u, true, 0u, &ignored);',f'cpu->r[{rd}] = {expression};'])
    extend=insn&0x0f8003f0
    if extend==0x06800070 and (insn>>20)&7 in (0,2,3,4,6,7):
        kind,rn,rd,rm=(insn>>20)&7,(insn>>16)&15,(insn>>12)&15,insn&15
        if 15 in (rd,rm):raise ValueError('extension PC operand is unsupported')
        return done([f'cpu->r[{rd}] = ctr_cpu_extend(cpu->r[{rm}], {kind}u, {((insn>>10)&3)*8}u, {register(rn,pc) if rn!=15 else "0u"}, {"true" if rn!=15 else "false"});'])
    media=insn&0x0fff0ff0
    if media in (0x06bf0f30,0x06bf0fb0,0x06ff0fb0,0x016f0f10):
        rd,rm=(insn>>12)&15,insn&15
        if 15 in (rd,rm):raise ValueError('reverse/count PC operand is unsupported')
        expression=f'ctr_cpu_clz(cpu->r[{rm}])' if media==0x016f0f10 else f'ctr_cpu_reverse(cpu->r[{rm}], {(0x06bf0f30,0x06bf0fb0,0x06ff0fb0).index(media)}u)'
        return done([f'cpu->r[{rd}] = {expression};'])
    if insn&0x0f900ff0==0x01000050:
        rd,rn,rm=(insn>>12)&15,(insn>>16)&15,insn&15
        if 15 in (rd,rn,rm):raise ValueError('saturating PC operand is unsupported')
        return done([f'cpu->r[{rd}] = ctr_cpu_saturating(cpu, cpu->r[{rm}], cpu->r[{rn}], {(insn>>21)&3}u);'])
    if insn&0x0fbf0fff==0x010f0000:
        rd=(insn>>12)&15
        if rd==15 or insn&0x400000:raise ValueError('MRS PC/SPSR is unsupported')
        return done([f'cpu->r[{rd}] = cpu->cpsr;'])
    if insn&0x0fb0fff0==0x0120f000 or insn&0x0fb0f000==0x0320f000:
        if insn&0x400000:raise ValueError('SPSR is privileged')
        mask=(0xf8000000 if insn&0x80000 else 0)|(0x000f0000 if insn&0x40000 else 0)
        if insn&0x2000000:
            rotation=((insn>>8)&15)*2;value=insn&255
            if rotation:value=((value>>rotation)|(value<<(32-rotation)))&0xffffffff
            value=literal(value)
        else:
            if insn&15==15:raise ValueError('MSR PC source is unsupported')
            value=register(insn&15,pc)
        return done([f'cpu->cpsr = (cpu->cpsr & ~{literal(mask)}) | ({value} & {literal(mask)});'])
    if insn&0x0f8000f0==0x00800090 or insn&0x0ff000f0==0x00400090:
        hi,lo,rs,rm=(insn>>16)&15,(insn>>12)&15,(insn>>8)&15,insn&15
        if 15 in (hi,lo,rs,rm) or hi==lo:raise ValueError('invalid long multiply registers')
        umaal=insn&0x0ff000f0==0x00400090
        args=', '.join('true' if x else 'false' for x in (bool(insn&0x400000) and not umaal,bool(insn&0x200000),bool(insn&0x100000),umaal))
        return done([f'ctr_cpu_result result = ctr_cpu_multiply_long(cpu, {lo}u, {hi}u, cpu->r[{rm}], cpu->r[{rs}], {args}, {literal(insn)});','if (result != CTR_CPU_OK) return result;'])
    if insn&0x0fc000f0==0x00000090:
        rd,rn,rs,rm=(insn>>16)&15,(insn>>12)&15,(insn>>8)&15,insn&15
        if 15 in (rd,rs,rm) or (insn&0x200000 and rn==15):raise ValueError('multiply PC operand is unsupported')
        statements=[f'uint32_t value = cpu->r[{rm}] * cpu->r[{rs}]'+(f' + cpu->r[{rn}]' if insn&0x200000 else '')+';',f'cpu->r[{rd}] = value;']
        if insn&0x100000:statements+=['ctr_cpu_logic_flags(cpu, value, (cpu->cpsr & CTR_FLAG_C) != 0u);']
        return done(statements)
    if insn&0x0fff0fff in (0x0e070f9a,0x0e070fba,0x0e070f95):
        if (insn>>12)&15==15:raise ValueError('CP15 barrier PC operand is unsupported')
        return body+['ctr_cpu_barrier();',f'cpu->r[15] = {next_pc};','return CTR_CPU_OK;']
    if insn&0x0f200f00 in (0x0d000a00,0x0d000b00):
        rn,reg=(insn>>16)&15,(insn>>12)&15;wide=(insn&0xf00)==0xb00;load=bool(insn&0x100000)
        first=(reg+((insn>>22)&1)*16)*2 if wide else reg*2+((insn>>22)&1)
        if first+(2 if wide else 1)>32 or (rn==15 and not load):raise ValueError('VFP bank/store PC base is unsupported')
        address=f'{register(rn,pc)} {"+" if insn&0x800000 else "-"} {literal((insn&255)*4)}'
        return body+[f'ctr_cpu_result result = ctr_cpu_fp_transfer(cpu, {first}u, {2 if wide else 1}u, {address}, {"true" if load else "false"}, {literal(insn)});','if (result != CTR_CPU_OK) return result;',f'cpu->r[15] = {next_pc};','return CTR_CPU_OK;']
    if insn&0x0fff0fff in (0x0ee10a10,0x0ef10a10):
        rt=(insn>>12)&15;read=bool(insn&0x00100000)
        if not read and rt==15:raise ValueError('VMSR PC operand is unsupported')
        statement=(f'cpu->r[{rt}] = cpu->fpscr;' if rt!=15 else 'cpu->cpsr = (cpu->cpsr & 0x0fffffffu) | (cpu->fpscr & 0xf0000000u);') if read else f'cpu->fpscr = cpu->r[{rt}];'
        return body+[statement,f'cpu->r[15] = {next_pc};','return CTR_CPU_OK;']
    if insn & 0x0fff0fff == 0x0e1d0f70 and (insn >> 12) & 15 != 15:
        return body + [f"cpu->r[{(insn >> 12) & 15}] = cpu->tls_address;",
                       f"cpu->r[15] = {next_pc};", "return CTR_CPU_OK;"]
    if insn & 0x0ffffff0 in (0x012fff10,0x012fff30):
        if insn&0x20 and insn&15==15:raise ValueError("BLX PC operand is unsupported")
        body += [f"uint32_t target = {register(insn & 15, pc)};"]
        if insn&0x20:body += [f"cpu->r[14] = {next_pc};"]
        body += ["ctr_cpu_branch_exchange(cpu, target);", "return CTR_CPU_OK;"]
        return body
    if insn & 0x0f000000 == 0x0f000000:
        return body + [f"ctr_cpu_result result = ctr_cpu_stop(cpu, CTR_CPU_SVC, {literal(insn & 0xffffff)});",
                       f"cpu->r[15] = {next_pc};", "cpu->exclusive_valid = false;", "return result;"]
    if insn & 0x0e000000 == 0x0a000000:
        offset = insn & 0xffffff
        if offset & 0x800000:
            offset -= 0x1000000
        if insn & 0x01000000:
            body += [f"cpu->r[14] = {next_pc};"]
        return body + [f"cpu->r[15] = {literal(pc + 8 + offset * 4)};", "return CTR_CPU_OK;"]
    if insn&0x0ff00090==0x01000080:
        rd,rn,rs,rm=(insn>>16)&15,(insn>>12)&15,(insn>>8)&15,insn&15
        if 15 in (rd,rn,rs,rm):raise ValueError('SMLAxy PC operands are unsupported')
        return body+[f'cpu->r[{rd}] = ctr_cpu_half_accumulate(cpu, cpu->r[{rm}], cpu->r[{rs}], cpu->r[{rn}], {(insn>>5)&1}, {(insn>>6)&1});',f'cpu->r[15] = {next_pc};','return CTR_CPU_OK;']
    if insn&0x0ff00ff0 in (0x06500f90,0x06500ff0,0x06800fb0):
        rn,rd,rm=(insn>>16)&15,(insn>>12)&15,insn&15
        if 15 in (rn,rd,rm):raise ValueError("PC operand in byte media instructions is unsupported")
        helper=f'ctr_cpu_byte_select(cpu, cpu->r[{rn}], cpu->r[{rm}])' if insn&0x0ff00ff0==0x06800fb0 else f'ctr_cpu_byte_parallel(cpu, cpu->r[{rn}], cpu->r[{rm}], {"true" if insn&0x0ff00ff0==0x06500ff0 else "false"})'
        return body+[f'cpu->r[{rd}] = {helper};',f'cpu->r[15] = {next_pc};','return CTR_CPU_OK;']
    if insn&0x0e000000==0x08000000:
        if insn&0x00400000:raise ValueError("user-bank/exception-return multiple transfers are unsupported")
        return body+multiple((insn>>16)&15,insn&0xffff,bool(insn&0x00800000),bool(insn&0x01000000),
            bool(insn&0x00200000),bool(insn&0x00100000),False,insn,pc+4)
    if insn&0x0e000090==0x00000090 and insn&0x60:
        rd,rn=(insn>>12)&15,(insn>>16)&15
        immediate=bool(insn&0x00400000)
        load=bool(insn&0x100000);kind=(insn>>5)&3;sign=load and kind!=1
        width=(2 if kind==1 else 1 if kind==2 else 2) if load or kind==1 else 8
        if width==8:load=kind==2
        pre=bool(insn&0x1000000);wb=not pre or bool(insn&0x200000)
        if rd==15 or (wb and (rn==15 or (load and rn==rd))) or (not immediate and (insn&0xf00 or insn&15==15)) or (width==8 and ((rd&1) or rd>=14 or (load and wb and rn==rd+1))):
            raise ValueError('invalid extra transfer registers/addressing')
        offset=literal(((insn>>4)&0xf0)|(insn&15)) if immediate else f"cpu->r[{insn&15}]"
        # Keep existing simple halfword output stable for generated fixtures.
        if width==2 and not sign and pre and not wb:
            action=f"ctr_cpu_load_data(cpu, address, 2u, &cpu->r[{rd}])" if load else f"ctr_cpu_store_data(cpu, address, 2u, cpu->r[{rd}])"
            return done([f'uint32_t address = {register(rn,pc)} {"+" if insn&0x800000 else "-"} {offset};',f'ctr_cpu_result result = {action};','if (result != CTR_CPU_OK) return result;'])
        args=', '.join('true' if flag else 'false' for flag in (load,sign,pre,wb,bool(insn&0x800000)))
        return done([f'ctr_cpu_result result = ctr_cpu_transfer(cpu, {rn}u, {rd}u, {offset}, {width}u, {args}, {literal(insn)});','if (result != CTR_CPU_OK) return result;'])
    if insn & 0x0c000000 == 0:
        op, rd, rn = (insn >> 21) & 15, (insn >> 12) & 15, (insn >> 16) & 15
        flags, test = bool(insn & 0x100000), 8 <= op <= 11
        if (test and not flags) or (rd == 15 and flags):
            raise ValueError("exception-return or miscellaneous encoding is unsupported")
        left = register(rn, pc)
        if insn & 0x02000000:
            rotation = ((insn >> 8) & 15) * 2
            right = insn & 255
            if rotation:
                right = ((right >> rotation) | (right << (32 - rotation))) & 0xffffffff
            rhs = literal(right)
            logic_carry = f"{right >> 31}u" if rotation else "(cpu->cpsr & CTR_FLAG_C) != 0u"
        else:
            reg_shift=bool(insn&0x10)
            if reg_shift and (insn&0x80 or (insn>>8)&15==15):
                raise ValueError('invalid register-controlled shift')
            amount=f'(cpu->r[{(insn>>8)&15}] & 255u)' if reg_shift else f'{(insn>>7)&31}u'
            body += ["unsigned shift_carry;",
                     f"uint32_t shifted = ctr_cpu_shift({register(insn & 15, pc)}, {(insn >> 5) & 3}u, {amount}, {'false' if reg_shift else 'true'}, (cpu->cpsr & CTR_FLAG_C) != 0u, &shift_carry);",
                     "(void)shift_carry;"]
            rhs, logic_carry = "shifted", "shift_carry"
        flag = "true" if flags else "false"
        arithmetic = {
            2: f"ctr_cpu_add(cpu, {left}, ~{rhs}, 1u, {flag})",
            3: f"ctr_cpu_add(cpu, {rhs}, ~{left}, 1u, {flag})",
            4: f"ctr_cpu_add(cpu, {left}, {rhs}, 0u, {flag})",
            5: f"ctr_cpu_add(cpu, {left}, {rhs}, (cpu->cpsr & CTR_FLAG_C) != 0u, {flag})",
            6: f"ctr_cpu_add(cpu, {left}, ~{rhs}, (cpu->cpsr & CTR_FLAG_C) != 0u, {flag})",
            7: f"ctr_cpu_add(cpu, {rhs}, ~{left}, (cpu->cpsr & CTR_FLAG_C) != 0u, {flag})",
            10: f"ctr_cpu_add(cpu, {left}, ~{rhs}, 1u, true)",
            11: f"ctr_cpu_add(cpu, {left}, {rhs}, 0u, true)",
        }
        logical = {0: f"{left} & {rhs}", 1: f"{left} ^ {rhs}", 8: f"{left} & {rhs}",
                   9: f"{left} ^ {rhs}", 12: f"{left} | {rhs}", 13: rhs,
                   14: f"{left} & ~{rhs}", 15: f"~{rhs}"}
        body += [f"uint32_t value = {arithmetic[op] if op in arithmetic else logical[op]};"]
        if op in logical and flags:
            body += [f"ctr_cpu_logic_flags(cpu, value, {logic_carry});"]
        if not test:
            body += [f"cpu->r[{rd}] = value;"]
        else:
            body += ["(void)value;"]
        body += [f"cpu->r[15] = {'value & ~UINT32_C(3)' if rd == 15 and not test else next_pc};", "return CTR_CPU_OK;"]
        return body
    if insn & 0x0c000000 == 0x04000000:
        rn, rd = (insn >> 16) & 15, (insn >> 12) & 15
        width = 1 if insn & 0x00400000 else 4
        load = bool(insn & 0x00100000)
        pre=bool(insn&0x1000000);wb=not pre or bool(insn&0x200000)
        if (wb and (rn==15 or (load and rn==rd))) or (rd==15 and width!=4):raise ValueError('invalid transfer PC/writeback')
        if insn&0x2000000:
            if insn&0x10:raise ValueError('media encoding is not a memory transfer')
            body+=['unsigned ignored;',f'uint32_t offset = ctr_cpu_shift({register(insn&15,pc)}, {(insn>>5)&3}u, {(insn>>7)&31}u, true, (cpu->cpsr & CTR_FLAG_C) != 0u, &ignored);']
            offset='offset'
        else:offset=literal(insn&0xfff)
        if rd==15 or wb:
            args=', '.join('true' if flag else 'false' for flag in (load,False,pre,wb,bool(insn&0x800000)))
            statements=[f'ctr_cpu_result result = ctr_cpu_transfer(cpu, {rn}u, {rd}u, {offset}, {width}u, {args}, {literal(insn)});','if (result != CTR_CPU_OK) return result;']
            if load and rd==15:return body+statements+['return CTR_CPU_OK;']
            return done(statements)
        sign = "+" if insn & 0x00800000 else "-"
        address = f"{register(rn, pc)} {sign} {offset}"
        action = f"ctr_cpu_load_data(cpu, address, {width}u, &cpu->r[{rd}])" if load else f"ctr_cpu_store_data(cpu, address, {width}u, cpu->r[{rd}])"
        return body + [f"uint32_t address = {address};", f"ctr_cpu_result result = {action};",
                       "if (result != CTR_CPU_OK) return result;", f"cpu->r[15] = {next_pc};", "return CTR_CPU_OK;"]
    raise ValueError("instruction outside the documented AOT subset")


def thumb(insn: int, pc: int) -> list[str]:
    body = ["++cpu->instructions;"]
    next_pc=literal(pc+2)
    carry='(cpu->cpsr & CTR_FLAG_C) != 0u'
    def finish(statements):
        return body+statements+[f'cpu->r[15] = {next_pc};','return CTR_CPU_OK;']
    if insn&0xf800<0x1800:
        rd,rm=insn&7,(insn>>3)&7
        return finish(['unsigned carry;',f'cpu->r[{rd}] = ctr_cpu_shift(cpu->r[{rm}], {(insn>>11)&3}u, {(insn>>6)&31}u, true, {carry}, &carry);',f'ctr_cpu_logic_flags(cpu, cpu->r[{rd}], carry);'])
    if insn&0xf800==0x1800:
        rd,rn=insn&7,(insn>>3)&7
        rhs=literal((insn>>6)&7) if insn&0x400 else f'cpu->r[{(insn>>6)&7}]'
        return finish([f'cpu->r[{rd}] = ctr_cpu_add(cpu, cpu->r[{rn}], {"~" if insn&0x200 else ""}{rhs}, {1 if insn&0x200 else 0}u, true);'])
    if insn&0xfc00==0x4000:
        rd,rm,op=insn&7,(insn>>3)&7,(insn>>6)&15
        left,right=f'cpu->r[{rd}]',f'cpu->r[{rm}]'
        if op in (2,3,4,7):
            return finish(['unsigned carry;',f'uint32_t value = ctr_cpu_shift({left}, {3 if op==7 else op-2}u, {right} & 255u, false, {carry}, &carry);',f'{left} = value;','ctr_cpu_logic_flags(cpu, value, carry);'])
        arithmetic={5:f'{left}, {right}, {carry}',6:f'{left}, ~{right}, {carry}',9:f'0u, ~{right}, 1u',10:f'{left}, ~{right}, 1u',11:f'{left}, {right}, 0u'}
        if op in arithmetic:
            return finish([f'{"(void)" if op in (10,11) else left+" = "}ctr_cpu_add(cpu, {arithmetic[op]}, true);'])
        expressions={0:f'{left} & {right}',1:f'{left} ^ {right}',8:f'{left} & {right}',12:f'{left} | {right}',13:f'{left} * {right}',14:f'{left} & ~{right}',15:f'~{right}'}
        statements=[f'uint32_t value = {expressions[op]};']
        if op!=8:statements+=[f'{left} = value;']
        return finish(statements+[f'ctr_cpu_logic_flags(cpu, value, {carry});'])
    if insn&0xfc00==0x4400 and (insn>>8)&3!=3:
        rd=(insn&7)|((insn>>4)&8);rm=(insn>>3)&15;op=(insn>>8)&3
        left,right=register(rd,pc,True),register(rm,pc,True)
        if op==1:return finish([f'(void)ctr_cpu_add(cpu, {left}, ~{right}, 1u, true);'])
        value=f'{left} + {right}' if op==0 else right
        if rd==15:return body+[f'cpu->r[15] = ({value}) & ~UINT32_C(1);','return CTR_CPU_OK;']
        return finish([f'cpu->r[{rd}] = {value};'])
    if insn&0xf800==0x4800:
        rd=(insn>>8)&7;address=literal(((pc+4)&~3)+(insn&255)*4)
        return finish([f'ctr_cpu_result result = ctr_cpu_load(cpu, {address}, 4u, &cpu->r[{rd}]);','if (result != CTR_CPU_OK) return result;'])
    if insn&0xf000==0x5000 or insn&0xe000==0x6000 or insn&0xf000 in (0x8000,0x9000):
        rd=insn&7;sign=False
        if insn&0xf000==0x5000:
            op=(insn>>9)&7;width=(4,2,1,1,4,2,1,2)[op];load=op>=3;sign=op in (3,7)
            address=f'cpu->r[{(insn>>3)&7}] + cpu->r[{(insn>>6)&7}]'
        elif insn&0xf000==0x9000:
            rd=(insn>>8)&7;width=4;load=bool(insn&0x800);address=f'cpu->r[13] + {literal((insn&255)*4)}'
        else:
            width=2 if insn&0xf000==0x8000 else 1 if insn&0x1000 else 4;load=bool(insn&0x800)
            address=f'cpu->r[{(insn>>3)&7}] + {literal(((insn>>6)&31)*width)}'
        statements=[f'uint32_t address = {address};']
        if sign:
            statements+=['uint32_t value;',f'ctr_cpu_result result = ctr_cpu_load_data(cpu, address, {width}u, &value);','if (result != CTR_CPU_OK) return result;',f'cpu->r[{rd}] = value | ((value & {literal(1<<(width*8-1))}) ? {literal(0xffffff00 if width==1 else 0xffff0000)} : 0u);']
        else:
            action=f'ctr_cpu_load_data(cpu, address, {width}u, &cpu->r[{rd}])' if load else f'ctr_cpu_store_data(cpu, address, {width}u, cpu->r[{rd}])'
            statements+=[f'ctr_cpu_result result = {action};','if (result != CTR_CPU_OK) return result;']
        return finish(statements)
    if insn&0xf000==0xa000:
        base='cpu->r[13]' if insn&0x800 else literal((pc+4)&~3)
        return finish([f'cpu->r[{(insn>>8)&7}] = {base} + {literal((insn&255)*4)};'])
    if insn&0xff00==0xb000:
        return finish([f'cpu->r[13] = cpu->r[13] {"-" if insn&128 else "+"} {literal((insn&127)*4)};'])
    if insn&0xff00==0xb200:
        return finish([f'cpu->r[{insn&7}] = ctr_cpu_extend(cpu->r[{(insn>>3)&7}], {(3,2,7,6)[(insn>>6)&3]}u, 0u, 0u, false);'])
    if insn&0xff00==0xba00:
        op=(insn>>6)&3
        if op==2:raise ValueError('undefined Thumb reverse encoding')
        return finish([f'cpu->r[{insn&7}] = ctr_cpu_reverse(cpu->r[{(insn>>3)&7}], {2 if op==3 else op}u);'])
    if insn&0xf800==0xf000:
        offset=(insn&0x7ff)<<12
        if offset&0x400000:offset-=0x800000
        return finish([f'cpu->r[14] = {literal(pc+4+offset)};'])
    if insn&0xf800 in (0xe800,0xf800):
        exchange=insn&0xf800==0xe800
        if exchange and insn&1:raise ValueError('unaligned Thumb BLX suffix')
        statements=[f'uint32_t target = cpu->r[14] + {literal((insn&0x7ff)*2)};',f'cpu->r[14] = {literal((pc+2)|1)};']
        if exchange:statements+=['cpu->cpsr &= ~CTR_FLAG_T;','cpu->r[15] = target & ~UINT32_C(3);']
        else:statements+=['cpu->r[15] = target & ~UINT32_C(1);']
        return body+statements+['return CTR_CPU_OK;']
    if insn&0xf000==0xd000 and (insn>>8)&15<14:
        offset=insn&255
        if offset&128:offset-=256
        return body+[f'cpu->r[15] = ctr_cpu_condition(cpu, {(insn>>8)&15}u) ? {literal(pc+4+offset*2)} : {next_pc};','return CTR_CPU_OK;']
    if insn&0xf800==0xe000:
        offset=insn&0x7ff
        if offset&0x400:offset-=0x800
        return body+[f'cpu->r[15] = {literal(pc+4+offset*2)};','return CTR_CPU_OK;']
    if insn & 0xe000 == 0x2000:
        rd, op, imm = (insn >> 8) & 7, (insn >> 11) & 3, literal(insn & 255)
        if op == 0:
            body += [f"cpu->r[{rd}] = {imm};", f"ctr_cpu_logic_flags(cpu, cpu->r[{rd}], (cpu->cpsr & CTR_FLAG_C) != 0u);"]
        else:
            right = imm if op == 2 else f"~{imm}"
            expr = f"ctr_cpu_add(cpu, cpu->r[{rd}], {right}, {0 if op == 2 else 1}u, true)"
            body += [f"{'(void)' if op == 1 else f'cpu->r[{rd}] = '}{expr};"]
    elif insn & 0xff07 == 0x4700:
        rm=(insn>>3)&15
        if insn&0x80 and rm==15:raise ValueError("Thumb BLX PC operand is unsupported")
        body += [f"uint32_t target = {register(rm,pc,True)};"]
        if insn&0x80:body += [f"cpu->r[14] = {literal((pc+2)|1)};"]
        return body+["ctr_cpu_branch_exchange(cpu, target);","return CTR_CPU_OK;"]
    elif insn&0xf600==0xb400:
        load=bool(insn&0x800);registers=(insn&255)|((1<<(15 if load else 14)) if insn&0x100 else 0)
        return body+multiple(13,registers,load,not load,True,load,True,insn,pc+2)
    elif insn&0xf000==0xc000:
        rn=(insn>>8)&7;load=bool(insn&0x800);registers=insn&255
        return body+multiple(rn,registers,True,False,not(load and registers&(1<<rn)),load,True,insn,pc+2)
    elif insn & 0xff00 == 0xdf00:
        return body + [f"ctr_cpu_result result = ctr_cpu_stop(cpu, CTR_CPU_SVC, {literal(insn & 255)});",
                       f"cpu->r[15] = {literal(pc + 2)};", "cpu->exclusive_valid = false;", "return result;"]
    else:
        raise ValueError("instruction outside the documented Thumb AOT subset")
    return body + [f"cpu->r[15] = {literal(pc + 2)};", "return CTR_CPU_OK;"]


def translate(data: bytes, base: int, symbol: str, is_thumb: bool) -> str:
    width = 2 if is_thumb else 4
    if not data or len(data) % width or base < 0 or base % width or base + len(data) > 0x100000000:
        raise ValueError("code must be nonempty, aligned, and fit the 32-bit guest address space")
    if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", symbol) is None:
        raise ValueError("symbol must be a C identifier")
    lines = ['/* Generated by CTR2WUP bounded AOT translator v1; original fixture code. */',
             '#include "ctr_cpu.h"', "", f'const char {symbol}_hash[] = "{hashlib.sha256(data).hexdigest()}";',
             f"uint8_t {symbol}_code[] = {{" + ", ".join(f"0x{x:02x}" for x in data) + "};",
             f"const size_t {symbol}_code_size = sizeof({symbol}_code);", ""]
    blocks = []
    for offset in range(0, len(data), width):
        pc = base + offset
        word = struct.unpack_from("<H" if is_thumb else "<I", data, offset)[0]
        try:
            body = thumb(word, pc) if is_thumb else arm(word, pc)
        except ValueError as exc:
            raise ValueError(f"0x{pc:08x} {'Thumb' if is_thumb else 'ARM'} instruction 0x{word:0{width * 2}x}: {exc}") from exc
        name = f"{symbol}_at_{pc:08x}"
        lines += [f"static ctr_cpu_result {name}(ctr_cpu *cpu) {{"] + [f"    {line}" for line in body] + ["}", ""]
        blocks += [f"    {{{literal(pc)}, {'true' if is_thumb else 'false'}, {name}}},"]
    lines += [f"const ctr_block {symbol}_blocks[] = {{"] + blocks + ["};",
              f"const size_t {symbol}_block_count = sizeof({symbol}_blocks) / sizeof({symbol}_blocks[0]);", ""]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--base", type=lambda text: int(text, 0), required=True)
    parser.add_argument("--symbol", default="ctr_fixture")
    parser.add_argument("--thumb", action="store_true")
    args = parser.parse_args()
    try:
        generated = translate(args.input.read_bytes(), args.base, args.symbol, args.thumb)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=args.output.parent, delete=False) as file:
            file.write(generated)
            temporary = file.name
        os.replace(temporary, args.output)
    except (OSError, ValueError) as exc:
        parser.exit(1, f"translation failed: {exc}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
