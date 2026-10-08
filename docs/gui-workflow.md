# Windows GUI and injection workflow

Final product specification, finalized 2026-10-07. The user explicitly selected
UWUVCI AIO as the workflow reference. The shipped product is a Windows GUI named
**CTR2WUP.exe**. The CLI is the internal/testable build engine; the standard user
flow runs entirely through the GUI.

## Reviewed reference

Reviewed [UWUVCI AIO WPF](https://github.com/stuff-by-3-random-dudes/UWUVCI-AIO-WPF)
at commit `d03b559bbac53642506557519108e5967d5fbd25`. The source shows:

- System navigation in `UI/Windows/MenuWindow.xaml`.
- Three groups in `UI/Frames/InjectFrame.xaml`: BaseRom, Configuration, Packing;
  packing exposes a “WUP Installable” button after injection.
- ROM selection, game name, icon/TV/GamePad artwork, image creation and an
  “Inject” action in `UI/Frames/InjectFrames/Configurations/OtherConfigs.xaml`.
- Configuration import/export, output-path settings, asynchronous injection and
  packing, progress and completion UI.

This was a source/workflow review, not a Windows execution test. Its README lists
DS/GBA/N64/SNES/NES/TurboGrafx/MSX/Wii/GCN, without 3DS support. Its source is
GPLv3; no code, artwork or binary was imported into CTR2WUP. It supplies the
interaction reference. CTR2WUP supplies the new 3DS AOT/HLE/GX2 runtime and uses
its own original Wii U title template instead of a commercial VC donor base.

## Main user flow

1. **Open CTR2WUP → 3DS Inject.** The injector page is the default destination;
   navigation also provides saved projects, settings and help.
2. **Choose the game.** Browse for a decrypted `.3ds`/`.cxi`. Display detected
   title, icon, region/revision and supported/experimental/unsupported status.
   Provide an actionable reason when the input cannot be injected. Automatically
   select the supported compatibility preset for the exact revision.
3. **Configure the injection.** Edit the Wii U Menu title; preview or choose icon,
   TV boot and GamePad boot artwork. Use original default artwork when needed.
   Default to top screen on TV, bottom screen/touch on GamePad, native resolution
   and aspect-preserving scaling. Offer only implemented display options.
4. **Choose WUP output.** Select a destination and review readiness. The app
   detects/provisions documented prerequisites through a first-run setup flow.
   Routine injection requires no manual compiler commands or shell session.
5. **Click Inject.** One primary action runs game preparation, translation,
   compilation, runtime/assets staging, metadata creation, WUP packing and
   validation. Display the actual stage, elapsed time, progress and Cancel.
   Detailed logs and technical settings live in expandable diagnostics/settings.
6. **Receive the finished WUP.** Show the completed package directory, title
   information, build report and installation guide. Offer Open output folder
   and an explicit Copy to SD action if implemented. Publish success only after
   the real package pipeline passes its checks.
7. **Install and launch on Wii U.** The user installs with the documented
   compatible homebrew installer/environment and launches the new per-game title
   from the Wii U Menu. The Windows app creates the WUP; installation remains a
   console step with recorded test evidence.

The visible term **Inject** includes all of CTR2WUP's conversion work. A user
selecting a supported game should receive its installable per-game WUP through
that action. RPX diagnostics, generated C and intermediate staging are development
artifacts, distinct from the completed user output.

## State and persistence

The injector follows: Choose game → Configure → Ready → Injecting → Complete,
with actionable Failed/Cancelled states. Unsupported games remain visibly
unsupported; experimental status requires an actual available conversion path.
Readiness includes the input/profile, toolchain, output space and required local
packaging configuration. Progress and cancellation come from the shared engine.

Save/import/export an injection project containing selected input identity,
settings, artwork paths and destination, keeping user-derived data private.
Rebuild uses the same per-game identity and preserves existing saves. Auto-assigned
title identities are stable and collision-checked; advanced overrides are
validated. Installation instructions identify the supported console environment.

Implement the GUI in C# WPF on a supported .NET LTS release. Its engine interface
uses versioned requests, stage events and reports. Document/provision all real
Windows dependencies, and keep keys in local settings with redacted diagnostics.

## GUI acceptance

A clean Windows machine can open CTR2WUP.exe, select the supported commercial
revision, configure title/artwork/output, click Inject, cancel/retry when needed,
and obtain a validated WUP without undocumented tools or hand-editing files.
That WUP installs and launches from the Wii U Menu and passes the agreed gameplay,
input/touch/audio/save/reload/rebuild test. The app stays responsive during the
entire conversion. Its completion message distinguishes validated package output
from previously recorded physical-console compatibility evidence.
