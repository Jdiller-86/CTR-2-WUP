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


def adapt_source(source):
    """Keep translated operations; replace synchronization and budget machinery."""
    result, instructions = [], 0
    for line in source.splitlines():
        if _MACRO.fullmatch(line) or _LOCAL.fullmatch(line):
            continue
        if line == "    int32_t budget_ = ctx->budget;":
            continue
        if line == "    uint8_t *const *read_pages_ = ctx->read_pages, *const *write_pages_ = ctx->write_pages;":
            continue
        if re.fullmatch(r"    BLOCK_TO\(.+, out_of_budget_\d+\);", line):
            continue
        if re.fullmatch(r"    OUT_OF_BUDGET_AT\(out_of_budget_\d+\);", line):
            continue
        match = _COMMENT.fullmatch(line)
        if match:
            address, opcode = match.groups()
            result.append(f"    CTR_INSTRUCTION(0x{address}u, 0x{opcode}u);")
            instructions += 1
        line = line.replace(
            "uint32_t target = reg15_, resume = 0;", "uint32_t target = ctx->r[15];")
        if line == "dispatch:":
            result.append("    goto dispatch;")

        def token(m):
            if m[2] is not None:
                if int(m[2]) > 15: raise ValueError("Invalid guest register")
                return f"ctx->r[{m[2]}]"
            if m[3] is not None: return f"ctx->{m[3]}"
            if m[4] is not None:
                if int(m[4]) > 31: raise ValueError("Invalid VFP register")
                return f"ctx->vfp[{m[4]}]"
            return "(*ctx->fpscr)" if m[0] == "fpscr_" else "ctx->budget"

        line = _TOKEN.sub(token, line)
        # Clang diagnoses the emitter's redundant equality parentheses under
        # -Wall/-Werror; keep the same comparison and one conditional pair.
        line = re.sub(r'^(\s*if )\(\((ctx->[nzcv] == ctx->[nzcv])\)\)( \{)$',r'\1(\2)\3',line)
        # The emitter declares acc even for non-accumulating scalar VFP ops.
        # Keep the declaration/evaluation while making that intent explicit.
        if "acc = vfp_from_" in line:
            at = line.find(";",line.index("acc = vfp_from_"))
            line = line[:at+1]+" (void)acc;"+line[at+1:]
        result.append(line)
    adapted = "\n".join(result) + "\n"
    if not instructions or re.search(r"\b(?:reg\d+_|vfp\d+_|read_pages_|write_pages_|resume|out_of_budget_\d+)\b", adapted):
        raise ValueError("Unexpected pinned lifter grammar; refusing unchecked output")
    return adapted, instructions
