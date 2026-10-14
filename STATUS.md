# CTR2WUP status

**Exact shader arithmetic development:** [PR #105](https://github.com/Jdiller-86/CTR-2-WUP/pull/105)
implements scoped #76 under #28/M4: component-wise ADD/MUL, independent operand
descriptors and checked integer dyadic evaluation. All 167,730 original rational
cases, four analytic pixel scenes and 282,626 pinned encodings agree. Late errors
preserve shader/target/guest queue state. Required full/platform checks are pending.
The source version is **0.24.0 (unreleased)**; the latest published diagnostic
release remains **0.23.0**. Hardware rounding, broader arithmetic/control, native
GX2 and commercial execution remain open. [Contract](docs/software-scenes.md#exact-add-and-mul).

**October 14 unified public hosting:** maintained documentation and the live
dashboard deploy from this public source repository at
[the canonical project site](https://jdiller-86.github.io/CTR-2-WUP/).
Merged PR #100/#101 pass required hosted platform/documentation checks.
[Canonical deployment](https://github.com/Jdiller-86/CTR-2-WUP/actions/runs/38601781297),
[repository settings](https://github.com/Jdiller-86/CTR-2-WUP/actions/runs/38601840961)
and [legacy redirect HTTP checks](https://github.com/Jdiller-86/CTR-2-WUP/actions/runs/38606446452)
verify successfully. Existing history, releases, visibility and protections are preserved.

The [reviewed Mario update](https://github.com/Jdiller-86/CTR-2-WUP/actions/runs/38605081338)
activates M5/intake/requirements and removes the resolved owned-input wait from
six targets while retaining console waits and owner notes. The
[label/dashboard refresh](https://github.com/Jdiller-86/CTR-2-WUP/actions/runs/38605491773)
and live HTTP read-back show M5 **In progress** and **0/7** accepted gates.
[Native/Project reconciliation](https://github.com/Jdiller-86/CTR-2-WUP/actions/runs/38608010840) verifies
69 parent links, 28 PR links and all 104 members, with no untracked PRs.
[Legacy retirement](https://github.com/Jdiller-86/CTR-2-WUP/actions/runs/38609420311) verifies
all redirects after archival, preserves history and retains the original manifest;
131 current checksums independently match. [#99](https://github.com/Jdiller-86/CTR-2-WUP/issues/99)
records delivery verification. The Actions management
secret works; the environment binding remains a separate access path.

**Merged shader capacity:** [PR #98](https://github.com/Jdiller-86/CTR-2-WUP/pull/98)
under [#97](https://github.com/Jdiller-86/CTR-2-WUP/issues/97), native parent #28/M4,
expands checked vertex code storage to 512 words with original high-entry,
upload/END and transactional guest rollback probes. Each scene state grows by
1,280 bytes; native compiler stack frames are unchanged. Required hosted checks
pass, including Windows. Project membership and native links verify; its original MOV-only scope and commercial
compatibility limits hold. See [the capacity contract](docs/software-scenes.md#vertex-code-capacity).

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

Updated **October 14, 2026 (America/New_York)** · **v0.24.0 source, unreleased**.
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
| Graphics | Direct/shared framebuffer presentation, five display formats, 14 base textures, scoped GX transfers and original guest/software draws with exact MOV/ADD/MUL | rounding/control, broader shaders/TEV/sampling, GPU resources/coherency and measured console performance |
| Packaging/GUI | Original synthetic packing/decryption verification and conversion foundations; GUI workflow specified | Commercial conversion, actual WUP install/Menu gameplay, clean Windows and final GUI |

## Executable intake follow-up

The derived USA Mario NCCH passes independent hashes, decompression and revision
checks; pinned static analysis succeeds. Exact hashes and commercial bytes stay
private. Its declared service list includes DSP and module loading, but runtime
use and broader nested module requirements remain unverified. The
new local asset/preparation reports agree on source, revision and IVFC evidence;
1,756 outer files are inventoried. Seven selected payloads now match every expected
hash, size and receipt binding: Yaz0/NARC, DVLB/CGFX/BYML and CSAR/CSTM structures
are identified. Three embedded shader regions exceed the former 256-word code
capacity; the merged 512-word increment addresses storage bounds while shader
semantics remain open. Both sampled streams declare stereo DSP ADPCM at 32 kHz. The
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

PR #98 merged at `926cf8cf23d17a091bea69ad1e2ee850b1374045` with owner approval
after passing hosted Required CI and Documentation. Its original probes do not
establish commercial shader semantics or physical renderer acceptance.

The source version is **0.24.0 (unreleased)**. [Development releases](https://github.com/Jdiller-86/CTR-2-WUP/releases)
identify immutable source commits and matching original diagnostic kits, developer
wheel and offline documentation. The release workflow verifies source/version,
pinned SDK, original frames, binaries, source/wheel equality, ZIP CRCs and checksums.
Use each release's manifest and successful workflow receipt for its actual
publication identity. Older tags/assets and their console evidence are preserved.

## Remaining acceptance

- Correct the drawn-frame confirmation gate and measure execution/rendering/
  transfer/wait phases; do not repeat unchanged passed guides.
- Extend shader rounding/remaining arithmetic/control, TEV/sampling, coherency, modules, guest audio
  and retail saves using original references and actual observed requirements.
- Record full platform stability/recovery and original WUP installation/Menu launch.
- Demonstrate the selected game's gameplay/audio/save/performance and full clean
  Windows conversion, then implement and accept the final GUI.

The tester reports Windows 11, Python 3.12+, an SD reader and mainstream Aroma/Wii U
firmware; exact console versions remain unrecorded. Source/API/host/cross-build
checks do not fill physical or commercial evidence gaps. Full acceptance stays
**0/7**. See [the detailed gates](docs/milestones.md),
[console evidence](docs/console-evidence.md) and
[development history](docs/development-history.md) for older checkpoints.
