# CTR2WUP product development plan

Finalized 2026-10-07 following the user's request to realign with the supplied
preview of “Evaluate 3DS Emulation Viability”, `project-brief.md`, and the user's
UWUVCI AIO GUI workflow clarification. `gui-workflow.md` specifies the reviewed
reference, screens, Inject action, output and acceptance criteria.
The referenced conversation supplies product context; its feasibility scores,
upstream coverage percentages and compatibility claims are not verified results.
The original brief remains the requirements document. This plan corrects the
execution priorities and preserves its hardware, correctness and licensing gates.

## Product and completion target

Deliver a Windows GUI, **CTR2WUP.exe**, with an UWUVCI-style **3DS Inject** page:

**Select ROM → configure title/artwork/settings → choose WUP output → Inject →
install the resulting WUP on Wii U → launch from the Wii U Menu.**

The GUI runs this conversion sequence behind its Inject action:

1. Select a user-owned decrypted Old 3DS `.3ds` or `.cxi` dump.
2. Detect the title/revision and report its actual support status.
3. Analyze and translate its ARM/Thumb/VFP code on the Windows PC.
4. Compile translated code and the reusable Wii U HLE runtime with devkitPPC;
   stage the game's required assets and shader/state data.
5. Generate original Wii U metadata and a stable per-game title identity, then
   produce a validated installable WUP directory and build report.
6. Install on the documented homebrew Wii U environment and launch from the Wii U
   Menu, with top screen on TV and bottom screen/touch on GamePad.

The first MVP supports **one exact commercial Old 3DS revision**. Completion
requires conversion by the Windows GUI, actual WUP installation/Menu launch,
repeatable interactive gameplay, input/touch as required, audio, save/reload,
rebuild/save continuity and measured useful performance on physical Wii U.
Record the supported revision, installer/environment and known limitations.
Broad compatibility follows that proof.
The CLI remains the shared build engine and an engineering tool. The finished
user product is the GUI; normal injection requires no manual shell commands.

## Foundation and scope correction

The version 0.2.0 baseline at commit `f42d449` contains useful container inspection, build
orchestration, checked guest memory, original CPU tests and native/guest Wii U
diagnostics. Commercial `build` currently stops at an unsupported-profile error;
its external lifter adapter performed analysis only. Version 0.3.0 now integrates
external C generation and checked static linking/execution; see
`upstream-integration.md`. There is no PICA
renderer, commercial runtime or Windows executable distribution. Version 0.4.0
implements an original scoped encrypted writer and independent decrypting
verifier; installer acceptance and the commercial pipeline remain unverified.
`packaging.md` defines its exact limits and local-input contract.

Keep those foundations and use the synthetic guest as a regression test for the
conversion/runtime boundary. Direct development effort toward integrating the
audited lifter, reusing suitable HLE behavior, producing WUPs and running the
selected title. Expand fixtures and CPU/service coverage to resolve concrete
adapter, title or console failures. Measure progress by working conversion stages
and supported game behavior, with test counts as supporting evidence.

The planned foundation is the pinned **fearkov/3dsrecomp** host lifter and
license-cleared **Zakuro CPU/HLE** components or selected behavior ported into a
WUT-compatible C/C++ runtime. The current Rust/Vulkan desktop runtime is not a
validated Wii U target. Define an explicit portable generated-code ABI and port
the required pieces; retain checked little-endian memory and controlled guest
scheduling. Normal guest execution uses AOT code, with observable, correct
fallback for supported misses. Adopt upstream implementations only after their
licenses and target suitability pass the existing audit gates.

## Ordered implementation plan

| Phase | Work and concrete deliverable | Acceptance / original milestone mapping |
|---|---|---|
| P1: Resolve conversion foundations | Extend the pinned lifter adapter from analysis to C generation and static linking; normalize its 32-bit ABI, little-endian fast memory paths, SVC/resume state and fallback boundary. Resolve lifter redistribution notices. In parallel, choose a license-cleared WUP packaging route and stage original `code/content/meta` for the existing native app. | Original ARM/Thumb/VFP tests execute through the adapted lifter on desktop and PowerPC; report code size, build/memory costs and fallback timing. Original homebrew WUP is generated and physically installed/launched/exited. Advances M1/M2 and establishes M6 packaging prerequisites. |
| P2: Select and analyze the first title | Compare a small candidate set using reproduced upstream evidence and service/GPU/module demands. Obtain one user-owned decrypted revision when its private inspection is needed. Extract code, RomFS and required module metadata; create an exact-hash profile and feature/test-route matrix. | A selected revision, traceable requirements, repeatable gameplay/save route and explicit unsupported features. Sets the scope for M3–M5. Title analysis can proceed while P1 console tests are pending. |
| P3: Run that title through the Wii U runtime | Implement or adapt only the required kernel/SVC/IPC/APT/FS/HID/CFG/GSP behavior, guest scheduling and memory layouts. Implement the required PICA-to-GX2 draws, shaders, textures/TEV/transfers/coherency with reference tests. Add exercised CRO relocation/dispatch, DSP/audio and save/extdata behavior. Use targeted redistributable fixtures for each interface. | Adapted guest fixture works on console; selected game progresses through boot → menu → gameplay → save/reload. Produce a manually staged per-game WUP through P1 packaging and verify Menu launch. Record physical CPU/GPU/fallback timings, memory, accuracy and stability. M3/M4/M5 acceptance remains evidence-based. |
| P4: Automate the Windows injection engine | Connect all proven stages to the existing host pipeline: validate → profile → extract → translate → compile → shader/cache preparation → link/stage → metadata/title ID → pack → validate → publish. Provide real internal `build/rebuild/import-trace/validate-package`, versioned GUI requests/progress events, cancellation, revision-aware caches, stable saves/IDs and actionable diagnostics. | A clean Windows machine converts the selected ROM into the functioning per-game WUP through the engine without hand edits. Verify install/Menu launch, rebuild/save continuity and uninstall behavior. Completes M6 after its console checks; the CLI alone does not complete the product. |
| P5: Deliver the Windows GUI injector | Build the C# WPF front end on a supported .NET LTS release over the verified engine, following `gui-workflow.md`. Provide the UWUVCI-style 3DS Inject page, ROM/compatibility detection, game name and artwork previews, settings/WUP destination, one Inject action, responsive progress/cancellation, completion/SD-install guidance and saved-project rebuild. Package or provision actual prerequisites through documented setup. | `CTR2WUP.exe` performs the supported injection on clean Windows without manual shell commands. Its WUP installs, launches from the Menu and passes the agreed physical-console gameplay/save test. Completes M7 and the first MVP. |

P1 has two independent work streams: translator/runtime portability and packaging.
Resolve packaging early so installer, metadata, certificate and key requirements
are known before completing the game runtime. P2 supplies the evidence for P3's
feature priorities. The GUI workflow is specified now; its implementation follows
the demonstrated conversion pipeline. Every engine stage supports the final
GUI-driven Inject action.

## Packaging and reuse decisions

The audited nuspacker/CNUS_Packer checkouts have no identified license grant;
their certificate templates also lack established redistribution provenance.
Find an applicable grant or a suitable licensed alternative. If neither is
available, implement an original scoped packer from documented formats with
license-cleared cryptographic dependencies. Record the selected route and actual
installation evidence before treating packaging as solved.

Complete 3dsrecomp's missing redistribution notices before bundling its executable
or ABI support. Its existing local invocation can support research while this is
resolved. Preserve applicable notices for adopted Zakuro code; validate each
ported subsystem against the portable ABI and target toolchain. A wholesale
desktop emulator port is outside the selected architecture.

Keys and any required certificate material use documented user-managed local
configuration with redacted diagnostics. ROMs, extracted assets, translated
commercial code, saves and per-game packages remain private. A structural package
pass is separate from signature/installer policy and successful installation.

## External inputs and checkpoints

| Needed input/evidence | When it is needed | What remains pending without it |
|---|---|---|
| Physical Wii U results and SD logs for existing native/guest diagnostics | P1; the builds are already available | Platform behavior, actual PowerPC correctness/performance and hardware-dependent fixes |
| One user-owned decrypted Old 3DS revision and agreed gameplay/save route | P2, before title-specific runtime/renderer work | Exact profile, code/assets/module analysis and commercial requirements/testing |
| Documented supported homebrew/installer environment and locally configured packaging prerequisites | P1 original homebrew WUP smoke test | Real installation/Menu-launch acceptance; these are not bundled credentials |
| Clean Windows execution/build results | Host adapter/executable validation, then P4/P5 acceptance | Native Windows toolchain, `.exe` distribution, paths/cancellation and GUI evidence |
| Physical selected-title gameplay/save/audio/performance evidence | P3–P5 | Commercial compatibility, useful speed and final MVP acceptance |

Continue independent implementation when a hardware check is pending, and record
the exact blocked acceptance. Reassess feasibility after the adapted-lifter
PowerPC measurements and first representative renderer workload. Optimize
measured bottlenecks; document a failed performance gate before widening scope.
Upstream desktop compatibility and coverage percentages cannot close these gates.

## Resumed implementation checkpoint

The user resumed development. P1's external generation/checked ABI/static-link
proof now executes on desktop and emulated big-endian PowerPC and builds an RPX.
Original metadata/artwork staging is implemented. Version 0.4.0 now writes the
scoped encrypted FST/content/TMD/ticket profile and independently verifies all
decrypted files. The real RPX passes the offline path with synthetic inputs;
physical install/Menu launch remains open. The legacy makefst candidate's
plaintext and test/production certificate combination was not adopted.

The user selected **Super Mario 3D Land** for P2. `first-title.md` records concrete
upstream source leads and intake commands. Its exact dump/revision is pending;
profile activation and title-specific implementation follow that evidence.
`STATUS.md` and `milestones.md` track implemented, verified and pending work.

P2 update, October 7 (America/New_York): the user supplied a CIA, but the
workspace transfer tool rejects it above 32 MiB. v0.5.0 implements direct
decrypted-CIA intake and a separate self-contained Windows console helper that
exports a small private executable view or encryption report. Actual game
inspection awaits that export; filename region/version claims remain unverified.
The original CIA can remain local for assets. This helper is not the GUI injector.

October 8 (America/New_York): the user is away from the CIA's computer and asked
for independent implementation. v0.6.0 advances P3's shared startup foundation:
checked CP15 TLS access, typed service sessions, bounded SendSyncRequest and
restricted SRV/CFG reads. An original 89-instruction ARM program exercises those
interfaces through interpreter/original/external AOT, without title-specific code.
The console diagnostics embed its original interpreter/AOT self-test. This does
not activate Mario or close any physical-console milestone; its exact revision,
broader kernel/services/GPU/audio/saves and installer acceptance remain pending.

The next ROM-independent increment, v0.7.0, implements checked ControlMemory and
QueryMemory: owned heap/linear CPU backing, atomic partial free/protection, bounds
and resource limits. Its original ARM lifecycle probe preserves AOT while
matching all three execution paths. This advances M3/P3; the physical tests,
shared-memory/thread/kernel work and title-specific requirements remain open.
GPU/DSP physical-address translation is not supplied by the linear allocation.

The user requested continued execution through concrete roadblocks. v0.8.0
adds shared-memory ownership, mutex/semaphore objects and wait-any/all as common
HID/GSP prerequisites. Continue into real input/service integration while
console/title-specific gates remain open. These are tested single-thread interfaces.

v0.9.0 implements the standard HID shared page/events and connects live Wii U
VPAD input to an original ARM consumer. The external compiler/target comparisons
remain ROM-independent. Continue next into the standard GSP framebuffer display
interface, before title-specific PICA/shader work. Console tests and actual Mario
intake remain required; the GUI still follows demonstrated execution/packaging.

v0.10.0 adds direct GSP framebuffer submission and five rotated format decoders.
The original ARM standard-HID/GSP guest now reaches the Wii U presentation backend
in all builds; actual external execution is verified separately on desktop and
emulated PPC. The next planned acceptance gate needs physical console evidence
and Mario executable intake. `current-roadblock.md` records that roadblock and
precise resume order. Generic runtime work remains possible, but broader game
feature selection and performance claims need the selected revision/reference
and console results. All commercial/GUI/full milestone gates stay open.
