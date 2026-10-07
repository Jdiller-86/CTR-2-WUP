#!/usr/bin/env python3
"""Bounded, fail-closed ARMv6K/Thumb-to-C translator for flat test executables.

This deliberately does not claim container loading, block discovery, VFP, or
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


def arm(insn: int, pc: int) -> list[str]:
    cond = insn >> 28
    if cond == 15:
        raise ValueError("unconditional extension space is unsupported")
    next_pc = literal(pc + 4)
    body = ["++cpu->instructions;"]
    if cond != 14:
        body += [f"if (!ctr_cpu_condition(cpu, {cond}u)) {{ cpu->r[15] = {next_pc}; return CTR_CPU_OK; }}"]
    if insn & 0x0ffffff0 == 0x012fff10:
        body += [f"ctr_cpu_branch_exchange(cpu, {register(insn & 15, pc)});", "return CTR_CPU_OK;"]
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
            if insn & 0x10:
                raise ValueError("register-controlled shifts and extended encodings are unsupported")
            body += ["unsigned shift_carry;",
                     f"uint32_t shifted = ctr_cpu_shift({register(insn & 15, pc)}, {(insn >> 5) & 3}u, {(insn >> 7) & 31}u, true, (cpu->cpsr & CTR_FLAG_C) != 0u, &shift_carry);",
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
    if insn & 0x0e000000 == 0x04000000 and insn & 0x01000000 and not insn & 0x00200000:
        rn, rd = (insn >> 16) & 15, (insn >> 12) & 15
        width = 1 if insn & 0x00400000 else 4
        load = bool(insn & 0x00100000)
        if rd == 15:
            raise ValueError("AOT memory PC destination/source is unsupported")
        sign = "+" if insn & 0x00800000 else "-"
        address = f"{register(rn, pc)} {sign} {literal(insn & 0xfff)}"
        action = f"ctr_cpu_load(cpu, address, {width}u, &cpu->r[{rd}])" if load else f"ctr_cpu_store(cpu, address, {width}u, cpu->r[{rd}])"
        return body + [f"uint32_t address = {address};", f"ctr_cpu_result result = {action};",
                       "if (result != CTR_CPU_OK) return result;", f"cpu->r[15] = {next_pc};", "return CTR_CPU_OK;"]
    raise ValueError("instruction outside the documented AOT subset")


def thumb(insn: int, pc: int) -> list[str]:
    body = ["++cpu->instructions;"]
    if insn & 0xe000 == 0x2000:
        rd, op, imm = (insn >> 8) & 7, (insn >> 11) & 3, literal(insn & 255)
        if op == 0:
            body += [f"cpu->r[{rd}] = {imm};", f"ctr_cpu_logic_flags(cpu, cpu->r[{rd}], (cpu->cpsr & CTR_FLAG_C) != 0u);"]
        else:
            right = imm if op == 2 else f"~{imm}"
            expr = f"ctr_cpu_add(cpu, cpu->r[{rd}], {right}, {0 if op == 2 else 1}u, true)"
            body += [f"{'(void)' if op == 1 else f'cpu->r[{rd}] = '}{expr};"]
    elif insn & 0xff87 == 0x4700:
        return body + [f"ctr_cpu_branch_exchange(cpu, {register((insn >> 3) & 15, pc, True)});", "return CTR_CPU_OK;"]
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
