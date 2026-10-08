# Architecture and implementation boundaries

The attached development brief is the project requirements. The repository was
empty at the start of this implementation. See `foundation-audit.md` for pinned
candidate source, measured builds, and licensing/PowerPC constraints.

The host CLI is Python 3.12 with no third-party Python dependencies, and uses the
same argument-list subprocess interface on Windows and Linux. Container parsing,
inspection, build reports, and package inspection are separate from execution.
Executable artifacts are the original native diagnostic and controlled guest RPXs.
An RPX is not an installable WUP directory.

The portable C core stores guest addresses as 32-bit integers and accesses bytes
through checked mappings. Guest memory is explicitly little-endian on every
host. The initial interpreter and direct generated-C backend are bounded CPU
subsets with explicit unsupported exits. `host/loader.py` extracts inspected NCCH
segments; the portable loader allocates checked permissions/stack/TLS/BSS. A
single-guest runtime implements bounded SVC sleep/events/handles/ticks/debug/exit.
The original ARM fixture writes both framebuffers and responds to input through
an explicit harness contract. Neither implements complete ARM11/VFP, a full
3DS kernel, IPC or game HLE. The pinned external lifter now generates C through
an original host driver. An original ABI 4 implementation statically executes
that code using checked little-endian callbacks and per-instruction state;
see `upstream-integration.md`. Its desktop runtime is not a Wii U dependency.

The native WUT backend provides independent TV and GamePad diagnostic output,
input, sound, process lifecycle, timing, and CPU self-test instrumentation.
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
is explicitly not an installable package; encrypted construction is pending.

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
