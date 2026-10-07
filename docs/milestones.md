# Milestone checklist

Acceptance remains tied to the complete requirements in `project-brief.md`.
Desktop passes and cross-compilation never check a physical-console box.

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

## Current evidence (2026-10-07)

| Milestone | Verified portion | Remaining acceptance |
|---|---|---|
| M1 | Real RPX/WUHB built; native diagnostics implemented; packaging audited | Physical screens/input/audio/exit and independently installed WUP |
| M2 | Bounded original CPU/AOT subset, 51,147 desktop assertions and sanitizers; console self-test linked | Complete semantic coverage/differential VFP and actual PowerPC timings |
| M3 | Original executable NCCH → checked loader → AOT/interpreter execution; bounded single-guest SVC/events; matching ARM-written frames/input; guest RPX/WUHB built | Physical guest frames/input/exit/stability; conventional homebrew boot, multi-thread kernel and required IPC/HID/GSP services |
| M4 | Tested bounded command/format/viewport references | Actual PICA-to-GX2 features, scene comparisons and console timings |
| M5 | Exact revision/hash identity preparation | User ROM, runtime/renderer/services/save/audio implementation and console gameplay |
| M6 | Doctor/inspect/prototype-build/package-validate, atomic output and installed CLI wheel | Full conversion, licensed packing/keys, caches/traces/rebuild, clean Windows and console install checks |
| M7 | Deliberately deferred | Proven pipeline before responsive Windows GUI implementation |

These are partial implementation outcomes. None of the full milestone checkboxes
is satisfied by source/build/test artifacts alone.

Version 0.2.0 adds the guest execution foundation. `docs/guest-fixture.md` records
its contract and desktop evidence. Validation passes 88 Python tests, 51,147 CPU
assertions and 221 guest assertions in each normal/sanitized C run, and 17 total
workflow checks. Physical-console and clean Windows acceptance remain pending.

## Deferred features

New 3DS exclusives, stereo TV, online services, cameras, microphone, DLC/update
merging, save import/export, and arbitrary-ROM compatibility are unsupported.
Titles requiring any of these need separate evidence and implementation.
