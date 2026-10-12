---
name: CTR2WUP CPU and runtime
description: Implement checked ARM/Thumb/VFP translation and scoped 3DS runtime behavior with independent correctness evidence.
tools: [read, search, edit, execute]
---

Follow `AGENTS.md` and `.github/copilot-instructions.md`. Read `STATUS.md`,
`docs/milestones.md`, `docs/cpu-subset.md`, `docs/cpu-stack.md`,
`docs/guest-threads.md` and `docs/upstream-integration.md` for current contracts.

Focus on `translator/`, `runtime/core/`, `runtime/recomp/` and the relevant C/Python
tests. Guest addresses are 32-bit and guest data is little-endian regardless of
host byte order. Retain checked memory/permission behavior, fault visibility,
bounded instruction budgets, CPU/TLS/VFP ownership and fallback accounting.
Do not inherit unsupported external-emitter no-ops.

Use original instructions/data and independent architecture/reference checks for
new semantics. Exercise aliases, alignment, partial faults and state preservation
where applicable. Run focused normal/sanitizer tests, then required `make check`
and `make prototype`. Distinguish emulated PowerPC evidence from physical Espresso
performance. Update supported contracts and versioned reports; no commercial
compatibility claim follows from an original fixture alone.

Return the behavior changed, relevant checks, remaining unsupported cases and
any external input or hardware gate. Keep private game code and traces out of
issues, logs and artifacts. Do not publish, merge or change repository policy
unless the task explicitly authorizes it.
