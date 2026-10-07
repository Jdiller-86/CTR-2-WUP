# CTR2WUP — Work Mode development brief

Paste this entire brief into Work Mode, or attach it and say: “Use this brief as the project requirements. Begin implementation and continue through the milestones that can be verified in this environment.”

## Objective

Develop **CTR2WUP**, a Windows desktop application that accepts a user-provided, decrypted Nintendo 3DS game dump and produces a per-game Wii U installation package.

The intended user experience is:

1. Select a supported decrypted ROM.
2. Review detected title information and compatibility.
3. Choose an output folder and press Build.
4. Receive an installable WUP directory.
5. Install it on a suitably configured homebrew Wii U.
6. Launch the game from the Wii U Menu, with the 3DS top screen on the TV and bottom screen and touch input on the GamePad.

Internally, use ahead-of-time ARM11-to-portable-C/C++ translation, devkitPPC compilation, a reusable Wii U 3DS HLE runtime, and a PICA200-to-GX2 graphics backend. The Windows PC performs analysis and compilation; the console executes the resulting PowerPC application and supplies compatibility services.

This is a staged research and development project. Producing a WUP is one milestone; proving a game runs correctly and at useful speed is another. Do not promise broad compatibility or full-speed gameplay before measurements exist.

## Scope and development rules

- Begin with Old 3DS application software, 2D output, native guest resolution, offline play, and one validated game revision.
- Accept decrypted NCSD .3ds and NCCH .cxi when the parser actually supports them. Reject encrypted input with a clear explanation. Add other formats later.
- Use open-source homebrew toolchains. Do not depend on Nintendo's proprietary SDK, a donor Virtual Console title, or bundled commercial assets.
- Keep user ROMs, extracted assets, translated game code, saves, generated packages, and credentials out of source control and public artifacts.
- Start with a CLI and a working Wii U prototype. Add the polished Windows GUI after the execution pipeline is demonstrated.
- Reuse suitable existing implementations after checking licenses and architecture. A publicly visible repository without a license is not automatically reusable.
- Proceed through routine implementation decisions without asking for repeated confirmation. Ask for a game dump, console results, or a material product decision only when needed.
- Continue useful independent work while external testing is pending. Keep externally dependent milestones marked unverified.
- Never replace missing core functionality with mock success. Unsupported instructions, services, and GPU states must be observable.
- Do not report console success from a desktop test, cross-compilation, emulator screenshot, or package validation alone.
- Maintain a Git repository, reproducible build instructions, dependency versions, a milestone checklist, and a concise STATUS.md so later Work Mode sessions can continue reliably.
- On a blocked dependency, record the actual error, implement a supported alternative where feasible, and identify the smallest user action needed. Do not repeatedly retry unavailable infrastructure.

## First actions: establish the actual foundation

Inspect any existing workspace and its instructions before creating a new project. Inventory the available compiler, Windows build tools, devkitPPC/WUT, network access, test fixtures, and console access.

Audit these candidate projects from their current source, recording commit hashes:

- https://github.com/fearkov/3dsrecomp
- https://github.com/fearkov/zakuro
- https://github.com/mtrdrgz/3ds_recompiler
- https://github.com/devkitPro/wut
- https://github.com/devkitPro/wut/tree/master/samples
- https://github.com/azahar-emu/azahar
- https://github.com/ihaveamac/nuspacker
- https://github.com/NicoAICP/CNUS_Packer

Treat this list as candidates, not approved dependencies. Verify each repository's existence, license, build process, CPU semantics, runtime dependencies, actual compatibility evidence, and suitability for 32-bit big-endian PowerPC.

The checked README of mtrdrgz/3ds_recompiler describes ARM-to-C++ lifting with HLE, interpreter fallback, and desktop/Web targets. It also lists gaps including dynamic CRO loading and some services. Its success claims are author reports, not Wii U benchmarks. Source access to the fearkov repositories was not fully verified when this brief was prepared; inspect them directly before relying on their claimed coverage.

Select the smallest viable foundation based on a reproduced build and code inspection. Prefer adapting a proven lifter and HLE implementation over writing every subsystem from scratch. Do not assume desktop-specific SIMD, Rust runtime dependencies, floating-point control, pointer casts, or host threads will port unchanged.

Deliver docs/foundation-audit.md with a reuse/license matrix, the chosen architecture, measured build results, and unresolved risks. Then immediately begin the first executable prototype.

## Proposed architecture

Keep these components independently buildable:

| Component | Responsibility |
|---|---|
| Windows host CLI | Input inspection, build orchestration, manifests, logging, profiles, packaging |
| ROM/container tools | NCSD/NCCH parsing, ExeFS/code decompression, RomFS access, metadata |
| Translator adapter | ARM/Thumb/VFP lifting, block discovery, code generation, missing-code feedback |
| Portable runtime core | Guest memory, registers, dispatcher, interpreter, kernel/HLE interfaces |
| Wii U platform backend | GX2 output, VPAD input, audio, timing, threads, files, saves |
| PICA backend | Commands, state, shader processing, textures, rendering, synchronization |
| Packaging adapter | Wii U executable metadata and validated WUP generation |
| Windows GUI | A usable front end over the same build pipeline |

Suggested source layout: host/, translator/, runtime/core/, runtime/wiiu/, renderer/, packaging/, profiles/, tests/, docs/.

Initially link translated code and the runtime into a single RPX. Consider RPL splitting only if executable size or loading measurements justify it. CRO relocation and guest module behavior still need their own implementation; making an RPL does not solve them.

Use a versioned manifest with input identity, executable/module hashes, runtime version, translator version, profile, display settings, toolchain versions, package identity, and stage results. Separate stable title/save identity from executable hashes used to invalidate compilation caches.

## Milestone 1 — Native Wii U platform and packaging smoke test

Build a minimal WUT/devkitPPC application before involving 3DS execution.

Implement:

- Separate TV and GamePad output with different diagnostic images.
- GamePad buttons, sticks, calibrated touch, and an orderly exit.
- A simple audio sample.
- Timing, logging, and allocation diagnostics.
- A minimal host build command and dependency doctor command.
- A packaging investigation using an original homebrew title and metadata.

Target top-screen presentation at 400 × 240 and bottom-screen presentation at 320 × 240. At a 1280 × 720 TV output, a centered 1200 × 720 top image is a useful default. For the GamePad, compute the viewport from the actual rendering buffer and calibrated input coordinates; do not assume physical panel pixels are identical to GX2 surface coordinates. Ignore touches in borders.

Acceptance: produce a valid RPX and record successful physical-console dual-screen, input, audio, and exit tests. Independently attempt a WUP smoke package, documenting the installer and homebrew environment required. WUP installation is not supported on an unmodified retail console merely because packaging succeeds.

## Milestone 2 — Portable CPU translation correctness and benchmarks

Translate small, reproducible ARMv6K/Thumb/VFP test programs and compile the generated code for desktop and Wii U.

Implement explicit guest little-endian memory access. Guest addresses remain 32-bit guest addresses; translate them through validated mappings rather than casting them into host pointers. Handle permissions, aliases, shared memory, overflow, alignment, and access failures deliberately.

Verify:

- ARM condition flags, conditional execution, shifts, carry/overflow, ARM/Thumb transitions, indirect branches, and exception behavior.
- Load/store ordering, exclusive access semantics where required, and code invalidation.
- VFP register layout, rounding modes, denormals, NaNs, signed zero, and integer conversion.
- Defined C/C++ behavior without unsafe aliasing or signed-overflow assumptions.
- A dispatcher and interpreter fallback that preserve guest state across calls, waits, interrupts, and yields.
- Missing-block logs keyed by executable/module hash and ARM/Thumb state.

Use trusted instruction-reference behavior and differential tests where available. Include tests running on PowerPC; a little-endian desktop pass cannot establish big-endian correctness.

Measure arithmetic, memory, branch-heavy, VFP, and dispatch workloads on real hardware, including generated-code size, link time, peak memory, and elapsed time. Measure fallback execution time as well as instruction counts. A tiny fraction of interpreted instructions can still consume substantial runtime.

Acceptance: documented correctness results and actual PowerPC timings. These benchmarks inform feasibility; they do not establish game performance. Fix semantic failures before expanding game coverage.

## Milestone 3 — Boot a recompiled 3DS test application

Create or reuse a redistributable 3DS homebrew fixture in the executable/container format this pipeline supports. A .3dsx fixture may require its own loader; do not silently treat it as NCCH.

Implement the minimum guest environment the fixture actually exercises:

- Executable layout, initial register state, stack, TLS, and memory regions.
- SVC handling and a deterministic guest scheduler.
- Handles, events, waits, timers, synchronization, and IPC transport.
- Required service discovery and minimal APT, FS, HID, CFG, and GSP behavior.
- Guest framebuffer presentation, frame timing, and touch/button state.

Start with controlled guest scheduling instead of assuming one Wii U core per 3DS subsystem or mapping every guest thread to a host thread. Preserve observable synchronization behavior.

Acceptance: the recompiled fixture executes guest code on Wii U, updates both framebuffers, responds to GamePad input and touch, and exits or runs a documented stability session. Distinguish a framebuffer demo from a working PICA renderer.

## Milestone 4 — PICA200-to-GX2 renderer

Implement and test incrementally:

1. PICA command/state decoding and draw submission.
2. Guest framebuffer formats, rotation, display transfers, and clears.
3. Vertex formats, attributes, uniforms, and vertex shader execution.
4. Texture formats, ETC decoding, swizzling, samplers, and caching.
5. Fragment combiner/TEV behavior, blend, depth, stencil, and alpha tests.
6. Render-to-texture, aliasing, readback, and CPU/GPU coherency.
7. Lighting LUTs, geometry processing, procedural textures, and other features required by the selected title.

Use software rendering or software vertex processing as temporary correctness references where useful. Do not assume a full software renderer is fast enough for the final Wii U build.

Investigate available Latte shader assembly/compilation tooling and its licenses before choosing a shader pipeline. Offline compilation may require runtime traces and title profiles. Scanning ROM assets cannot guarantee discovery of every shader or dynamic PICA state.

Build a versioned shader/state cache and explicit handling for misses: supported generic translation, slower correctness fallback, or a diagnostic failure. Never silently render unsupported states incorrectly.

Acceptance: fixture scenes compare against a trusted reference using defined tolerances and visual inspection; console timing includes CPU submission, GPU work, transfers, and synchronization. Expand features according to observed requirements.

## Milestone 5 — One commercial Old 3DS title

Request one user-owned decrypted dump when needed. Choose the initial title using reproduced upstream compatibility, required services, renderer features, module complexity, and a short repeatable gameplay test. Do not select a demanding title solely because it is popular.

For the exact revision supplied:

- Complete required services and kernel behaviors.
- Add CRO loading/relocations and translated-module dispatch if exercised.
- Implement required DSP/audio behavior and Wii U output.
- Implement RomFS reads and correct save/extdata archive behavior.
- Preserve saves across application exit and later rebuilds.
- Add missing-block trace import and cache invalidation.
- Use narrowly scoped, documented profiles matched by hashes.
- Log unimplemented IPC with enough context to implement it accurately; generic success responses are acceptable only when their behavior is explicitly justified.

Acceptance: boot, menu, interactive gameplay, working input and required touch, synchronized audio, a successful save/reload, and a repeatable stability run on physical Wii U.

Record game revision, settings, console environment, test route, frame-time distribution, memory peak, fallback time, graphical/audio issues, and logs. Use the title's intended frame rate: a 30 fps title has a 33.3 ms frame budget; a 60 fps title has 16.7 ms. Preserve guest timing rather than speeding up the game to improve a counter.

If useful performance remains out of reach, identify measured bottlenecks and attempt targeted optimizations. Document the resulting limits before broadening scope.

## Milestone 6 — Automated ROM-to-WUP CLI

Expose real commands such as doctor, inspect, build, rebuild, import-trace, and validate-package. Use machine-readable results and nonzero exit codes for failures.

The build pipeline must:

1. Validate the container and detect executable revision.
2. Resolve a supported profile or report experimental status.
3. Extract/analyze code and required modules.
4. Generate and compile PowerPC code.
5. Prepare available shader/state caches.
6. Link the runtime and stage content.
7. Generate original Wii U metadata and title artwork.
8. Package the staged title through the validated packer.
9. Validate identities, sizes, metadata consistency, and content integrity.
10. Emit the package, build report, and installation requirements.

WUP generally means a directory of installable files, not a single executable. Distinguish it from RPX launch and WUHB/menu shortcuts.

Assign application title IDs using a documented homebrew policy, collision registry, and override validation. Hashing a ROM is insufficient to guarantee a safe, collision-free Wii U title ID. Keep IDs stable for rebuilds and saves; reject system/reserved identities and mismatches among metadata and package files.

Where packaging needs user-supplied credentials or keys, use a documented local configuration and redact them from logs. Do not bundle them. Validate signature/installer requirements separately from package structure.

Support cancellation, paths with spaces and Unicode, sufficient-disk-space checks, interrupted builds, and cached stages keyed by all relevant inputs. Publish final output atomically so incomplete builds cannot be mistaken for finished packages.

Acceptance: a clean Windows machine with documented prerequisites builds and validates the test title through the CLI. Confirm installation, Wii U Menu launch, separate saves, rebuild behavior, and uninstall behavior on the supported console environment. Never automate installation or overwriting console titles without the user's direction.

## Milestone 7 — Windows desktop application

Build a native Windows desktop front end, preferably C# with a supported .NET LTS release and WPF unless the repository provides a better justified choice.

Include:

- ROM picker and detected title, region, revision, and platform information.
- Compatibility state: untested, unsupported, experimental, or console verified.
- Output folder, artwork preview, and simple display settings.
- Dependency checks and actionable errors.
- Real per-stage progress, elapsed time, cancellation, and exportable diagnostics.
- Build reports and an Open output folder action.
- Rebuild from runtime updates and imported fallback logs.
- Advanced settings behind an expandable panel.

Default to top on TV, bottom on GamePad, native rendering, and aspect-preserving scaling. Add screen swapping if straightforward. Mark unsupported features unavailable rather than letting users select ineffective options.

Acceptance: the GUI invokes the same proven pipeline, remains responsive during builds, and completes the supported-title conversion on Windows. Package the application's actual dependencies; do not advertise a self-contained one-click converter while requiring undocumented shells or tools.

## Completion and continuation

The first MVP is complete only when one supported Old 3DS revision is converted by the Windows app, installed as a Wii U title, and verified through the agreed gameplay/save test. Broad compatibility is a later release.

For every milestone, provide:

- Source changes and reproducible build commands.
- Automated test results where they establish correctness.
- Physical-console checks with actual evidence, or a precise “awaiting console test” status.
- Current supported features, known failures, and the next concrete task.

Maintain docs/architecture.md, docs/build-windows.md, docs/console-testing.md, docs/compatibility.md, THIRD_PARTY_NOTICES, STATUS.md, and versioned build reports.

When console access is unavailable, provide a ready-to-run diagnostic RPX or package plus a short test checklist and requested logs. Continue other verifiable tasks, but leave dependent feasibility and performance gates open. Do not end after generating a skeleton or a GUI mockup.

Defer New 3DS exclusives, stereoscopic TV output, online services, cameras, microphone support, DLC/update merging, save import/export, and broad arbitrary-ROM claims until the initial title works. Track each deferred feature so titles requiring it are identified accurately.

Begin now with the source/license audit and executable Wii U platform prototype.

