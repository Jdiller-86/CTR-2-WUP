# CTR2WUP status

Updated October 7, 2026 (America/New_York); report filenames use UTC.
Requirements: `docs/project-brief.md`; product plan:
`docs/development-plan.md`; GUI workflow: `docs/gui-workflow.md`.
The user resumed implementation and selected **Super Mario 3D Land** as the first
title candidate. The target remains an UWUVCI-style Windows GUI: select a
decrypted game, configure title/artwork, click Inject, install its per-game WUP
and launch from Wii U Menu. The CLI is the shared engine, not the final product.

**Version 0.5.0 adds CIA intake and a self-contained Windows executable export
helper.** The user supplied the Mario CIA, but its attachment exceeds the
workspace's 32 MiB transfer limit; no commercial bytes were inspected.
`docs/cia-intake.md` explains the small private export workaround.
Version 0.4.0 adds original encrypted homebrew packaging and independent
decryption/FST/file verification to v0.3.0's external-lifter C generation and
checked static execution/linking, title staging and private revision intake.
Commercial conversion and the Windows GUI remain incomplete. Installer acceptance
is unverified; no milestone requiring physical-console evidence is complete.

## Verified implementation

- Original bounded CIA parsing verifies metadata tables, ticket/TMD identity,
  sparse bitmap/content extents and plaintext hashes; selects executable index 0
  and rejects base-title/encryption failures explicitly. Checked loader offsets
  and external lifter analysis/C generation work for original CIA fixtures.
- `prepare-intake` creates a small derived executable CXI/receipt ZIP, omitting
  RomFS/tickets/certificates/keys and preserving decompressed executable revision.
  Encryption produces a report-only archive. Source and prior outputs are preserved.
- The separate .NET console helper produces identical derived CXI bytes for
  tested inputs, including compressed code and >32 MiB original placeholder data.
  It passes 13 Linux .NET cases; the self-contained Windows x64 EXE is
  cross-published with its .NET 10.0.12 runtime/notices. It needs no separate
  Python/SDK/runtime installation. Native Windows execution remains unverified;
  it is an intake utility, not the final GUI/injector.

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
- `pack-title` streams four encrypted `0x2001` FST/code/content/meta contents,
  original TMD/ticket rights and locally supplied certificates. It wraps a fresh
  title key with a user-local common key and publishes only after independent
  decryption/hash/FST/source-file checks. It refuses changed/unsafe inputs and
  existing outputs. Signatures are absent; FST/COS/installer behavior is pending.
  No credentials are bundled or printed. `verify-package` is a separate reader.
- The real WUT RPX passes staging -> encrypted packing -> independent verification
  and both CLI paths with spaces/Japanese characters. **Synthetic key/certificates
  only**, temporary package deleted; this is not an installable title. OpenSSL
  independently decrypts the ticket and all four test contents. Wrong keys,
  repaired-hash malicious FSTs, corruption, mutation and cancellation are tested.
- `analyze-title` writes a private exact-hash intake with segments, stack/BSS,
  section inventory, extended-header service allowlist and optional lifter
  analysis. Declared service access is not executed/implemented IPC. No intake
  enables a compatibility profile. Mario investigation leads and the proposed
  gameplay/save route are in `docs/first-title.md`.
- Existing strict NCCH/NCSD/ExeFS/BLZ/SMDH inspection, safe loader, bounded CPU,
  single-guest events/handles/sleep/debug/exit, native diagnostics and structural
  WUP validator remain tested. Commercial `build` still fails without publishing
  a placeholder package. Paths with spaces/Japanese characters work on Linux.
- **131 Python tests**, 51,147 CPU assertions and 221 original-guest assertions
  pass; both C suites also pass ASan/UBSan. The complete v0.5.0 runner passes
  **26 checks**, including external generation/execution, all three RPX builds,
  original staging/intake, offline encrypted packaging and installed-wheel
  inspection/execution/packing. Crypto remains an optional external dependency.

## Reports and deliverables

Main validation: `artifacts/reports/validation-v1-20261008T015329Z/report.json`.
Helper evidence: `artifacts/reports/cia-helper-final/report.json` (13 checks).
Additional checks: `artifacts/reports/upstream-ppc-final/`,
`upstream-clang-final/`, and the Unicorn reference report under
`upstream-native-v5/`. `python3 tools/validate.py` regenerates the complete checks;
`tools/validate-upstream.py` supports GCC/sanitizers/container compiler/QEMU runs.
See `docs/upstream-integration.md` for commands and precise limits.

Versioned source/native/original-guest/external-guest delivery bundles, wheel,
checksums and original title-staging output are under `artifacts/`. Earlier
v0.1.0/v0.2.0/v0.3.0/v0.4.0 reports and bundles are preserved. Source is maintained in the
existing Git checkout; do not reset, force-push or replace user changes.

## Awaiting user inputs and physical tests

- **Mario intake:** the user supplied a CIA whose filename suggests USA
  `0004000000054000` / `CTR-P-AREE` / `v0.3.0`, but it exceeds the attachment
  tool's 32 MiB limit. Those filename claims, encryption and actual executable
  revision are unverified. Use the delivered helper and return only its small
  private `CTR2WUP-intake.zip`. The full original can stay local for later asset
  work. If it reports encryption, decrypted executable data is still needed.
  No commercial profile is enabled until actual private intake and runtime evidence.
- **Wii U:** run the delivered RPX/WUHB and return screen/input/audio/exit,
  external-AOT guest frame/touch/restart/stability results and SD logs per
  `docs/console-testing.md`. Record firmware and homebrew/launcher environment.
  No commercial dump or encryption key is needed for these diagnostic builds.
- **Windows execution:** a manual workflow is prepared locally at
  `.github/workflows/windows-validation.yml`, including actual external AOT and
  Unicode execution. Publication/one run remain subject to the earlier requested
  approval. Native Windows, interactive WPF and Windows devkitPPC are unverified.
- **Packaging inputs/console:** configure the documented user-local key and
  certificate chain outside staging, establish provenance and report the actual
  homebrew/installer environment. Then verify a diagnostic package's installation,
  Menu launch, dual screens/input/audio/exit and removal. No authentic signatures,
  production credentials or installation test are available in this environment.

## Internal work still required

P1 packaging now has the original scoped writer, local-input contract and
independent integrity proof. Physical installer/Menu results must validate its
nonhashed contents, FST logical clusters/ownership, XML/COS/artwork and ticket
policy before expanding the profile or claiming installation. The MIT-root
makefst candidate was not incorporated or run; no packer code or credential
template was copied. Full hashed/shared/update/disc formats remain unsupported.

P2/P3: inspect the supplied Mario revision, then implement required kernel/IPC,
CP15/TLS, multi-thread scheduling, PICA/GX2 state/shaders/textures/coherency,
DSP/audio, RomFS/saves and module behavior. Complete CPU/FP correctness and
measure real console costs. Existing raw framebuffer harness I/O does not supply
HID/GSP/PICA or conventional 3DS homebrew boot. No commercial profile is enabled.

P4/P5: automate the verified Windows conversion engine, caching/rebuild/trace and
save/title identity, then implement/package the responsive WPF GUI injector.

## Reusable development environment

The restored SDK/tooling works: pinned devkitPPC/WUT, Rust 1.95.0, .NET 10.0.401,
Unicorn, cryptography 50.0.0 and container Clang/CMake/GDB/PowerPC Linux/QEMU. Activate with
`source /workspace/ctr2wup-tools/environment/activate.sh`.
`install_script` remains the tested reusable installer. `start_skill` was updated
and saved to reflect resumed implementation, Mario selection and current gates.
Saving the draft is not environment publication or proof of a new-task snapshot.
Git/API read access works with existing authentication; no new secret is needed
for local development. The manual Windows workflow has not been dispatched.
