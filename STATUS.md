# CTR2WUP status

Updated 2026-10-07. Requirements: `docs/project-brief.md`.

Version 0.2.0 adds a working controlled NCCH → generated C → guest execution
pipeline, on top of the native diagnostic and host/portable foundations.
The full commercial ROM-to-WUP converter is incomplete. No milestone
requiring physical-console evidence is marked complete.

## Delivered and verified locally

- Source/license audit of all seven candidates with actual commit pins;
  `docs/foundation-audit.md` records risks and measured builds. External lifter:
  48 unit tests +1 doctest pass. Zakuro CPU reference: 92 instruction tests pass.
- Actual RPX/WUHB cross-compilation with official SDK image digest
  `sha256:44cb1a920e1ec3ec7c06767493c3b85f8d643d6137cc4661f0201895ac6e4967`;
  devkitPPC r50/GCC16.1.0, WUT1.9.1, wut-tools1.3.5. Fresh selective SDK extraction
  and idempotent installation tested; no signature/TLS/checksum bypass.
- Independent TV/GamePad diagnostics, calibrated/border-aware touch, sticks and
  buttons, A tone, X allocation test, Minus/lifecycle exit, timing and SD/UDP logs
  implemented and linked. Their hardware behavior remains unverified.
- Checked little-endian mapped C guest memory, bounded ARM/Thumb interpreter,
  direct generated-C fixture, missing-block diagnostics/fallback/invalidation and
  single-guest word-exclusive model. Both CPU paths are included in the RPX.
- Real NCCH extraction/loading with checked segment permissions, zeroed BSS,
  stack/TLS, bounded allocations and exact fixture profile validation. A bounded
  single-guest runtime implements exit/debug/ticks, sleep/yield and event/handle
  SVCs with stale-handle/timeout/resource/fault behavior. It does not implement
  multi-thread scheduling, IPC or HID/GSP services.
- Original 85-instruction ARM guest fills both framebuffers, responds to
  injected button/touch input, sleeps and exits. AOT/interpreter states and full
  data/BSS agree. Two touched/button frames execute 1,382,534 guest instructions,
  zero fallback instructions, and exit normally. `docs/guest-fixture.md` records
  exact identities, commands and matching image hashes. This is an explicit
  harness I/O contract, not a stock 3DS graphical homebrew application.
- Native and guest RPX/WUHB builds include the controlled guest; Y toggles modes,
  Plus restarts, B requests guest exit, Minus exits the host. Actual calibrated
  input and raw ARM framebuffer presentation are linked; hardware is unverified.
- CLI `doctor`, `inspect`, prototype/guest `build`, `make-fixture`, `run-fixture`,
  and `validate-package`; strict NCCH
  v2/NCSD checks, ExeFS hashes/BLZ/SMDH, revision/save identity separation,
  cancellation cleanup and atomic no-overwrite publication. Commercial build
  fails honestly before publishing any output.
- Original WUP structural validator and explicit local title registry; content
  tables/H3 hashes checked with authenticity/plaintext/install limits explicit.
- Bounded PICA command inspection and RGB565/viewport/touch reference utilities;
  unsupported draw execution is explicit.
- 88 Python tests pass; 51,147 CPU assertions and 221 guest assertions pass in
  each normal and ASan/UBSan run. Source and output paths with spaces/Japanese
  characters build successfully on Linux.
- Python wheel builds, installs offline into an isolated directory and performs
  real fixture inspection and guest translation/compilation/execution using the
  complete source recipes. This is Linux evidence, not a Windows execution pass.
- Versioned logs/report: `artifacts/reports/validation-v1-20261007T224242Z/`;
  17 checks passed. Regenerate with `python3 tools/validate.py`. Version 0.1.0
  reports and delivery bundles are preserved. Version 0.2.0 source, native and
  guest bundles plus the wheel and checksums are under `artifacts/delivery/`
  and `artifacts/dist/`.

## External tests and internal implementation still needed

Physical Wii U: use the delivered native/guest RPX/WUHB and
`docs/console-testing.md`; return screen/input/audio/exit/CPU evidence, guest
frame/color/touch/exit/restart/stability results and SD logs. PowerPC execution
and performance remain unverified. No commercial ROM is needed for either test.

Windows: clean-machine Python/devkitPro source build, cancellation, wheel/CLI
and Unicode path checks remain unverified; see `docs/build-windows.md`.

Internal gates: complete ARM/VFP/differential correctness and conventional
homebrew loading, multi-thread kernel/services/IPC, PICA-to-GX2
shaders/textures/TEV, DSP/audio, RomFS/saves and CRO/module behavior. The controlled
NCCH executes through the implemented harness; broader layouts, CP15 TLS,
memory-control SVCs and conventional service boot remain unsupported. No game profile is
enabled. `rebuild`/`import-trace`, caches/full packing pipeline and GUI are deferred.

Commercial title testing needs one user-owned fully decrypted Old 3DS revision
after the controlled fixture/runtime/rendering gates can support it. None has
been supplied; no commercial gameplay or save support is claimed.

Packaging needs a license-cleared implementation, certificate provenance and
secure local key configuration. Candidate packers have no identified license
grant; external lifter redistribution notices need completion. No installable
WUP, common key, proprietary SDK or commercial asset is included.

## Environment and continuation

`install_script` and `start_skill` were saved successfully to the environment
draft. Review and save them in environment settings, then publish the environment
through the product. This task has not published it or validated a new-task
restore. No network/secret bindings were added; the official Docker route works
around the observed pkg.devkitpro.org CONNECT403 denial. Existing Git auth worked.

The selected remote initially advertised no commits/refs. The user has now
authorized publishing version 0.2.0 to `origin/main`. Source and diagnostic bundles
also preserve the deliverable outside Git. Do not reset or force-push history
without user direction.

Next concrete task: inspect physical native/guest diagnostic logs and repair any
platform/CPU failures. Continue independently toward conventional homebrew boot:
expand proven CPU/TLS and memory-control semantics, then implement bounded IPC,
HID shared input and GSP framebuffer submission with a matching original fixture.
Add multi-thread scheduling and reference-tested PICA features before commercial
testing. Adapt the pinned external lifter's native-endian ABI before adopting it
as a PowerPC backend. Keep hardware and commercial gates open.
