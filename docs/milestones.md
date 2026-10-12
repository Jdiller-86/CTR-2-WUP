# Milestone checklist

Acceptance remains tied to the complete requirements in `project-brief.md`.
Desktop passes and cross-compilation never check a physical-console box.

The execution order is now defined in `development-plan.md`: integrate the
audited AOT lifter and resolve installable WUP packaging early, select one exact
Old 3DS revision, implement its required Wii U runtime/PICA features, automate
Windows ROM-to-WUP conversion, then deliver the UWUVCI-style Windows GUI injector
specified in `gui-workflow.md`. The CLI is its shared engine; the MVP is completed
through the GUI's Inject-to-WUP workflow. Existing diagnostics
and the custom framebuffer guest are regression tools within that product path.
All acceptance checkboxes below remain open until their evidence is recorded.

- [ ] M1 Native Wii U platform: build diagnostic RPX; physical dual-screen,
  buttons/sticks/touch/audio/exit test; independently investigate WUP.
- [ ] M2 CPU correctness/benchmarks: tested bounded portable subset; complete
  instruction/exception/VFP/exclusive semantics and actual PowerPC timings.
- [ ] M3 3DS fixture: real executable loader, scheduler/kernel/IPC/HLE, guest
  framebuffers, input, and stability run on Wii U.
- [ ] M4 PICA-to-GX2: staged state/shader/texture/TEV/coherency implementation,
  reference comparisons and measured console rendering.
- [ ] M5 One commercial Old 3DS revision: supplied decrypted dump, required
  services/modules/audio/saves, boot/gameplay/save/reload/stability evidence.
- [ ] M6 ROM-to-WUP CLI: true end-to-end translation/runtime/packing, safe title
  identities, Windows clean-machine test, install/menu/rebuild/uninstall checks.
- [ ] M7 Windows GUI: proven same execution pipeline, usable responsive front
  end and documented packaged prerequisites.

## Current evidence (October 12, 2026, America/New_York)

The current rough implementation estimate is **approximately 20% of the finished
GUI-to-playable-WUP product**; full milestone acceptance is **0/7**. These measure
different things. See [progress.md](progress.md) for the assessment and evidence.

| Milestone | Verified portion | Remaining acceptance |
|---|---|---|
| M1 | RPX/WUHB built; older native report records ten guide/nine self-test passes; Minus exit confirmed; Native/Guarded/Compiled completion reported for v0.14.5 | New result-file/save console evidence, full lifecycle acceptance and installed WUP |
| M2 | Original CPU/AOT subset plus stack/register transfers, BLX, user CP15 TLS, byte media, barriers and scoped FP; external generation/checked ABI; GCC/sanitizers/Clang/emulated big-endian PPC checks; 944 independent ARM1176 cases (U=1/A=0 single-data semantics and defined base-in-list stores) and nested-call trace | Complete ARM/VFP semantics, workload memory/fallback costs and actual Espresso timings |
| M3 | Checked NCCH loading and eight single-core contexts; CPU/TLS/VFP/FPSCR ownership, priorities/quanta, joins/locks/arbiters/quotas; SRV/CFG/heap/shared/HID/GSP/VBlank; seven normal-CRT/libctru applications including exact CPU framebuffer pixels, private FS save/reload, APT lifecycle and read-only Level-3 RomFS assets; five current diagnostic variants built | Verified new result-file/save/RomFS console evidence; broader services, applets, NCCH/IVFC and commercial assets/retail saves, cross-process semantics, motion, GX commands/PICA and guest audio |
| M4 | Bounded command references, standard direct/shared GSP display and five tested rotated CPU display formats | Actual PICA-to-GX2 features, scene comparisons and console timings |
| M5 | Mario selected; original attachment exceeds 32 MiB; October 11 small intake receipt reports outer-encrypted executable content and contains no executable; intake/export helper tested; shared runtime foundations | Decrypted private executable export/revision, title-specific runtime/renderer/services/save/audio implementation and console gameplay |
| M6 | Local key/seed import and CIA/NCCH preparation, original encrypted-input→intake→AOT→RPX/WUHB proof; external C generation/static compilation, staging/artwork/intake, original encrypted writer and independent decrypt/FST/file checks with synthetic inputs, atomic output/wheel; Windows workflow prepared | Actual local certificate/key provenance and installer acceptance, full commercial conversion, caches/traces/rebuild, Windows execution and console install checks |
| M7 | Deliberately deferred | Proven pipeline before responsive Windows GUI implementation |

These are partial implementation outcomes. None of the full milestone checkboxes
is satisfied by source/build/test artifacts alone.

v0.14.6 records the user's latest **Native/Guarded/Compiled v0.14.5 completion
without errors**, separately from the supplied older text logs. Missing new TXT
reports remain unexplained. The valid visit-1 snapshot proves initial record
persistence, not the failed update/reload. Independent read-back-verified TXT
results and portable backup/publication/rollback now have desktop/sanitizer
checks; a quick Save then RomFS console round is requested. The new original
RomFS application verifies root/nested/Unicode reads, directory listing and
write denial, including emulated big-endian PPC, without interpreter fallback.
These advance M2/M3 foundations; no full milestone is closed.
See [console-retest.md](console-retest.md) and [romfs-assets.md](romfs-assets.md).

v0.14.1 added controller-driven guided native/compiled/save tests and checked
per-launch TXT evidence, with upload guidance. Automatic observations, user
confirmations, failures and skips are separate. Its native partial results are
recorded above; this test-collection improvement closes no milestone. See `guided-testing.md`.

Version 0.3.0 connects the audited lifter to the runtime; `upstream-integration.md`
records its checks and limits. Version 0.4.0 adds scoped encrypted packing and a
separate decrypting verifier; all offline test credentials are synthetic.
Version 0.5.0 adds CIA intake/export (16 Python cases) and a self-contained Windows
console helper (13 Linux .NET cases; native Windows unverified). The attached
commercial CIA is not inspected because of the 32 MiB transfer limit.
Version 0.6.0 adds the ROM-independent TLS/SRV/CFG startup foundation and original
ARM probe/self-test, with documented supported commands and visible unsupported
states (`shared-services.md`). This is partial shared-runtime implementation;
no commercial title profile or console milestone is enabled.
Version 0.7.0 advances M3/P3 with owned heap/linear CPU allocation, atomic free/
protection and region queries (`guest-memory.md`). Its original ARM lifecycle
self-test is embedded in all three Wii U builds; physical behavior remains
pending. No commercial profile, GUI or full milestone is enabled.
v0.8.0 adds checked shared-memory/synchronization and its embedded original
148-instruction ARM self-test (`kernel-objects.md`), with single-thread scope.
Validation passes 139 Python tests, 51,175 CPU
assertions, 221 original-guest assertions, 1,001 service assertions and 307 heap
assertions plus 197 kernel assertions in each normal/sanitized C run, 566+34+297+35+1,778+639 external integration
assertions and 29 complete workflow checks, including
real-RPX offline packaging and installed-wheel packing.
Physical-console and clean Windows acceptance remain pending.
Clang and emulated PowerPC also pass the shared/sync probe/API suite; each v0.8
compiler/target report records 14 checks. Native Wii U service/memory/synchronization behavior/timings
are pending even though the original self-test is embedded in all diagnostic builds.

## Deferred features

New 3DS exclusives, stereo TV, online services, cameras, microphone, DLC/update
merging, save import/export, and arbitrary-ROM compatibility are unsupported.
Titles requiring any of these need separate evidence and implementation.

Version 0.9.0 adds shared standard HID pad/touch input and an original ARM
consumer, including live VPAD-to-ARM diagnostics in all three Wii U builds.
The complete runner passes 141 Python tests and 30 checks; HID API has 336
normal/sanitized assertions, and external HID comparison has 858 assertions
(185 instructions/18 SVCs). Clang and emulated PPC reports have 16 checks each.
Motion, real-console input/timing, standard graphics boot and all full milestone
acceptance remain pending. Continue into standard GSP framebuffer presentation.

Version 0.10.0 connects standard HID input to direct GSP framebuffer display in
an original ARM guest, embeds its original AOT/self-test in all Wii U builds and
checks all five rotated display formats. Validation has 144 Python tests, 31
complete workflow checks, 51,202 CPU assertions, 362 GSP API assertions and 3,603
external display assertions with four reference PPMs. Compiler/target reports
record 18 checks each. None closes a physical-console or commercial milestone.
The next planned gate is actual Wii U display/input/stability and private Mario
revision intake; `current-roadblock.md` records the missing evidence and resume
steps. Wider graphics queues/kernel/PICA/audio/save work remains incomplete.

Version 0.11.0 adds one local shared GSP framebuffer queue, retained relay event,
deterministic VBlank0/1 IRQ delivery, ring overflow/wrap and atomic two-screen
preflight. An original ARM queued guest uses exclusives and matches interpreter/
original/external AOT including forced retry. It is embedded in all Wii U builds.
This advances the common M3/P3 runtime; it closes no physical milestone. Further
shared runtime coding remains possible while console/intake evidence is pending.
Validation for v0.11 passes 147 Python tests, 32 full workflow checks and
762 relay API assertions normally/sanitized. The external relay comparison has
11,541 assertions and full three-backend state/pixel parity. Clang/emulated PPC
reports contain 20 checks each. Required `make check` and `make prototype` pass;
physical Wii U, clean Windows, commercial gameplay and installation remain open.

Version 0.12.0 advances M2/M3/P3 with user-mode ARM multiple-register transfers,
Thumb16 stack/register transfers and bounded original-AOT register BLX calls.
The original nested-call fixture restores its stack and matches three backends
in 47 instructions/one SVC; six partial-fault and six invalid-Thumb cases are
checked externally. Independent Unicorn ARM1176 checks cover 220 instruction
cases and the full original call trace. All Wii U builds embed its self-test.
No full acceptance milestone is closed by this implementation increment.
Validation for v0.12 passes 150 Python tests, 33 complete workflow checks,
160 stack/transfer API assertions normally/sanitized, and all eleven external
probes. Required `make check` and `make prototype` pass. Compiler/target checks
and independent reference evidence are recorded in STATUS.md; physical-console,
clean Windows, commercial gameplay and WUP installation remain open.

Version 0.13.0 advances the common M2/M3/P3 foundation through original C
applications compiled with normal devkitARM CRT/newlib/libctru. Eight single-core
threads retain CPU/TLS/VFP/FPSCR, stack and wait ownership; locks/arbiters/quota
queries, correct BSS placement and explicit synthetic kernel policy allow boot.
Real compiled threads/C TLS/LightLock, standard HID transitions/coordinates and
three exact two-screen RGB565 graphics frames run without interpreter fallback.
The fourth interactive RPX/WUHB consumes live input and its own B-button exit.
APT/FS and optional startup lookups remain visibly unavailable. CPU byte media,
FPSCR/scalar FP word transport, CP15 ARMv6 barriers, ARM PC stores and SMLAxy have
870 independent ARM1176 cases plus the original nested-call trace. These results
do not close a physical, commercial, installation or GUI milestone.

v0.13 validation passes 155 Python tests and 36 complete workflow checks,
including all 14 external probes and five normal compiled libctru applications
normally/sanitized. Thread API has 310 normal/sanitized assertions; coherent CPU
GSP flush/invalidate brings its API to 508 assertions. Real compiled threads
pass Clang; the interactive program's exact frames/input/exit match under
emulated big-endian PPC Linux. `make check` and `make prototype` pass. These
are software/cross-build results; physical Wii U and native Windows remain open.

Version 0.14.0 continues M3/P3 through normal compiled savedata I/O and
standalone APT lifecycle. Explicit private archives/files have checked ownership,
rights, whole-buffer preflight and CRC-checked snapshot commits. The original
program proves fresh-process reload and exact independently checked save bytes.
Normal APT event/wakeup/exit runs; privileged and optional lookup failures remain
explicit. A fifth save diagnostic RPX/WUHB awaits physical SD save/reload.
`filesystem.md` records bounds; ROMFS/retail saves/applications, PICA/audio and
all full milestone acceptance remain open.

v0.14 verification: 155 Python tests, 38 complete workflow checks, six compiled
applications normally/sanitized, 1,387 filesystem and 671 APT API assertions.
Clang/emulated PPC pass both APIs and actual save/reload with byte-identical
snapshots. Spaced Japanese console-output paths pass after controlled build
staging. The fifth save RPX also passes offline encrypted packing with temporary
synthetic inputs; no installable WUP or physical acceptance is claimed.


v0.15.0 adds the user's optional encrypted-input route, scoped local support-file
import, known-vector/original encrypted fixture checks and a real original-guest
pipeline through decryption/intake/external AOT/Wii U cross-build. This advances
P2/P4/M6 preparation, not commercial compatibility. Actual local 3DS exports and
Windows protected-key import/device-helper/GUI behavior are pending. Earlier
console successes and the latest U.S. firmware/Aroma/WUP Installer GX2 environment
are retained as user evidence; exact versions/new logs remain open. No full gate
is closed; original requirements and amendments are in `encrypted-input.md`.
