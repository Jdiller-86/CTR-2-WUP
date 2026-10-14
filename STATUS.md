# CTR2WUP status

**October 14 development continuity:** hosted Actions refuses jobs before
starting. Issue [#95](https://github.com/Jdiller-86/CTR-2-WUP/issues/95) adds
[secure direct API management and exact-commit local validation](docs/direct-development.md).
Code, commits, pushes and scoped maintenance merges can continue. The named
credential still returns 403 for personal Project #2; live M5 status/resource
review, native catalog reconciliation and public tracker refresh remain pending.
Source milestone/table evidence is In progress. No full gate is accepted.

**Unreleased shader capacity:** draft [PR #98](https://github.com/Jdiller-86/CTR-2-WUP/pull/98)
under [#97](https://github.com/Jdiller-86/CTR-2-WUP/issues/97), native parent #28/M4,
expands checked vertex code storage to 512 words with original high-entry,
upload/END and transactional guest rollback probes. Each scene state grows by
1,280 bytes; native compiler stack frames are unchanged. Required Windows/hosted
coverage and Project membership remain pending; MOV-only semantics and commercial
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

Updated **October 13, 2026 (America/New_York)** · **v0.23.0**.
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
are identified. Three embedded shader regions exceed the former 256-word code
capacity; the unreleased 512-word increment addresses storage bounds while shader
semantics remain open. Both sampled streams declare stereo DSP ADPCM at 32 kHz. The
standalone Python helper checks matching existing reports, the full local blob
and selected file hashes without reinstalling the 0.20.0 wheel. Scoped issue
[#92](https://github.com/Jdiller-86/CTR-2-WUP/issues/92) and
[PR #93](https://github.com/Jdiller-86/CTR-2-WUP/pull/93) track that route under
#33/M5. The inspector now accepts
strict decrypted v0 CFA data partitions with original negative/compatibility
coverage, without accepting prototype or v0 executable layouts. See
[first-title findings](docs/first-title.md). No commercial profile is enabled.

## v0.23.0 increment

An original compiled ARM/libctru program now submits two standard GSP PICA
command lists, retains shader/state across them, draws to owned tiled targets,
checks the pixels, transfers both framebuffers and exits with its own PASS.
The route executes **17,420,794 ARM instructions**, zero fallback, four triangles
and **97,200 covered fragments**. Independent displayed-image checks cover all
172,800 top/bottom pixels. See [the guest draw contract](docs/guest-pica-draws.md).

Guest physical ownership, permission splits, resource/command/relay aliases,
retained uploads, late failure and queue completion have **83,108 assertions**.
The raw VFP push/pop guard now preserves exact bits under CRT FPSCR controls;
unsupported arithmetic still fails. Required coverage is **415 Python tests and
22 normal/ASan/UBSan C suites**; the report guide has 636 assertions. Native,
Scene Check and compiled Guest Draws RPX/WUHB builds and exact reports are retained
with source/SDK provenance; the October 13 physical follow-up is reviewed above.

Development is visible in PR #57 (guest draws) and merged PR #58 (tracking).
Scoped feature issues connect native parents, milestones, Project items and PRs;
35 established gate tasks retain their acceptance boundaries. The bounded
command task #27 meets both documented criteria and has a recorded Done review;
full M4 remains open. AGENTS.md, the
repository tour, shared runtime manifest and current product plan now provide a
clear route for incoming contributors. The Actions credential remains in Actions; the owner can independently supply
the same identity through the secure direct environment binding.
Completed remote/local branches are removed after merge.

CPU software rendering supports the documented canonical target/fragment and
MOV shader subset. Arithmetic shaders, sampling/general TEV, depth/blend/stencil,
clipping and native GPU translation remain open. The Audio Check/GX Transfers **0.20.0** launch-music regression has reviewed
physical passes. Commercial profiles, useful performance and all full gates
remain pending. Host passes and cross-builds do not establish physical acceptance.

## v0.22.0 increment (historical)

The first original scenes now execute PICA-format loader/index state, fixed
attributes, packed/float32 uniforms and actual MOV/NOP/END shader instructions
through a checked software triangle renderer. Byte and short index formats
produce the same two-triangle square, with exact independent pixel comparison
and whole-list rollback on late failure. A separate pinned nihstro comparison
checks **12,290 shader/descriptor encodings**, without copying or linking an
emulator into the runtime.

The original shader/scene suite passes **1,393,972 assertions**. Required tests
now include **372 Python tests and 21 normal/ASan/UBSan C suites**; the report
guide has 624 assertions. Native and focused Scene Check RPX/WUHB cross-builds
pass. Receipts and actual desktop PNG output are retained in
`artifacts/reports/first-scenes-v022/`; CI also publishes a focused
`wiiu-scene-check-<source commit>` artifact with provenance and notices.

See [the scene contract and console guide](docs/software-scenes.md). The focused
check runs CPU rasterization and existing GX2 surface copies; physical picture,
timing and exit evidence remains pending. Standard guest GSP still rejects
draws. Arithmetic, textures/TEV, depth/blend, clipping, GPU shader translation and
commercial profiles remain unsupported. This is a significant M4 foundation,
not full M4 acceptance. The Audio Check/GX Transfers **0.20.0** launch-music
regression remains open. Completed PR branches are deleted after merge.

## v0.21.0 increment (historical)

The original vertex decoder now gathers resolved array attributes through
unsigned 8-bit or little-endian unsigned 16-bit index buffers. It preserves
duplicates and index order, handles unaligned buffers and explicit byte offsets,
and checks every referenced range/value before changing any output. Unreferenced
nonfinite vertex data does not reject a valid gather; 0xff/0xffff remain ordinary
indices. Index buffers and descriptors receive the existing full-span alias
protection. No heap allocation, PICA register interpretation, primitive assembly,
shader execution or draw submission is introduced.

The focused vertex suite passes **364,123 original assertions**, including every
8-bit/16-bit index, reordered multi-stream/interleaved records, late rejection,
aliasing, truncation and overflow. Required full-source and native-build receipts
are retained in `artifacts/reports/indexed-vertex-v021/`. This advances M4's
vertex/shader task without accepting the task or any full milestone.

The focused Audio Check/GX Transfers **0.20.0** console handoff remains current;
this independent vertex increment requires no repeated completed console guide.
GitHub management continues through its verified Actions credential. Completed
PR branches are deleted after merge.

## v0.20.0 increment (historical)

All diagnostic variants initialize a fresh native AX context before running
self-tests or waiting for input. An inherited same-process context is reset;
owned voices are released before AX shutdown and foreground changes. Compiled
apps acquire no tone voice. Failure states and lifecycle counters enter the
small result and full trace. A two-step Audio Check asks whether launch music
stopped and whether the original native tone played on both outputs. This is
not 3DS guest audio, and physical silence remains pending.

The private physical round now verifies named TXT read-back, original fresh-launch
save reload, embedded/SD RomFS and GX transfer/configuration. Correct pictures and
Minus return have user confirmation; the launch-music defect remains separate.
The save completion prompt now explicitly distinguishes a fresh app launch from
an in-app Plus restart.

Project #2 population/overview/fields/views are verified using the Actions secret.
Source tracking still cannot overwrite existing board values. Docs-only Pages
publication and the dynamic chart are live through a reviewed trusted-main workflow.
The site, dashboard, JSON and SVG return HTTP 200. Application source remains private.

Validation: 371 Python tests and 20 normal/ASan/UBSan C suites pass;
report/guide has 603 assertions and AX lifecycle has 149. Current Audio Check and
compiled GX RPX/WUHB cross-builds passed. The pinned PICA matrix passes 353 original cases. Strict docs/link and public
export/privacy checks pass; hosting/chart receipts are retained with this increment. Full milestone acceptance remains 0/7.

## v0.19.0 increment (historical)

An original bounded vertex-attribute decoder handles signed8, unsigned8, signed16
and finite little-endian float32 from explicit streams/input permutations. It
checks complete layouts, ranges, capacity and aliases before publishing output.
Lane defaults are explicit; fixed/index attributes, PICA loader-state adaptation,
float24 arithmetic, shaders and draws remain unsupported. See
[the vertex contract](docs/vertex-attributes.md). GCC and Clang normal/ASan/UBSan
and emulated big-endian PowerPC Linux pass **166,547 original assertions** each.
The devkitPPC module compiles; its measured decoder stack frame is 64 bytes.
This is not physical Espresso execution or a rendered scene.

Project #2 now owns tracking policy. Prepared tooling adds a preserved detailed
Project overview, priority/work-type fields and filtered views, reads canonical
board values, mirrors issue status/resources and exports safe aggregates for an
interactive dashboard. Source sync carries requirements/evidence only, never
existing tracking state. The README's static progress image is removed. Live
population and hosted publication remain blocked: the secure personal requirement
is saved, but the environment reports no saved binding and Project reads remain
forbidden. No Project, website or token-enabled Actions run is falsely marked live.

The focused console handoff is in `artifacts/delivery/console-focus-2026-10-12/`.
Its hash-verified ZIP combines GX/PICA setup 0.18.0, retained Save/RomFS result
fixes 0.14.6 and optional SD assets 0.16.0; every app version is explicit. Existing
reports/snapshot siblings are preserved. The tester is now at their Wii U;
new console evidence remains pending until reviewed. No long native/input rerun
is required solely for the version increment.

Reports: `artifacts/reports/project-vertex-v019/` and the earlier vertex host receipt.

## v0.18.0 increment

The bounded native PICA command front end now executes finalized raw viewport/
scissor configuration through standard GX command 1. Masks, repeated/grouped
writes, full-stream/counter validation, staged guest input and P3D completion
are implemented. Unknown registers, draws, uploads and control flags fail before
state/output changes, retaining pending queue work. See [the contract](docs/pica-commands.md).

Validation passes **306 Python tests and 18 C suites**, normal and ASan/UBSan;
the new PICA suite has 67,706 assertions and the integrated GX suite has 1,619.
The independent pinned-libctru builder/C/Python matrix passes 353 original cases,
with identical emulated big-endian PowerPC receipts.
The compiled GX application executes 17 commands including one no-draw PICA list,
exits after 1,489,172 instructions with zero fallback and retains exact prior
transfer pictures. Native and compiled GX v0.18.0 RPX/WUHB cross-builds pass.
Physical console behavior, actual draws/shaders and full M4 acceptance remain open.

The owner supplied [Project #2](https://github.com/users/Jdiller-86/projects/2).
The tracking tool selects it directly and cannot create a duplicate on access
failure. Direct Project reads still return Resource not accessible by integration;
its fields/items and automated population remain unverified. Normal repository
Git/issue/CI work continues.

Reports: `artifacts/reports/pica-v018/`. Earlier diagnostic kits remain preserved.

## v0.17.0 increment

The standard GSP command ring now executes DMA, triggered memory fills, unscaled
equal-size display conversion, raw strided texture copy and cache-region checks.
It commits commands in order and preserves a failing entry/later work; all
ranges/permissions/allocations are checked before that command changes output.
Staging handles guest overlap, relay aliases are protected, and bulk memory
writes invalidate exclusive/code snapshots including executable storage aliases.
PSC/PPF/DMA completions follow bounded ring behavior. PICA lists remain unsupported.

The original stock-libctru application uses 16 GX operations and verifies exact
green/red/green TV images with blue GamePad pixels. Its sanitized AOT run exits
after **1,487,784 instructions**, **zero fallback**, with three deliberately
unavailable startup services still recorded. Desktop PASS does not imply all
services or Wii U rendering are implemented. [The GX contract](docs/gx-transfers.md)
records alignment/size/format/layout limits and the optional two-step console guide.

Independent review found and corrected missing fill-trigger validation and
signaling for dropped interrupts. Required `make check` passes **291 Python tests
and 17 C suites**, normal and ASan/UBSan. GX has **1,102 assertions**; report/guide
has **587**. The transfer matrix has **400 original C/Python/reference color/layout
comparisons** and matching emulated big-endian PowerPC hashes. Reference color/
Morton primitives were tested; the external full transfer engine was not.
Fresh Native and GX Transfers RPX/WUHB cross-builds carry v0.17.0.

Reports: `artifacts/reports/gx-transfers-v017/`.
Delivery: `artifacts/delivery/gx-github-0.17.0/`, containing source/wheel,
original Native/GX diagnostics, reference images, guides and checksum/manifest.
The release record and CI provide repository-visible delivery/evidence; consult
the actual [Actions run status](https://github.com/Jdiller-86/CTR-2-WUP/actions).

## GitHub development

Seven [milestone issues](https://github.com/Jdiller-86/CTR-2-WUP/issues?q=is%3Aissue+label%3Amilestone)
and [milestones](https://github.com/Jdiller-86/CTR-2-WUP/milestones) group 35 focused
work issues with acceptance criteria, dependencies and waiting resources. The
overviews coordinate full gates rather than substituting for implementation tasks. Managed sections synchronize from
`docs/milestone-status.json`; comments/manual notes are retained and no acceptance
gate is auto-closed. Contribution/console-result forms, ownership, security
reporting guidance and four specialist Copilot profiles support review. Exclusive
status labels, priority/component tags and trusted API-only automation report
issue, PR and associated branch state. This follows the requested Odysseus model;
merged code never automatically accepts console or full-milestone gates.

Automatic CI covers Python 3.12/3.14, GCC/Clang sanitizers, Windows host/portable-C,
workflow lint and immutable-SDK native Wii U artifacts. **Required CI** aggregates
those checks. Deep Windows AOT/WPF and eligibility-gated CodeQL are manual;
agent profiles do not enable a paid subscription or run unattended.
[Hosted main CI passed](https://github.com/Jdiller-86/CTR-2-WUP/actions/runs/38297581496)
on source `d280dad`: all eight CI checks, including Python 3.12/3.14, GCC/Clang
sanitizers, the Windows baseline and Wii U build. Windows runs 237 Python cases
with three documented filesystem/privilege skips plus all 17 normal C suites.
The separate [Documentation build passed](https://github.com/Jdiller-86/CTR-2-WUP/actions/runs/38297581490).
[Deep Windows validation passed](https://github.com/Jdiller-86/CTR-2-WUP/actions/runs/38289835013),
including real pinned external AOT, Unicode execution and .NET 10 WPF tooling.
This verifies development tools; full Windows conversion and GUI remain unfinished.
[GitHub development](docs/github-development.md) explains
artifacts, release policy and trusted-main management workflows.

The existing GitHub integration rejected Projects creation and repository
administration/security-setting writes with **Resource not accessible by
integration**. Issue/milestone writes work. Project #2 is now populated and linked through its Actions secret. Direct cloud
administration access remains limited. The requested free About/topics,
Discussions, branch cleanup and dependency-alert settings are applied and
API-verified through the dedicated repository-features Actions workflow; its
receipts establish live outcomes.
Searchable documentation has a strict build, rendered-link checks, an offline
HTML guide and a free public docs-only Pages export. The user confirms private
Pages is unavailable on their free plan; application source remains private.
The user explicitly declines branch protection for the private free-plan repository.
Topic branches, reviewable PRs and passing CI are the maintenance policy.
[Free public documentation](https://jdiller-86.github.io/CTR-2-WUP-docs/) and the
[Project dashboard](https://jdiller-86.github.io/CTR-2-WUP-docs/progress/) are now
deployed and HTTP-verified using repository Actions. Source remains private.
The standalone HTML reader remains available for offline use.
Future Codex commits use an opt-in attribution hook and verified public bot identity;
normal commits and existing history retain their authors. No repository visibility
or paid entitlement was changed.

## Console evidence and inputs still needed

Reviewed original Save/reload, assets, GX, launch-audio and bounded draw results
are retained in [the evidence review](docs/console-evidence.md). The user confirms
pictures, silence and correct Minus return. Exact environment versions, full
Home/APT/recovery/stability and original installed-WUP acceptance remain open.

- **Draw performance/evidence:** investigate the 57.33-second rendered scene and
  early visual-prompt confirmation. Add accurate gating, bounded guest progress
  and phase measurements; the existing aggregate host timing is not game speed.
- **Mario:** the uploaded derived USA executable/revision and private static
  analysis are verified. The original-container and local RomFS preparation reports now match the
  executable revision and asset source hash. Raw CCI/blob bytes remain local;
  selected archive/shader/audio contents and observed runtime requirements
  still need inspection. Exact hashes and commercial bytes stay private.
- **Windows/install:** complete conversion, actual WUP installation/Menu launch,
  gameplay/audio/save/reload and the planned GUI remain separate gates.

No repeat of the completed audio/Save/RomFS routes is requested solely for an
evidence/version refresh. New builds need their own focused regression results.

## Continuing work

Implement the PICA/GX2 features, services, modules/audio and retail saves exercised
by original reference scenes and the selected title. Measure real Wii U costs
before promising performance. Complete the proven Windows conversion engine,
then implement the specified GUI. Generic work can continue, but cannot replace
actual game requirements or physical acceptance. The [remaining-gates page](docs/current-roadblock.md)
is the handoff for pending resources.

Earlier implementation details, source audits and versioned receipts are preserved
in [development history](docs/development-history.md). The verified cloud toolchain
uses the existing checkout, SDK and `/workspace/ctr2wup-tools/environment/activate.sh`;
no real keys, private console files or commercial data enter published artifacts.
