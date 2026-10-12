# Project completion assessment

Updated **October 11, 2026**, against runtime version **0.14.6** and the
[product plan](development-plan.md).

## Estimated implementation: approximately 20%

The completion target is a Windows GUI that converts one verified commercial
3DS revision into a WUP, followed by successful Wii U installation, Menu launch,
interactive gameplay, audio, and save/reload. Clean Windows operation, rebuild/save
continuity, stability, and measured useful performance are part of acceptance.
Broad game compatibility is a later goal.

**About 20%** is a rough engineering judgment about implementation toward that
target. It is not calculated from tests, commits, version numbers, or subsystem
counts. The remaining work includes large, uncertain areas, so this estimate
does not predict a release date or mean that 80% of the time remains.

The earlier estimate was roughly 15% at v0.12. Subsequent work added normal
compiler-built homebrew execution, guest thread/context support, standard input
and CPU-framebuffer applications, private filesystem save/reload, and standalone
application lifecycle support. These advance a reusable runtime needed by the
injector. They do not yet demonstrate commercial game execution.

**Full milestone acceptance: 0 of 7.** This separate measure tracks complete
requirements and required hardware evidence. Partial implementation can exist
while every full acceptance checkbox remains open. See the
[milestone checklist](milestones.md) for each verified portion and missing gate.

## What supports the estimate

| Area | Verified implementation | Limits for the finished product |
|---|---|---|
| Host preparation | Decrypted container inspection, executable loading, private intake, external code generation, static builds, original metadata/artwork staging | No complete commercial conversion or clean Windows execution |
| CPU and shared runtime | Bounded ARM/Thumb/VFP support, checked AOT boundary, memory, eight single-core guest thread contexts, scoped services | Incomplete CPU/HLE coverage; actual game and Wii U timing requirements unknown |
| Homebrew execution | Seven original normal-CRT/libctru applications, input/exact CPU-written frames, save/reload, standalone lifecycle and read-only RomFS assets | Desktop/sanitizer/emulated PPC evidence; v0.14.5 interactive/guarded completion user-reported, new file/save/asset acceptance pending |
| Packaging | Original encrypted writer and independent decrypt/hash/FST/file verification, including real diagnostic RPX payloads | Synthetic credentials; no installer acceptance or retained installable package |
| Wii U platform | Current five-app kit; older native guide/exit evidence and user-reported v0.14.5 Native/Guarded/Compiled success | Missing new traces; visit-1 snapshot received, later save step failed; v0.14.6 reports/saves/assets and installation pending |

v0.14.6 passes **155 Python tests** and fourteen C suites normally and with
ASan/UBSan, including 1,892 FS and 574 report/guide assertions in each mode.
Fourteen external AOT probes pass 28 compile/execute checks; 944 independent
ARM1176 cases verify the scoped CPU additions. The new RomFS program passes
normally, under sanitizers and on emulated big-endian PowerPC with zero fallback.
The compiled save/interactive regressions and guarded/live-input replays pass.
These are supporting checks, not a measurement of product completion.

Current reports are under `artifacts/reports/console-results-v0146/`; the
five-app kit and matching source are under
`artifacts/delivery/console-results-0.14.6/`. Private uploaded logs/snapshots are
excluded. [STATUS.md](../STATUS.md) records validation and the remaining gates.
These local artifacts are not a published playable-game release.

## Largest unfinished work

- **PICA-to-GX2 renderer:** actual game draws, shaders, textures, transfers, and
  coherency. Displaying CPU-written pixels does not implement this renderer.
- **Commercial runtime integration:** NCCH/IVFC and retail assets, required services/modules,
  guest DSP/audio, retail saves, and any CPU behavior exercised by the first game.
- **Selected-title proof:** exact Mario revision analysis, boot, menu, gameplay,
  audio, save/reload, stability, and useful Wii U performance.
- **Finished injection engine:** commercial end-to-end orchestration, caching,
  cancellation/rebuild behavior, native Windows execution, and real WUP installation.
- **Windows GUI:** the specified UWUVCI-style workflow is designed but not
  implemented. Its Inject action will use the proven conversion engine.

## Evidence and inputs still needed

The supplied Mario CIA has not been inspected: it exceeds the attachment tool's
32 MiB transfer limit. The [CIA intake helper](cia-intake.md) can export a small
private executable view for revision and code analysis when the user's computer
is available. An encrypted executable needs decrypted data. That export excludes
game assets; commercial asset integration will require access to those separately.

The native guide and later Minus exit have user-provided console evidence.
The user now reports Native/Guarded/Compiled completed without errors on 0.14.5.
No new logs were saved; supplied logs have older build headers. Save failed,
though a valid two-record visit-1 snapshot was received. v0.14.6 supplies an
independent named TXT result with closed-file read-back and portable save
publication/rollback/recovery. The exact prior console failure causes remain
unconfirmed. The [follow-up](console-retest.md) requests Save, then a quick
original RomFS test, with no long native/input repetition.

Actual WUP installation also needs the documented console environment and local
packaging prerequisites. Native Windows tests remain pending.

Missing inputs block those acceptance checks and title-specific decisions.
Further reusable runtime work remains possible; it does not substitute for the
missing evidence. Reassess progress after title boot/rendering, physical diagnostic
tests, successful installation, and GUI-driven conversion provide new evidence.
The next steps are maintained in [current-roadblock.md](current-roadblock.md).
