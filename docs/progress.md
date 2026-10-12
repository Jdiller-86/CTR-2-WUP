# Project completion assessment

Updated **October 11, 2026**, against runtime version **0.14.5** and the
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
| Homebrew execution | Six original normal-CRT/libctru applications, checked input and exact CPU-written frames; filesystem save/reload and standalone lifecycle | Desktop/emulated PPC evidence; older console log records compiled guest PASS, newer A-button freeze unresolved |
| Packaging | Original encrypted writer and independent decrypt/hash/FST/file verification, including real diagnostic RPX payloads | Synthetic credentials; no installer acceptance or retained installable package |
| Wii U platform | Five diagnostic variants; native guided picture/input/touch/audio/stability checks passed on console | Native Minus exit now passes; guarded A was delayed once and froze on relaunch; save and installation untested |

The completed v0.14 regression report records **155 Python tests and 38 passing
workflow checks**. Normal and sanitized runs cover six compiled homebrew
applications and fourteen external probes. Independent Clang and emulated
big-endian PowerPC tests include exact save/reload snapshots. These are supporting
correctness checks, not a measurement of product completion or a console result.

Evidence paths, toolchain pins, and the separately consolidated fresh-artifact
report are recorded in [STATUS.md](../STATUS.md). That document also distinguishes
the completed regression run from a retry whose outer exit was not observed.
Reports and generated builds are local ignored artifacts, not published releases.
The v0.14 source bundle remains a verified snapshot from before this documentation
update; the repository carries the latest README and assessment.

## Largest unfinished work

- **PICA-to-GX2 renderer:** actual game draws, shaders, textures, transfers, and
  coherency. Displaying CPU-written pixels does not implement this renderer.
- **Commercial runtime integration:** RomFS/assets, required services/modules,
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

The native guide and later Minus exit now have user-provided console evidence.
The guarded app displayed green late once, then froze on a second launch. Its
stability remains unaccepted. The first report is reviewed and reaches shutdown;
the user confirms that the frozen second launch left no file. v0.14.5 adds file
sync/close and mounted-volume sync with visible failure/guest pause. One short
guarded retest is requested; physical recovery and freeze resolution are open.
v0.14.4 improves scaling and waiting/latency reporting, and rejects an AOT
no-progress loop in local tests; the hard freeze's cause is still unknown.
The [follow-up instructions](console-retest.md) separate delay from stability.

Actual WUP installation also needs the documented console environment and local
packaging prerequisites. Native Windows tests remain pending.

Missing inputs block those acceptance checks and title-specific decisions.
Further reusable runtime work remains possible; it does not substitute for the
missing evidence. Reassess progress after title boot/rendering, physical diagnostic
tests, successful installation, and GUI-driven conversion provide new evidence.
The next steps are maintained in [current-roadblock.md](current-roadblock.md).
