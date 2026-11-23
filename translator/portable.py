"""Adapt the pinned emitter's C to an original, checked ABI implementation.

This deliberately removes the emitter's register-local optimization. State at
every guest instruction and memory fault remains visible to the runtime. The
transformation accepts only the audited emitter's grammar, not arbitrary C.
"""
import re

_LOCAL = re.compile(r"    (?:uint32_t|uint8_t) (reg\d+_|flag_[nzcv]_|vfp\d+_|fpscr_) = .+;")
_TOKEN = re.compile(r"\b(reg(\d+)_|flag_([nzcv])_|vfp(\d+)_|fpscr_|budget_)\b")
_COMMENT = re.compile(r"    /\* ([0-9A-F]{8}) ([0-9A-F]{4}|[0-9A-F]{8}) \*/")
_MACRO = re.compile(r"#(?:undef|define) (SYNC_OUT|SYNC_IN|BUDGET_LEFT|READ_PAGES|WRITE_PAGES|FPSCR)\b.*")
_DISPATCH = re.compile(r"    if \((!?ctx->thumb)\) switch \((target(?: - module_base)?)\) \{")
_CASE = re.compile(r"    case 0x([0-9A-F]{8})u: goto L_\1;")
_CASE_SHIFTED = re.compile(r"    case 0x[0-9A-Fa-f]+u: goto L_[0-9A-Fa-f]{8};")
_FUNC_START = re.compile(r"^(?:static\s+)?void f_[0-9A-Fa-f]+\(Context \*ctx\)\s*\{")
_LABEL = re.compile(r"^L_([0-9A-Fa-f]{8}):\s*$")
_CTR_INSN = re.compile(r"^    CTR_INSTRUCTION\(0x([0-9A-Fa-f]{8})u, 0x([0-9A-Fa-f]{4,8})u\);$")
_CTR_NEXT = re.compile(r"^    CTR_INSTRUCTION_NEXT\(0x([0-9A-Fa-f]{4,8})u\);$")
_GOTO_LBL = re.compile(r"\bgoto\s+(L_[0-9A-Fa-f]{8})\b")
_DISPATCH_FIXUP = "    ctx->r[15] = target - (ctx->thumb ? 2u : 4u);"
_CF_TOKENS = ("CALL(", "SVC(", "INTERPRET(", "recomp_call(", "ctx->r[15]")
_NON_PURE_REG_TOKENS = (
    "mem_", "CTR_SCALAR_VFP", "VFP_", "vfp_", "CALL(", "SVC(", "INTERPRET(",
    "recomp_call(", "ctx->r[15]", "RETURN", "JUMP_TO", "goto ", "dispatch",
    "STALE_CHECK", "media_", "ctr_recomp_abort", "ctr_recomp_unwind",
)
_TAIL_NON_PURE_TOKENS = (
    "mem_", "CTR_SCALAR_VFP", "VFP_", "vfp_", "CALL(", "SVC(", "INTERPRET(",
    "recomp_call(", "ctx->r[15]", "STALE_CHECK", "media_",
    "ctr_recomp_abort", "ctr_recomp_unwind",
)
_ANY_CASE = re.compile(r"^(    case 0x[0-9A-Fa-f]+u: goto )(L_[0-9A-Fa-f]{8})(;)$")
_MAX_REG_RUN = 127
_SPAN_BLOCK = re.compile(
    r"\{ (const )?uint8_t \*span = mem_(read|write)_span\(ctx, ([^,]+), (\d+)u\);\n"
    r"    if \(LIKELY\(span\)\) \{\n(.*?)\n    \} else \{\n(.*?)\n    \} \}",
    re.S,
)
_SPAN_GPR = re.compile(r"^    ctx->r\[(\d+)\] = mem_read32\(ctx, .+? \+ (\d+)u\);$|^    mem_write32\(ctx, .+? \+ (\d+)u, ctx->r\[(\d+)\]\);$")
_SPAN_NEXT = re.compile(r"^    next = mem_read32\(ctx, .+? \+ (\d+)u\);$")
_SPAN_VFP = re.compile(r"^    (?:ctx->vfp\[(\d+)\] = mem_read32\(ctx, .+? \+ (\d+)u\);|mem_write32\(ctx, .+? \+ (\d+)u, ctx->vfp\[(\d+)\]\);)$")
_BLOCK_TO = re.compile(r"    BLOCK_TO\(.+, out_of_budget_\d+\);")
_OUT_OF_BUDGET = re.compile(r"    OUT_OF_BUDGET_AT\(out_of_budget_\d+\);")
_NEG_MVN = re.compile(r'(\{ uint32_t a = [^;]+;)( r = (?:0 - b|~b);)')
_PAREN_CMP = re.compile(r'^(\s*if )\(\((ctx->[nzcv] == ctx->[nzcv])\)\)( \{)$')
_LEFTOVER = re.compile(r"\b(?:reg\d+_|vfp\d+_|read_pages_|write_pages_|resume|out_of_budget_\d+)\b")


def _replace_token(m):
    if m[2] is not None:
        if int(m[2]) > 15:
            raise ValueError("Invalid guest register")
        return f"ctx->r[{m[2]}]"
    if m[3] is not None:
        return f"ctx->{m[3]}"
    if m[4] is not None:
        if int(m[4]) > 31:
            raise ValueError("Invalid VFP register")
        return f"ctx->vfp[{m[4]}]"
    return "(*ctx->fpscr)" if m[0] == "fpscr_" else "ctx->budget"


def _map_functions(lines, fn):
    out = []
    i = 0
    n = len(lines)
    while i < n:
        if _FUNC_START.match(lines[i]):
            start = i
            i += 1
            while i < n and not _FUNC_START.match(lines[i]):
                i += 1
            out.extend(fn(lines[start:i]))
        else:
            out.append(lines[i])
            i += 1
    return out


def _is_reg_only_arm(op):
    if op <= 0xFFFF or (op >> 28) == 15:
        return False
    if (
        (op & 0x0C000000) == 0x0C000000
        or (op & 0x0F900090) == 0x01000080
        or (op & 0x0FF00FF0) == 0x06500F90
        or (op & 0x0FF00FF0) == 0x06500FF0
        or (op & 0x0FF00FF0) == 0x06800FB0
        or (op & 0x0E000000) == 0x08000000
        or (op & 0x0FFFFFFF) == 0x012FFF3F
        or (op & 0xFFFFFFF0) == 0xF57FF040
        or (op & 0xFFFFFFF0) == 0xF57FF050
        or (op & 0xFFFFFFF0) == 0xF57FF060
    ):
        return False
    return (
        (op & 0x0E000000) == 0x0A000000
        or (op & 0x0FFFFFF0) == 0x012FFF10
        or ((op & 0x0FFFFFF0) == 0x012FFF30 and (op & 15) != 15)
        or (((op & 0x0FC000F0) == 0x00000090 or (op & 0x0F8000F0) == 0x00800090) and ((op >> 16) & 15) != 15)
        or (((op & 0x0E000000) == 0x02000000 or not (op & 0x0E000010)
             or ((op & 0x0E000090) == 0x00000010 and ((op >> 8) & 15) != 15 and (op & 15) != 15))
            and ((op & 0x01800000) != 0x01000000 or (op & 0x00100000)) and ((op >> 12) & 15) != 15)
        or ((op & 0x0FB00000) == 0x03000000 and ((op >> 12) & 15) != 15)
        or (op & 0x0FFFFFFF) == 0x0320F000
        or ((op & 0x0FFF0FF0) == 0x016F0F10 and ((op >> 12) & 15) != 15 and (op & 15) != 15)
        or ((op & 0x0F8003F0) == 0x06800070 and ((op >> 20) & 7) not in (1, 5) and ((op >> 12) & 15) != 15 and (op & 15) != 15)
        or ((op & 0x0FF00F30) == 0x06B00F30 and ((op >> 12) & 15) != 15 and (op & 15) != 15)
    )


def rewrite_straight_line_steps(lines):
    """Use 1-arg CTR_INSTRUCTION_NEXT for straight-line ARM fallthrough steps."""
    return _map_functions(lines, _rewrite_function_steps)


def rewrite_reg_runs(lines):
    """Coalesce straight-line pure-register ARM runs into CTR_INSTRUCTION_RUN*."""
    return _map_functions(lines, _rewrite_function_reg_runs)


def _rewrite_function_reg_runs(chunk):
    if any("if (ctx->thumb" in l for l in chunk) or "dispatch:" not in chunk or not any("switch (" in l for l in chunk):
        return chunk
    gotos = set()
    first_lbl_idx = None
    for idx, line in enumerate(chunk):
        if first_lbl_idx is None and _LABEL.match(line):
            first_lbl_idx = idx
        if not _ANY_CASE.match(line):
            for g in _GOTO_LBL.findall(line):
                gotos.add(f"L_{g[2:].upper()}")
    if first_lbl_idx is None:
        return chunk
    header = chunk[:first_lbl_idx]
    close_idx = len(chunk)
    for idx in range(len(chunk) - 1, first_lbl_idx - 1, -1):
        if chunk[idx] == "}":
            close_idx = idx
            break
    blocks = []
    cur = None
    for line in chunk[first_lbl_idx:close_idx]:
        ml = _LABEL.match(line)
        if ml:
            if cur is not None:
                blocks.append(cur)
            cur = {"lbl": f"L_{ml.group(1).upper()}", "lbl_line": line,
                   "addr": int(ml.group(1), 16), "insn_line": None,
                   "op": None, "is_next": False, "body": []}
        elif cur is not None:
            if cur["insn_line"] is None and not cur["body"]:
                m1 = _CTR_INSN.match(line)
                m2 = _CTR_NEXT.match(line) if not m1 else None
                if m1 and int(m1.group(1), 16) == cur["addr"]:
                    cur["insn_line"] = line
                    cur["op"] = int(m1.group(2), 16)
                    cur["is_next"] = False
                    continue
                if m2:
                    cur["insn_line"] = line
                    cur["op"] = int(m2.group(1), 16)
                    cur["is_next"] = True
                    continue
            cur["body"].append(line)
    if cur is not None:
        blocks.append(cur)
    if not blocks:
        return chunk
    for b in blocks:
        btxt = "\n".join(b["body"])
        reg_op = b["op"] is not None and _is_reg_only_arm(b["op"])
        b["pure"] = reg_op and not any(tok in btxt for tok in _NON_PURE_REG_TOKENS)
        b["tail_branch"] = (
            reg_op
            and not b["pure"]
            and not any(tok in btxt for tok in _TAIL_NON_PURE_TOKENS)
            and any(tok in btxt for tok in ("goto ", "RETURN_TO", "JUMP_TO", "RETURN()"))
        )
    removed_lbls = set()
    i = 0
    nb = len(blocks)
    while i < nb:
        if not blocks[i]["pure"]:
            i += 1
            continue
        k = 1
        while (i + k < nb and k < _MAX_REG_RUN
               and (blocks[i + k]["pure"] or blocks[i + k]["tail_branch"])
               and blocks[i + k]["addr"] == blocks[i + k - 1]["addr"] + 4
               and blocks[i + k]["lbl"] not in gotos):
            k += 1
            if blocks[i + k - 1]["tail_branch"]:
                break
        if k >= 2:
            head = blocks[i]
            head["insn_line"] = (f"    CTR_INSTRUCTION_RUN_NEXT({k});" if head["is_next"]
                                 else f"    CTR_INSTRUCTION_RUN(0x{head['addr']:08X}u, {k});")
            for j in range(1, k):
                mid = blocks[i + j]
                mid["lbl_line"] = None
                mid["insn_line"] = None
                removed_lbls.add(mid["lbl"])
            i += k
        else:
            i += 1
    if not removed_lbls:
        return chunk
    new_header = []
    redirected = False
    for line in header:
        mc = _ANY_CASE.match(line)
        if mc and mc.group(2).upper() in removed_lbls:
            new_header.append(f"{mc.group(1)}ctr_step_mid{mc.group(3)}")
            redirected = True
        else:
            new_header.append(line)
    if redirected:
        new_header.extend(["ctr_step_mid:", "    CTR_STEP_MID(target);", "    return;"])
    out = list(new_header)
    for b in blocks:
        if b["lbl_line"] is not None:
            out.append(b["lbl_line"])
        if b["insn_line"] is not None:
            out.append(b["insn_line"])
        out.extend(b["body"])
    out.extend(chunk[close_idx:])
    return out


def _rewrite_function_steps(chunk):
    has_thumb_switch = any("if (ctx->thumb" in l for l in chunk)
    has_dispatch = "dispatch:" in chunk
    gotos = set()
    patched = []
    for idx, line in enumerate(chunk):
        patched.append(line)
        if line == "dispatch:" and (idx + 1 >= len(chunk) or chunk[idx + 1] != _DISPATCH_FIXUP):
            patched.append(_DISPATCH_FIXUP)
        if not _CASE_SHIFTED.match(line):
            for g in _GOTO_LBL.findall(line):
                gotos.add(f"L_{g[2:].upper()}")
    if has_thumb_switch or (not has_dispatch and any("switch (" in l for l in patched)):
        return patched
    prev_addr = None
    prev_safe = False
    cur_addr = None
    cur_insn_idx = None
    cur_body_has_cf = False
    cur_eligible = False
    for idx, line in enumerate(patched):
        m_lbl = _LABEL.match(line)
        if m_lbl:
            if cur_addr is not None:
                prev_addr = cur_addr
                prev_safe = cur_insn_idx is not None and not cur_body_has_cf
            cur_addr = int(m_lbl.group(1), 16)
            cur_insn_idx = None
            cur_body_has_cf = False
            cur_eligible = False
            continue
        if cur_addr is not None:
            if cur_insn_idx is None:
                m_ctr = _CTR_INSN.match(line)
                if m_ctr and int(m_ctr.group(1), 16) == cur_addr:
                    cur_insn_idx = idx
                    op_hex = m_ctr.group(2)
                    lbl = f"L_{cur_addr:08X}"
                    if (prev_safe and prev_addr is not None and cur_addr == prev_addr + 4
                            and len(op_hex) == 8 and int(op_hex, 16) > 0xFFFF
                            and lbl not in gotos):
                        cur_eligible = (idx, op_hex)
                    continue
            if cur_eligible:
                patched[cur_eligible[0]] = f"    CTR_INSTRUCTION_NEXT(0x{cur_eligible[1]}u);"
                cur_eligible = False
            if any(tok in line for tok in _CF_TOKENS):
                cur_body_has_cf = True
    if cur_eligible:
        patched[cur_eligible[0]] = f"    CTR_INSTRUCTION_NEXT(0x{cur_eligible[1]}u);"
    return patched


def _replace_span_fallback(m):
    const_q, kind, addr, nbytes_str, fast, slow_text = m.groups()
    slow = slow_text.splitlines()
    nbytes = int(nbytes_str)
    if not slow or len(slow) * 4 != nbytes:
        return m.group(0)
    mask = 0
    last_reg = -1
    has_next = False
    gpr_ok = True
    for idx, line in enumerate(slow):
        mg = _SPAN_GPR.match(line)
        if mg:
            is_read_line = mg[1] is not None
            if (kind == "read") != is_read_line:
                gpr_ok = False
                break
            reg = int(mg[1] if is_read_line else mg[4])
            off = int(mg[2] if is_read_line else mg[3])
            if off != idx * 4 or reg >= 15 or reg <= last_reg:
                gpr_ok = False
                break
            last_reg = reg
            mask |= 1 << reg
        elif kind == "read" and idx == len(slow) - 1:
            mn = _SPAN_NEXT.match(line)
            if not mn or int(mn[1]) != idx * 4:
                gpr_ok = False
                break
            has_next = True
            mask |= 1 << 15
        else:
            gpr_ok = False
            break
    if gpr_ok:
        call = (f"next = ctr_recomp_read_span_gpr(ctx, 0x{mask:X}u);" if has_next else
                f"ctr_recomp_{kind}_span_gpr(ctx, 0x{mask:X}u);")
    else:
        v0 = None
        for idx, line in enumerate(slow):
            mv = _SPAN_VFP.match(line)
            if not mv:
                return m.group(0)
            is_read_line = mv[1] is not None
            if (kind == "read") != is_read_line:
                return m.group(0)
            reg = int(mv[1] if is_read_line else mv[4])
            off = int(mv[2] if is_read_line else mv[3])
            if off != idx * 4:
                return m.group(0)
            if v0 is None:
                v0 = reg
            elif reg != v0 + idx:
                return m.group(0)
        if v0 is None or v0 < 0 or v0 + len(slow) > 32:
            return m.group(0)
        spec = v0 | (len(slow) << 5)
        call = f"ctr_recomp_{kind}_span_vfp(ctx, 0x{spec:X}u);"
    return (f"{{ {const_q or ''}uint8_t *span = mem_{kind}_span(ctx, {addr}, {nbytes}u);\n"
            f"    if (LIKELY(span)) {{\n{fast}\n    }} else {{\n    {call}\n    }} }}")


def rewrite_span_fallbacks(source):
    """Route cold LDM/STM/VLDM/VSTM miss fallbacks through shared C helpers."""
    return _SPAN_BLOCK.sub(_replace_span_fallback, source)


def adapt_source(source):
    """Keep translated operations; replace synchronization and budget machinery."""
    result, instructions = [], 0
    shift = 0
    scalar_vfp = False
    for line in source.splitlines():
        match = _COMMENT.fullmatch(line) if line.startswith("    /* ") else None
        if scalar_vfp and (match or line == "}" or line.startswith(("L_", "    OUT_OF_BUDGET", "    target = "))):
            result.append("    } }")
            scalar_vfp = False
        if shift:
            if line == "    }":
                shift = 0
            else:
                case = _CASE.fullmatch(line)
                if not case or int(case[1], 16) & ((1 << shift) - 1):
                    raise ValueError("Unexpected or unaligned pinned lifter dispatch label")
                result.append(f"    case 0x{int(case[1], 16) >> shift:08X}u: goto L_{case[1]};")
                continue
        if line.startswith("    if ("):
            dispatch = _DISPATCH.fullmatch(line)
            if dispatch:
                mode, offset = dispatch.groups()
                shift = 1 if mode == "ctx->thumb" else 2
                alignment_mask = (1 << shift) - 1
                # The full address lookup has no entries between guest instructions.
                # Index by aligned instructions to avoid redundant PowerPC jump table relocations.
                result.append(f"    if ({mode} && !(({offset}) & {alignment_mask}u)) switch (({offset}) >> {shift}) {{")
                continue
        if line.startswith("#") and _MACRO.fullmatch(line):
            continue
        if line.startswith("    uint") and _LOCAL.fullmatch(line):
            continue
        if line == "    int32_t budget_ = ctx->budget;":
            continue
        if line == "    uint8_t *const *read_pages_ = ctx->read_pages, *const *write_pages_ = ctx->write_pages;":
            continue
        if line.startswith("    BLOCK_TO(") and _BLOCK_TO.fullmatch(line):
            continue
        if line.startswith("    OUT_OF_BUDGET_AT(") and _OUT_OF_BUDGET.fullmatch(line):
            continue
        if match:
            address, opcode = match.groups()
            scalar_vfp = len(opcode) == 8 and int(opcode, 16) & 0x0F000E10 == 0x0E000A00
            if scalar_vfp:
                result.append(f"    {{ CTR_SCALAR_VFP(0x{address}u, 0x{opcode}u) {{")
            else:
                result.append(f"    CTR_INSTRUCTION(0x{address}u, 0x{opcode}u);")
            instructions += 1
        if "reg15_" in line:
            line = line.replace(
                "uint32_t target = reg15_, resume = 0;", "uint32_t target = ctx->r[15];")
        if line == "dispatch:":
            result.append("    goto dispatch;")
        if "_" in line:
            line = _TOKEN.sub(_replace_token, line)
        # Thumb NEG/MVN share the emitter's binary-ALU declaration. Preserve
        # its reads while marking the unused destination snapshot explicitly.
        if "{ uint32_t a = " in line:
            line = _NEG_MVN.sub(r'\1 (void)a;\2', line)
        # Clang diagnoses the emitter's redundant equality parentheses under
        # -Wall/-Werror; keep the same comparison and one conditional pair.
        if "((" in line:
            line = _PAREN_CMP.sub(r'\1(\2)\3', line)
        # The emitter declares acc even for non-accumulating scalar VFP ops.
        # Keep the declaration/evaluation while making that intent explicit.
        if "acc = vfp_from_" in line:
            at = line.find(";", line.index("acc = vfp_from_"))
            line = line[:at+1] + " (void)acc;" + line[at+1:]
        result.append(line)
    if scalar_vfp:
        result.append("    } }")
    step_lines = rewrite_reg_runs(rewrite_straight_line_steps(result))
    adapted = rewrite_span_fallbacks("\n".join(step_lines) + "\n")
    if shift or not instructions or _LEFTOVER.search(adapted):
        raise ValueError("Unexpected pinned lifter grammar; refusing unchecked output")
    return adapted, instructions
