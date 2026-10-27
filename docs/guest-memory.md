# Shared guest memory management

The loader places BSS directly after initialized DATA, including its last page's
unused tail, and copies no ExeFS padding into BSS. A separate read-only synthetic
kernel configuration page at 1ff80000 supplies documented HLE policy; it counts
toward backing limits. Thread stacks pin owned allocations until their guest
context exits, and eight 512-byte TLS slots share the original TLS page. See
`cpu.md`; no retail firmware image or cross-process mapping is supplied.


A separate read-only shared time page at 1ff81000 is also mapped. Its double-bank
milliseconds/tick-frequency/tick-offset reference advances with the guest clock
from an explicitly synthetic Unix-zero epoch. Ordinary unmodified libctru time
queries can run; actual RTC/timezone/full firmware config is not supplied. This
page adds 4 KiB to loader backing/quota accounting; the minimal thread fixture
owns 36 KiB. Both host and portable loaders reject segment
collisions and include the allocation in their bounds.

ControlMemory (SVC 1) and QueryMemory (SVC 2) manage guest allocations.
This is original C11 code with checked
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

Renderer resource keys bind a live memory owner, virtual range, permissions,
mapping version and maximum backing-page write version. A fixed 1024-entry table
tracks hashed host 4 KiB pages; aliases share dirty versions. Page sharing and
hash collisions conservatively invalidate extra resources. Mapping changes
invalidate all keys; failed writes and unchanged protection do not. Versions
never wrap: an exhausted counter disables keys and forces owned snapshot misses.
Keys cannot survive owner reset, copying or closure.

Checked scalar, bulk and atomic writes update these versions. Trusted shared,
TLS, configuration and synthetic-time publications notify the same backing
tracker; executable aliases invalidate code snapshots. Synthetic-time updates
retain their existing exclusive-monitor policy. Renderer cache hits also compare
the exact current bytes, preserving correctness for caller-supplied backing
changes outside the notification API. Permissions and allocation ownership are
validated before reuse; no version authorizes access.

`ctr_memory_write_ranges` publishes up to 32 owned output ranges atomically.
It validates every destination, source and descriptor before copying; overlapping
virtual destinations or backing aliases reject. A successful nonempty batch
advances the memory generation once and the code generation once if any output
touches executable backing. This supports a single color/depth publication;
a late invalid range leaves all bytes and generations unchanged.

Linear backing is contiguous for checked CPU accesses. The bounded guest-draw
adapter maps owned Old-family linear allocations to physical resources; general
FCRAM/VRAM/DSP mapping and asynchronous GPU coherency remain unsupported. MAP/UNMAP aliases, RESERVE, system/base regions, New-family linear
windows and unknown flags stop visibly as unsupported. Image reprotection, general physical accounting and broader kernel behavior
remain unfinished. Thread stacks and shared objects retain their owned backing.

## Validation

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

The same original interpreter/AOT self-test is embedded in native diagnostics.
Host reference runs and physical observations remain separate evidence.
Source allocations pinned by shared objects reject FREE/PROT until final release;
owned shared backing uses the same combined budget. See [services](services.md).
