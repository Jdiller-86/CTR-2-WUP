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

## Current evidence (2026-10-08 UTC)

| Milestone | Verified portion | Remaining acceptance |
|---|---|---|
| M1 | Native/original/external-AOT RPX/WUHB built; native diagnostics; original staging and offline encrypted packaging | Physical screens/input/audio/exit and independently installed WUP |
| M2 | Original CPU/AOT subset plus user CP15 TLS; external generation/checked ABI; 566+34+297+35 integration assertions on GCC/sanitizers/Clang/emulated big-endian PPC; scalar VFP Unicorn match | Complete ARM/VFP semantics, workload memory/fallback costs and actual Espresso timings |
| M3 | Checked NCCH → AOT/interpreter execution; single-guest SVC/events; shared typed sessions, bounded SRV/CFG startup, original 89-instruction three-backend probe; matching ARM frames/input; RPX/WUHB/self-tests built | Physical guest/service startup evidence; conventional homebrew boot, memory control, multi-thread kernel and broader IPC/HID/GSP services |
| M4 | Tested bounded command/format/viewport references | Actual PICA-to-GX2 features, scene comparisons and console timings |
| M5 | Mario selected and CIA attachment supplied; transfer exceeds 32 MiB; intake/export helper tested; shared startup development while user is away; upstream requirement leads | Actual smaller private executable export/revision, title-specific runtime/renderer/services/save/audio implementation and console gameplay |
| M6 | External C generation/static compilation, staging/artwork/intake, original encrypted writer and independent decrypt/FST/file checks with synthetic inputs, atomic output/wheel; Windows workflow prepared | Actual local certificate/key provenance and installer acceptance, full commercial conversion, caches/traces/rebuild, Windows execution and console install checks |
| M7 | Deliberately deferred | Proven pipeline before responsive Windows GUI implementation |

These are partial implementation outcomes. None of the full milestone checkboxes
is satisfied by source/build/test artifacts alone.

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
Validation passes 135 Python tests, 51,175 CPU
assertions, 221 original-guest assertions and 1,001 service assertions in each
normal/sanitized C run, 566+34+297+35 external integration assertions and 27 complete workflow checks, including
real-RPX offline packaging and installed-wheel packing.
Physical-console and clean Windows acceptance remain pending.
Clang and emulated PowerPC also pass the full service suite; each v0.6 final
compiler/target report records 10 checks. Native Wii U service behavior/timings
are pending even though the original self-test is embedded in all diagnostic builds.

## Deferred features

New 3DS exclusives, stereo TV, online services, cameras, microphone, DLC/update
merging, save import/export, and arbitrary-ROM compatibility are unsupported.
Titles requiring any of these need separate evidence and implementation.
