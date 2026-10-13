# First visit to the repository

CTR2WUP aims to create a playable per-game Wii U WUP through a Windows injector
GUI. Today the repository provides reusable runtime foundations, developer tools
and original homebrew diagnostics. Commercial gameplay, full WUP installation
acceptance and the final GUI remain open.

## Choose a starting point

| You want to… | Start here |
| --- | --- |
| Understand what actually works | [Current status](../STATUS.md) and [compatibility](compatibility.md) |
| See the product and next work | [Development plan](development-plan.md) and [milestones](milestones.md) |
| Follow an implementation to its PR | [Development tracking](development-tracking.md) and [Project #2](https://github.com/users/Jdiller-86/projects/2) |
| Download an original diagnostic | [Getting started](getting-started.md) |
| Build or contribute | [Contributing](../CONTRIBUTING.md), `make help` and [build notes](README.md#build-and-development) |
| Continue as a coding agent | [AGENTS.md](../AGENTS.md) |
| Supply console results | [Guided testing](guided-testing.md) and [evidence checklist](console-testing.md) |
| Work with owned game input | [Private input guide](encrypted-input.md) and [first title](first-title.md) |

## Source map

| Location | Responsibility |
| --- | --- |
| `host/` | Shared CLI, intake, build stages, package checks and diagnostic workflows |
| `translator/` | Original translation fixtures and the checked external lifter adapter |
| `runtime/core/` | Portable checked guest memory, CPU, kernel, services and software renderer |
| `runtime/recomp/` | Generated-code ABI, dispatch and observable execution boundary |
| `runtime/wiiu/` | WUT platform, GX2 display copies, VPAD/AX and original native diagnostics |
| `tests/` | Original host/C fixtures; `tests/homebrew/` contains normal-CRT ARM programs |
| `tools/` | Verified SDK/dependency setup, validators, documentation and trusted GitHub management |
| `.github/` | Required CI, publication, issue/PR templates and repository policy |
| `docs/` | Current contracts, plans, acceptance gates and recorded evidence |
| `artifacts/`, `private/` | Ignored generated output and private inputs; never application source |

`runtime/core/sources.list` is the canonical portable guest runtime manifest.
The test Makefiles, native build and Python build/validation adapters consume it;
add a shared runtime module there once. Consumer-specific fixtures and platform
helpers remain in their own recipes. Standalone component suites keep narrow lists.

## First host check

Use Python 3.12+, GNU Make 4.2+ and a C compiler. The SDK and external lifter are optional
for this first check:

```sh
python3 -m venv .venv
. .venv/bin/activate
python3 -m pip install -e '.[packaging,decryption]'
make help
python3 -m host doctor
make check
```

For an original native diagnostic, follow [the SDK setup](README.md#build-and-development),
then run `make prototype`. For a compiled original ARM program, follow
[compiled homebrew](compiled-homebrew.md); its validator requires the pinned
libctru source, devkitARM and external lifter. These checks use no commercial ROM.

## What counts as progress

The repository milestone is the gate, its task is a substantial work area, and a
scoped feature records one reviewable change. Native parent and PR links connect
them in GitHub. A merged feature can be Done while its task and milestone remain
open. Project status, checked source provenance and explicit evidence limits make
that distinction visible without relying on a chat transcript.
