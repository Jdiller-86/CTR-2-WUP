# CPU execution and guest threads

The portable C11 runtime is independent of host byte order, desktop threads,
SIMD and a flat 4 GiB reservation. Guest addresses are `uint32_t`; checked memory
access never converts them into host pointers. See [memory](guest-memory.md) and
[external AOT integration](upstream-integration.md).

## Interpreter and original AOT

The interpreter implements a bounded ARM/Thumb subset: conditions, integer ALU/
shifts/flags, branches/link/exchange, MUL/MLA, selected byte/halfword/word loads and
stores, LDREX/STREX, user read-only CP15 TLS and observable SVC exits. Thumb16
includes ALU, branch/exchange, selected loads/stores and SVC. ARMv6 register
SXTB/SXTH/UXTB/UXTH accepts rotations 0/8/16/24 and source/destination aliases;
paired/add forms and PC operands are unsupported.

`translator/aot.py` specializes a smaller flat-executable subset into direct C.
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
rejected before effects. General exception-return behavior, Thumb long-immediate
calls and complete ARM11/VFP coverage remain unsupported.

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

Startup support includes UADD8/USUB8/SEL with GE flags, SMLAxy with sticky Q,
VMRS/VMSR FPSCR transport, checked scalar VLDR/VSTR and user ARMv6 DMB/DSB
ordering. Barriers order checked CPU backing, not GPU completion. Interpreter
and original AOT transport floating-point words without general VFP arithmetic;
the external adapter rejects unsupported arithmetic/control modes before effects.
Raw VFP memory transport preserves bits under CRT FPSCR controls.

## Validation

```sh
make -C tests/cpu check sanitize
make -C tests/multiple check sanitize
make -C tests/threads check sanitize
python3 tools/validate-upstream.py --multiple-api --thread-api --output artifacts/cpu-check
```

Original fixture programs compare complete CPU/mapped-memory state across
interpreter, original AOT and the pinned external lifter at uneven budgets.
`tools/reference-multiple.py` and `tools/reference-extend.py` use optional pinned
Unicorn ARM1176 checks for independent instruction behavior. Tests include partial
faults, lifetimes, scheduling, aliases, invalidation, exclusive retries and exact
unsupported failures. Such checks do not predict commercial game performance.
