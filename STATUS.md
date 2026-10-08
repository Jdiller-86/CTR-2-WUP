# CTR2WUP status

Updated October 8, 2026 (America/New_York); report filenames use UTC.
Requirements: `docs/project-brief.md`; product plan:
`docs/development-plan.md`; GUI workflow: `docs/gui-workflow.md`.
The user resumed implementation and selected **Super Mario 3D Land** as the first
title candidate. The target remains an UWUVCI-style Windows GUI: select a
decrypted game, configure title/artwork, click Inject, install its per-game WUP
and launch from Wii U Menu. The CLI is the shared engine, not the final product.

**Version 0.12.0 adds checked ARM/Thumb stack and register-transfer operations**.
An original 47-instruction nested ARM → Thumb → ARM call program preserves
registers and restores its stack through interpreter/original/external AOT.
Independent Unicorn ARM1176 checks pass 220 instruction cases and the full trace.
Partial-fault and unsupported-encoding probes validate the external ABI. All
Wii U builds embed `ARM/THUMB STACK CALLS`/`stack_selftest`; see `docs/cpu-stack.md`.
The requested overall estimate is approximately **15% of the final GUI-to-playable
WUP product**. This is a rough engineering estimate, not a measured percentage or
milestone tally. The main runtime/renderer/audio/save/gameplay/Windows GUI and
physical acceptance work remains; all seven full milestone checkboxes stay open.

**Version 0.11.0 adds shared GSP framebuffer queues and a VBlank relay**.
An original ARM guest now registers/maps the GSP page, queues both RGB565 screens,
waits for the relay and drains its IRQ ring with exclusive updates. It matches
interpreter/original/external AOT even when a host VBlank interrupts LDREX/STREX.
See `docs/gsp-queues.md`. The queued guest replaces direct submission in the
standard HID/GSP diagnostic mode; direct-display tests and all five decoders remain.
The earlier pause was an acceptance-evidence gate, not a block on all development.
Shared runtime work resumed without the ROM. Physical Wii U results and actual
Mario executable intake are still required; see `docs/current-roadblock.md`.

Version 0.7.0 added the checked heap/linear CPU backing lifecycle; see
`docs/guest-memory.md`.

Version 0.6.0 added shared TLS/SRV/CFG startup and its original ARM self-test;
see `docs/shared-services.md`.

Version 0.5.0 adds CIA intake and a self-contained Windows executable export
helper. The user supplied the Mario CIA, but its attachment exceeds the
workspace's 32 MiB transfer limit; no commercial bytes were inspected.
`docs/cia-intake.md` explains the small private export workaround.
Version 0.4.0 adds original encrypted homebrew packaging and independent
decryption/FST/file verification to v0.3.0's external-lifter C generation and
checked static execution/linking, title staging and private revision intake.
Commercial conversion and the Windows GUI remain incomplete. Installer acceptance
is unverified; no milestone requiring physical-console evidence is complete.

## Verified implementation

- User-mode ARM LDM/STM, Thumb16 PUSH/POP/LDMIA/STMIA and original-AOT register
  BLX calls use checked word access and interworking. Base writeback occurs after
  successful transfers; partial faults preserve earlier words and the original
  base. Empty/user-bank/PC-store/unpredictable forms fail visibly. 160 API
  assertions plus the nested-call self-test pass; the external comparison has
  352 normal/fault assertions and 48 invalid-Thumb assertions. The independent
  reference has 220 instruction cases and a 47-instruction full-program trace.
  Thread scheduling, Thumb long-immediate calls, complete ARM/VFP and actual
  PowerPC timings remain open.

- GSP relay registration returns a retained read/write shared page and client0.
  Framebuffer updates commit both screens atomically at the diagnostic VBlank;
  IRQ2/3 wrap/full/coalesced cases, exclusive invalidation, source faults and
  typed resource/lifetime rollback are checked. All Wii U builds embed the relay
  self-test and queued original-AOT display guest. 762 API assertions and the original self-test pass normally and with sanitizers.
  The external comparison has 11,541 assertions, 1,383,167 instructions and
  34 SVCs per path, zero fallback and four reference images; GCC/sanitizers,
  Clang and emulated PPC pass. No stock libctru startup,
  real interrupt timing, physical execution, GX commands or PICA is claimed.
- Standard direct GSP acquire/release/force-black/SetBufferSwap checks rights,
  modes and readable source extents before state changes. Presentation handles
  rotated RGBA8/BGR8/RGB565/RGB5A1/RGBA4, padding and adjacent mappings; invalid
  source leaves host output unchanged. 362 API assertions and the original
  HID/GSP graphics self-test pass normally and under sanitizers. The external
  display comparison has 3,603 assertions including four reference PPMs, and
  passes Clang/emulated PPC. All Wii U builds embed the self-test and original
  AOT display guest. GX commands, physical/VRAM translation, PICA and stock
  graphical startup remain absent. No console run
  or gameplay is claimed.
- Checked unsigned immediate preindexed ARM LDRH/STRH without writeback works
  in interpreter/original AOT and is exercised by the external graphics probe.
  Conditions, golden halfwords, alignment/permissions, fault PCs and unsupported
  forms are checked; this does not complete ARM transfer semantics.

- HID GetHandles returns checked shared/event copies; shared input is guest
  read-only. The original ARM consumer maps, waits, reads pad/circle/touch and
  unmaps/closes/exits through standard interfaces. 858 external boundary and 336
  API assertions cover state/bytes/lifetimes, transitions, wrap timestamps,
  invalid input and capacity rollback. GCC/sanitizers, Clang and emulated PPC
  checks pass. All Wii U builds embed its self-test and a live original-AOT
  VPAD-to-ARM consumer. Physical input, timing and shutdown remain unverified;
  motion/New-family controls and conventional homebrew boot remain absent.

- Shared/mutex/semaphore handles share the generation-tagged table. Mappings and
  waits retain objects independently of handles. Heap-backed shared objects pin
  their source allocation; aliases enforce rights and report Shared state.
  Shared backing shares the 64 MiB budget. Wait-any selects the first ready
  object; wait-all consumes nothing until every distinct object is ready.
  Close/stale/invalid/timeout/resource failures are checked. This does not
  implement cross-process ownership, priority inheritance or multi-thread scheduling.
- The original 148-instruction ARM shared/sync probe matches state/memory across
  interpreter/original/external AOT with 639 boundary assertions and zero fallback.
  The 197 kernel API assertions and probe pass GCC, sanitizers, Clang and emulated
  PPC Linux. All three RPX/WUHB builds embed its original `SHARED/SYNC LIFECYCLE`
  and `kernel_selftest` result. Console/native Windows acceptance remains pending.

- ControlMemory/QueryMemory enforce page bounds, ownership and permissions,
  with zero-filled first-fit heap/Old-family linear CPU backing. Partial free/
  protection preflight mappings and capacity atomically. Loader image/stack/TLS
  cannot be freed or reprotected. Guard/write-only permissions, holes, resource
  limits and retained-backing release are tested. Limits are 16 allocations,
  32 fragments and 64 MiB total retained backing. Linear GPU/DSP physical-address
  translation remains absent; local shared aliases are now implemented.
- The original heap probe executes 151 instructions, 14 memory SVCs and exit.
  External/original AOT and interpreter CPU/mapping/backing match at 1/7/23
  budgets; 1,778 assertions cover lifecycle and protected/freed guest-store fault
  PCs with zero fallback. Ordinary data mapping changes preserve AOT snapshots;
  executable mapping/protection and executable-alias writes still invalidate.
  The 307 heap API assertions pass GCC, sanitizers, Clang and emulated PPC Linux.
  All three RPX/WUHB builds embed `GUEST HEAP LIFECYCLE`/`heap_selftest` results;
  physical Wii U and native Windows acceptance remain pending.

- ARM user read-only CP15 TLS access works in interpreter/original/external AOT.
  The external ABI refreshes TLS on execution entry and rejects unsupported
  cache/barrier operations even when the pinned emitter would erase them.
- Typed, reference-counted service sessions share the generation-tagged handle
  table. Checked ConnectToPort/SendSyncRequest supports SRV registration/CFG
  lookup and configured region/model/language reads. It rejects malformed
  messages, unsupported commands, wrong handles and invalid/aliased output.
  The Old2DS model ID is verified against libctru (3; New3DS2 is rejected).
- The original 89-instruction ARM startup probe executes five IPC requests/nine
  SVCs with matching state/memory through all three backends. 297 external
  boundary assertions include TLS changed after attach; 35 CP15 assertions cover
  conditional skips and explicit cache/DMB/DSB/ISB failures. These and the 1,001
  service API assertions pass on Clang and emulated big-endian PPC Linux/QEMU.
  All three RPX/WUHB builds embed the original interpreter/AOT service self-test
  and native-mode/SD-log result. Physical service execution remains unverified.

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
  +297 service-startup assertions +35 CP15 assertions +1,778 heap assertions +639 shared/sync +858 HID +3,603 direct HID/GSP display assertions**, plus the v0.11 relay probe
  under GCC, ASan/UBSan, Clang and emulated 32-bit big-endian PowerPC
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
- **150 Python tests**, 51,202 CPU assertions, 221 original-guest assertions,
  1,001 service, 307 heap, 197 kernel, 336 HID, 362 direct GSP, 762 relay and 160 stack/transfer
  assertions pass; all C suites also pass ASan/UBSan. The complete v0.12.0 runner
  passes **33 checks**, including all eleven external probes normally/sanitized,
  three RPX builds, staging/intake, offline encrypted packaging and installed-wheel
  inspection/execution/packing. Crypto remains an optional external dependency.

## Reports and deliverables

The v0.12 source, native diagnostic, original/external guest and title-staging
bundles, wheel and SHA256 manifest are under `artifacts/delivery/` and
`artifacts/dist/`. The native bundle includes direct and shared display references.
Diagnostic output is RPX/WUHB, not an installable WUP.
Delivery/fresh-source verification report: `artifacts/reports/delivery-v12/report.json`.

Main validation: `artifacts/reports/validation-v1-20261008T203213Z/report.json`.
New compiler/target checks: `artifacts/reports/stack-clang-v12/report.json`
and `artifacts/reports/stack-ppc-v12/report.json` (24 checks each).
Independent reference: `artifacts/reports/multiple-oracle-final-v12/report.json`
(220 instruction cases, 47-instruction nested-call trace).
Earlier v0.11 evidence: `validation-v1-20261008T193827Z/report.json`,
`relay-clang-corrected-v11/report.json`, `relay-ppc-v11/report.json` under
`artifacts/reports/`.
Initial v0.11 failed reports are preserved; stricter monitor comparison exposed
and then verified a corrected ordinary-store exclusive validity flag.
Earlier v0.10 evidence: `validation-v1-20261008T172231Z/report.json`,
`gsp-clang-v10/report.json`, `gsp-ppc-v10/report.json` under `artifacts/reports/`.
Helper evidence: `artifacts/reports/cia-helper-final/report.json` (13 checks).
Earlier checks: `artifacts/reports/upstream-ppc-final/`,
`upstream-clang-final/`, and the Unicorn reference report under
`upstream-native-v5/`. `python3 tools/validate.py` regenerates the complete checks;
`tools/validate-upstream.py` supports GCC/sanitizers/container compiler/QEMU runs.
See `docs/upstream-integration.md` for commands and precise limits.

Versioned source/native/original-guest/external-guest delivery bundles, wheel,
checksums and original title-staging output are under `artifacts/`. Earlier
v0.1.0/v0.2.0/v0.3.0/v0.4.0/v0.5.0/v0.6.0/v0.7.0/v0.8.0/v0.9.0/v0.10.0/v0.11.0 reports and bundles are preserved. Source is maintained in the
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
  TLS/SRV/CFG, heap, shared/sync, stack-call and HID PASS/FAIL/SD log, live ARM-read
  buttons/Circle Pad/touch/snapshot/clean exit, HID/GSP rotated color/corner/touch
  display/stability and exit,
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

P2/P3: inspect the supplied Mario revision, then expand the kernel/IPC and
CP15/thread context beyond the tested shared startup subset. Implement memory
control beyond the checked owned-heap/shared-object subsets, cross-process maps,
multi-thread scheduling, PICA/GX2 state/shaders/textures/coherency,
DSP/audio, RomFS/saves and module behavior. CFG user/Mii/console-unique identity
and broader configuration blocks remain pending. Complete CPU/FP correctness and
measure real console costs. The original framebuffer harness uses custom I/O; the separate standard-HID
consumer and direct/queued HID/GSP graphics guests supply standard-interface evidence.
GSP GX commands, threads/PICA and conventional 3DS homebrew boot remain unimplemented. No commercial profile is enabled.

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
