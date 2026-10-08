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
| M2 | Original CPU/AOT subset; external generation/checked ABI; 566+34 integration assertions on GCC/sanitizers/Clang/emulated big-endian PPC; scalar VFP Unicorn match | Complete ARM/VFP semantics, workload memory/fallback costs and actual Espresso timings |
| M3 | Original executable NCCH → checked loader → AOT/interpreter execution; bounded single-guest SVC/events; matching ARM-written frames/input; guest RPX/WUHB built | Physical guest frames/input/exit/stability; conventional homebrew boot, multi-thread kernel and required IPC/HID/GSP services |
| M4 | Tested bounded command/format/viewport references | Actual PICA-to-GX2 features, scene comparisons and console timings |
| M5 | User selected Super Mario 3D Land; private hash/service intake; Mii/save-identity and GPU-readback source leads | User ROM/revision, runtime/renderer/services/save/audio implementation and console gameplay |
| M6 | External C generation/static compilation, staging/artwork/intake, original encrypted writer and independent decrypt/FST/file checks with synthetic inputs, atomic output/wheel; Windows workflow prepared | Actual local certificate/key provenance and installer acceptance, full commercial conversion, caches/traces/rebuild, Windows execution and console install checks |
| M7 | Deliberately deferred | Proven pipeline before responsive Windows GUI implementation |

These are partial implementation outcomes. None of the full milestone checkboxes
is satisfied by source/build/test artifacts alone.

Version 0.3.0 connects the audited lifter to the runtime; `upstream-integration.md`
records its checks and limits. Version 0.4.0 adds scoped encrypted packing and a
separate decrypting verifier; all offline test credentials are synthetic.
Validation passes 115 Python tests, 51,147 CPU
assertions and 221 original-guest assertions in each normal/sanitized C run,
566+34 external integration assertions and 26 complete workflow checks, including
real-RPX offline packaging and installed-wheel packing.
Physical-console and clean Windows acceptance remain pending.

## Deferred features

New 3DS exclusives, stereo TV, online services, cameras, microphone, DLC/update
merging, save import/export, and arbitrary-ROM compatibility are unsupported.
Titles requiring any of these need separate evidence and implementation.
