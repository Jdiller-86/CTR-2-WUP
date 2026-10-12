# Architecture and implementation boundaries

The attached development brief is the project requirements. The repository was
empty at the start of this implementation. See `foundation-audit.md` for pinned
candidate source, measured builds, and licensing/PowerPC constraints.

The host CLI is Python 3.12; inspection/runtime tooling uses the standard library
and encrypted packaging optionally uses cryptography. It uses the
same argument-list subprocess interface on Windows and Linux. Container parsing,
inspection, build reports, and package inspection are separate from execution.
Executable artifacts are the original native diagnostic and controlled guest RPXs,
plus the separate .NET CIA intake console utility cross-published for Windows x64.
An RPX is not an installable WUP directory.

The CIA intake path uses bounded signature-layout/TMD/bitmap/content checks and
selects index-0 application NCCH with checked absolute file offsets. Its derived
CXI view preserves ExeFS/exheader code identity while explicitly removing RomFS
and header signature. The standalone .NET helper validates stored hashes; the
shared Python engine also validates BLZ/decompressed revision. This private view
supports analysis through a 32 MiB attachment constraint, not game execution or
asset staging. See `cia-intake.md` for supported profile and limitations.

The portable C core stores guest addresses as 32-bit integers and accesses bytes
through checked mappings. Guest memory is explicitly little-endian on every
host. The initial interpreter and direct generated-C backend are bounded CPU
subsets with explicit unsupported exits. `host/loader.py` extracts inspected NCCH
segments; the portable loader allocates checked permissions/stack/TLS/BSS. A
single-guest runtime implements bounded SVC sleep/events/handles/ticks/debug/exit.
Version 0.6.0 adds user read-only CP15 TLS access, typed/refcounted session handles,
checked TLS IPC transport and restricted SRV/CFG startup commands. Its original
ARM probe reads configured region/model/language through all three execution
paths; see `shared-services.md`. This is shared runtime code, not a game patch.
Version 0.7.0 adds owned heap/linear CPU allocation, atomic partial free/protection
and queries; `guest-memory.md` defines the bounded contract. Ordinary heap
changes preserve AOT snapshots; executable writes/aliases and execute protection
still invalidate them. GPU/DSP physical memory remains pending.
The original ARM fixture writes both framebuffers and responds to input through
an explicit harness contract. Neither implements complete ARM11/VFP, a full
3DS kernel, general IPC or game HLE. The pinned external lifter now generates C through
an original host driver. An original ABI 4 implementation statically executes
that code using checked little-endian callbacks and per-instruction state;
see `upstream-integration.md`. Its desktop runtime is not a Wii U dependency.

The native WUT backend provides independent TV and GamePad diagnostic output,
input, sound, process lifecycle, timing, and CPU self-test instrumentation.
v0.14.6 reserves a separate fixed-size 4 KiB result TXT independently of WHB
trace handlers. Every update checks the reserved identity, writes, closes and
reads back all bytes; named guide outcomes persist in the result even if the
full trace fails. First errors are sticky; result-storage failure pauses guest
input/execution, while full-trace failure is reported independently. Result
writes happen at startup/self-tests/guest/checkpoint/guide/save/periodic/cleanup
boundaries. Guest state is captured before close so cleanup cannot erase its
final stats. Final result writes precede system transition.

The longer trace retains v0.14.5 `fflush`/`fsync`, checked close, mounted-volume
flush and existing-file reopen. Handler registration is now checked. WUT owns
the SD mount; the diagnostic releases its extra FSA client before transition.
Neither exact read-back nor desktop abrupt-exit tests establish SD power-loss
durability. The user reports successful v0.14.5 Native/Guarded/Compiled runs but
no new logs; the uploaded text reports are older. Cause is unknown. See
[console-evidence.md](console-evidence.md).

v0.14.4 caches bounded nearest-neighbor coordinate maps and reuses scaled rows,
with byte-order-independent RGBA output, checked source/output capacity and black
borders/padding. The native backend flushes/invalidates the produced GX2 surfaces.
The guide waits for a fresh decoded green frame after input before offering R,
and records A-to-green delay separately from user visual confirmation.
AOT execution returns a missing-block fault after 64 consecutive dispatches with
no guest instruction progress, so empty/unwinding entries cannot consume unlimited
host time without spending their budget. This guard cannot recover GPU/platform
hangs and has not been attributed to the console freeze. Varied-clock live-input
replay checks original compiled red/green transitions and exit on desktop.
v0.14.3 polls ProcUI directly, suspends input/rendering outside foreground and
releases guest/owned AX/GX2/report resources before ProcUIShutdown. On Minus it
cleans up, clears stale graphics callbacks while retaining save acknowledgement,
requests Menu and polls to EXITING; HBL relaunch follows cleanup/shutdown.
WUT's devoptab owns SD mounting. Checked TXT records include lifecycle states and
bounded input/guest/render checkpoints; end-of-report does not establish menu
return. Native AX ownership is explicit; compiled apps do not use AX.
The guarded AOT comparison uses a call limit of two instead of the standard 64,
with the existing unwind/dispatcher. Exact desktop frames/input/exit match; depth,
unwinds and frame-boundary stack headroom are diagnostic evidence, not proof of
a console stack fault. See `console-retest.md`.
All diagnostic builds also contain the original TLS/SRV/CFG startup self-test
with a visible result and SD log. Unsupported cache operations fail visibly
instead of inheriting the external emitter's no-op behavior.
All builds also embed the original ARM heap lifecycle self-test and its SD log.
Version 0.8.0 adds shared/mutex/semaphore objects and wait-any/all, with mapping/
wait ownership independent of handles. `kernel-objects.md` defines its
single-thread scope. Its original ARM self-test is also embedded.
Version 0.9.0 exposes standard read-only HID pad/touch rings and events. An
independent original ARM consumer in each Wii U build reads live VPAD input
through that service; `hid-input.md` defines its bounds and absent motion support.
Version 0.10.0 adds checked direct GSP framebuffer submission and portable
rotated RGBA8/BGR8/RGB565/RGB5A1/RGBA4 conversion. A separate original ARM guest
reads HID, writes RGB565 and displays through this interface in all Wii U builds;
see `gsp-display.md`. Version 0.11.0 adds one local GSP relay/shared framebuffer
page, diagnostic VBlank and atomic queued presentation; see `gsp-queues.md`.
The standard display guest now uses this queue. GX commands and PICA remain absent.
Its guest mode presents completed ARM-written raw framebuffers through the same
GX2 copying path. It is separate from PICA/GSP emulation; see `guest-fixture.md`.
Version 0.12 adds checked user-mode ARM/Thumb multiple-register/stack transfers
and BLX calls, with an embedded original nested-call self-test and independent
ARM1176 reference checks; see `cpu-stack.md`.
Compilation proves toolchain integration. Only physical console results can
prove rendering, input, sound, guest semantics on PowerPC, or performance.

Version 0.13 adds eight single-core guest contexts with strict priorities,
instruction quanta, CPU/TLS/raw VFP/FPSCR isolation and retained wait/stack
ownership. Thread joins, owner-aware locks, address arbiters and local quota
queries support normal compiled libctru threads/startup. BSS shares the final
DATA page correctly; a read-only synthetic kernel configuration page supplies
explicit HLE policy. See `guest-threads.md` for supported SVCs and limits.
`compiled-homebrew.md` records normal devkitARM CRT/newlib/libctru applications,
checked three-frame CPU display, standard input and a fourth interactive Wii U
build. The v0.13 path left APT/FS startup requests visibly unavailable; v0.14 adds
explicit standalone APT and platform-owned private FS volumes with checked
archive/file sessions and snapshot commits. Guest filenames never address the
host filesystem. The original compiled test proves save/read/fresh-process reload;
see `filesystem.md` for supported commands and remaining commercial boundaries.
It does not execute PICA or supply commercial compatibility.

v0.14.6 adds an explicit borrowed 64-bit read-only current-process Level-3 RomFS
view. Checked FSFILE transport exposes bytes; unmodified libctru resolves nested
and Unicode names/directories in guest code. Lifetime and read publication are
checked, with no host path exposure. v0.16 adds canonical NCCH/IVFC verification and asset preparation; signature
authentication and commercial asset integration remain open; see `romfs-assets.md`.

Save publication reserves `.pending`, moves a validated primary to absent
`.previous`, publishes into the vacant primary name, then removes the backup.
Failed publish attempts rollback; absent-primary load can recover a validated
backup. Corruption and ambiguous siblings remain visible failures. This avoids
requiring FSA rename replacement; it is not a concurrent-writer/power-loss guarantee.

A read-only shared time page at 1ff81000 uses two banks and a published sequence,
explicit synthetic Unix-zero epoch, guest-clock progression and matching 268111856
Hz ticks. It is separate from kernel policy/TLS and counts as an additional 4 KiB
backing allocation. ARM single-data byte/halfword/word accesses use checked
little-endian U=1/A=0 unaligned semantics; other instruction classes stay strictly
aligned. Defined lowest-base STM writeback is supported. Reference and generated
original/external AOT checks are recorded in `cpu-stack.md`.


`renderer/` contains bounded command inspection and small format/presentation
references. Capturing PICA register writes does not execute a draw. An explicit
exception reports the absent shader/texture/TEV/GX2 translation path.

`packaging/` inspects original/supplied package structure and title identities.
Content structure, cryptographic authenticity, and installer acceptance are
different checks. There is no bundled common key and no unlicensed packer.
Original `stage-title` metadata/artwork is a packaging prerequisite. Its output
is explicitly not an installable package. Version 0.4.0 implements scoped
encrypted FST/content/TMD/ticket construction with user-local inputs, and a
separate streaming decrypting verifier. The original four-content profile has
no authentic signatures and awaits installer/console acceptance. No key or
certificate is bundled; offline proof uses temporary synthetic inputs only.

Generated artifacts are ignored by Git. User ROMs, saves, extracted assets,
translated commercial code, keys, and packages must stay under ignored private
directories. Synthetic fixtures are original instructions/data generated from
source; no commercial assets are distributed.

## Execution gates

1. Native RPX compile and desktop tests can be completed in this environment.
2. Physical RPX screen/input/audio/exit tests and PowerPC CPU results are pending.
3. Checked loading and bounded guest-thread/kernel services now execute original
   compiled libctru CPU/input/framebuffer applications. Console stability,
   physical interface evidence and wider APT/FS/GX service semantics are needed.
4. PICA rendering, required services/modules, filesystem/save/audio semantics,
   and exact-ROM revision profiles must precede a commercial conversion.
5. License-cleared packing plus locally configured keys and actual install/menu
   launch tests must precede a WUP conversion claim.
6. The Windows GUI follows a demonstrated execution pipeline; it is deferred.

The interpreter implements ARMv6 SXTB/SXTH/UXTB/UXTH register extensions, ROR
#0/8/16/24, conditional execution and destination/source aliasing without flag
changes. PC operands and adding/paired forms remain unsupported. Unit/sanitizer
checks and 2,048 Unicorn ARM1176 cases validate this scope. Full compiled libctru
interpreter startup still stops on STRD, so no interpreter comparison is shipped.
For the optional reference, install `unicorn==2.1.4` in a developer Python
environment, then run `python3 tools/reference-extend.py --output NEW_DIRECTORY`.


v0.15.0 adds input preparation ahead of container inspection. A bounded local
key provider loads selected 16-byte file references in-process, with redacted
failures/repr. Plaintext support-file imports create a separate explicitly private
store. CIA AES-CBC main-content hashes are checked before inner NCCH decryption;
NCCH v2 primary/secondary CTR, seed/key derivation and extents are checked before
publishing a derived CXI. Output includes the main RomFS, but not other contents,
tickets or key files. Input/output identities and lack of signature authentication are explicit;
v0.16 verifies the full canonical IVFC tree when RomFS is present. Read-only sources, bounded chunks, input-change checks
and atomic non-overwriting publication reuse the existing host boundaries.
The validated decrypted CXI enters the unchanged loader/lifter pipeline; key
values never reach generated C, runtime binaries or WUP staging. See
`encrypted-input.md` for profiles and pending hardware/Windows checks.


v0.16 separates validated asset preparation from execution. The host verifies
NCCH's anchored RomFS superblock, the complete canonical IVFC tree, every file
hash and the bounded Level-3 directory/hash metadata. It atomically exports one
Level-3 `romfs.bin` plus a manifest, never host paths derived from guest names.
The file provider streams SHA-256 once at open, checks the configured expected
hash and lends a read-only view to existing FS transport. It uses standard-C
`fseek`/`ftell`, so the Wii U/32-bit limit is LONG_MAX, and requires immutable
backing during execution. It owns no extra SD mount/client. Guest teardown
releases mounts/references before provider close and system transition.

The portable texture decoder converts bounded base-level PICA tiles to explicit
RGBA bytes without native guest-word loads. It preflights compressed blocks,
lengths and aliases before publication. The Python/reference tools and emulated
PPC checks establish decoder behavior, not GX2 upload/sampling or draw execution.
See `romfs-assets.md` and `renderer-status.md` for the supported contracts.
