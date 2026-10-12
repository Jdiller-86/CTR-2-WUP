# CTR2WUP documentation

CTR2WUP is developing an UWUVCI-style Windows GUI that turns an owned 3DS game
into an individual playable Wii U WUP. Current releases contain original
homebrew diagnostics and developer tooling. **Commercial games and the injector
GUI are not supported yet.**

## Start here

- [Getting started](getting-started.md): downloads, Aroma launch, SD layout and reports.
- [Troubleshooting](troubleshooting.md): missing reports, storage failures and hangs.
- [Testing](testing.md): which checks establish desktop, Windows and console evidence.
- [Current status](../STATUS.md) and [progress assessment](progress.md).
- [GitHub development](github-development.md): CI, releases, issues, agents and permissions.
- [Contributing](../CONTRIBUTING.md) and [security reports](../SECURITY.md).

## Build and development

- [Build on Windows](build-windows.md) and [environment/toolchain audit](foundation-audit.md).
- [Architecture](architecture.md), [product plan](development-plan.md) and [GUI workflow](gui-workflow.md).
- [Milestone acceptance](milestones.md) and [remaining inputs](current-roadblock.md).
- [CPU/AOT integration](upstream-integration.md), [compiled homebrew](compiled-homebrew.md),
  [threads](guest-threads.md), [memory](guest-memory.md) and [kernel objects](kernel-objects.md).

## Runtime and preparation contracts

- [Renderer status](renderer-status.md), [GX transfers](gx-transfers.md),
  [framebuffer display](gsp-display.md), [shared GSP/VBlank](gsp-queues.md) and [input](hid-input.md).
- [Filesystem/saves](filesystem.md), [RomFS assets](romfs-assets.md) and [application lifecycle](compiled-homebrew.md).
- [Local encrypted input](encrypted-input.md), [decryptor investigation](batch-decryptor-audit.md),
  [CIA intake](cia-intake.md) and [title staging/packaging](packaging.md).

## Evidence and history

[Compatibility](compatibility.md) describes supported boundaries.
[Console evidence](console-evidence.md) separates supplied logs from verbal
reports. The [pending save/result round](console-retest.md) remains open.
[Development history](development-history.md) preserves earlier implementation
notes and receipts; its old pending statements are historical.

No desktop pass, CI badge or RPX build establishes commercial gameplay, hardware
performance or successful WUP installation. Keep ROMs, keys, saves, game traces
and extracted commercial assets private.
