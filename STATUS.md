# CTR2WUP status

Updated **October 12, 2026 (America/New_York)** · **v0.18.0**.
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
| Homebrew | Eight normal-CRT/libctru applications: boot, threads, graphics, input, interactive, filesystem, RomFS and GX transfer | New console/result/save/asset/GX evidence and broader game integration |
| Graphics | Direct/shared framebuffer presentation, five display formats, 14 base textures, scoped CPU-backed GX queues/copies/fills/transfers | PICA vertex/shader/TEV/draw execution, GPU resources/coherency and measured console performance |
| Packaging/GUI | Original synthetic packing/decryption verification and conversion foundations; GUI workflow specified | Commercial conversion, actual WUP install/Menu gameplay, clean Windows and final GUI |

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
The tracking tool now selects it directly and cannot create a duplicate on access
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
artifacts, release policy and management operations still awaiting access.

The existing GitHub integration rejected Projects creation and repository
administration/security-setting writes with **Resource not accessible by
integration**. Issue/milestone writes work. Project board,
About/topics/Discussions and security-setting changes need the relevant GitHub
connection permissions; those features are not falsely marked enabled.
Searchable documentation has a strict build, rendered-link checks, an offline
HTML guide and a free public docs-only Pages export. The user confirms private
Pages is unavailable on their free plan; application source remains private.
The user explicitly declines branch protection for the private free-plan repository.
Topic branches, reviewable PRs and passing CI are the maintenance policy.
Local documentation builds do not establish live hosting.
The connection rejected public docs-repository creation with HTTP 403, so its
free hosted publication needs owner access. The standalone HTML reader and
complete public export remain available for download.
Future Codex commits use an opt-in attribution hook and verified public bot identity;
normal commits and existing history retain their authors. No repository visibility
or paid entitlement was changed.

## Console evidence and inputs still needed

The user reports Native, Guarded AOT and Compiled AOT completed without errors on
**0.14.5**. Supplied logs have older 0.14.0–0.14.3 headers. The valid visit-1 snapshot
proves an initial save, while the later Save test failed and no new logs appeared.
Exact causes remain unconfirmed. The earlier native guide and Minus-exit reports
are retained as scoped evidence; [console-evidence.md](docs/console-evidence.md)
distinguishes logs and verbal confirmation.

The tester reports a U.S. Wii U with latest firmware, Aroma and WUP Installer GX2;
exact versions remain unknown. No console is attached. The user is away and
permits continued independent development using earlier verbal successes.

- **Save/results/RomFS:** the existing v0.14.6 follow-up remains pending.
  Preserve snapshot/siblings/reports; see [console-retest.md](docs/console-retest.md).
- **Assets/GX:** optional v0.16 Asset Files and v0.17 GX Transfers await physical
  SD/read/picture/lifecycle evidence. Neither requires a ROM/key. Do not repeat
  completed long guides merely for a source version bump.
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
