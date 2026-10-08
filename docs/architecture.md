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
still invalidate them. GPU/DSP physical memory and shared-memory remain pending.
The original ARM fixture writes both framebuffers and responds to input through
an explicit harness contract. Neither implements complete ARM11/VFP, a full
3DS kernel, general IPC or game HLE. The pinned external lifter now generates C through
an original host driver. An original ABI 4 implementation statically executes
that code using checked little-endian callbacks and per-instruction state;
see `upstream-integration.md`. Its desktop runtime is not a Wii U dependency.

The native WUT backend provides independent TV and GamePad diagnostic output,
input, sound, process lifecycle, timing, and CPU self-test instrumentation.
All diagnostic builds also contain the original TLS/SRV/CFG startup self-test
with a visible result and SD log. Unsupported cache operations fail visibly
instead of inheriting the external emitter's no-op behavior.
All builds also embed the original ARM heap lifecycle self-test and its SD log.
Its guest mode presents completed ARM-written raw framebuffers through the same
GX2 copying path. It is separate from PICA/GSP emulation; see `guest-fixture.md`.
Compilation proves toolchain integration. Only physical console results can
prove rendering, input, sound, guest semantics on PowerPC, or performance.

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
3. The controlled fixture now has real checked loading and a single-guest SVC
   runtime. Console stability and conventional homebrew boot using required CPU,
   IPC/HID/GSP/shared-memory interfaces are still needed.
4. PICA rendering, required services/modules, filesystem/save/audio semantics,
   and exact-ROM revision profiles must precede a commercial conversion.
5. License-cleared packing plus locally configured keys and actual install/menu
   launch tests must precede a WUP conversion claim.
6. The Windows GUI follows a demonstrated execution pipeline; it is deferred.
