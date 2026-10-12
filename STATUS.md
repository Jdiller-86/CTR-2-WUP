# CTR2WUP status

Updated October 11, 2026 (America/New_York); report filenames use UTC.
Requirements: `docs/project-brief.md`; product plan:
`docs/development-plan.md`; GUI workflow: `docs/gui-workflow.md`.
The user resumed implementation and selected **Super Mario 3D Land** as the first
title candidate. The target remains an UWUVCI-style Windows GUI: select a
decrypted game, configure title/artwork, click Inject, install its per-game WUP
and launch from Wii U Menu. The CLI is the shared engine, not the final product.

**Overall implementation estimate: approximately 20%**, reassessed against the
v0.14 GUI-to-playable-WUP target on October 9, 2026. This is a rough engineering
judgment, not measured task completion. **0/7 full milestones are accepted.**
See [the completion assessment](docs/progress.md) for its basis, remaining work,
and external testing gates; the README provides a concise progress overview.

**Version 0.14.4 follows confirmed native exit and inconsistent guarded A input.**
The user confirms Minus now returns normally. The first v0.14.3 guarded run
produced green late, after L/advancement, and exited; a second launch froze the
whole console while holding A. This is a delayed response plus an intermittent
stability failure, not a successful guarded acceptance test. The two latest
TXT reports have not been supplied. Return both `compiled-guarded-*.txt` files
before another console run (`docs/console-retest.md`). Save testing remains deferred.
Feedback is saved privately; no new ROM/intake or installation evidence exists.

Cached nearest-neighbor coordinate maps/repeated rows remove per-pixel 64-bit
scaling divisions and preserve all RGBA bytes/borders/padding in desktop tests.
The guide requires a fresh decoded green frame after A before enabling R and
logs response delay; user confirmation remains separate from pixel evidence.
The old AOT engine can dispatch forever without charging an instruction budget;
a 64-idle-dispatch guard now yields a visible missing-block fault. The old engine
fails a deadline regression and the new engine passes. This has not been linked
to the console freeze. Eight live-input timing replays of the original compiled
guest (standard/guarded, 1 ms to 1 s clocks, 10,000/50,000 budgets) complete repeated
red/green transitions and exit. At 285 ms host pacing the response takes about
7.1 s; initial blank frames are counted separately, with no mixed frames in these
cases. No physical performance improvement or freeze resolution is claimed.

Native exit is positive scoped evidence for v0.14.3. v0.14.4 code/build validation
does not establish console stability. Implementation remains approximately 20%
and all seven full milestone gates stay open.

**Version 0.14.3 corrects shutdown ordering and adds a compiled-input comparison.**
The user clarifies that Minus reaches the Menu splash and keeps playing its music,
but never reaches the actual menu. This is a transition stall, not a complete
console freeze. The compiled app still freezes the whole system when holding A,
without green; no tone played in the newer run. Latest-run TXT files have not
been supplied. The first v0.14.1 native report remains positive scoped evidence
for ten guided steps/nine embedded checks and resource cleanup. Save testing
remains deferred. Feedback/received logs remain under ignored `private/`.

The previous helper could shut ProcUI down before app resource cleanup. The app
now polls ProcUI directly, pauses input/rendering outside the foreground, cleans
owned guest/audio/GX2/report resources, clears stale graphics callbacks, preserves
save acknowledgement, then requests Menu and waits for EXITING before ProcUI
shutdown. HBL relaunch also follows cleanup/ProcUI shutdown. Native AX ownership
is tracked across foreground changes; compiled apps never initialize/quit AX.

A new guarded compiled-AOT app limits native call nesting to two instead of 64,
using the existing unwind/dispatcher. It runs the same original guest with exact
matching frames/input/exit and recorded depth/unwinds on desktop. App menu names
show 0.14.3. Checkpoints include call depth, unwinds, frame-boundary stack headroom
and lifecycle states; they do not identify the cause of the console freeze.
Follow `docs/console-retest.md`: native Minus first, guarded A next only if exit
works, then standard AOT if guarded succeeds. Full console resolution remains
**awaiting retest**; rough implementation stays 20% and no full gate is closed.

The interpreter comparison exposed missing startup instructions. ARMv6
SXTB/SXTH/UXTB/UXTH rotations/conditions/aliases now pass 2,048 independent Unicorn
ARM1176 cases and unit/sanitizer checks. Full compiled-interpreter startup still
stops visibly on unsupported STRD; no interpreter app is delivered or claimed.

**Version 0.14.2 attempted the first console corrections; follow-up failures are above.**
The received v0.14.1 native TXT records ten passing guided steps, nine passing
software self-tests, zero input errors/faults and completed resource cleanup.
The user reports a freeze after Minus, requiring reboot. This is partial physical
evidence, not successful launcher return. The newer compiled run reportedly
shows red/blue and freezes after A's native tone; its run log was not supplied.
The older uploaded v0.14.0 `.log` identifies compiled-interactive and records a
guest PASS; it must not be attributed to the newer failed run. Save testing was
not attempted. Uploaded raw logs and receipts remain under ignored `private/`.

Menu launches now request SYSLaunchMenu and poll ProcUI until exit; HBL/Mii Maker
routes retain their relaunch path. WUT owns the SD mount, with no duplicate WHB
mount/unmount. UDP is optional/off by default. Compiled apps do not initialize
or stop AX or play native tones; A is guest input only. Bounded input/guest/render
checkpoints, visible progress counters and closed-file exit stages support the
next diagnosis. Local checks passed, but the user's follow-up reports both
failures persisting. Current v0.14.3 steps are in `docs/console-retest.md`; defer
long guides and the save app. No full milestone is closed.

**Version 0.14.1 made console testing guided and recorded readable TXT reports.**
The native, compiled-interactive and compiled-save apps prompt for inputs and
controller confirmations, record completed/failed/skipped steps, and show report
filenames and upload instructions. Button/stick/touch samples, guest PASS/exit,
save reload, diagnostic timing and an explicit session summary are recorded.
Exclusive per-launch files preserve previous reports; checked writes expose SD
errors. Interrupted/cancelled tests remain incomplete, and subjective visual/audio
confirmations are labeled as user evidence. See `docs/guided-testing.md`.
This improves test collection; the implementation estimate remains roughly 20%
and all seven full acceptance gates remain open. Partial console results and
remaining failures are recorded above.

October 11 private intake: a valid report-only ZIP was received. The helper
reports `encrypted_cia_content` for the Mario executable; no executable view or
game assets were supplied. The original CIA bytes were not independently inspected.
The user is preparing hardware tests; decrypted executable intake remains needed.

**Version 0.14.0 adds bounded private FS saves and standalone APT lifecycle**.
Normal compiled libctru/newlib code writes, rereads and commits original save data,
uses a Japanese filename, deletes a temporary file, exits, then reloads and updates
both records in a fresh process. Guest paths never become host paths. Checked
archive/file ownership, access/buffer bounds, read-only mounts and CRC-checked
snapshot publication support shared filesystem work without a game patch.
Standalone APT runs its real library event thread, wakeup and process shutdown;
the boot checks `aptIsActive`/`aptMainLoop`. The only remaining traced startup
lookup failures are privileged APT:S/APT:A and optional ptm:sysm. A fifth
RPX/WUHB supplies an original persistent-save console diagnostic. FS has 1,387
normal/sanitized assertions and APT has 671; see `docs/filesystem.md`.
ROMFS/assets/retail save integration, applets, GX/PICA/audio, Mario execution,
installer and Windows GUI remain unfinished. Physical acceptance remains open.

**Version 0.13.0 runs original compiler-built libctru applications** through
normal CRT/newlib startup and checked external AOT, with zero interpreter fallback.
Boot, real libctru threads/LightLock/C TLS, standard HID edges/Circle Pad/touch,
and actual graphics-thread/shared-GSP RGB565 display are verified on desktop.
All three exact two-screen frames are checked pixel by pixel. An additional
interactive RPX/WUHB is built for live console input, touch display and guest exit.
Eight guest thread slots preserve CPU/TLS/VFP/FPSCR context; address arbitration,
quota queries, BSS layout and the kernel configuration page supply shared startup
prerequisites. CPU media/FPSCR/scalar FP transport/ARMv6 barriers/SMLAxy have
independent ARM1176 evidence (870 cases). See `docs/guest-threads.md` and
`docs/compiled-homebrew.md`. The v0.13 startup recorded eight failed IPC requests
(six APT lookups, one fs:USER lookup and one invalid-session request); no complete
service or commercial boot is claimed.

**Version 0.12.0 added checked ARM/Thumb stack and register-transfer operations**.
An original 47-instruction nested ARM → Thumb → ARM call program preserves
registers and restores its stack through interpreter/original/external AOT.
Independent Unicorn ARM1176 checks pass 220 instruction cases and the full trace.
Partial-fault and unsupported-encoding probes validate the external ABI. All
Wii U builds embed `ARM/THUMB STACK CALLS`/`stack_selftest`; see `docs/cpu-stack.md`.

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

- Normal devkitARM/libctru/newlib boot reaches main and exits after 12,416
  instructions/35 SVCs. Real libctru threads/LightLock/initialized C TLS pass in
  19,989 instructions/40 SVCs. Standard input passes 40,539 instructions/38 SVCs.
  Graphics and input-driven interactive guests produce all expected pixels in
  3,653,198/3,653,794 instructions, with zero fallback; unavailable APT/FS lookups
  remain explicit. The interactive application also matches on emulated PPC
  Linux; real compiled threads pass Clang. The scoped native physical results and
  remaining compiled/save/exit gates are recorded above.
- Eight process-local thread slots have separate CPU/TLS/raw FP state and waits,
  strict priority/32-instruction quanta, retained handles/joins, stack pins,
  recursive mutex ownership and exit cleanup. Address arbiters/local quota
  output preflight are checked. The thread API has 310 normal/sanitized assertions;
  the original three-thread probe has 2,694 external boundary assertions, and
  FP contexts have a separate 61-assertion external arithmetic probe. All native
  diagnostics embed `GUEST THREAD SCHEDULER`/`thread_selftest`.

- User-mode ARM LDM/STM, Thumb16 PUSH/POP/LDMIA/STMIA and original-AOT register
  BLX calls use checked word access and interworking. Base writeback occurs after
  successful transfers; partial faults preserve earlier words and the original
  base. Empty/user-bank/unpredictable forms fail visibly; v0.13 implements ARM
  STM's PC+8 source value. 160 API
  assertions plus the nested-call self-test pass; the external comparison has
  352 normal/fault assertions and 48 invalid-Thumb assertions. The independent
  reference has 220 instruction cases and a 47-instruction full-program trace.
  Thumb long-immediate calls, complete ARM/VFP and actual
  PowerPC timings remain open.

- GSP relay registration returns a retained read/write shared page and client0.
  Framebuffer updates commit both screens atomically at the diagnostic VBlank;
  IRQ2/3 wrap/full/coalesced cases, exclusive invalidation, source faults and
  typed resource/lifetime rollback are checked. All Wii U builds embed the relay
  self-test and queued original-AOT display guest. 762 API assertions and the original self-test pass normally and with sanitizers.
  The external comparison has 11,541 assertions, 1,383,167 instructions and
  34 SVCs per path, zero fallback and four reference images; GCC/sanitizers,
  Clang and emulated PPC pass. No complete stock service startup,
  real interrupt timing, physical execution, GX commands or PICA is claimed.
- Standard direct GSP acquire/release/force-black/SetBufferSwap checks rights,
  modes and readable source extents before state changes. Presentation handles
  rotated RGBA8/BGR8/RGB565/RGB5A1/RGBA4, padding and adjacent mappings; invalid
  source leaves host output unchanged. 508 API assertions (including v0.13
  checked coherent CPU cache flush/invalidation) and the original
  HID/GSP graphics self-test pass normally and under sanitizers. The external
  display comparison has 3,603 assertions including four reference PPMs, and
  passes Clang/emulated PPC. All Wii U builds embed the self-test and original
  AOT display guest. GX commands, physical/VRAM translation, PICA and stock
  GPU rendering remain absent. Scoped compiled CPU-framebuffer startup is now
  verified separately. No console run
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
  motion/New-family controls remain absent; compiled libctru HID input is now
  verified separately.

- Shared/mutex/semaphore handles share the generation-tagged table. Mappings and
  waits retain objects independently of handles. Heap-backed shared objects pin
  their source allocation; aliases enforce rights and report Shared state.
  Shared backing shares the 64 MiB budget. Wait-any selects the first ready
  object; wait-all consumes nothing until every distinct object is ready.
  Close/stale/invalid/timeout/resource failures are checked. This does not
  implement cross-process ownership or priority inheritance. v0.13 adds the
  separate bounded guest-thread scheduler and owner-aware locks.
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
  The current 306 heap API assertions pass GCC, sanitizers, Clang and emulated PPC Linux.
  All three RPX/WUHB builds embed `GUEST HEAP LIFECYCLE`/`heap_selftest` results;
  physical Wii U and native Windows acceptance remain pending.

- ARM user read-only CP15 TLS access works in interpreter/original/external AOT.
  The external ABI refreshes TLS on execution entry and rejects unsupported
  cache operations even when the pinned emitter would erase them. v0.13 explicitly
  implements user ARMv6 DMB/DSB ordering for checked CPU backing.
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
- **155 Python tests**, 51,202 CPU assertions, 222 original-guest assertions,
  1,001 service, 306 heap, 197 kernel, 336 HID, 508 direct GSP, 762 relay,
  160 stack/transfer and 310 thread assertions pass; all C suites also pass
  ASan/UBSan. The complete v0.13.0 runner passes **36 checks**, including all
  14 external probes normally/sanitized, four RPX/WUHB builds, all five compiled
  libctru applications normally/sanitized, staging/intake, offline encrypted
  packaging and installed-wheel inspection/execution/packing. Required
  `make check` and `make prototype` pass. Crypto and ARM/libctru are optional
  external developer dependencies.

## Reports and deliverables

v0.14.4 verification is under `artifacts/reports/input-delay-v0144/`: required
`make check` covers 155 Python tests and all normal/ASan/UBSan C suites. The
lifecycle/guide/scale/report suite has 425 assertions, including a delayed fresh
frame, stale-frame rejection and pixel-by-pixel equivalent viewport/pitch/border
output. AOT progress tests have 19 assertions per run, preserving valid sparse
progress while rejecting empty/unwinding/cyclic dispatches; the old engine's
observed deadline failure is recorded independently. Original interactive and
save validators/cross-builds pass; eight live-input replays run standard and
2-deep guarded AOT through held/released A, repeated red/green and guest exit.
Source and the SD kit are under `artifacts/delivery/input-delay-0.14.4/`.
The actual intermittent console freeze requires the user's two guarded TXT
reports; the diagnostic update is not a verified hardware fix.


v0.14.3 evidence is under `artifacts/reports/menu-exit-v0143/`: required `make check`
passes 155 Python tests and all normal/ASan/UBSan C suites. The guide/report/lifecycle
suite has 347 assertions, including the actual exit coordinator's resource,
callback, Menu, foreground release, background, EXITING, ProcUI shutdown and HBL
order. CPU tests have 51,796 assertions; optional ARM extension reference has
2,048 independent cases. Current native/save and standard/guarded interactive
RPX/WUHB builds are cross-compiled; interactive variants match all original exact
frames/input/exit with zero fallback. Guarded execution must actually exercise
unwind, not just compile with a different limit. See current builder receipts.
Source and the four-app SD kit are under `artifacts/delivery/menu-exit-0.14.3/`.
Physical menu return/input freeze resolution, save and WUP acceptance remain open.


v0.14.2 verification is under `artifacts/reports/console-retest-v0142/`:
required `make check` passes 155 Python tests and all normal/ASan/UBSan C suites;
the guide/report/exit suite has 213 assertions per run, including all WUT HBL
routes, idempotent menu exit requests and preserving reports across closed-file
checkpoints/missing files/write failures. `make prototype`, the current native
build and full interactive/save validators pass. The compiled validators have
8/10 checks, exact interactive frames and first/fresh-process save/reload with
zero fallback, plus actual rebuilt RPX/WUHB pairs. The SD-ready kit and matching
source are under `artifacts/delivery/retest-0.14.2/`. No corrected console run or
save/installer acceptance is inferred from these desktop/build results.

v0.14.1 guided-test verification is under `artifacts/reports/txt-reports-v0141/`:
the required `make check` passes 155 Python tests and all normal/sanitized C suites;
the final guide/report suite checks successful, failed, skipped, cancelled and
interrupted evidence, report preservation/write failures, countdown boundaries,
old-session rejection and fresh-launch reload. `make prototype` and three current
RPX/WUHB builds pass. The original interactive and filesystem applications are
revalidated on desktop, including exact frames and save/fresh-process reload.
Final console receipts explicitly retain that original host evidence while
recording fresh console build logs and hashes after prompt refinements.
The source and SD-ready kit are under `artifacts/delivery/guided-0.14.1/`;
`manifest.json` records verified hashes and the source snapshot. These are new
diagnostics, not installable WUPs. The later partial console results are recorded above.

October 9 documentation follow-up: the README now presents the product workflow,
current limits, progress graphic and milestone overview; `docs/progress.md`
records the reassessment. Fresh `make check` (155 Python tests plus normal/
ASan/UBSan C suites), `make prototype`, and the README's diagnostic/fixture
quick start pass. Documentation verification checks 26 local links, SVG metadata
and all seven open acceptance boxes. Logs/reports are under
`artifacts/reports/readme-assessment-20261009/`. The v0.14 source bundle is the
previously verified snapshot; it predates this documentation-only follow-up.

The v0.14 source, native/original/external/compiled interactive and compiled-save
RPX/WUHB bundles, wheel and SHA256 manifest are under `artifacts/delivery/` and
`artifacts/dist/`. Consolidated current-artifact evidence is
`artifacts/reports/validation-v1-20261009T183044Z/report.json`. It reuses the 155
Python tests and 38 observed successful workflow checks from the completed
`validation-v1-20261009T182145Z/report.json`, with an identical current runtime
digest. Fresh retry component reports verify all 14 external probes and six
compiled applications normally/sanitized; five current RPX/WUHB builds and exact
builder/packaging hashes were independently checked. The retry lost its execution
session before publishing the aggregate; its outer exit is explicitly unverified.
The validator now journals observed statuses before stdout and tolerates a closed
pipe. `closed-pipe-fixed-v14/report.json` verifies this with a real closed OS pipe
and observed subprocess exit codes 0/2. No lost exit status was fabricated.
The filesystem application verifies save/read then fresh-process reload, with
57,135/58,806 guest instructions, zero fallback and exact snapshot bytes.
The first run has one expected missing-file result plus the three explicitly
unavailable startup lookups; reload has only those three lookup failures.
The scoped boot trace is `libctru-startup-final-v14/report.json` (32 IPC requests,
19,620 instructions, 97 SVCs, six switches). GCC, Clang and emulated PPC snapshots
all hash to `a8a36b8acb27ca58bf4664e75bb960c76c4393deac9a13744c93173042cc7d78`.
Independent reports: `libctru-filesystem-clang-v14/report.json`,
`libctru-filesystem-ppc-v14/report.json`, `upstream-clang-fs-apt-container-v14/report.json`
and `upstream-ppc-fs-apt-v14/report.json` (six checks each). The source/runtime
required `make check` / `make prototype` passes are preserved in
`make-check-final-v14.log` and `make-prototype-sd-path-v14.log`.
The initial full v0.14 pass is preserved at `validation-v1-20261009T182145Z/`;
final builds include the corrected WUT SD mount path/version banner. A failed
spaced generated-source Make build prompted controlled relative staging; the
follow-up `compiled console paths 日本語 v14/report.json` verifies the correction,
including fresh-process saves and valid RPX/WUHB at a spaced Unicode destination.
The actual compiled save and interactive RPXs also pass original staging,
encrypted packing and independent decrypt/FST/file comparison in
`libctru-filesystem-packaging-v14/report.json` and
`libctru-interactive-packaging-v14/report.json`, with temporary synthetic inputs
and no package/credentials retained. This does not prove installer acceptance.
Delivery/fresh-source report: `artifacts/reports/delivery-v14/report.json`.


The v0.13 source, native diagnostic, original/external guest, compiled interactive and title-staging
bundles, wheel and SHA256 manifest are under `artifacts/delivery/` and
`artifacts/dist/`. The native bundle includes direct and shared display references.
Diagnostic output is RPX/WUHB, not an installable WUP.
Delivery/fresh-source verification report: `artifacts/reports/delivery-v13/report.json`.

Main validation: `artifacts/reports/validation-v1-20261009T140533Z/report.json`.
Compiled application evidence is included under that report's `libctru-native/`
and `libctru-sanitized/` directories. Independent compiler/target evidence:
`libctru-clang-final-v13/report.json` (real threads), `libctru-ppc-v13/report.json`
(exact interactive frames), `upstream-clang-v13/report.json`,
`upstream-clang-dsp-v13/report.json`, `upstream-ppc-dsp-v13/report.json` and
`upstream-ppc-final-v13/report.json` (32 passing checks). The full target run is recorded separately;
never infer hardware execution from these reports. The first PPC media run failed
because its generated six-instruction snapshot preceded the updated ten-instruction
test; fresh generation passes. A Clang warning on redundant emitter parentheses
was corrected in the adapter and real compiled threads then passed.
Independent reference: `artifacts/reports/cpu-oracle-dsp-v13/report.json`
(870 instruction cases, 47-instruction nested-call trace).
Follow-up evidence: `libctru-startup-trace-v13/report.json` captures all 11 boot
IPC requests at single-instruction boundaries, explaining all eight failures.
`libctru-driver-final-v13/report.json` checks the default interactive driver after
adding trace mode and fixing its 32-bit host-time multiplication. The trace also
has a separate sanitized run. `libctru-offline-packaging-v13/report.json` verifies
the new real interactive RPX through encrypted packing/decryption/FST/file checks
with temporary synthetic credentials (5,505,024 encrypted bytes). No package is
retained or declared installable. Its real metadata/artwork staging is at
`artifacts/libctru-title-staging-v13/`; production inputs and installer/Menu
acceptance remain external gates.
Earlier v0.12 validation is preserved at `validation-v1-20261008T203213Z/report.json`.
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
- **Wii U:** first run the short v0.14.3 native-exit/compiled-input retest per
  `docs/console-retest.md`, upload the new TXT reports and note menu return/splash stall/system freeze.
  After both failures are resolved, complete the other guided/save tests. Native
  guided input/display/audio/stability and embedded self-tests have scoped evidence;
  full launcher/compiled/save/installation acceptance remains open. Return screen/input/audio/exit,
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
CPU semantics beyond the tested shared startup subset. Implement memory
control beyond the checked owned-heap/shared-object subsets, cross-process maps,
broader thread/affinity/priority-inheritance behavior, PICA/GX2 state/shaders/textures/coherency,
DSP/audio, RomFS/saves and module behavior. CFG user/Mii/console-unique identity
and broader configuration blocks remain pending. Complete CPU/FP correctness and
measure real console costs. The original framebuffer harness uses custom I/O; the separate standard-HID
consumer and direct/queued HID/GSP graphics guests supply standard-interface evidence.
GSP GX commands/PICA and full service startup remain unimplemented. Scoped
conventional libctru CPU/input/graphics homebrew is verified separately; no
commercial profile is enabled.

P4/P5: automate the verified Windows conversion engine, caching/rebuild/trace and
save/title identity, then implement/package the responsive WPF GUI injector.

## Reusable development environment

The restored SDK/tooling works: pinned devkitPPC/WUT, Rust 1.95.0, .NET 10.0.401,
Unicorn, cryptography 50.0.0 and container Clang/CMake/GDB/PowerPC Linux/QEMU. Activate with
`source /workspace/ctr2wup-tools/environment/activate.sh`.
`install_script` remains the tested reusable installer. `start_skill` was updated
and saved to reflect resumed implementation, Mario selection and current gates.
The v0.14 resume draft was saved as
`8c0c4ff7-e331-4103-9a95-c2a932f2e421~cecfgdraft_6ac934e11b2c819eacb36421a6f04b5f`;
network, credentials and repository membership were preserved. It requires review,
save and publication in environment settings. Saving the draft is not environment publication or proof of a new-task snapshot.
Git/API read access works with existing authentication; no new secret is needed
for local development. The manual Windows workflow has not been dispatched.
