# Milestones and remaining work

Updated **October 14, 2026** for **v0.24.0 source (unreleased)**. The product is an UWUVCI-style
Windows GUI: **Select owned 3DS ROM → configure title/artwork → Inject →
install its WUP → play from the Wii U Menu**. The CLI is the shared engine.
The USA Super Mario 3D Land executable/revision is privately verified; reported local asset inventory is matched; selected byte formats,
observed runtime requirements and commercial compatibility remain unverified.

**Approximately 20% implemented; 0/7 full milestones accepted.** The estimate is
a rough engineering judgment, not a percentage of issues, tests or commits. These
**35 gate work items** make the work actionable; creating or closing an issue
does not automatically accept a milestone or raise the completion estimate.

The [project brief](project-brief.md) defines requirements, the
[product plan](development-plan.md) defines execution order, and
[STATUS.md](../STATUS.md) records current results. Historical version details and
receipts remain in [development history](development-history.md).

## How to use the roadmap

The existing seven GitHub milestone issues remain overview trackers. Each work
item below is a separate implementation or evidence task feeding its milestone.
[Project #2](https://github.com/users/Jdiller-86/projects/2) is the authority for
status, blockers and reviewed full acceptance. [milestone-status.json](milestone-status.json)
records versioned scope/evidence and initial seed values; it cannot overwrite
existing Project values. The states below are a dated evidence snapshot. Manual
issue notes and acceptance checkmarks remain reviewable. Native feature/PR links
and the one-time empty/Todo status migration are documented in
[development tracking](development-tracking.md); later owner edits remain authoritative.

| State | Meaning |
|---|---|
| Planned | Defined remaining work; implementation has not begun. |
| In progress | Partial implementation exists; acceptance remains open. |
| Awaiting evidence | The scoped implementation/test artifact exists, but the required physical or user evidence is missing. |
| Blocked | An identified input or acceptance prerequisite prevents completing this task. |

`needs-console`, `needs-input` and `needs-windows` identify required evidence or
resources, independently of the work state. Dependencies describe prerequisites
for **accepting** a task; independent original fixtures, interface review and
preparation may proceed while prerequisites remain open. A task is complete only
when its stated criteria have evidence. Milestone closure requires its full exit
criteria and reviewed console/Windows/title evidence, never an automatic checklist.

Commercial ROMs, code, assets, saves, keys, traces and packages stay in the
documented private workflow. GitHub issue evidence uses original fixtures or
redacted reports; never attach a game or key. Desktop/sanitizer/emulated-PowerPC
passes establish their stated boundaries, not physical-console execution.

## Milestone overview

| Milestone | State | Remaining acceptance |
|---|---|---|
| [M1: Native Wii U platform](#m1) | Awaiting evidence | Platform lifecycle/recovery, stability and installed original WUP evidence |
| [M2: CPU translation and performance](#m2) | In progress | Exercised CPU/ABI/fallback coverage and actual Espresso timing |
| [M3: Homebrew execution runtime](#m3) | In progress | Stable physical compiled guest plus broader services/modules/audio |
| [M4: PICA to GX2 rendering](#m4) | In progress | Real PICA/GX2 draws/shaders/TEV/coherency and measured scenes |
| [M5: Super Mario 3D Land revision](#m5) | In progress | Broader required resources, observed runtime requirements and Mario gameplay/audio/save/performance |
| [M6: Automated ROM to WUP conversion](#m6) | In progress | Full Windows conversion, installer acceptance and rebuild/save continuity |
| [M7: Windows GUI injector](#m7) | Planned | Responsive packaged Windows GUI Inject-to-playable-WUP workflow |

The implementation sequence is [P1–P5 in the product plan](development-plan.md):
resolve portable translation and original WUP packaging early; identify the
selected revision; implement its execution/renderer requirements; prove Windows
conversion; then implement the GUI. Milestones track acceptance boundaries,
so independent work can overlap without claiming downstream completion.

<a id="m1"></a>

## M1: Native Wii U platform

**State: Awaiting evidence. Full acceptance: open.**

**Entry:** Versioned original Native/guest diagnostics build with WUT/devkitPPC, and the tester can run them under the documented Wii U environment. A diagnostic RPX/WUHB is distinct from an installed WUP.

**Deliverables:** A reproducible native platform build, checked result/save paths, a short guided console kit, an installed original diagnostic and a versioned platform acceptance record.

**Exit criteria:** Physical evidence establishes both screens, required controls/touch, native audio, reliable launch/exit, SD persistence/recovery and a documented stability session. An original WUP installs and launches/exits from the Wii U Menu.

**Verified desktop/earlier evidence:** Older native reports and the confirmed Minus exit are retained. The user reports Native/Guarded/Compiled completed without errors on v0.14.5; supplied text logs have older headers. A valid visit-1 snapshot proves initial persistence, while a later Save update failed and no new reports appeared. Current independent TXT read-back and save publication/recovery have desktop/sanitizer checks, not new console acceptance.

**Reviewed physical follow-up:** Named TXT collection, original fresh-launch save reload, embedded/SD assets and GX transfers pass their scoped checks; pictures and Minus return have user confirmation. October 13 Audio Check/GX reports and user confirmation verify the launch-music regression; full lifecycle/recovery and installed-title acceptance remain open.

**Still needs external evidence/input:** Focused audio regression, exact firmware/Aroma/installer versions and native/installed lifecycle/recovery evidence require the Wii U. No game dump is needed for the original diagnostics.

References: [Console evidence](console-evidence.md) · [Short follow-up](console-retest.md) · [Guided tests](guided-testing.md) · [Console guide](console-testing.md).

### Actionable work items

<a id="m1-results"></a>

#### m1-results: Verify current TXT result collection on Wii U

Current Save/RomFS/SD-asset/GX named reports are physically verified, including fresh-launch save reload. Retain older failed/missing reports separately; their exact original cause remains unknown. Project review records this scoped collection task independently of M1 acceptance.

State: **Awaiting evidence** · Needs: `needs-console`.

Depends on: none.

Acceptance:

- [ ] A fresh launch creates a uniquely named, closed-file read-back-verified TXT report in the documented SD results directory; its header identifies the tested build and diagnostic.
- [ ] Upload the requested TXT files and preserved snapshot/siblings through the private intake route; retain automatic observations, user confirmations, failures and skips separately.

<a id="m1-lifecycle"></a>

#### m1-lifecycle: Verify repeat launch, exit and SD publication recovery

Exercise the native application lifecycle and result/save persistence without discarding the earlier Minus-exit confirmation.

State: **Awaiting evidence** · Needs: `needs-console`.

Depends on: [m1-results](#m1-results).

Acceptance:

- [ ] A documented repeat-launch and Minus/Home/APT exit sequence returns to the Wii U Menu without a hang; record exact firmware, Aroma, launch route and build.
- [ ] Save/relaunch retains the expected bytes, and any observed publication/recovery failure preserves readable previous data or reports an actionable error; record SD files while retaining the original failure evidence.

<a id="m1-controls-audio"></a>

#### m1-controls-audio: Record native display, controls and audio acceptance

Retain older native/control evidence and fill only the missing checks needed for current platform acceptance.

State: **Awaiting evidence** · Needs: `needs-console`.

Depends on: none.

Acceptance:

- [ ] The native guide records TV/GamePad mapping, buttons, sticks, touch and the original native tone with observed results and build identity.
- [ ] Any changed display, input, audio or lifecycle path has a focused console regression result; earlier successful checks are cited instead of repeating a long guide solely for a version bump.

<a id="m1-package-launch"></a>

#### m1-package-launch: Verify an original diagnostic as an installed Wii U title

Use the validated original-homebrew WUP smoke package to prove installation and Wii U Menu launch rather than inferring them from RPX/WUHB launch.

State: **Blocked** · Needs: `needs-console`.

Depends on: [m6-original-wup](#m6-original-wup).

Acceptance:

- [ ] The original diagnostic installs with the recorded homebrew/installer environment and launches from the Wii U Menu with its expected title identity and screens.
- [ ] The installed diagnostic exits normally and can be relaunched; retain the versioned build/package report and installation observations without exposing local packaging material.

<a id="m1-platform-acceptance"></a>

#### m1-platform-acceptance: Review and accept the native Wii U platform gate

Collect the scoped native platform results into one reproducible acceptance record.

State: **Blocked** · Needs: `needs-console`.

Depends on: [m1-results](#m1-results), [m1-lifecycle](#m1-lifecycle), [m1-controls-audio](#m1-controls-audio), [m1-package-launch](#m1-package-launch).

Acceptance:

- [ ] The current result, lifecycle, controls/audio and installed-diagnostic tasks have reviewable evidence and no unexplained hang or persistence failure.
- [ ] A versioned platform report states the tested firmware/Aroma/installer, supported launch routes and stability session; M1 remains open until this evidence is reviewed.

<a id="m2"></a>

## M2: CPU translation and performance

**State: In progress. Full acceptance: open.**

**Entry:** The audited external lifter generates C through a checked portable ABI; original interpreter/AOT/reference fixtures and an emulated big-endian PowerPC correctness route are available.

**Deliverables:** Documented exercised ARM/Thumb/VFP support, checked generated-code/memory behavior, observable fallback and private trace handling, plus actual Espresso timing/memory reports.

**Exit criteria:** Original exercised workloads have independently supported state/fault parity, explicit unsupported behavior and reviewable real Wii U performance limits. The selected title must separately demonstrate useful gameplay performance in M5.

**Verified desktop/earlier evidence:** Current original CPU/reference and external AOT checks cover a bounded subset, eight single-core contexts, scoped synchronization and independent ARM1176 cases. GCC/Clang sanitizers and emulated big-endian PowerPC are supporting correctness evidence. No actual Espresso benchmark or full ARM/VFP coverage is claimed.

**Still needs external evidence/input:** Original benchmarks can proceed without a ROM. Real timing requires the Wii U; title-driven coverage and fallback analysis need the private exact-revision executable/trace.

References: [CPU subset](cpu-subset.md) · [Stack semantics](cpu-stack.md) · [External lifter ABI](upstream-integration.md) · [Threads](guest-threads.md).

### Actionable work items

<a id="m2-instruction-coverage"></a>

#### m2-instruction-coverage: Complete exercised ARM, Thumb and VFP correctness

Close concrete CPU gaps with independent references and original fixtures, preserving visible errors for unsupported behavior.

State: **In progress** · Needs: none for original-fixture development.

Depends on: none.

Acceptance:

- [ ] Every supported new instruction/exception/exclusive/VFP case has independently checked register, flags, memory and fault results through the relevant interpreter/AOT paths.
- [ ] Supported scope, remaining gaps and reference-model limits are documented; no unsupported instruction is accepted by a fabricated success result.

<a id="m2-abi-memory"></a>

#### m2-abi-memory: Complete the generated-code ABI and checked memory boundary

Measure and tighten generated C integration, little-endian guest access, resume state and code/data ownership on the PowerPC target.

State: **In progress** · Needs: none for original-fixture development.

Depends on: none.

Acceptance:

- [ ] Original generated-code fixtures match interpreter/reference state across calls, SVC/resume, threading and partial-fault boundaries on host and emulated big-endian PowerPC.
- [ ] Release-build code size, memory/RSS, build cost and checked-access cost are recorded; optimizations preserve bounds, permissions and executable-store invalidation.

<a id="m2-fallback-tracing"></a>

#### m2-fallback-tracing: Implement observable fallback and missing-block trace handling

Provide a correct, bounded recovery route for supported AOT misses and an actionable stop for unavailable execution states.

State: **In progress** · Needs: none for original-fixture development.

Depends on: [m2-instruction-coverage](#m2-instruction-coverage), [m2-abi-memory](#m2-abi-memory).

Acceptance:

- [ ] An original missed-block/dynamic-dispatch fixture either executes through a verified supported fallback with state parity or fails explicitly before corrupting state.
- [ ] Reports separate AOT instructions, fallback time/counts and unsupported locations; trace import validates input identity and stays private for user-derived code.

<a id="m2-espresso-benchmarks"></a>

#### m2-espresso-benchmarks: Measure real Espresso execution and memory costs

Run representative original CPU/AOT workloads on physical Wii U before promising useful game performance.

State: **Planned** · Needs: `needs-console`.

Depends on: [m2-abi-memory](#m2-abi-memory), [m2-fallback-tracing](#m2-fallback-tracing).

Acceptance:

- [ ] A versioned console benchmark records actual CPU/AOT/fallback, checked-memory and scheduling costs, build flags, memory peak and workload identity.
- [ ] The report distinguishes emulated PowerPC correctness from actual Espresso timing and identifies measured bottlenecks for targeted optimization.

<a id="m2-cpu-acceptance"></a>

#### m2-cpu-acceptance: Review CPU correctness, fallback and performance acceptance

Close the CPU milestone only against the exercised execution requirements and physical timing evidence.

State: **Blocked** · Needs: `needs-console`.

Depends on: [m2-instruction-coverage](#m2-instruction-coverage), [m2-abi-memory](#m2-abi-memory), [m2-fallback-tracing](#m2-fallback-tracing), [m2-espresso-benchmarks](#m2-espresso-benchmarks).

Acceptance:

- [ ] Instruction/ABI/fallback evidence establishes the documented supported workload without unexplained state mismatches or silent unsupported execution.
- [ ] Actual Wii U timing and memory reports support a stated feasibility limit; useful selected-title gameplay performance is subsequently confirmed by the title gate.

<a id="m3"></a>

## M3: Homebrew execution runtime

**State: In progress. Full acceptance: open.**

**Entry:** Original normal-CRT/devkitARM/libctru applications execute through the portable runtime on desktop. Their required guest memory, scheduler, service and display contracts are defined.

**Deliverables:** Stable compiled guest execution with checked kernel/IPC/lifecycle/display/input/filesystem contracts; focused module and guest-audio fixtures feed later selected-title integration.

**Exit criteria:** A real compiled guest executes on Wii U, updates both screens, responds to required GamePad input/touch and exits or completes a documented stability session. Exercised save/assets behavior is verified. This fixture gate does not require claiming all retail services or PICA draw support.

**Verified desktop/earlier evidence:** Nine original compiled applications cover boot, threads, input, graphics, interactive execution, save/reload, RomFS, GX transfers and bounded PICA draws. Desktop/sanitizer and selected emulated PowerPC checks support exact pixels/state/files. Reviewed physical Save/results/assets/GX evidence is retained; October 13 compiled draw and launch-audio results are reviewed, with a 57.33-second draw-scene delay and premature visual-prompt guard requiring follow-up; a native tone does not establish guest DSP/audio.

**Still needs external evidence/input:** Original guest modules, audio probes and shared service work can proceed independently. Hardware validates display/input/audio/lifecycle/assets; observed retail service/module/save requirements need M5 intake.

References: [Compiled applications](compiled-homebrew.md) · [Services](shared-services.md) · [Filesystem](filesystem.md) · [RomFS assets](romfs-assets.md).

### Actionable work items

<a id="m3-homebrew-console"></a>

#### m3-homebrew-console: Verify the normal-CRT homebrew suite on Wii U

Collect focused physical evidence for the original compiled applications, especially Save, RomFS/Asset Files and GX Transfers.

State: **Awaiting evidence** · Needs: `needs-console`.

Depends on: [m1-results](#m1-results).

Acceptance:

- [ ] Current compiled guests update both screens, respond to the required input/touch, complete their documented exit and retain their versioned observations.
- [ ] Save fresh-process reload, nested/Unicode read-only assets and GX transfer pictures/results match the documented original fixtures; unresolved errors are investigated instead of marked passed.

<a id="m3-service-contracts"></a>

#### m3-service-contracts: Extend exercised kernel, IPC and application contracts

Advance the shared scheduler/memory/SRV/APT/CFG/HID/GSP/FS contracts using original applications and later observed title requirements.

State: **In progress** · Needs: none for original-fixture development.

Depends on: none.

Acceptance:

- [ ] Each added service or kernel behavior has checked request shapes, ownership, scheduling/teardown and failure cases backed by an original fixture.
- [ ] Unsupported applets, cross-process semantics and privileged/optional services stay explicit; generic success is used only where its observable behavior is justified.

<a id="m3-module-loading"></a>

#### m3-module-loading: Implement checked dynamic-module loading and dispatch

Add the portable CRO/CRS loading, relocation and translated-module boundary required for executable integration.

State: **Planned** · Needs: none for original-fixture development.

Depends on: [m2-abi-memory](#m2-abi-memory), [m3-service-contracts](#m3-service-contracts).

Acceptance:

- [ ] An original module fixture loads, relocates, calls and unloads through interpreter/AOT with checked ranges, symbols and code ownership; malformed relocation inputs fail visibly.
- [ ] Supported module forms and dispatch/ownership limits are documented; unsupported forms fail visibly, while actual title module requirements are subsequently established in the private revision matrix.

<a id="m3-guest-audio"></a>

#### m3-guest-audio: Build the guest DSP/audio path with original probes

Separate native diagnostic tone evidence from the unimplemented guest audio protocol and Wii U output pipeline.

State: **Planned** · Needs: `needs-console`.

Depends on: [m3-service-contracts](#m3-service-contracts).

Acceptance:

- [ ] Original guest audio fixtures exercise the implemented DSP/service path with checked buffers/voices and independently verified sample/timing results.
- [ ] Wii U output, synchronization, pause/exit and resource cleanup have focused console observations; unsupported audio commands fail visibly.

<a id="m3-runtime-acceptance"></a>

#### m3-runtime-acceptance: Accept a stable compiled guest runtime on Wii U

Prove the milestone fixture really executes guest code with the documented kernel/services, display and input on physical hardware.

State: **Blocked** · Needs: `needs-console`.

Depends on: [m3-homebrew-console](#m3-homebrew-console), [m3-service-contracts](#m3-service-contracts), [m1-lifecycle](#m1-lifecycle).

Acceptance:

- [ ] A versioned normal-CRT guest demonstrates guest execution, two-screen updates, input/touch, filesystem/assets as exercised and clean exit or a recorded stability session on Wii U.
- [ ] Its CPU/service/fallback requirements and known limits are documented; passing the fixture does not claim retail services, PICA draws or Mario compatibility.

<a id="m4"></a>

## M4: PICA to GX2 rendering

**State: In progress. Full acceptance: open.**

**Entry:** Guest framebuffer presentation and bounded GSP/GX operations work on original fixtures. Independent original scenes/reference primitives are available for each new PICA feature.

**Deliverables:** Real PICA draw/state decoding, vertex/shader and fragment translation, checked GPU resources/coherency, explicit shader/state cache behavior and original reference scene/timing evidence.

**Exit criteria:** Exercised fixture scenes compare to a trusted reference using stated tolerances and visual checks. Physical Wii U measurements include CPU submission, GPU work, transfers, waits and memory. Unsupported renderer features remain visible.

**Verified desktop/earlier evidence:** Five rotated framebuffer formats, all 14 base texture formats and scoped DMA/fill/display conversion/raw-copy/cache operations have original/reference checks. The stock-libctru transfer fixture has exact desktop frames and zero fallback. These CPU-backed transfers and decoder checks do not implement PICA draws or prove GX2 scene correctness.

**Still needs external evidence/input:** Original PICA/reference scenes and license review can progress without a ROM. Real GX2 images/timing need a Wii U; the selected revision determines additional lighting/LUT/geometry/procedural or readback requirements.

References: [Renderer status](renderer-status.md) · [GX transfer contract](gx-transfers.md) · [Display](gsp-display.md) · [GSP queues](gsp-queues.md).

### Actionable work items

<a id="m4-pica-commands"></a>

#### m4-pica-commands: Decode PICA state and submit bounded draw commands

The standard guest path handles configuration and bounded owned-resource draws through canonical tiled targets, retained shader/state and transactional completion. An original compiled guest verifies exact software scenes; broader pipeline state and native GPU translation remain open.

State: **Done (bounded command acceptance reviewed)** · Needs: none for original-fixture development.

Depends on: none.

Verified foundation: [PICA command contract](pica-commands.md), 353 original builder/reference cases, transactional native state and the compiled standard command-queue fixture. No draws or full task acceptance are claimed.

v0.23.0 connects [compiled guest draws](guest-pica-draws.md) to the standard GSP queue with owned resources, exact target/display checks and preserved failure state. October 13 physical own PASS/exit and user picture confirmation are reviewed; rendered presentation at 57.33 seconds and premature guide timing need a focused follow-up.

v0.22.0 adds [owned software scenes](software-scenes.md) with complete-list
state/pixel rollback, explicit resource binding and independent exact pixels.
These execute draws in the experimental boundary; the guest queue remains
configuration-only. Full task acceptance remains open.

Acceptance:

- [x] Original command streams establish register/state changes, draw inputs and validated resource ranges; malformed or unsupported state fails visibly before modifying output.
- [x] The decoder feeds a documented renderer boundary, with reference traces that distinguish CPU-written framebuffer presentation from actual PICA draws.

Reviewed implementation: [PR #48](https://github.com/Jdiller-86/CTR-2-WUP/pull/48)
and [PR #57](https://github.com/Jdiller-86/CTR-2-WUP/pull/57). Their original
configuration/draw traces and exact output satisfy both criteria. Task #27 closes
through the recorded Actions review; broader rendering and full M4 remain open.

<a id="m4-vertex-shaders"></a>

#### m4-vertex-shaders: Implement vertex formats, shader execution and GX2 translation

Merged scoped #97/PR #98 expands vertex code capacity to 512 checked words
with original entry/upload/rollback probes and native storage/stack measurements.
Required hosted platform checks pass; Project membership and native links verify; broader arithmetic,
control flow, GX2 translation and full task acceptance remain open. See
[the capacity contract](software-scenes.md#vertex-code-capacity).

Scoped #76/PR #105 adds [exact ADD/MUL](software-scenes.md#exact-add-and-mul),
independent rational/encoding/pixel checks and late guest state/queue rollback.
Required full/platform checks gate PR #105; rounding, control flow, native GX2,
physical measured costs and full task acceptance remain open.

Choose a license-cleared Latte shader route and validate exercised vertex attributes, uniforms and shader instructions.

State: **In progress** · Needs: none for original-fixture development.

Depends on: [m4-pica-commands](#m4-pica-commands).

Verified foundation: [resolved vertex decoding](vertex-attributes.md) now covers
both contiguous streams and checked 8-bit/16-bit indexed gathers. The original
suite covers every index value and transactional failure behavior. The first
owned scenes now exercise loaders/fixed state, uniforms, MOV/NOP/END shaders,
output maps and opaque triangles. Independent analytic pixels and 12,290 pinned
encoding comparisons pass, and a focused Wii U diagnostic builds. Broader shader
behavior, hardware/reference raster evidence, physical measured costs and a
license-cleared GX2 shader route remain open; this checkpoint does not accept the task.

Acceptance:

- [ ] Original vertex/shader scenes compare independently checked outputs across the reference and chosen GX2/software correctness path using stated tolerances.
- [ ] Shader tooling licenses, generated-code ownership, supported instructions and miss behavior are documented; a software correctness path has measured costs rather than an assumed final speed.

<a id="m4-fragment-pipeline"></a>

#### m4-fragment-pipeline: Implement textures, samplers, TEV and raster state

Connect the verified base texture decoders to real draws and the fragment behavior required by reference scenes and the selected title.

State: **Planned** · Needs: none for original-fixture development.

Depends on: [m4-vertex-shaders](#m4-vertex-shaders).

Acceptance:

- [ ] Original scenes cover exercised texture layouts/samplers, TEV stages, blend, depth, stencil and alpha tests with reference images and stated comparison tolerances.
- [ ] Required lighting/LUT/geometry/procedural behavior is added from observed requirements or remains explicitly unsupported; texture-decoder tests alone do not claim renderer acceptance.

<a id="m4-coherency-cache"></a>

#### m4-coherency-cache: Implement GPU resource coherency and versioned shader/state caches

Handle render-to-texture, aliases, completion/readback and CPU/GPU ownership while making shader/cache misses explicit.

State: **Planned** · Needs: none for original-fixture development.

Depends on: [m4-pica-commands](#m4-pica-commands), [m4-fragment-pipeline](#m4-fragment-pipeline).

Acceptance:

- [ ] Original resource scenes prove guest-visible writes/readbacks, alias invalidation and synchronization ordering without stale data or unchecked GPU access.
- [ ] Cache identity includes relevant code/runtime/renderer/settings inputs; missing or invalid state follows a supported translation/fallback or diagnostic failure, never silent incorrect rendering.

<a id="m4-console-scenes"></a>

#### m4-console-scenes: Compare rendered scenes and measure Wii U graphics costs

Retain reviewed bounded compiled draw results; correct premature visual gating and measure execution/scaling/GPU/wait/memory costs separately before full graphics or title acceptance.

State: **Blocked** · Needs: `needs-console`.

Depends on: [m4-vertex-shaders](#m4-vertex-shaders), [m4-fragment-pipeline](#m4-fragment-pipeline), [m4-coherency-cache](#m4-coherency-cache).

Acceptance:

- [ ] Physical Wii U images match the defined scene tolerances and required visual checks; build/reference identity and unsupported features accompany the results.
- [ ] CPU submission, GPU work, transfers, waits, memory and frame-time distribution are measured on Wii U; selected-title scenes are later validated through M5.

<a id="m5"></a>

## M5: Super Mario 3D Land revision

**Current private intake:** a derived USA v2 executable has independent stored
hash/decompression/revision verification and successful pinned static analysis.
The newly received local preparation/asset reports match the executable and
report 1,756 outer files with identical IVFC evidence. Seven selected payloads
now match all manifest/receipt hashes and are independently inspected here.
Yaz0/NARC/DVLB/CGFX/BYML and CSAR/CSTM structures inform active shared shader and
audio work; broader nested resources and observed use remain open. Declared
service access is not traced use. The original full container hash is reported,
not independently verified here. No commercial profile or full gate is accepted.
See [first-title findings](first-title.md).

**State: In progress. Full acceptance: open.**

The explicit `mario-input-received-v023` review applied and read back successfully
in [run 38605081338](https://github.com/Jdiller-86/CTR-2-WUP/actions/runs/38605081338).
M5/intake/requirements are In progress; the resolved owned-input wait was removed
from six targets while console waits and owner notes were preserved. Mirrored
labels and the [canonical live dashboard](https://jdiller-86.github.io/CTR-2-WUP/progress/)
agree. No source edit alone changes the board, and no full gate is accepted.

**Entry:** The user supplies a privately prepared executable and required assets for an owned exact Old 3DS revision. The uploaded USA Mario executable/revision is verified; local asset provenance reports are now matched, and seven selected payloads now have independent private checks. Original full-container integrity is still reported local evidence.

**Deliverables:** An exact-hash revision profile, observed feature/test-route matrix, required shared runtime/renderer/audio/retail-save integration and a measured supported-title console acceptance report.

**Exit criteria:** That exact revision cold-boots, reaches its menu, plays the agreed course with required input/touch and synchronized audio, saves/reloads, and completes a recorded stability session at useful intended-speed performance on Wii U.

**Historical intake evidence:** The original CIA attachment exceeds the workspace attachment transfer limit. The received small intake reports encrypted executable content and contains no executable; it supplies no observed game behavior. Local preparation and original-fixture proofs establish tooling only. Upstream source observations are leads, not reproduced CTR2WUP compatibility.

**Still needs external evidence/input:** The executable is available for private analysis; analysis can continue with the received assets. Full acceptance still requires observed runtime requirements, any specifically required additional resources and later console gameplay. Keys remain local; no commercial code, assets or traces are posted in GitHub issues or public artifacts. Actual Wii U gameplay evidence follows implementation.

References: [First-title intake](first-title.md) · [CIA helper](cia-intake.md) · [Local encrypted input](encrypted-input.md) · [Compatibility](compatibility.md).

### Actionable work items

<a id="m5-intake"></a>

#### m5-intake: Identify the owned Super Mario 3D Land executable and assets

Retain the verified private USA executable, matched local reports and seven independently checked sample payloads; validate required broader resources and preserve the full-container integrity boundary.

State: **In progress** · Needs: none for current private analysis.

Depends on: none.

Acceptance:

- [ ] The actual NCCH/container metadata and input/executable hashes establish the exact title, region and revision; filename claims are not treated as verified identity.
- [ ] Required RomFS/modules/assets are available through the documented private workflow; keys stay local and no commercial payload is placed in issues, source or public artifacts.

<a id="m5-requirements"></a>

#### m5-requirements: Create the exact-revision feature and test-route matrix

Refine the matrix from verified sample structures, then trace actual required execution. Shader capacity/arithmetic/control and stereo DSP ADPCM have concrete static evidence; declared access and static resources stay separate from observed use.

State: **In progress** · Needs: none for current private analysis.

Depends on: [m5-intake](#m5-intake).

Acceptance:

- [ ] A private analysis records code/module layout, actual unsupported CPU/IPC/GPU/audio/save behavior and the distinction between declared services and observed use.
- [ ] A hash-matched profile and short repeatable boot/menu/course/save route are documented with explicit support gaps; upstream desktop claims do not activate the profile.

<a id="m5-runtime-integration"></a>

#### m5-runtime-integration: Implement the selected revision requirements

Connect the private title to the shared CPU/runtime/renderer and implement only its evidenced missing behaviors.

State: **Blocked** · Needs: implementation dependencies, rather than missing owned input.

Depends on: [m5-requirements](#m5-requirements), [m2-instruction-coverage](#m2-instruction-coverage), [m3-service-contracts](#m3-service-contracts), [m3-module-loading](#m3-module-loading), [m3-guest-audio](#m3-guest-audio), [m4-fragment-pipeline](#m4-fragment-pipeline), [m4-coherency-cache](#m4-coherency-cache).

Acceptance:

- [ ] Observed requirements drive CPU/services/applets/CFG-Mii identity, modules, RomFS, guest audio, retail saves/extdata and PICA integration with targeted original regression fixtures.
- [ ] The exact-revision build progresses through recorded boot/menu/gameplay stages without unjustified stub success; prefer reusable shared fixes, and keep any necessary title-specific adjustment narrow, hash-matched and documented.

<a id="m5-gameplay-route"></a>

#### m5-gameplay-route: Run Mario boot, gameplay, audio and save/reload on Wii U

Execute the agreed short route on a manually staged per-game build before automating the finished injection.

State: **Blocked** · Needs: `needs-console` (after implementation dependencies).

Depends on: [m5-runtime-integration](#m5-runtime-integration), [m4-console-scenes](#m4-console-scenes), [m6-original-wup](#m6-original-wup).

Acceptance:

- [ ] Physical evidence covers cold boot, menu, interactive course movement/jump and required touch, synchronized audio, save, exit and fresh-launch reload.
- [ ] Record exact revision/build/environment, graphics/audio faults, frame-time distribution, memory peak and fallback time while preserving intended guest speed.

<a id="m5-title-acceptance"></a>

#### m5-title-acceptance: Review supported-title accuracy, stability and performance

Enable a verified commercial profile only after review of the exact-revision console route and its measured limits.

State: **Blocked** · Needs: `needs-console` (after implementation dependencies).

Depends on: [m5-gameplay-route](#m5-gameplay-route), [m2-espresso-benchmarks](#m2-espresso-benchmarks).

Acceptance:

- [ ] The recorded gameplay/save route and agreed stability session pass with documented graphical/audio limitations, intended timing and useful measured performance.
- [ ] The profile identifies exactly the tested hashes/settings/environment and unresolved limitations; unavailable revisions and arbitrary-ROM compatibility remain unsupported.

<a id="m6"></a>

## M6: Automated ROM to WUP conversion

**State: In progress. Full acceptance: open.**

**Entry:** Original AOT/Wii U staging and structural packaging work; the selected-game runtime and required assets become available through M5. Resolve real packaging/installer requirements early, independently of commercial execution.

**Deliverables:** One checked shared ROM-to-WUP engine with safe stable title IDs, local preparation, actual translation/build/assets/cache/staging/packing, atomic output, cancellation/rebuild/trace support and a clean-Windows acceptance report.

**Exit criteria:** A clean native Windows machine converts the supported revision without manual source edits into a WUP that installs and Menu-launches on Wii U. Rebuild/update preserves saves, titles keep separate identities, and user-directed uninstall behavior is documented and verified.

**Verified desktop/earlier evidence:** Original encrypted input reaches actual external AOT and native cross-built diagnostics. The scoped writer and independent decrypt/FST/hash/file verifier pass with synthetic credentials. Hosted Windows host/portable-C and deep real AOT/Unicode/.NET 10 WPF tooling checks passed. They do not prove actual local credential provenance, installer acceptance or full commercial native Windows conversion.

**Still needs external evidence/input:** Real locally managed packaging prerequisites and console installation are still required. Commercial stage integration needs M5 code/assets; clean native Windows full-conversion and continuity checks are separate from the already passed tooling CI.

References: [Packaging boundaries](packaging.md) · [Windows build](build-windows.md) · [Encrypted preparation](encrypted-input.md) · [Host architecture](architecture.md).

### Actionable work items

<a id="m6-original-wup"></a>

#### m6-original-wup: Validate original-homebrew WUP packaging and installer prerequisites

Take the original scoped writer/verifier beyond synthetic structure checks using documented local material and a smoke-test diagnostic.

State: **Blocked** · Needs: `needs-input`, `needs-console`.

Depends on: none.

Acceptance:

- [ ] The original diagnostic WUP has consistent collision-checked nonreserved identity, original metadata/artwork, verified content integrity and documented local packaging provenance/installer requirements.
- [ ] The supported installer accepts the actual package on Wii U; retain the package/build report and observations privately where needed, distinguishing integrity from signature policy and install success.

<a id="m6-conversion-pipeline"></a>

#### m6-conversion-pipeline: Connect the real selected-game ROM-to-WUP stages

Complete the shared engine sequence from supported input through AOT/runtime/assets/shaders/staging/packing/validation to published output.

State: **In progress** · Needs: `needs-input`.

Depends on: [m5-runtime-integration](#m5-runtime-integration), [m6-original-wup](#m6-original-wup).

Acceptance:

- [ ] The exact supported revision completes inspect/profile/extract/translate/compile/cache/link/stage/metadata/pack/validate with machine-readable reports and nonzero errors for failed stages.
- [ ] Output is published atomically only after identity/content checks; an unsupported game or experimental gap cannot be reported as a finished playable WUP.

<a id="m6-rebuild-cache"></a>

#### m6-rebuild-cache: Finish cancellation, trace import and revision-aware rebuilds

Make interrupted/repeated conversion reliable while keeping per-game identities and saves stable.

State: **In progress** · Needs: none for original-fixture development.

Depends on: [m2-fallback-tracing](#m2-fallback-tracing).

Acceptance:

- [ ] Original and selected-title cases verify stage cancellation, disk-space failures, interruption cleanup and cache invalidation keyed by all relevant inputs/settings/tool/runtime versions.
- [ ] Rebuild and validated private trace import preserve title/save identity, reject unsafe overrides/collisions, and avoid presenting incomplete output as a successful package.

<a id="m6-windows-acceptance"></a>

#### m6-windows-acceptance: Run full native conversion on clean Windows

Extend the passed hosted Windows baseline/deep AOT/WPF tooling proof to the actual supported-game engine and its devkitPPC prerequisites.

State: **Blocked** · Needs: `needs-windows`, `needs-input`.

Depends on: [m6-conversion-pipeline](#m6-conversion-pipeline), [m6-rebuild-cache](#m6-rebuild-cache).

Acceptance:

- [ ] A clean Windows machine follows documented setup and completes the selected-title conversion, including actual native toolchain execution, spaced/Unicode paths and actionable missing-dependency errors.
- [ ] Cancellation/retry/rebuild and report/output behavior pass without manual source edits or undocumented shells; existing CI tooling checks remain supporting evidence rather than full conversion acceptance.

<a id="m6-install-continuity"></a>

#### m6-install-continuity: Verify installed commercial WUP, rebuild/save continuity and uninstall

Establish the real engine output as a stable per-game Wii U Menu title after Windows conversion.

State: **Blocked** · Needs: `needs-input`, `needs-windows`, `needs-console`.

Depends on: [m6-windows-acceptance](#m6-windows-acceptance), [m5-gameplay-route](#m5-gameplay-route).

Acceptance:

- [ ] The generated WUP installs and Menu-launches into the agreed supported gameplay/audio/save route in the recorded console/installer environment.
- [ ] Rebuilding/updating the same title preserves saves, independent titles do not share saves accidentally, and documented user-directed uninstall behavior is verified.

<a id="m7"></a>

## M7: Windows GUI injector

**State: Planned. Full acceptance: open.**

**Entry:** The selected title has a proven execution path and the shared engine has demonstrated real Windows conversion. Specify/validate interfaces now; do not implement the GUI before the execution/conversion pipeline works.

**Deliverables:** Packaged C# WPF CTR2WUP.exe with the reviewed UWUVCI-style Inject page, prerequisite/local-key setup, responsive real progress/cancellation, saved projects/rebuild and redacted reports.

**Exit criteria:** On clean Windows, the user selects the supported owned ROM, configures title/artwork/output and clicks Inject to obtain its validated WUP without undocumented tools or manual compiler steps. That package passes installation/Menu gameplay/audio/save/reload/stability and rebuild continuity on Wii U.

**Verified desktop/earlier evidence:** The GUI workflow and encrypted-input amendment are documented, and supported WPF tooling passed hosted Windows validation. No finished GUI exists. A mockup, diagnostic launcher or CLI alone cannot close this final product milestone.

**Still needs external evidence/input:** Implementation follows the proven M5/M6 pipeline. Packaging/usability needs clean Windows; local-key and selected-game tests require owned inputs; final installation/gameplay evidence needs the Wii U.

References: [GUI workflow](gui-workflow.md) · [Windows build](build-windows.md) · [Product execution plan](development-plan.md) · [Encrypted-input amendment](encrypted-input.md).

### Actionable work items

<a id="m7-engine-contract"></a>

#### m7-engine-contract: Finalize the GUI engine request, progress and report contract

After a proven conversion pipeline, expose its versioned readiness/build/cancel/rebuild results to the Windows front end.

State: **Planned** · Needs: none for original-fixture development.

Depends on: [m5-title-acceptance](#m5-title-acceptance), [m6-windows-acceptance](#m6-windows-acceptance).

Acceptance:

- [ ] The GUI contract uses the same proven engine, reports real stage/progress/errors and preserves profile/compatibility and package-validation versus console-evidence distinctions.
- [ ] Original integration fixtures demonstrate readiness, cancellation and retry without hiding failed stages; GUI implementation starts only after the execution/conversion gate is met.

<a id="m7-inject-ui"></a>

#### m7-inject-ui: Implement the UWUVCI-style 3DS Inject workflow

Build the C# WPF Windows app around the proven engine and the reviewed user workflow.

State: **Planned** · Needs: `needs-windows`.

Depends on: [m7-engine-contract](#m7-engine-contract).

Acceptance:

- [ ] CTR2WUP.exe provides ROM detection/status, title and icon/TV/GamePad artwork configuration, implemented display settings, WUP output choice and one primary Inject action.
- [ ] The app remains responsive during real conversion, supports Cancel/Retry and offers completion/report/Open output guidance without routine manual compiler commands.

<a id="m7-key-setup"></a>

#### m7-key-setup: Provide dependency setup and private local key import help

Make decrypted input and optional user-owned encrypted-input preparation usable without bundling keys or undocumented prerequisites.

State: **Planned** · Needs: `needs-windows`, `needs-input`.

Depends on: [m7-engine-contract](#m7-engine-contract).

Acceptance:

- [ ] First-run setup documents/provisions actual supported tools; local export/import/remove help identifies needed keys/seeds and gives actionable missing/wrong-material failures.
- [ ] Windows protected storage, redacted diagnostics, cancellation cleanup and exported-project/package exclusions are verified with original fixtures and a locally owned encrypted input; console extraction help never claims stock USB key access.

<a id="m7-diagnostics-projects"></a>

#### m7-diagnostics-projects: Implement saved projects, rebuild and exportable diagnostics

Expose stable per-game rebuilds and helpful reports through the GUI while keeping technical detail in expandable controls.

State: **Planned** · Needs: `needs-windows`.

Depends on: [m7-engine-contract](#m7-engine-contract), [m6-rebuild-cache](#m6-rebuild-cache).

Acceptance:

- [ ] Saved-project reopen/import/export and runtime-update rebuild preserve validated title/settings identity and save continuity; imported private traces follow engine validation.
- [ ] GUI reports identify build/profile/status, actual stage failures and output location while excluding keys and commercial payloads; advanced unavailable settings cannot appear effective.

<a id="m7-gui-acceptance"></a>

#### m7-gui-acceptance: Package and accept clean-Windows Inject-to-playable-WUP

Deliver the complete first MVP through the Windows GUI and the agreed physical-console route.

State: **Planned** · Needs: `needs-input`, `needs-windows`, `needs-console`.

Depends on: [m7-inject-ui](#m7-inject-ui), [m7-key-setup](#m7-key-setup), [m7-diagnostics-projects](#m7-diagnostics-projects), [m6-install-continuity](#m6-install-continuity), [m5-title-acceptance](#m5-title-acceptance).

Acceptance:

- [ ] A clean Windows machine runs the packaged app with documented/provisioned prerequisites, selects the exact supported revision, configures it and obtains its validated WUP via Inject, including cancel/retry/rebuild checks.
- [ ] That WUP installs, launches from the Wii U Menu and passes gameplay/input/audio/save/reload/stability plus rebuild/save continuity; release notes identify exact support and limitations.

## Acceptance review and deferred scope

Close a work issue only with its scoped result and links to evidence. Update the
versioned source state when implementation/evidence changes. The overview tracker
remains open until the milestone exit criteria are reviewed; closing its child
issues by itself does not prove physical behavior or complete the product.

The first MVP completes only when M7 demonstrates GUI conversion of the exact
supported revision into an installed playable per-game WUP, including the agreed
gameplay/audio/save/reload/stability and rebuild tests. Record any measured
performance limit before broadening compatibility.

New 3DS exclusives, stereoscopic TV, online services, cameras, microphone,
DLC/update merging, save import/export and broad arbitrary-ROM compatibility
remain deferred. Titles that require them need separate implementation/evidence.

Public documentation delivery is separate from gameplay acceptance. The owner
made the source public on October 14; source, documentation and the dashboard
share the source repository's Pages deployment. Commercial inputs, private
traces/notes and credentials remain excluded. Existing topic-branch, required-CI
and no-protection preferences continue. See [GitHub development](github-development.md)
for actual deployment and access evidence.
