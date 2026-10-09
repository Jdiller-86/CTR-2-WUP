# Portable CPU and AOT evidence

This is original C11 code, independent of desktop threads, a flat 4 GiB host
reservation, SIMD, or host byte order. Addresses remain guest `uint32_t` values.
Mapped read/write/execute permissions, end-of-address-space overflow, alignment,
aliasing, capacity and overlap faults are explicit. Writes invalidate code
snapshots through executable aliases. The exclusive model supports one
deterministically scheduled guest CPU, and invalidates its monitor on writes;
it is not a multicore ARM memory-model implementation.

Interpreter subset: ARM conditions, data-processing ALU/shifts/flags, branch and
link/exchange, MUL/MLA, immediate/shifted-register byte/word transfers, unsigned
immediate preindexed LDRH/STRH without writeback (v0.10), word
LDREX/STREX, user read-only CP15 TLS MRC (Rd other than PC) and observable SVC
exits; Thumb16 shifts/ALU, high-register operations,
branch/exchange, selected loads/stores/SP-relative operations and SVC exits.
Version 0.12 adds checked user-mode ARM LDM/STM, Thumb16 PUSH/POP/LDMIA/STMIA
and original-AOT register BLX calls; see `cpu-stack.md` for scope and reference
evidence. Unaligned transfers fault deliberately. Exception-return/general VFP arithmetic,
Thumb long calls, ARM user-bank/exception-return multiple forms, other halfword
transfers and privileged instructions
are unsupported. The supported subset is research code, not a complete ARM11.

`translator/aot.py` specializes a smaller flat-executable subset into direct C
instruction blocks. It fails with guest PC/opcode context for unsupported code.
Version 0.11.0 also specializes word LDREX/STREX directly, with the same bounded
monitor/generation/fault behavior as the interpreter. The queued GSP probe checks
successful updates and a device publication forcing retry across execution paths.
Each block is instruction-budgeted. The dispatcher keys address plus ARM/Thumb
state, records missing blocks with module hash and PC, and permits explicit
interpreter fallback. Invalidated code cannot execute a stale translated block.
The dispatcher binary-searches strictly increasing block-address tables and
retains linear lookup for unsorted or duplicate-address tables.

`make -C tests/cpu check sanitize` exercises explicit golden values, randomized
integer carry/overflow against widened arithmetic, permissions/faults/aliases,
exclusive monitoring, branching, condition skips, SVC state/logging, budget,
fallback and invalidation. ASan/UBSan are enabled for a separate run. Python tests
verify generator reproducibility and rejection behavior.
The original translator also specializes this TLS read. v0.6.0's separate
89-instruction original startup probe matches interpreter/original/external AOT
state and memory through actual SRV/CFG SVCs; see `shared-services.md`.
v0.13 adds user CP15 ARMv6 DMB/DSB ordering, UADD8/USUB8/SEL with GE flags,
SMLAxy with signed halfwords and sticky Q, FPSCR transport and checked scalar
VLDR/VSTR word transport. See `guest-threads.md`. Other CP15/cache operations
remain unsupported, including emitter no-ops. General interpreter VFP arithmetic
remains absent; external arithmetic rejects unsupported controls.
v0.7.0's 151-instruction heap lifecycle probe executes allocation, permissions,
partial free and queries through all three backends with zero fallback. Data
mapping changes preserve AOT; executable mapping/protection and alias writes
still invalidate it. Protected/unmapped stores preserve the exact fault PC
across backends; see `guest-memory.md`.

The original fixture has ten ARM and three Thumb instructions. A loop computes
10, stores/reloads it through guest memory, branches to Thumb, computes 8 and
exits on SVC127. Interpreter and generated execution take 40 guest instructions,
and compare registers, flags, memory and instruction counts. Both paths are
linked into the RPX; it logs the result on actual console launch.

The separate 85-instruction NCCH guest exercises a checked executable loader,
SVC handling, long ARM framebuffer loops, input and orderly process exit. It
matches interpreter/AOT registers, flags and full data/BSS memory after two
frames. See `guest-fixture.md`; this adds single-guest sleep/event scheduling,
not full kernel/service emulation. v0.13 separately adds bounded single-core
multi-thread scheduling and conventional compiled libctru evidence.

Desktop timing is reported separately from console results. Its combined tiny
self-test time includes setup/interpreter/generated execution and cannot predict
game performance. Actual PowerPC correctness/timings, VFP/reference differential
testing, interrupt/wait scheduling, complete exclusive behavior and broader
arithmetic/memory/branch/VFP/dispatch/fallback benchmarks are outstanding.

`translator.upstream` adds validated-container, pinned-source external analysis
using the audited lifter. It does not use its native-endian desktop ABI as a
PowerPC backend and does not distribute that tool or its incomplete notices.
