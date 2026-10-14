# CTR2WUP status

**October 14 public-repository consolidation:** the owner made source public.
Hosted Actions is running again; the shader-capacity PR's exact head passes
Required CI (Windows, Python 3.12/3.14, GCC/Clang sanitizers and Wii U) and
Documentation. The Actions management identity successfully reads personal
Project #2; the environment binding remains a separate access path.

Scoped [#99](https://github.com/Jdiller-86/CTR-2-WUP/issues/99) and
[PR #100](https://github.com/Jdiller-86/CTR-2-WUP/pull/100) consolidate maintained
documentation and the live dashboard at
[the canonical project site](https://jdiller-86.github.io/CTR-2-WUP/).
The trusted-main management workflow supports reviewed Mario transitions,
traceability and dashboard refresh. Canonical deployment, old-site redirects,
live reconciliation and refreshed release verification are pending until their
actual receipts are recorded. No full gate is accepted.

**October 13 physical follow-up:** Audio Check and GX Transfers 0.20.0, plus
Guest Draws 0.23.0, have reviewed scoped passes and user-confirmed silence and
normal exit. The draw trace reaches the rendered second presentation at 57.33
seconds; aggregate diagnostic timing is not game performance. Its visual prompt
accepted an answer before that scene, so accurate presentation gating and phase
profiling are the next follow-up. The full CCI exceeds the transfer
limit, but the [small private intake](docs/cia-intake.md) now verifies the USA
Mario executable and exact revision identity. Local RomFS inventory reports now
match the verified executable. All seven selected asset payloads now pass
independent hash/provenance checks, and their shader/container/audio structures
are privately reviewed. M5 analysis can proceed; source evidence and the explicit
live Project review replace the resolved input blocker. Actual runtime use,
broader modules and all full
milestones remain open. [Evidence review](docs/console-evidence.md).

Updated **October 14, 2026 (America/New_York)** · **v0.23.0**.
The target remains an UWUVCI-style Windows GUI: select an owned 3DS game, configure
title/artwork, click Inject, install its individual WUP and play from Wii U Menu.
[Requirements](docs/project-brief.md), [product plan](docs/development-plan.md),
[documentation](docs/README.md) and [acceptance gates](docs/milestones.md) define scope.

**Approximately 20% implemented; 0/7 full milestones accepted.** This is a rough
engineering estimate, not a test-count percentage or release-date prediction.
No commercial profile, playable Mario build, general PICA renderer or finished GUI
is enabled. Current binaries are original homebrew diagnostics.

## Current implementation

| Area | Verified foundation | Main remaining work |
|---|---|---|
| Input/preparation | Bounded CIA/NCSD/NCCH inspection/decryption, full local RomFS verification and independently checked selected bytes with identified formats | Reusable bounded format readers, broader module/runtime observation and Windows conversion acceptance |
| CPU/runtime | Checked ARM/Thumb/VFP/AOT subsets, owned memory, eight single-core contexts and scoped services | Complete exercised CPU/HLE coverage, modules, guest audio and retail saves |
| Homebrew | Nine normal-CRT/libctru applications: boot, threads, graphics, input, interactive, filesystem, RomFS, GX transfer and PICA draws | Broader guest services/audio and game integration |
| Graphics | Direct/shared framebuffer presentation, five display formats, 14 base textures, scoped GX transfers and compiled original guest PICA/MOV/software draws | broader shaders/TEV/sampling, GPU resources/coherency and measured console performance |
| Packaging/GUI | Original synthetic packing/decryption verification and conversion foundations; GUI workflow specified | Commercial conversion, actual WUP install/Menu gameplay, clean Windows and final GUI |

## Executable intake follow-up

The derived USA Mario NCCH passes independent hashes, decompression and revision
checks; pinned static analysis succeeds. Exact hashes and commercial bytes stay
private. Its declared service list includes DSP and module loading, but runtime
use and broader nested module requirements remain unverified. The
new local asset/preparation reports agree on source, revision and IVFC evidence;
1,756 outer files are inventoried. Seven selected payloads now match every expected
hash, size and receipt binding: Yaz0/NARC, DVLB/CGFX/BYML and CSAR/CSTM structures
are identified. Three embedded shader regions exceed the current 256-word code
capacity; both sampled streams declare stereo DSP ADPCM at 32 kHz. The
standalone Python helper checks matching existing reports, the full local blob
and selected file hashes without reinstalling the 0.20.0 wheel. Scoped issue
[#92](https://github.com/Jdiller-86/CTR-2-WUP/issues/92) and
[PR #93](https://github.com/Jdiller-86/CTR-2-WUP/pull/93) track that route under
#33/M5. The inspector now accepts
strict decrypted v0 CFA data partitions with original negative/compatibility
coverage, without accepting prototype or v0 executable layouts. See
[first-title findings](docs/first-title.md). No commercial profile is enabled.

## Original diagnostic evidence

The compiled normal-CRT/libctru guest draw route executes 17,420,794 ARM
instructions with zero fallback, four triangles and 97,200 covered fragments.
Independent checks cover all 172,800 displayed pixels. Original guest ownership,
permissions, aliases, retained uploads and late-failure transactions have scoped
host/sanitizer evidence. The October 13 console observations above remain bound
to their actual earlier builds; a new cross-build is not new console evidence.

[PR #98](https://github.com/Jdiller-86/CTR-2-WUP/pull/98) adds checked 512-word
vertex storage and original entry/upload/rollback probes. Its exact head has
passing hosted Required CI and Documentation. Merge authorization and subsequent
tracking reconciliation remain separate from those test results.

The source version is **0.23.0**; the latest published prerelease before this
migration is **0.20.0**. A fresh versioned build/release must pass its own source,
SDK, original-frame, wheel, documentation, ZIP and checksum checks before being
claimed current. Older tags and assets are preserved.

## Remaining acceptance

- Correct the drawn-frame confirmation gate and measure execution/rendering/
  transfer/wait phases; do not repeat unchanged passed guides.
- Extend shader arithmetic/control, TEV/sampling, coherency, modules, guest audio
  and retail saves using original references and actual observed requirements.
- Record full platform stability/recovery and original WUP installation/Menu launch.
- Demonstrate the selected game's gameplay/audio/save/performance and full clean
  Windows conversion, then implement and accept the final GUI.
- Finish the reviewed live Project reconciliation, canonical site/legacy redirects
  and artifact verification recorded in the consolidation issue.

The tester reports Windows 11, Python 3.12+, an SD reader and mainstream Aroma/Wii U
firmware; exact console versions remain unrecorded. Source/API/host/cross-build
checks do not fill physical or commercial evidence gaps. Full acceptance stays
**0/7**. See [the detailed gates](docs/milestones.md),
[console evidence](docs/console-evidence.md) and
[development history](docs/development-history.md) for older checkpoints.
