# Shared guest memory management

Version 0.7.0 advances M3/P3 with ControlMemory (SVC 1) and QueryMemory (SVC 2).
It does not enable a commercial profile. This is original C11 code with checked
little-endian accesses and no guest-to-host pointer casts. Public interface
values/register conventions were checked against libctru
`9b55eda44cf80b971503991e0c78f9bc8fe50425` (`svc.h`/`svc.s`); the already-audited
Azahar source supplied semantic comparisons. No upstream implementation was copied.

## Supported contract

ControlMemory receives operation in r0, address in r1, source address in r2,
page-aligned size in r3 and permissions in r4. It returns Result in r0 and the
allocated/affected guest address in r1. Errors clear r1 and leave mappings and
owned backing unchanged. Other registers remain intact.

| Operation | Supported behavior |
|---|---|
| ALLOC `3` | Zero-filled owned heap allocation; address 0 selects the first free page range, or use a specified free address |
| ALLOC_LINEAR `0x10003` | Same checked CPU allocation in the Old-family linear window; reports Continuous state |
| FREE `1` | Remove a fully owned mapped range, including a subrange or adjacent owned allocations; permissions must be 0 |
| PROT `6` | Enforce permissions on an owned range, splitting mappings at page boundaries |
| Application region flag `0x100` | Accepted with the supported operations |

Heap addresses occupy `[0x08000000,0x10000000)` and Old-family linear addresses
`[0x14000000,0x1c000000)`. Allocation skips existing image/stack mappings.
Addresses and sizes must be page aligned; sizes must be nonzero. Source address
must be zero. Permissions 0/1/2/3 mean inaccessible/read/write/read-write;
execute and other flags return an error. Reads/writes enforce permissions in
every backend. FREE/PROT cannot mutate loader-owned image, stack or TLS mappings,
even inside the heap window. Partial operations preflight the full range and
fragment capacity before publishing a change.

QueryMemory receives address in r2 and returns r0 Result, r1 base, r2 size,
r3 permissions, r4 state and r5 page flags. It reports the containing logical
region, merging adjacent regions with equal state and permissions. A region may
include immutable loader pages and mutable heap pages: its query result does not
authorize FREE/PROT of the entire extent. Gaps report Free state 0/permissions 0.
Image text/rodata report Code 4, data/heap Private 5, linear Continuous 7,
stack/TLS Locked 11. Page flags are 0; physical-page attributes are unavailable.
Queries outside `[0,0x40000000)` return an address error and clear r1-r5.
No host pointer is returned.

Misaligned address/size, invalid combination, invalid address/state and resource
exhaustion return nonzero Result codes (`e0e01bf1`, `e0e01bf2`, `e0e01bee`,
`e0e01bf5`, `e0a01bf5`, `d86007f3`). Exact precedence and unsupported combinations
are a scoped HLE contract, not full retail-kernel conformance. Request/failure
counts and last operation/address/result remain in `ctr_guest` for diagnosis.

## Capacity, ownership and translation

There are 16 owned allocations and 32 mapping fragments, with a combined 64 MiB
retained-backing limit including image/BSS/stack/TLS. Partial free retains the
original host allocation until its last mapped byte is removed; all its backing
remains charged. Removed pages can be allocated again with new zeroed storage.
Final free and guest close release ownership. Fragment exhaustion fails atomically.
These limits need revisiting against actual title requirements and Wii U costs.

Non-executable mapping changes advance the memory/exclusive generation without
invalidating unchanged executable snapshots. Adding/removing executable mappings,
changing execute protection and writes through executable aliases still invalidate
AOT. Dynamic heap management therefore preserves normal translated execution.

Linear backing is contiguous for checked **CPU** accesses only. FCRAM physical
addresses, GPU/DSP allocation, cache/coherency support and the GX2 memory bridge
are absent. MAP/UNMAP aliases, RESERVE, system/base regions, New-family linear
windows and unknown flags stop visibly as unsupported. Shared-memory blocks,
image reprotection, multi-thread ownership, physical accounting and broader
kernel behavior remain unfinished.

## Original execution evidence

`host/heap_fixture.py` generates an original NCCH with 151 ARM instructions.
It allocates three heap pages, writes little-endian values, queries the region,
protects the middle page read-only, reads it, restores permissions and frees it.
It queries/reallocates that hole, verifies zeroed memory, allocates/queries linear
memory and frees all backing before exit: 14 memory requests plus ExitProcess.
Independent expected SVC outputs accompany full state/memory comparisons.

`make -C tests/heap check sanitize` checks the real ARM lifecycle, guard/write-only
permissions, static-page ownership, overlap/overflow, holes, retained-backing
budget, slot/fragment exhaustion and atomic failures. `tests/recomp/test_heap.c`
compares actual external AOT, original AOT and interpreter CPU/mapping/backing
state at 1/7/23 instruction budgets. It also checks guest-store fault PCs on
protected/freed pages, zero fallback and preserved executable generations.

All three Wii U builds embed the original interpreter/AOT self-test. Native mode
displays `GUEST HEAP LIFECYCLE PASS CODE 0`; SD logs record
`heap_selftest PASS result=0`, or an exact FAIL code. The external-AOT heap probe
is a desktop/emulated-PPC test, separate from the console guest program.
Physical behavior/timings, native Windows and conventional 3DS homebrew boot
remain pending. STATUS.md records current report paths.
