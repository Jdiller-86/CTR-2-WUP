# CTR2WUP status

Updated **October 13, 2026 (America/New_York)** · **v0.20.0**.
The target remains an UWUVCI-style Windows GUI: select an owned 3DS game, configure
title/artwork, click Inject, install its individual WUP and play from Wii U Menu.
[Requirements](docs/project-brief.md), [product plan](docs/development-plan.md),
[documentation](docs/README.md) and [acceptance gates](docs/milestones.md) define scope.

**Approximately 20% implemented; 0/7 full milestones accepted.** This is a rough
engineering estimate, not a test-count percentage or release-date prediction.
No commercial profile, playable Mario build, PICA draw renderer or finished GUI
is enabled. Current binaries are original homebrew diagnostics.

## Current implementation

| Area | Verified foundation | Main remaining work |
|---|---|---|
| Input/preparation | Bounded CIA/NCSD/NCCH inspection, optional local key/seed imports/decryption, canonical full RomFS IVFC/metadata verification and private asset blobs | Actual user exports/Mario assets, broader forms and Windows acceptance |
| CPU/runtime | Checked ARM/Thumb/VFP/AOT subsets, owned memory, eight single-core contexts and scoped services | Complete exercised CPU/HLE coverage, modules, guest audio and retail saves |
| Homebrew | Eight normal-CRT/libctru applications: boot, threads, graphics, input, interactive, filesystem, RomFS and GX transfer | Launch-audio regression, broader services and game integration |
| Graphics | Direct/shared framebuffer presentation, five display formats, 14 base textures, scoped CPU-backed GX queues/copies/fills/transfers | PICA vertex/shader/TEV/draw execution, GPU resources/coherency and measured console performance |
| Packaging/GUI | Original synthetic packing/decryption verification and conversion foundations; GUI workflow specified | Commercial conversion, actual WUP install/Menu gameplay, clean Windows and final GUI |

## v0.20.0 increment

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

Five new result files establish original Save, embedded RomFS, SD-file assets
and GX transfer/configuration checks. The additional Save 0.14.6 full trace
confirms reload in its first guest session after closing and reopening the app.
All new guides complete without failures/skips; guest fallback/fault are zero.
The user confirms correct screens and normal Minus return, but reports Wii U
launch music continuing throughout. Raw uploads remain private and older failures
are retained separately in [the evidence review](docs/console-evidence.md).

The tester reports latest U.S. firmware, Aroma and WUP Installer GX2; exact
versions remain unknown. No console is attached.

- **Audio:** run only [Audio Check and GX Transfers 0.20.0](docs/launch-audio.md)
  for launch silence, native tone and compiled stability/exit. No Save/RomFS or
  long controls-guide repeat is required solely for this version.
- **Platform:** exact versions, Home/APT routes, recovery and installed original
  WUP evidence remain separate acceptance work.
- **Mario:** supplied CIA exceeds the 32 MiB attachment limit; small intake has
  encrypted content and no executable. Prepare locally or use an already decrypted
  owned dump, then privately export the small executable intake. Keys remain local.
  Actual revision/assets/required services and gameplay are unverified.
- **Windows/install:** full native devkitPPC conversion, actual WUP
  installation/Menu launch/audio/gameplay/save/reload and GUI acceptance remain
  separate gates. CI tooling success does not close them.

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
