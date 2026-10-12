# Physical Wii U test checklist

**Current workflow: use the v0.14.5 kit and first follow
[guided-testing.md](guided-testing.md).** The diagnostic variants now prompt for inputs
and observations and write per-launch `.txt` reports under `sd:/ctr2wup/`.
Upload those reports in this chat. Photos are optional when illustrating a visual
problem; the manual checklist below remains a reference for deeper investigation
and older artifacts. Native guided checks have now passed on the user's console,
and native Minus exit now works. Guarded A was delayed once and froze on a second
launch. The first report is reviewed; the second saved no file. The next action
is one short guarded test of the stronger SD checkpoint commits.
Follow `console-retest.md` before repeating long guides or running the save app.

No console is attached to this environment. Full physical acceptance remains
open despite the scoped native results. Cross-compilation, host tests and RPX headers do not
establish console behavior.

v0.14 adds `CTR2WUP-libctru-filesystem-0.14.0.zip`. Launch its WUHB/RPX with
an accessible mounted SD card; the existing diagnostic creates its `ctr2wup` directory under the WHB SD mount. A fresh diagnostic
save must show/log `CTR2WUP compiled filesystem SAVE 1 PASS` and `state=exited`.
Press Plus: it must reload, update and show `RELOAD PASS`. Exit with Minus and
relaunch to test another fresh process. Preserve the private diagnostic snapshot
and logs on failures; do not discard a corrupt snapshot or stale `.pending` file.
Its path, atomic-publication limits and detailed procedure are in `filesystem.md`.
The fifth build executes standard libctru/newlib savedata code, not a Mario save.
Native SD exclusive-write/rename behavior and stability are unverified.

The v0.14 compiled interactive build also enables standalone APT startup/exit and
an empty private virtual SDMC, with no host SD-card exposure. Boot tracing now
records only privileged APT:S/APT:A and optional ptm:sysm lookup failures; all
scoped APT/FS startup requests succeed. The v0.13 notes below describe that older
artifact's unavailable APT/FS path. Use current bundles for new hardware evidence.

v0.13 adds `GUEST THREAD SCHEDULER PASS CODE 0` in native mode and
`thread_selftest PASS result=0` in the SD log. Record its 345 instructions/eight
switches, or the exact failure. This compares original interpreter/AOT contexts,
per-thread TLS, owner-aware locks, waits and exit; no ROM is needed.

The separate `CTR2WUP-libctru-interactive-0.13.0.zip` supplies
`ctr2wup-libctru-interactive.rpx`/`.wuhb`, a real original compiled libctru program.
It starts in standard GSP display mode: top red, A held turns it green; bottom
blue, a valid GamePad touch marks the corresponding white pixel. B is consumed
by the ARM application itself and exits its graphics thread/process. Plus
restarts; Y switches between its display and native diagnostics; Minus exits the
host. Check all native self-test rows as well. This build has no custom ARM
framebuffer mode and no guest PICA/audio/FS implementation.

Record `CTR2WUP INTERACTIVE READY`, increasing paired GSP swaps, zero fallback,
touch corners/release, the guest `CTR2WUP compiled interactive PASS` and
`state=exited` after B. Startup currently records eight failed IPC requests
(six APT lookups, one FS lookup and one invalid session); preserve the IPC logs
rather than interpret these as successful
APT/FS initialization. Test ten-minute stability, repeated Plus restarts and
clean host shutdown. Do not compare its diagnostic instruction budget with
commercial performance or claim graphical-game compatibility from these frames.

## Artifacts and launch

Use `artifacts/wiiu/ctr2wup-prototype.rpx` with an RPX-capable homebrew launcher,
or the `.wuhb` with a compatible Aroma environment. For Aroma, place the WUHB
under `sd:/wiiu/apps/` following that environment's homebrew application guidance.
Record the precise homebrew environment, firmware and launcher versions used.
These are diagnostic applications, not WUP installation directories. Do not
attempt to install the RPX/WUHB through a WUP installer.

Current v0.14.0 delivery is `CTR2WUP-diagnostic-0.14.0.zip`; it contains the native
RPX/WUHB, the instructions and direct/shared display reference PPMs.
The separate original/external-guest bundles contain the other starting modes.

Version 0.2.0 also includes `ctr2wup-guest-fixture.rpx`/`.wuhb`, which starts in
controlled guest mode. All current builds contain three modes; Y cycles native,
custom-I/O ARM guest, standard HID/GSP ARM guest, then native.
Use the native mode for the original checks below, then run the guest checks.

Version 0.3.0 also supplies `ctr2wup-upstream-guest.rpx`/`.wuhb`. Run the same
guest checks with this build: it executes the actual external lifter's code
through the checked portable ABI. The embedded CPU self-test remains the original
bounded test; collect the guest instruction/frame/fault logs for this adapter.
Neither the RPX/WUHB nor the separate title-staging ZIP is an installable WUP.

Version 0.6.0 embeds the original TLS/SRV/CFG startup probe in all three builds.
In native mode, record `TLS/SRV/CFG STARTUP PASS CODE 0` and the corresponding
`service_selftest PASS result=0` SD log, or the exact FAIL code. This compares an
89-instruction ARM service sequence in interpreter/original AOT, without a ROM.
The separate external-AOT service probe has desktop/emulated PPC evidence only.

Version 0.7.0 embeds the original ARM heap lifecycle probe in all builds.
Record `GUEST HEAP LIFECYCLE PASS CODE 0` in native mode and
`heap_selftest PASS result=0` in the SD log, or the exact FAIL code. It compares
151 instructions across interpreter/original AOT: heap/linear CPU allocation,
queries, protection, partial free/hole reuse and complete release. No ROM is
required. GPU/DSP physical memory and commercial startup remain untested.

v0.8.0 embeds the shared-memory/synchronization probe. Record
`SHARED/SYNC LIFECYCLE PASS CODE 0` and `kernel_selftest PASS result=0`, or FAIL
codes. Its 148 ARM instructions/27 SVCs check aliases, mutex/semaphore/event
waits and ownership release. v0.13 separately embeds the multi-thread test below.

v0.9.0 adds `HID ARM CONSUMER PASS CODE 0` / `hid_selftest PASS result=0`.
Native GamePad rows show ARM-read HID buttons, packed signed Circle Pad XY,
packed touch XY/validity and an increasing snapshot count. Compare these with
the native VPAD rows while holding/releasing A/B/L/R/directions, moving the left
stick and touching corners/center/borders. Touch release must clear position and
validity; border touches must remain invalid. SD `HID live` logs must show waiting
between samples, increasing snapshots, fallback0 and no fault; shutdown should
report exited. Motion and C-stick are absent. Host Y/Plus/Minus controls are
reserved for diagnostics. No commercial dump is needed for this test.

v0.10.0 adds `HID/GSP RGB565 DISPLAY PASS CODE 0` and
`gsp_selftest PASS result=0`. This compares an original ARM standard-HID/GSP
graphics program through interpreter/original AOT, including rotated RGB565
pixels and cleanup. The external translator comparison is separate desktop/
emulated-PPC evidence, even in the external-guest RPX/WUHB build.

## Requested evidence

1. Launch and photograph both independent screens together. TV shows moving
   colored bands at logical 400×240; GamePad shows a distinct checkerboard at
   320×240. Verify centered aspect-preserving borders on your actual outputs.
2. Both sticks and held buttons update on the GamePad. Confirm VPAD errors are
   absent under normal input. Press A several times: a short 440 Hz tone should
   play from TV and GamePad, with the tone counter increasing.
3. Touch the guest image's corners and center; the crosshair should match. Touch
   the black side borders: no guest touch should be accepted. The code calibrates
   using VPAD's selected 1280×720 coordinate space then maps into the queried GX2
   surface dimensions; logs record both dimensions and viewport.
4. CPU ARM/Thumb AOT self-test should show PASS, result0. It executes an original
   40-instruction fixture in interpreter and generated paths and compares their
   registers, flags, little-endian data, and counts. Report FAIL codes exactly.
5. X repeats the 64 KiB aligned allocation/write/read diagnostic. Leave it running
   for ten minutes and record hangs, frame-time changes, glitches and logs.
6. Minus should return cleanly to the launcher/homebrew environment. Also test
   normal HOME/lifecycle handling supported by your environment. Record any
   failure to exit or audio continuing after exit.

The older v0.14.0 builds write `sd:/ctr2wup/native-*.log`; v0.14.1 uses named
`native-*.txt`, `compiled-interactive-*.txt`, and `compiled-save-*.txt` reports.
Optional WHB UDP logging uses port4405 (`UDP_LOG=1` developer build); it is off
by default in v0.14.2. SD TXT logging remains enabled. Supply
the log and the two-screen photo, test duration and console environment. The log
includes CPU outcome/timing, pointer bits, output surface dimensions, allocation,
input/touch/tone events, frame timing and shutdown. These timings include CPU
software image scaling and GPU copy/synchronization; they are not game timings.

## Controlled ARM guest checks

See `guest-fixture.md` for the contract and desktop reference images. No ROM is
needed: this build embeds only the original generated ARM fixture.

1. Enter guest mode with Y or launch the guest build. Confirm completed guest
   frame count increases; the TV background starts blue and GamePad starts teal.
   Record state/counters and any fault. Each guest frame needs multiple display
   iterations because execution is limited to 50,000 instructions per iteration.
2. Hold A through the next complete guest frame: the top changes to green.
   Hold touch inside the bottom viewport: the next bottom frame changes to
   purple and draws a white guest pixel at the touch position. Check corners and
   center; touches in black borders must not enter guest input. Record latency.
3. Press B: the guest should show exited after completing its current frame;
   the host remains responsive. Plus restarts it and its frame counter. Y cycles into the standard HID/GSP mode and then native diagnostics. Minus exits the entire application.
4. Leave guest mode running for ten minutes. Collect both-screen images and SD
   logs with `guest_open`, debug/SVC events, increasing frames/instructions,
   fallback0 and no faults. Include your test duration and firmware/launcher.

Guest pixels are ARM-generated; the status overlay is host-generated. Success
would establish this controlled fixture's PowerPC/input/presentation behavior,
not stock 3DS graphics, PICA/HID/GSP or commercial compatibility.

v0.11.0 also adds `GSP QUEUE/VBLANK PASS CODE 0` and
`gsp_relay_selftest PASS result=0`, checking mapped queues, delayed presentation,
IRQ delivery and cleanup through the original interpreter/AOT paths.

v0.12.0 adds `ARM/THUMB STACK CALLS PASS CODE 0` and
`stack_selftest PASS result=0`, checking 47 original instructions, nested ARM/Thumb
calls, stack/register restoration and cleanup. Include that line in your SD log
and native-screen results. This is original interpreter/AOT console evidence;
external and Unicorn results remain separate developer checks.

## Standard HID/GSP queued guest checks (0.11.0)

1. Cycle Y until `CTR2WUP HID / GSP QUEUE / VBLANK` is shown. Completed frame
   count must increase without faults or fallback. This guest reads HID shared
   memory/events and submits both RGB565 screens through the GSP shared queue,
   then waits for VBlank and drains IRQs.
2. Top should be blue, or green while holding A. Check the corner markers:
   white top-left, red top-right, green bottom-left, blue bottom-right. The host
   status overlay is in the middle so every corner remains visible.
3. Bottom should be cyan; valid touch changes it to magenta with a white pixel
   at the guest touch coordinate. Check center/corners and border rejection.
   Release must restore cyan. ARM fills run over several bounded host slices,
   so record visible latency rather than assuming one display-frame response.
4. B exits this guest; Plus restarts both graphics guests. Cycle all three modes,
   leave the GSP mode running for ten minutes and test Minus/HOME lifecycle.
   Return both-screen photos and SD `gsp_selftest`, `gsp_relay_selftest`,
   `GSP relay`, `GSP guest`,
   `gsp_guest_shutdown` logs, duration and firmware/launcher environment.

Compare with `shared-display-reference/frame-{1,2}-{top,bottom}.ppm` in the
native diagnostic bundle: frame1 holds A/touch at 100,80, frame2 releases both.
These reference images have no host status overlay. Success establishes the
scoped CPU/shared framebuffer bridge and relay on the diagnostic clock.
It does not establish stock libctru initialization, real 3DS timing or PICA.
Check registered=1 while running, increasing VBlank and swap counters, no device
faults or dropped IRQs, and registered=0/clean exit after B. Record coalesced
intervals and input latency; slow CPU fills can legitimately coalesce host ticks.

## Packaging prerequisite checks

Version 0.4.0 supplies an experimental original encrypted writer, not a packaged
installable diagnostic. Its offline proof uses synthetic key/certificates and
deletes that temporary package. Use the local-input contract in `packaging.md`
only after establishing your own inputs and console/installer environment.
Never try to install the synthetic test fixture.

A separately generated original homebrew package needs recorded installer
acceptance, Wii U Menu icon/boot artwork, launch into both screens, input/audio,
clean exit and removal. Record the title ID and installed-title inventory;
confirm no existing title was replaced. FST/ownership/COS metadata and the
nonhashed content/ticket policy may need corrections from those results.
No console installation, replacement or removal is automated by these tools.

## Not yet available for console validation

Proven installable WUP, conventional 3DS homebrew/service boot, PICA renderer scenes, commercial
gameplay, DSP, saves, CRO modules, and representative CPU/VFP/fallback performance
benchmarks. No commercial ROM is needed for the native test above. One user-owned
fully decrypted Old 3DS revision will be requested for the commercial milestone
for the selected Super Mario 3D Land candidate's private revision analysis.
