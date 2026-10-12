# CTR2WUP

[![CI](https://github.com/Jdiller-86/CTR-2-WUP/actions/workflows/ci.yml/badge.svg)](https://github.com/Jdiller-86/CTR-2-WUP/actions/workflows/ci.yml)

[Documentation](docs/README.md) · [Development releases](https://github.com/Jdiller-86/CTR-2-WUP/releases) · [Roadmap issues](https://github.com/Jdiller-86/CTR-2-WUP/issues?q=is%3Aissue+label%3Amilestone) · [Contributing](CONTRIBUTING.md)

**A project to turn 3DS games into individual Wii U titles through a Windows GUI.**

The intended experience follows [UWUVCI AIO](https://github.com/stuff-by-3-random-dudes/UWUVCI-AIO-WPF):
choose a game, use a decrypted dump or import locally obtained 3DS key material,
customize its name and artwork, click **Inject**, and
receive a WUP package to install and launch from the Wii U Menu.
The TV displays the top screen; the GamePad supplies the bottom screen, touch,
and controls.

**CTR2WUP is in early development. It cannot yet convert or play commercial 3DS
games, and the Windows GUI has not been implemented.** Current builds are
developer tools and original homebrew diagnostics. **Super Mario 3D Land** is the
first planned supported game; its exact revision still needs private analysis.

## How it will work

**Select game → Configure title → Inject → Install WUP → Play on Wii U**

During injection, the PC will translate the game's ARM code into code compiled
for the Wii U's PowerPC CPU. A shared compatibility runtime will provide the
required 3DS services, graphics, input, audio, and saves. Each package will contain
the translated program, runtime, and game assets.

The first release targets one verified game revision. Support for additional
games will grow as the shared runtime gains the features they require. See the
[product plan](docs/development-plan.md) and [GUI workflow](docs/gui-workflow.md).

## Overall progress

![Estimated implementation progress: approximately 20%; zero of seven full milestones accepted.](docs/images/progress.svg)

**Assessment: October 12, 2026 · version 0.17.0.** About **20%** is a rough
engineering estimate toward the finished GUI-to-playable-WUP product, not a
measured task percentage or a compatibility rating. **0 of 7 full milestones
have met their acceptance criteria.** Most renderer, commercial-game, GUI, and
hardware validation work remains.

| Milestone | Current status | Main remaining work |
|---|---|---|
| M1 · Wii U platform | Native guide/exit have evidence; three v0.14.5 apps reported passing | Verified new result files, save behavior and WUP installation |
| M2 · CPU translation | Tested subset and AOT integration | Broader instruction support and Wii U performance |
| M3 · Homebrew runtime | Eight original applications; saves, RomFS assets and GX transfers | Broader services, retail integration and console acceptance |
| M4 · PICA → GX2 graphics | Textures and GX transfers; draw renderer not implemented | 3DS draws, shaders, sampling, and reference scenes |
| M5 · First commercial game | Mario selected; dump analysis pending | Boot, gameplay, guest audio, and retail saves |
| M6 · Automated WUP conversion | Local decryption, build and packaging foundations tested | Commercial end-to-end conversion, Windows, and installer acceptance |
| M7 · Windows GUI | Workflow specified | Implement and verify the complete Inject experience |

Read the [completion assessment](docs/progress.md) for the reasoning and evidence,
or the [milestone checklist](docs/milestones.md) for detailed acceptance gates.

## What works today

- **Game preparation:** bounded 3DS/CXI/CIA inspection plus optional local
  decryption using user-supplied keys/seeds. Supported key database imports,
  recovered-code hashes and original encrypted pipeline tests are implemented.
  Full RomFS hash verification and private asset preparation are implemented.
  Console extraction and native Windows key handling still need validation.
- **Translation and runtime foundations:** ARM/Thumb/VFP subsets, external AOT
  integration, checked memory, guest threads, and scoped 3DS services.
- **Original homebrew execution:** normal compiler-built programs exercise boot,
  threads, input, CPU-written two-screen images, private save/reload, and
  standalone application startup and exit, and read-only RomFS asset access.
- **Wii U diagnostic builds:** the current five-app kit includes Native,
  Compiled AOT, Guarded AOT, Save and RomFS. The user reports the first three
  passed on v0.14.5, but found no new reports and a save failure. v0.14.6 adds
  independent read-back-verified TXT results and safer save publication;
  their console checks are pending.
- **Texture decoding:** all 14 base formats, tiled layouts and ETC1 compression
  have original fixture/reference checks. This does not yet render 3DS game scenes.
- **Standard GX operations:** original stock-libctru code exercises memory fills,
  DMA, tiled display conversion and raw copies through the graphics command queue.
  The new GX Transfers diagnostic provides a short guided console test.
- **Packaging foundations:** original metadata/artwork staging and encrypted
  packing with independent decryption and file checks using synthetic test inputs.
  Actual WUP installation remains unverified.

The v0.16.0 asset increment also builds an optional **Asset Files** diagnostic
that reads a tiny original SD blob and records guided TXT results. Its console
test is pending; see [asset preparation and testing](docs/romfs-assets.md).

CPU-written framebuffer display is implemented; **3DS PICA rendering and guest
audio are not**. Homebrew snapshot persistence does not establish commercial save
compatibility. Commercial conversion requests fail explicitly instead of producing
a placeholder package. The separate Windows CIA intake helper is a console
utility, not the planned injector GUI.

## Download and test

Use a matching [development release](https://github.com/Jdiller-86/CTR-2-WUP/releases)
and its checksums. Copy the kit’s `sd-card/` contents to SD root and launch the
named WUHB in Aroma. Guided TXT results are saved under `sd:/ctr2wup/`.
See [getting started](docs/getting-started.md) for prerequisites, launch routes
and the exact files to return. No ROM or key is needed for original diagnostics.

GitHub now tracks each milestone with an issue and acceptance checklist. CI
provides versioned build/test artifacts; [the GitHub guide](docs/github-development.md)
explains automatic checks, manual Windows validation and agent profiles.

## Try the developer tools

The following Linux/cloud workflow builds an original Wii U diagnostic and runs
an original synthetic 3DS fixture. Use the repository root with Python 3.12+,
Make, and a C compiler; the SDK installer provisions the Wii U toolchain.
The first SDK installation also requires Docker.

```sh
bash tools/install-wiiu-sdk.sh
python3 -m host doctor
make check
python3 -m host build --target prototype --output "artifacts/my diagnostic"
python3 -m host make-fixture --output "artifacts/my fixture"
python3 -m host run-fixture "artifacts/my fixture/ctr2wup-guest.cxi" --output "artifacts/my guest frames"
```

The outputs are **RPX/WUHB diagnostics and reference images**, not installable
commercial-game WUPs. Generated builds and reports are kept in ignored
`artifacts/` directories. See [fixture builds](docs/guest-fixture.md),
[compiled homebrew](docs/compiled-homebrew.md), [RomFS assets](docs/romfs-assets.md), and
[external translation setup](docs/upstream-integration.md) for further workflows.

To inspect a decrypted dump locally, run `python3 -m host inspect PATH`.
Keep ROMs and derived game data under ignored `private/` directories.
See [CIA intake](docs/cia-intake.md) for the small private executable export helper,
and [local encrypted-input preparation](docs/encrypted-input.md) for the new engine
commands. Keys stay on your PC; the app does not obtain them from a 3DS yet.

## Development and testing

- [Guided console tests and TXT uploads](docs/guided-testing.md) — v0.14.6 adds verified result files; test Save, then RomFS.
- [Console evidence review](docs/console-evidence.md) — separates older uploaded logs, reported v0.14.5 successes and the valid snapshot.
- [Current status and reports](STATUS.md) — implementation history and verification evidence.
- [Next testing and input gates](docs/current-roadblock.md) — what awaits a dump, Wii U, or Windows test.
- [Console testing](docs/console-testing.md) — launch and record the diagnostic results.
- [Windows build notes](docs/build-windows.md) — prerequisites and pending native Windows checks.
- [Architecture](docs/architecture.md) and [compatibility](docs/compatibility.md) — supported behavior and limits.
- [Packaging](docs/packaging.md) — local requirements and the distinction between integrity and installation.
- [Project requirements](docs/project-brief.md) — the original development brief.

No ROMs, commercial assets, keys, or proprietary SDK are bundled. Keep saves,
translated commercial code, packages, traces, and credentials private as well.
Original fixtures and linked support carry the notices recorded in
[THIRD_PARTY_NOTICES](THIRD_PARTY_NOTICES). Dependency and redistribution limits
are documented in the [foundation audit](docs/foundation-audit.md).
