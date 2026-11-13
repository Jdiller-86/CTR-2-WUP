# CPU execution and guest threads

The portable C11 runtime is independent of host byte order, desktop threads,
SIMD and a flat 4 GiB reservation. Guest addresses are `uint32_t`; checked memory
access never converts them into host pointers. See [memory](guest-memory.md) and
[external AOT integration](upstream-integration.md).

## Interpreter and original AOT

The initial implementation covers ARMv6K, Thumb and scalar VFPv2 user-mode execution
in both the interpreter and original AOT translator:

| Family | Implemented scope |
| --- | --- |
| Arithmetic | Conditions, integer ALU/flags, immediate/register shifts, CLZ, MUL/MLA, signed/unsigned long multiply/accumulate and UMAAL |
| ARMv6 media | Rotated signed/unsigned extensions including paired/add forms; REV/REV16/REVSH; PKH; QADD/QSUB/QDADD/QDSUB; SSAT/USAT and halfword forms; signed/unsigned normal, saturating and halving parallel arithmetic; SEL |
| DSP multiply | Halfword and word-by-halfword products/accumulates; dual add/subtract and long accumulates; rounded/unrounded high-word products/accumulates; USAD8/USADA8 |
| Memory | Immediate and shifted-register byte/word accesses, pre/post indexing and writeback, signed byte/halfword loads, halfword stores and paired LDRD/STRD |
| Atomic operations | SWP/SWPB; byte, halfword, word and doubleword LDREX/STREX; CLREX; typed monitors tied to checked backing generations |
| Control | ARM/Thumb branches, link/exchange including immediate BLX and Thumb long calls; user CPSR flags/GE transport; CP15 TLS; ARMv6 DMB/DSB/ISB; PLD hints; observable SVC exits |
| Thumb | Immediate/register ALU, high registers, literals, register/immediate/SP-relative and signed loads/stores, address/SP arithmetic, extensions/reversals, stack/multiple transfers and conditional/unconditional branches |
| Scalar VFP | Single/double add, subtract, multiply, divide, square root, non-fused multiply/accumulate and negated forms; moves, absolute/negate, comparisons, precision and signed/unsigned integer conversions; ARM/FP register transfers, VLDR/VSTR and VLDM/VSTM including VPUSH/VPOP |

`translator/aot.py` specializes flat executable instructions into direct C.
It shares decoded semantic helpers for checked transfers, atomics and media
operations; generated code never calls the interpreter or decodes an opcode.
Blocks consume instruction budgets and dispatch by address plus ARM/Thumb state.
Missing blocks record module identity and PC; interpreter fallback is explicit.
Executable writes, aliases and protection changes invalidate stale blocks.
Strictly ordered block tables use binary search; unsorted/duplicate tables retain
linear lookup. External AOT executes through the separate checked ABI.

CPU single-data accesses use ARM1176-style U=1/A=0 bytewise unaligned semantics
across fully checked ranges. Failed ranges produce no partial stores.
Instruction fetch, multiple-register, exclusive and floating-point accesses
remain strictly aligned. This is a fixed userspace policy, not privileged SCTLR
emulation. Unimplemented privileged/cache operations fault visibly.
Paired LDRD/STRD use an eight-byte-aligned boundary and transfer ascending words,
with base writeback last. Earlier successful word effects remain visible if a
later word faults. Exclusive doubleword accesses preflight their full range;
byte/halfword/word/doubleword monitors cannot authorize a different access size.
The external generated-code ABI retains its word-exclusive profile and may
invalidate a non-word monitor when changing execution engines.

## Stack and multiple-register transfers

Supported forms are user-mode ARM LDM/STM (IA/IB/DA/DB), optional writeback,
interworking LDM PC, Thumb16 PUSH/POP/LDMIA/STMIA and register BLX. The target is
captured before BLX updates LR, including BLX LR. ARM STM PC stores PC+8.
Thumb LDM with its base listed suppresses writeback. ARM/Thumb STM writeback
with a listed base is supported only when the base is the lowest listed register.

Words transfer in ascending register/address order; arithmetic wraps at 32 bits
but each word still checks mapping, alignment and permissions. On a partial
access fault, the instruction PC and original base remain, while earlier word
loads/stores stay visible. These CPU instructions are not atomic publications.
The external ABI restores an emitter's early LDM base update on abort.

Empty lists, PC as base, user banks/SPSR returns, ARM loads with writeback and a
listed base, non-first listed-base stores, Thumb stores of PC and BLX PC are
rejected before effects. Thumb long-call prefix/suffix halves are independently
budgeted and retain LR between dispatches, including across budget exhaustion.
Privileged modes, banked registers, exception returns, Jazelle and big-endian guest
execution remain outside this initial user-mode scope.

## Threads and synchronization

One emulated CPU schedules eight process-local slots, including the initial
thread. Each owns registers/CPSR, 32 raw VFP words/FPSCR, a 512-byte TLS slot,
waits and deadlines. Priorities are 0x18..0x3f; equal priorities rotate after
32 instructions. Host budget boundaries do not reset the quantum. A context
switch invalidates the exclusive monitor. This is not SMP or priority inheritance.

Thread SVCs are CreateThread (08), ExitThread (09), GetThreadPriority (0b),
SetThreadPriority (0c) and GetThreadId (37). Core 0/default -2/any -1 use the
single CPU; other explicit cores fail. Entry permissions/alignment, stack and
capacity are checked before allocation. Stacks remain pinned until exit;
generations prevent stale slot reuse. Handle closure does not kill a thread.
Waiters and retained handles preserve terminated objects; ExitProcess ends all threads.

Mutexes have an owning thread and recursive depth; wrong-owner releases fail,
and owner exit releases locks. Events, mutexes, semaphores and joins support
wait-one/any/all and timeouts. Process-local address arbiters (21/22) support
signal, signed wait/decrement and timed forms. References survive handle closure.
Resource-limit queries (38/39/3a) report actual memory/thread/object quotas for
IDs 0..8 after full output preflight; timers have quota zero.

A synthetic read-only kernel page at `0x1ff80000` supplies compatibility version
2.58.0, application budget and single-system-core policy. A separate two-bank
shared time page at `0x1ff81000` uses a published sequence, Unix-zero epoch and
268111856 Hz guest ticks. These are explicit HLE values, not firmware dumps.

## Media and floating-point state

Interpreter and original AOT share decoded scalar VFP helpers. S0..S31 alias
D0..D15 through raw words, independently of host byte order. Scalar arithmetic
supports all four FPSCR rounding modes, sticky exception flags, flush-to-zero and
default-NaN controls. NaN payloads/signaling behavior are handled before host
arithmetic; multiply/accumulate rounds the product separately. Comparisons update
FPSCR NZCV; VMRS can copy them into CPSR. Precision and 32-bit integer conversions
include explicit saturation on invalid/out-of-range results.

The portable implementation requires IEEE binary32/binary64 and the C floating
environment; it restores the host rounding mode, exception flags and errno after
each arithmetic operation. Vector length/stride, enabled exception traps and
alternate half precision are rejected before arithmetic effects. Raw register
and memory transfers preserve payload bits without applying arithmetic controls.
Multiple FP transfers check their register range first, transfer ascending words
and write back the base last; partial faults retain earlier successful words.
This scalar baseline does not certify every floating-point boundary case or
Espresso performance. The [external adapter](upstream-integration.md) retains its
separate, narrower arithmetic-control profile.

User ARMv6 barriers order checked CPU backing, not GPU completion.

## Validation

```sh
make -C tests/cpu check sanitize
make -C tests/multiple check sanitize
make -C tests/threads check sanitize
python3 tools/validate-upstream.py --multiple-api --thread-api --output artifacts/cpu-check
python3 tools/validate-integer-cpu.py --output artifacts/integer-reference
python3 tools/validate-vfp-cpu.py --output artifacts/vfp-reference
```

Original fixture programs compare complete CPU/mapped-memory state across
interpreter, original AOT and the pinned external lifter at uneven budgets.
`tools/reference-multiple.py` and `tools/reference-extend.py` use optional pinned
Unicorn ARM1176 checks for independent instruction behavior. Tests include partial
faults, lifetimes, scheduling, aliases, invalidation, exclusive retries and exact
unsupported failures. Such checks do not predict commercial game performance.
The optional integer reference command needs Unicorn 2.1.4 and checks original
instruction encodings against ARM1176 registers, flags and memory in both the
interpreter and generated C. It is a focused instruction-family check, not full
ISA certification or a production dependency.
The scalar VFP reference command uses the same optional pin and checks both
execution paths against ARM1176, including signed zero, infinities, NaNs,
denormals, rounding modes, conversions and raw register/memory transport.
