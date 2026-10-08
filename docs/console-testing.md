# Physical Wii U test checklist

No console is attached to this environment. Every item below is awaiting a
physical console test. Cross-compilation, host tests and RPX headers do not
establish console behavior.

## Artifacts and launch

Use `artifacts/wiiu/ctr2wup-prototype.rpx` with an RPX-capable homebrew launcher,
or the `.wuhb` with a compatible Aroma environment. For Aroma, place the WUHB
under `sd:/wiiu/apps/` following that environment's homebrew application guidance.
Record the precise homebrew environment, firmware and launcher versions used.
These are diagnostic applications, not WUP installation directories. Do not
attempt to install the RPX/WUHB through a WUP installer.

Version 0.2.0 also includes `ctr2wup-guest-fixture.rpx`/`.wuhb`, which starts in
controlled guest mode. Both applications contain both modes; press Y to switch.
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

Logs are written under `sd:/ctr2wup/native-*.log` if SD mounting succeeds.
WHB UDP logging uses port4405; SD logging is enough if UDP is unavailable. Supply
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
   the host remains responsive. Plus restarts it and its frame counter. Y switches
   back to native diagnostics. Minus exits the entire application.
4. Leave guest mode running for ten minutes. Collect both-screen images and SD
   logs with `guest_open`, debug/SVC events, increasing frames/instructions,
   fallback0 and no faults. Include your test duration and firmware/launcher.

Guest pixels are ARM-generated; the status overlay is host-generated. Success
would establish this controlled fixture's PowerPC/input/presentation behavior,
not stock 3DS graphics, PICA/HID/GSP or commercial compatibility.

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
