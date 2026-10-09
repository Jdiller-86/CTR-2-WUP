# Shared memory and synchronization

Version 0.8.0 adds original C11 objects needed by HID/GSP-style startup. Public
SVC register interfaces were checked against pinned libctru `svc.h`/`svc.s`;
no upstream implementation is copied. The original v0.8 fixture is single-thread;
v0.13 extends ownership/waits to eight process-local contexts (`guest-threads.md`).
This remains a single-process subset, not full retail-kernel behavior or a
commercial profile.

| Interface | Implemented behavior |
|---|---|
| CreateMemoryBlock `1e` | r0 other rights/r1 source/r2 size/r3 own rights; r0 Result/r1 handle. Zero source creates local zeroed backing; nonzero pins one contiguous owned RW heap allocation |
| MapMemoryBlock `1f` | r0 handle/r1 destination/r2 own rights/r3 other rights; checked local aliases in `[10000000,14000000)` |
| UnmapMemoryBlock `20` | r0 handle/r1 exact mapped address; removes mapping/releases reference |
| CreateMutex `13` / ReleaseMutex `14` | Initially locked/unlocked; recursive acquisition by the sole thread; checked release |
| CreateSemaphore `15` / ReleaseSemaphore `16` | Nonnegative initial count, positive signed maximum, bounded release and old count in r1 |
| WaitSynchronization `24` | Event/mutex/semaphore acquisition, consumption, timeout/deferred wait |
| WaitSynchronizationN `25` | Up to 16 checked handles; wait-any index/atomic wait-all. r0 timeout low/r1 array/r2 count/r3 all/r4 timeout high |

Typed objects share the existing 64-slot generation-tagged handle table. Pools
allow 8 shared objects, 16 mutexes, 16 semaphores and the existing events/sessions.
Shared objects allow four mappings each within the 32-fragment memory limit.
Owned shared backing shares the combined 64 MiB budget; pinned heap is charged
once. Guest close frees owned shared backing and heap storage.

Shared aliases enforce rights: own permissions must be a subset of object rights;
other permissions must match policy or DONTCARE `10000000`. Only none/read/write/
read-write are supported. Failed mapping changes neither published memory nor
references. Mapped objects survive closing their last handle until unmapping or
process close. A source allocation cannot be freed/reprotected while an object
pins it; final release removes the pin. This conservative rule pins the whole
original allocation. Aliases report QueryMemory Shared state 6. Foreign processes,
rights transfer and GPU/DSP physical backing remain absent.

Waits retain objects independently of handle closure. Wait-any consumes the first
ready object in caller order. Wait-all preflights every object before acquiring
any and returns index `ffffffff`; timeout returns the same index. Duplicate
objects in wait-all are rejected explicitly; wait-any duplicates are accepted.
Invalid handles/arrays/counts fail without consuming objects. Polling/external
event signals resume the sole waiter deterministically. Negative timeout values
are infinite; finite deadline overflow is rejected. Mutex ownership belongs to
the sole guest thread, so acquisition while owned is recursive. Priority
inheritance, other-thread ownership, pulse events, address arbitration and
multi-thread scheduling remain unfinished.

`host/kernel_fixture.py` generates 148 original ARM instructions using 27 SVCs.
It allocates/pins heap memory, maps/reads a read-only shared alias, duplicates/
closes handles, acquires/releases a mutex, consumes/releases a semaphore,
checks timeout and wait-any indexes, then releases all backing/objects and exits.
Expected outputs and interpreter/original/external AOT state/memory are checked.
`make -C tests/kernel check sanitize` covers lifetimes, pins, permissions, wrong/
stale handles, wake/timeout/close while waiting, atomic wait-all and exhaustion/
rollback. External validation adds the real lifter probe; `--kernel-api` adds
the API suite to a compiler/target run.

All three Wii U builds embed the original interpreter/AOT self-test: native mode
`SHARED/SYNC LIFECYCLE PASS CODE 0`, SD log `kernel_selftest PASS result=0`, or a
specific FAIL code. Console/native Windows evidence is still required.
See STATUS.md for current reports.
