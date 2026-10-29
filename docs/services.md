# Services, kernel objects and input

The runtime implements one local process with explicit service commands and
checked TLS transport. Unknown commands fail with endpoint/header context;
unavailable service names return SRV NotFound `d88067fa`. No generic-success
stubs or asynchronous service directory are provided. [CPU threads](cpu.md) and
[memory](guest-memory.md) define scheduling and backing ownership.

## TLS, SRV and CFG

ARM `MRC p15,0,Rd,c13,c0,3` reads the current thread's user read-only TLS address.
Rd=PC, writes and other unsupported CP15 operations fault. The external adapter
refreshes TLS at every execution entry so thread changes cannot retain stale TLS.

| Interface | Request | Behavior |
|---|---|---|
| ConnectToPort `2d` | r1: checked NUL-terminated `srv:` | Result in r0, generation-tagged SRV session in r1 |
| SendSyncRequest `32` | r0: session; command at TLS+`0x80` | Checked synchronous request/reply; SVC and service results are separate |
| SRV RegisterClient | `00010002`, PID descriptor `20`, placeholder | Registers the session; reply `00010040` |
| SRV GetServiceHandle | `00050100`, name[8], length, flags 0/1 | Opens supported named service; move descriptor `10` |
| CFG SecureInfoGetRegion | `00020000` | Configured region |
| CFG GetSystemModel | `00050000` | Configured Old-family model |
| CFG GetModelNintendo2DS | `00060000` | 0 for Old2DS, 1 otherwise |
| CFG GetConfigInfoBlk2 | `00010082`, size 1, language block `000a0002`, descriptor `1c`/`1e`, output | One configured language byte |

`ctr_guest_configure` accepts region 0..6, language 0..11 and Old-family models
0 (Old3DS), 1 (Old3DS XL), 3 (Old2DS), before service access. Defaults are USA (1),
English (1), Old3DS (0). Values are emulated policy, not inferred console settings.
New-family models, notifications, user/server IPC, arbitrary descriptors,
cross-process mappings and general CFG/Mii behavior are unsupported.

The 64-word command buffer is read/written as little-endian words. Reserved bits,
word counts, descriptor shapes and full permissions are checked before effects.
Mapped outputs cannot overlap the command buffer, including through aliases.
The 64-slot generation table holds typed handles, including 16 session objects.
Duplicate, stale, wrong-type and exhausted handles fail; closing SRV leaves
already acquired service sessions alive.

## Shared memory and synchronization

| SVC | Contract |
|---|---|
| CreateMemoryBlock `1e` | Zero source creates local zeroed backing; nonzero pins one contiguous owned RW heap allocation |
| MapMemoryBlock `1f` | Checked local aliases in `[0x10000000,0x14000000)` |
| UnmapMemoryBlock `20` | Exact mapped address; releases the mapping reference |
| CreateMutex `13` / ReleaseMutex `14` | Owner-aware recursive mutex with checked release |
| CreateSemaphore `15` / ReleaseSemaphore `16` | Nonnegative count, positive signed maximum, bounded release; previous count in r1 |
| WaitSynchronization `24` | Event/mutex/semaphore/join acquisition or deferred timeout |
| WaitSynchronizationN `25` | Up to 16 handles; wait-any index or atomic wait-all |

Pools allow eight shared objects, 16 mutexes and 16 semaphores. Shared objects
allow four mappings within the 32-fragment memory limit. Owned backing shares
the combined 64 MiB budget; pinned heap is charged once. Own alias permissions
must be a subset of object rights; other permissions match policy or DONTCARE
`10000000`. None/read/write/read-write are supported. Failed mapping preserves
memory and references. Mapped objects survive final-handle close; pinned source
allocations cannot be freed/reprotected before their last reference releases.
Aliases report QueryMemory Shared state 6. Foreign-process and general GPU/DSP
shared backing are absent; the bounded audio service owns its separate DRAM map.

Wait references survive handle closure. Wait-any consumes the first ready
object in caller order. Wait-all preflights every object, rejects duplicates and
returns index `ffffffff`; timeout uses the same index. Wait-any duplicates are
accepted. Invalid requests consume nothing. Negative timeouts are infinite;
finite overflow fails. Mutex owner exit releases its locks. Priority inheritance,
pulse events and cross-process synchronization are unsupported.

## Standard HID

`hid:USER` GetHandles request `000a0000` replies `000a0047`, result 0, copy
descriptor `14000000` and six typed handles: a shared page plus Pad0/Pad1,
accelerometer, gyro and debug events. The last three events have no producer.
Motion enable/disable, other HID commands, `hid:SPVR`, IRRST and New-family input
fail. The device retains its six internal handles; returned copies and read-only
mappings follow normal lifetime rules. Exhaustion rolls back allocations.

`ctr_hid_update` publishes physical button bits 0..11, Circle Pad axes -156..156
and valid touch coordinates within 320×240. Direction bits 28..31 use radius 40
and diagonal sectors; clients derive touch bit 20. ZL/ZR, C-stick, motion, slider
and raw calibration are absent. Invalid values/nonincreasing timestamps publish
nothing.

The eight-entry LE pad ring stores index at `0x10`, current buttons at `0x1c`
and 16-byte entries from `0x28`: held/added/removed/packed signed XY. Touch index
is at `0xb8`; eight-byte entries from `0xc8` store packed unsigned XY/validity.
Indexes advance together. At index 0 wrap, current/previous timestamps at
`0`/`8` and `0xa8`/`0xb0` use the 268111856 Hz guest tick scale. Pad0/Pad1 signal
after publication. One host thread prevents concurrent partial reads.

VPAD maps to Old-family button bits; the left stick scales to ±156 and touch
passes through the bottom-screen viewport. Diagnostic Plus/Minus/Y remain host
controls, and Minus exits before publishing its press. The displayed right stick
is not exposed as New-family input.

## Validation and interface sources

```sh
make -C tests/services check sanitize
make -C tests/kernel check sanitize
make -C tests/hid check sanitize
python3 tools/validate-upstream.py --service-api --kernel-api --hid-api --output artifacts/service-check
```

Original ARM fixtures compare full state/memory through interpreter and both
AOT paths. API checks cover wire layouts, complete preflight, stale/wrong handles,
lifetimes, aliasing, rollback, waits, ring transitions and exhaustion. Public
interface facts were checked against libctru
`9b55eda44cf80b971503991e0c78f9bc8fe50425`; no upstream implementation was copied.
[GSP/display](graphics-transport.md), [filesystem/APT](filesystem.md) and
[audio](audio.md) document the other service boundaries.
