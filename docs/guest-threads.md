# Single-core guest threads

Version 0.13 adds eight process-local thread slots, including the initial thread.
Each holds ARM registers/CPSR, 32 raw VFP words/FPSCR, a separate 512-byte TLS slot,
waits and deadlines. One emulated CPU executes strict priorities 0x18..0x3f;
equal priorities rotate after 32 instructions. Host budget changes do not restart
the quantum. Context switches invalidate the exclusive monitor. This is not SMP,
priority inheritance, complete affinity or a full 3DS kernel.

Implemented thread SVCs are CreateThread (08), ExitThread (09), GetThreadPriority
(0b), SetThreadPriority (0c) and GetThreadId (37). Core 0/default -2/any -1 use
the single emulated CPU; other explicit cores are rejected. Entry permissions,
alignment, stack and capacity are checked before allocation. Heap stack backing
is pinned until exit. IDs and handle generations prevent stale slot reuse.
Closing a handle cannot kill a running thread. Terminated objects survive retained
handles and waiters; waits can join them. ExitProcess terminates every thread.

Mutexes track their owning thread and recursive depth. Wrong-owner releases fail;
closing a locked handle retains its object, and owner exit releases its locks.
Events, mutexes, semaphores and thread joins support existing wait-one/any/all and
timeouts across contexts. Process-local address arbiters (21/22) support signal,
signed wait/decrement and timed forms; wait references survive handle closure.
No cross-process arbitration or kernel priority inheritance is claimed.

The local process resource-limit object (38/39/3a) exposes the runtime's actual
bounded memory/thread/object quotas for IDs 0..8. Query lists are fully checked
before any output writes. Timers remain unavailable (quota zero); CPU-time quota
and other processes are unsupported. A synthetic read-only kernel configuration
page at 1ff80000 supplies compatibility version 2.58.0, application budget and
single-system-core fields needed by libctru startup. These values are HLE policy,
not measurements or a firmware image. Other configuration fields remain zero.

The checked loader places BSS immediately after initialized DATA, including the
unused tail of its last page. ExeFS padding never overwrites BSS. Tests cover
non-page-aligned DATA, allocation bounds and conflicts with reserved mappings.

CPU additions required by compiled startup are ARM UADD8/USUB8/SEL and GE flags,
SMLAxy with signed halfwords/sticky Q, VMRS/VMSR FPSCR transport, checked scalar
VLDR/VSTR for S0..31/D0..15, ARM STM's
PC+8 source value and user CP15 ARMv6 DMB/DSB ordering. Interpreter and original
AOT transport floating-point words; they do not implement general VFP arithmetic.
External AOT preserves FP contexts and rejects unsupported arithmetic/control
modes before effects. Cache-maintenance operations and unimplemented forms still
fail visibly. Barriers order checked CPU backing; they do not complete PICA work.

`make -C tests/threads check sanitize` tests context ownership, scheduling across
host budgets, joins, failure rollback, stack pins, references, address arbitration
and quota output preflight. The original three-thread fixture executes 345
instructions, 22 SVCs and eight switches with three-backend state/memory parity.
All diagnostic RPX/WUHB variants embed its interpreter/original-AOT self-test.
Separate external AOT probes check media instructions/barriers and two threads'
VFP arithmetic/FPSCR isolation. `compiled-homebrew.md` records real libctru tests.
Every physical Wii U result remains pending.
