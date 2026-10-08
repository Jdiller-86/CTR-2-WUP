# CTR2WUP status

Updated 2026-10-08 UTC. Requirements: `docs/project-brief.md`; product plan:
`docs/development-plan.md`; GUI workflow: `docs/gui-workflow.md`.
The user resumed implementation and selected **Super Mario 3D Land** as the first
title candidate. The target remains an UWUVCI-style Windows GUI: select a
decrypted game, configure title/artwork, click Inject, install its per-game WUP
and launch from Wii U Menu. The CLI is the shared engine, not the final product.

**Version 0.3.0 implements external-lifter C generation and checked static
execution/linking, original title staging, and private revision/service intake.**
Commercial conversion, encrypted WUP generation and the Windows GUI are still
incomplete. No milestone requiring physical-console evidence is complete.

## Verified implementation

- The clean pinned fearkov/3dsrecomp checkout is called through an original Rust
  generation-only driver. A build receipt checks source/driver/binary hashes.
  Generated C is adapted to an original ABI 4 implementation; the upstream
  header and tool binary are not bundled. Missing upstream redistribution
  notices remain an external-tool bundling issue.
- The checked C engine uses fixed-width guest addresses, explicit little-endian
  callbacks, executable hash checks, per-instruction state/budget boundaries,
  precise fault PCs, SVC unwinding to the existing scheduler, invalidation through
  writable aliases and visible bounded fallback. CROs and unsupported FP modes
  fail; complete ARM/VFP semantics are not claimed.
- Actual external AOT execution of the original framebuffer NCCH matches the
  interpreter's registers/flags and every mapped memory region across uneven
  budgets. Two touched/button frames execute 1,382,534 instructions, four SVCs,
  zero fallback and orderly exit. Framebuffer files match byte-for-byte.
- A separate original 30-instruction ARM/Thumb/scalar-VFP probe checks single and
  double arithmetic, flags, stack transfers, state switching and one-instruction
  resume. Its registers/VFP words match Unicorn 2.1.4 independently.
- External integration passes **566 boundary/frame assertions +34 ABI probe
  assertions** under GCC, ASan/UBSan, Clang and emulated 32-bit big-endian PowerPC
  Linux/QEMU 750. Emulation does not establish Wii U behavior or useful speed.
- Real devkitPPC/WUT builds produce native, original-AOT guest and external-AOT
  guest RPX/WUHB artifacts. The new external build starts in guest mode and uses
  the existing TV/GamePad/input/exit platform implementation.
- `stage-title` copies a validated RPX and generates original `code/content/meta`
  metadata and checker artwork. It refuses system IDs and existing destinations.
  Its launch settings are candidates needing console validation. It produces
  **staging, not an installable WUP**; no certificate/key/donor is included.
- `analyze-title` writes a private exact-hash intake with segments, stack/BSS,
  section inventory, extended-header service allowlist and optional lifter
  analysis. Declared service access is not executed/implemented IPC. No intake
  enables a compatibility profile. Mario investigation leads and the proposed
  gameplay/save route are in `docs/first-title.md`.
- Existing strict NCCH/NCSD/ExeFS/BLZ/SMDH inspection, safe loader, bounded CPU,
  single-guest events/handles/sleep/debug/exit, native diagnostics and structural
  WUP validator remain tested. Commercial `build` still fails without publishing
  a placeholder package. Paths with spaces/Japanese characters work on Linux.
- **95 Python tests**, 51,147 CPU assertions and 221 original-guest assertions
  pass; both C suites also pass ASan/UBSan. The complete v0.3.0 runner passes
  **24 checks**, including external generation/execution, all three RPX builds,
  original staging/intake and installed-wheel inspection/execution.

## Reports and deliverables

Main validation: `artifacts/reports/validation-v1-20261008T004051Z/report.json`.
Additional checks: `artifacts/reports/upstream-ppc-final/`,
`upstream-clang-final/`, and the Unicorn reference report under
`upstream-native-v5/`. `python3 tools/validate.py` regenerates the complete checks;
`tools/validate-upstream.py` supports GCC/sanitizers/container compiler/QEMU runs.
See `docs/upstream-integration.md` for commands and precise limits.

Versioned source/native/original-guest/external-guest delivery bundles, wheel,
checksums and original title-staging output are under `artifacts/`. Earlier
v0.1.0/v0.2.0 reports and bundles are preserved. Source is maintained in the
existing Git checkout; do not reset, force-push or replace user changes.

## Awaiting user inputs and physical tests

- **Mario dump:** a user-owned fully decrypted Old 3DS `.3ds` or `.cxi`, with its
  region/revision identified, under ignored `private/`. None is present. It is
  needed to inspect actual code/modules/assets/services and set an exact profile.
- **Wii U:** run the delivered RPX/WUHB and return screen/input/audio/exit,
  external-AOT guest frame/touch/restart/stability results and SD logs per
  `docs/console-testing.md`. Record firmware and homebrew/launcher environment.
  No commercial dump or encryption key is needed for these diagnostic builds.
- **Windows execution:** a manual workflow is prepared locally at
  `.github/workflows/windows-validation.yml`, including actual external AOT and
  Unicode execution. Publication/one run remain subject to the earlier requested
  approval. Native Windows, interactive WPF and Windows devkitPPC are unverified.

## Internal work still required

P1 packaging: implement an original encrypted FST/content/TMD/ticket writer and
independent decryption/integrity checks. The newly audited MIT-root makefst
candidate bundles GPLv2-or-later PolarSSL and uses plaintext content with test
issuers plus production certificates; it was not incorporated or run. The
selected route is an original scoped encrypted writer; that implementation and
its local key/certificate inputs remain work. Console installation/Menu launch
must validate its result.

P2/P3: inspect the supplied Mario revision, then implement required kernel/IPC,
CP15/TLS, multi-thread scheduling, PICA/GX2 state/shaders/textures/coherency,
DSP/audio, RomFS/saves and module behavior. Complete CPU/FP correctness and
measure real console costs. Existing raw framebuffer harness I/O does not supply
HID/GSP/PICA or conventional 3DS homebrew boot. No commercial profile is enabled.

P4/P5: automate the verified Windows conversion engine, caching/rebuild/trace and
save/title identity, then implement/package the responsive WPF GUI injector.

## Reusable development environment

The restored SDK/tooling works: pinned devkitPPC/WUT, Rust 1.95.0, .NET 10.0.401,
Unicorn and container Clang/CMake/GDB/PowerPC Linux/QEMU. Activate with
`source /workspace/ctr2wup-tools/environment/activate.sh`.
`install_script` remains the tested reusable installer. `start_skill` was updated
and saved to reflect resumed implementation, Mario selection and current gates.
Saving the draft is not environment publication or proof of a new-task snapshot.
Git/API read access works with existing authentication; no new secret is needed
for local development. The manual Windows workflow has not been dispatched.
