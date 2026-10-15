# CTR2WUP

[![CI](https://github.com/Jdiller-86/CTR-2-WUP/actions/workflows/ci.yml/badge.svg)](https://github.com/Jdiller-86/CTR-2-WUP/actions/workflows/ci.yml)

[Documentation](https://jdiller-86.github.io/CTR-2-WUP/) · [Releases](https://github.com/Jdiller-86/CTR-2-WUP/releases) · [Project](https://github.com/users/Jdiller-86/projects/2) · [Contributing](CONTRIBUTING.md)

First visit? [Take the repository tour](docs/repository-guide.md).

Testing on Wii U? The [focused 0.24.1 hardware round](docs/console-retest.md)
contains two guided apps and a Windows collector for returning one log ZIP.
Its [reviewed hardware results](docs/console-evidence.md) pass two arithmetic
launches and the first draw/exit; Plus restart is later user-confirmed PASS.
Updated repeat logs and performance work remain open.
It is a tested PR snapshot awaiting merge review.

Contributors can [develop and manage tracking without hosted Actions](docs/direct-development.md)
using secure API access and recorded local validation.

Source is public. Documentation and the live aggregate dashboard share
this repository's Pages site; [current status](STATUS.md)
records verified deployment, tracking and artifact evidence.

CTR2WUP is an experimental 3DS-to-Wii U converter. Its goal is a Windows GUI
that translates a 3DS game into an individual WUP title, with the top screen
on the TV and the bottom screen, touch and controls on the GamePad.

**Early development: commercial games, playable-game WUP conversion and the
Windows GUI are not available yet.** Current releases provide developer tools
and original homebrew diagnostics. Super Mario 3D Land is the first planned
compatibility target. Its private USA executable and selected asset bytes have
matching verified provenance. M5 analysis is active; shader/container/audio
formats are identified, while runtime requirements and gameplay support remain
unfinished.

## Approach

The PC translates ARM game code ahead of time into C, which is compiled for
Wii U PowerPC. A shared native runtime supplies the required 3DS services,
graphics, input, audio and saves. The intended workflow is:

**Select game → Configure title and artwork → Inject → Install WUP → Play**

Support will expand as the shared runtime implements more game requirements.
See [architecture](docs/architecture.md) and [compatibility](docs/compatibility.md)
for the supported subsets and current limits.

## Current capabilities

- Bounded 3DS/CXI/CIA inspection, optional local decryption with supplied keys,
  RomFS integrity checks and private asset preparation, with a
  [standalone checked sample exporter](docs/private-asset-samples.md).
- ARM/Thumb/VFP translation foundations, checked memory, guest threads and
  scoped 3DS services.
- Original compiled homebrew with two-screen framebuffer output, input,
  SD save/reload and read-only assets, including reviewed Wii U test results.
- Texture decoding, GX memory/display transfers, PICA configuration and first
  original [software triangle scenes](docs/software-scenes.md), now driven by a
  [compiled guest through standard GSP](docs/guest-pica-draws.md). Broader PICA
  state, GPU shader translation and 3DS guest audio remain unfinished.
- Merged [512-word vertex code capacity](docs/software-scenes.md#vertex-code-capacity)
  has original upload/entry/rollback probes and passing required hosted platform
  checks in PR #98; broader shader semantics remain open.
- Exact component-wise ADD/MUL software shaders use checked integer arithmetic,
  independent rational references and transactional rejection of rounding or
  unsupported state. See [the arithmetic boundary](docs/software-scenes.md#exact-add-and-mul).
- Reproducible RPX/WUHB diagnostics and original packaging fixtures. Actual
  WUP installation and full Windows conversion still require acceptance.

## Progress

[![Project progress](https://jdiller-86.github.io/CTR-2-WUP/progress/gates.svg)](https://jdiller-86.github.io/CTR-2-WUP/progress/)

The chart is generated from live Project fields and refreshed hourly. It shows
task status separately from full milestone acceptance. Click it for the
interactive dashboard and update time; GitHub may cache the embedded image.

[Development Project](https://github.com/users/Jdiller-86/projects/2) tracks the
seven milestones and their implementation/evidence tasks. Completing a task
does not accept a full milestone. See [the roadmap](docs/milestones.md) and
[current status](STATUS.md) for scope and verification details.

[Development tracking](docs/development-tracking.md) explains the native
milestone → task → feature → PR relationships and the contributor workflow.

## Getting started

Download a matching [development release](https://github.com/Jdiller-86/CTR-2-WUP/releases)
and check its `SHA256SUMS.txt` and source manifest. Separate Native/GX,
Audio Check/GX and Guest Draws kits accompany source, the developer wheel and
offline documentation. For Wii U diagnostics, copy the kit's `sd-card/`
contents to your SD root and launch the WUHB in Aroma. Follow its prompts;
plain-text results are saved to `sd:/ctr2wup/`. Original diagnostics need no ROM
or key. See [getting started](docs/getting-started.md) and [console testing](docs/console-testing.md).

For development, use Python 3.12+, GNU Make 4.2+ and a C compiler. Host checks run without the SDK. Its first setup
also requires Docker. From the repository root:

```sh
python3 -m venv .venv
. .venv/bin/activate
python3 -m pip install -e '.[packaging,decryption]'
make help
python3 -m host doctor
make check
bash tools/install-wiiu-sdk.sh
python3 -m host build --target prototype --output "artifacts/my-diagnostic"
```

This builds an original RPX/WUHB diagnostic. [Build instructions](docs/README.md#build-and-development),
[Windows notes](docs/build-windows.md) and [compiled homebrew](docs/compiled-homebrew.md)
cover other workflows. Generated files stay under ignored `artifacts/`; local
ROMs, keys and derived game data belong under ignored `private/`.

## Contributing

Focused fixes, original reference fixtures, documentation and reproducible
console reports are welcome. Start with [Contributing](CONTRIBUTING.md),
[open work items](https://github.com/Jdiller-86/CTR-2-WUP/issues?q=is%3Aissue+is%3Aopen+label%3Awork-item)
and the [Project](https://github.com/users/Jdiller-86/projects/2). Report bugs with
build identity and relevant test results; keep game data and keys out of issues.
Security reports follow [SECURITY.md](SECURITY.md).

## License and acknowledgments

CTR2WUP source is [MIT licensed](LICENSE). Dependencies retain their own licenses;
see [THIRD_PARTY_NOTICES](THIRD_PARTY_NOTICES) and the [dependency audit](docs/foundation-audit.md).
Builds use [devkitPro](https://devkitpro.org), [WUT](https://github.com/devkitPro/wut)
and [libctru](https://github.com/devkitPro/libctru). The planned injector workflow
is inspired by [UWUVCI AIO](https://github.com/stuff-by-3-random-dudes/UWUVCI-AIO-WPF).
