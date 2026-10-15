# CTR2WUP product development plan

Updated **October 15, 2026 (America/New_York)** for
**v0.25.0 display-cache evaluation**. The requirements remain
[the original brief](project-brief.md), the reviewed [GUI workflow](gui-workflow.md)
and the user amendment allowing [optional local encrypted-input preparation](encrypted-input.md).
Upstream feasibility scores and compatibility percentages are research context,
not verified results. [Current status](../STATUS.md) records actual evidence.

## Product and completion target

Deliver **CTR2WUP.exe**, a Windows GUI with an UWUVCI-style **3DS Inject** page:

**Select owned ROM → configure title/artwork/settings → choose WUP output →
Inject → install on Wii U → launch from the Wii U Menu.**

The shared engine must identify one supported Old 3DS revision, validate its
executable/assets, translate ARM/Thumb/VFP ahead of time, compile the reusable
Wii U runtime and shader/state data, stage stable per-game metadata/identity and
produce a validated installable WUP. Decrypted `.3ds`, `.cxi` and supported
base-application `.cia` inputs are supported intake routes; encrypted input uses
user-managed local keys. No key or commercial asset is bundled or uploaded.

The first MVP supports **one exact commercial revision**. Super Mario 3D Land is
the selected candidate; its uploaded USA executable/revision is privately verified.
The local RomFS reports and seven independently checked samples match that
revision. Selected shader/container/audio structures now inform active M5
analysis; broader modules and observed runtime use remain open. No profile is
enabled. Completion requires clean-Windows GUI conversion, physical
WUP installation/Menu launch, repeatable gameplay, TV/GamePad mapping, required
input/touch, audio, save/reload, rebuild/save continuity and useful measured
performance. Record the revision, environment and remaining limits. The CLI is
the shared engine; the final user workflow requires no manual shell commands.

## Current checkpoint

Original normal-CRT/libctru applications now exercise boot, threads, input,
framebuffer display, interactive execution, save/reload, RomFS, GX transfers and
bounded guest PICA draws. The new draw route executes compiled ARM code through
standard GSP commands and compares exact original two-screen scenes. It uses CPU
software rendering and existing presentation; native GX2 shader translation,
broader PICA state and physical renderer acceptance remain open.

Checked generated-code integration, owned guest memory, scoped services, original
synthetic packaging and hosted Windows foundations exist. Reviewed physical
Save/results/assets/GX evidence is retained, and the launch-music regression has
reviewed physical passes. Physical cache comparison, complete platform stability,
installed-WUP acceptance, commercial conversion and final GUI are still pending.
**0/7 full milestones are accepted.** Completed scoped features do not change that.

## Development continuity

The owner made source public on October 14. Hosted Required CI and Documentation
now pass for the shader-capacity PR, and the Actions management identity can read
Project #2. [Reviewed management](direct-development.md) uses immutable trusted
main and the existing Actions secret. PR #100/#101 consolidate canonical Pages,
reviewed management and current artifacts. Canonical/legacy HTTP checks and the
Mario transitions/label refresh verify successfully; [current status](../STATUS.md)
records the final native tracking and delivery receipts. The environment binding
is a separate fallback whose access must be independently verified.
No spending, protection or full-gate acceptance change is implied.

Merged PR #98 implements the scoped #97/#28/M4 vertex code capacity increment:
512 checked words with original upload/entry/rollback probes and native storage/
stack measurements. Required hosted platform checks and native/Project reconciliation verify. Arithmetic/control and broader renderer work follow this capacity
boundary; it does not establish commercial shader execution.

Scoped #76/PR #105 now implements exact ADD/MUL with independent operand
descriptors and integer dyadic evaluation. Original rational, encoding, scene and
transaction probes pass; required full/platform checks gate delivery through PR #105. This advances
#28/M4 without establishing float24 rounding, control flow, native GX2 or a full gate.

## Now, next and later

PR #106/#87 has complete source-matched evidence for the 0.24.1 hardware round:
two arithmetic guides and all three Guest Draws/Plus-repeat steps pass. All five
traces reach `main_return`; Minus and optional HOME returns also have the user's
confirmation on firmware 5.5.6 U / Aroma Beta 27. Correct picture-confirmation
order and both new-session PASS/exit events verify. Scenes take 91.190–91.613 s;
native scale/publication averages 132.218–132.281 ms and is the next measured
target. All files are received; no further rerun is requested for this round.
Version 0.25.0 now implements changed-span scaling and coalesced row
publication with independent pixel/failure and six-workload host checks.
Static/small-counter publication-byte reductions are measured on host;
full motion saves no bytes. Physical correctness/speed need the focused new
build. This remains a premerge snapshot with full gates open.

| Priority | Concrete deliverable | Tracking and acceptance |
| --- | --- | --- |
| Delivered checkpoint | Bounded compiled guest draws, transactional targets/completion, exact frames and checked 512-word shader storage | Scoped M4 command and capacity increments in PR #48/#57/#98; broader #28/#30 and full M4 acceptance remain open |
| Next independent renderer work | Extend shader/control or bounded TEV and texture sampling using original references | M4 #28/#29; separate scoped features with numeric/pixel and rollback criteria |
| Resource/native renderer work | Version resource caches, prove invalidation/readback, define native shader ABI and translate the proven subset into GX2 | M4 #28/#30; retain software reference and explicit shader/cache misses |
| Physical evidence and performance | Retain completed 0.24.1 arithmetic, picture-gating, repeat-draw and launcher evidence; optimize measured native scale/publication cost | #87/#20, M1 #13–#16, M3 #22/#26 and M4 #31; broader Espresso, stability/recovery and full renderer criteria remain open |
| Title intake and analysis | Retain verified executable/report/sample provenance; validate required nested resources and refine the observed runtime/test-route matrix | M5 #32/#33 are active analysis; selected Yaz0/NARC/DVLB/CGFX/BYML/CSAR/CSTM structures are inspected; actual runtime selection/use remains pending |
| Asset-informed shared work | Extend shader code capacity/arithmetic/control safely and add original DSP ADPCM/container probes | M4 #28 and M3 #25; static sampled resources justify scoped implementation, while runtime/console use needs separate evidence |
| Required game runtime | Implement observed CPU, services, dynamic modules, DSP/audio, saves and rendering requirements | M2/M3/M4 and M5 #34–#36; original probes support shared interfaces, then real title evidence |
| Conversion engine | Connect proven stages on clean Windows with cancellation, traces, revision caches, stable identities, rebuild and safe diagnostics | M6 #37–#41; structural package checks stay separate from installation/launch |
| Final GUI | Implement the responsive WPF Inject page, private setup help, saved projects and packaged prerequisites over the proven engine | M7 #42–#46; GUI-to-playable-WUP acceptance completes the MVP |

The user is available for focused physical tests with Windows 11 and Aroma.
October 13 audio/GX/draw results are retained. October 15 results confirm the new
arithmetic and first draw route while identifying native scale/publication as the
dominant measured cost. Continue original renderer/runtime
and host development alongside targeted hardware follow-ups. Keep exact blocked acceptance
visible; do not treat a host pass as a console result or ask for repeated tests of
unchanged completed routes. Priorities may change when actual title requirements
or measured bottlenecks become available.

## Execution order and gates

1. **P1 — Translation and packaging foundations:** extend exercised CPU/ABI/
   fallback correctness and measure PowerPC costs. Resolve original-homebrew WUP
   installer/metadata/certificate prerequisites and prove Menu launch/exit.
   Host/emulated proofs already exist; physical acceptance remains open.
2. **P2 — Exact title analysis:** identify the owned revision, code/assets/modules,
   observed service/GPU demands and a repeatable gameplay/save route. Enable a
   profile only after actual executable identity and support evidence are recorded.
3. **P3 — Shared runtime and renderer:** implement the required reusable interfaces,
   shader/fragment/resource paths, modules, guest audio and retail saves. Advance
   the selected game through boot, menu, gameplay and save/reload; measure accuracy,
   timing, memory and stability on Wii U. Independent original probes can proceed now.
4. **P4 — Automated Windows engine:** connect optional decryption, profile, extraction,
   translation, compile, shader/cache setup, stage, metadata/title ID, pack, validate
   and publish. Prove cancellation, trace import, revision-aware rebuild, save
   continuity and clean-Windows conversion without hand edits. Complete M6 gates.
5. **P5 — Windows injector:** implement the specified WPF workflow over P4, provision
   actual prerequisites and prove a normal user can Inject, install and play the
   supported title. Complete M7 and the first MVP; broader compatibility follows.

Packaging and translator portability are independent streams. Resolve installation
prerequisites early while runtime work continues. The GUI contract is specified
now; GUI implementation follows the demonstrated conversion pipeline.

## Tracking and visible development

[Project #2](https://github.com/users/Jdiller-86/projects/2) owns live status. Each
change belongs to a scoped feature issue with native parent task, M1–M7 repository
milestone and linked PR. The 35 gate tasks have native milestone parents. The
reviewed private feature catalog backfills historical PRs and defines renderer
follow-ups; [development tracking](development-tracking.md) explains the workflow.

Use focused branches, attributable commits and early pushed draft PRs. Required
CI and documentation checks precede merge; completed remote/local branches are
removed and verified. Trusted-main Actions perform management with the Actions
credential and retain live audit receipts. A merge completes only its scoped
feature criteria, never a full hardware/title/Windows gate by inference.

## Reuse, privacy and feasibility

The pinned external host lifter has a checked portable ABI and original runtime
adapter. Complete redistribution notices before bundling the external tool or ABI
support. Adopt CPU/HLE components only after license and Wii U suitability review;
a wholesale Rust/Vulkan desktop emulator port is outside the selected architecture.
External emulator/reference code remains in separate test checkouts.

The original scoped packer/decrypting verifier exists. Earlier nuspacker/
CNUS_Packer candidates lacked an identified license grant and established
certificate-template provenance; no such implementation or material is bundled.
Synthetic package correctness does not establish authenticity, installer policy
or physical Menu launch. Local packaging requirements remain explicit.

ROMs, keys, assets, translated commercial code, saves, traces and per-game packages
stay private, with redacted diagnostics and user-managed local setup. Optimize
measured bottlenecks and document failed performance gates before widening scope.
Test counts and upstream desktop compatibility cannot replace product acceptance.

See [milestones](milestones.md) for full exit criteria, [repository tour](repository-guide.md)
for source layout and [development history](development-history.md) for dated checkpoints.
