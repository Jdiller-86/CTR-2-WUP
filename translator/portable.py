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


def adapt_source(source):
    """Keep translated operations; replace synchronization and budget machinery."""
    result, instructions = [], 0
    shift = 0
    for line in source.splitlines():
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
        if line.startswith("    /* "):
            match = _COMMENT.fullmatch(line)
            if match:
                address, opcode = match.groups()
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
    adapted = "\n".join(result) + "\n"
    if shift or not instructions or _LEFTOVER.search(adapted):
        raise ValueError("Unexpected pinned lifter grammar; refusing unchecked output")
    return adapted, instructions
