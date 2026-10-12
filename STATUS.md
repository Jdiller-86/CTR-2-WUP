# CTR2WUP status

Updated **October 12, 2026 (America/New_York)** · **v0.17.0**.
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
signaling for dropped interrupts. Required `make check` passes **210 Python tests
and 17 C suites**, normal and ASan/UBSan. GX has **1,102 assertions**; report/guide
has **583**. The transfer matrix has **400 original C/Python/reference color/layout
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
and [milestones](https://github.com/Jdiller-86/CTR-2-WUP/milestones) now show verified
foundations and remaining gates. Managed sections synchronize from
`docs/milestone-status.json`; comments/manual notes are retained and no acceptance
gate is auto-closed. Contribution/console-result forms, ownership, security
reporting guidance and four specialist Copilot profiles support review.

Automatic CI covers Python 3.12/3.14, GCC/Clang sanitizers, Windows host/portable-C,
workflow lint and immutable-SDK native Wii U artifacts. **Required CI** aggregates
those checks. Deep Windows AOT/WPF and eligibility-gated CodeQL are manual;
agent profiles do not enable a paid subscription or run unattended.
Workflow lint/schema checks pass locally; hosted execution must be read from
its own run evidence. [GitHub development](docs/github-development.md) explains
artifacts, release policy and protected setup still awaiting access.

The existing GitHub integration rejected Projects creation and repository
administration/security-setting writes with **Resource not accessible by
integration**. Issue/milestone writes work. Project board, branch protection,
About/topics/Discussions and security-setting changes need the relevant GitHub
connection permissions; those features are not falsely marked enabled.
No repository visibility or paid entitlement was changed.

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
- **Windows/install:** native host results, full devkitPPC conversion, actual WUP
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
