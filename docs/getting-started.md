# Getting started

**CTR2WUP currently runs original diagnostic applications, not commercial 3DS
games.** The final Windows GUI and playable-game WUP conversion are still being
developed. Choose a diagnostic release only when you want to help test the
runtime. Native `.wuhb`/`.rpx` diagnostics are different from installable WUPs.

## Download a matching release

Use the repository's [development releases](https://github.com/Jdiller-86/CTR-2-WUP/releases).
Read its release notes and `SHA256SUMS.txt`; keep the kit, source version and
instructions together. CI builds are available from each successful
[Actions run](https://github.com/Jdiller-86/CTR-2-WUP/actions), with the source
commit in their artifact names. Hosted builds are structural/test evidence.

For the unreleased **0.27.0** checkpoint, the successful run's
`wiiu-m1-acceptance` artifact is a focused **install check**, not another native
controls/stability round. Read its sole `START-HERE.md`, FTP `sd-card/` to the SD
root, use WUP Installer GX2 under signature-patched Aroma, then return the whole
`sd:/ctr2wup/install-check/` folder. [The short guide](m1-acceptance.md) names the
new installer/Menu path and remaining automatic SD recovery check. No keys or
certificates need to be supplied. Developer material is under `support/`.

Earlier reviewed native controls/touch/tone/ten-minute, results, ordinary exits
and save reload are carried forward. The immutable 0.25.0 cache and 0.26.0 Audio
Streams kits retain separate pending changed-feature scopes; no repeat of those
older rounds is requested by the install check.

Release manifests identify separate **Native/GX**, **Audio Check/GX** and
**Guest Draws** kits, together with matching source, a developer wheel, offline
documentation and checksums. Keep each kit's guide and exact source/version
identity with its binaries. The wheel supplies the CLI; it does not supply the
finished Windows injector GUI.

The October 13 Audio Check/GX 0.20.0 and Guest Draws 0.23.0 scoped results are
reviewed; unchanged passed guides need no repeat. Earlier 0.14.6 Save/RomFS and
0.16.0 Asset Files evidence is retained. Use a focused guide only for a changed
path or a specific remaining acceptance check. Preserve save snapshots and old reports.

## Wii U prerequisites

The diagnostic launch route targets a homebrew Wii U with **Aroma** and an SD
card. The tester previously confirmed firmware **5.5.6 U / Aroma Beta 27** and now
selects **WUP Installer GX2**. Report versions as unchanged or correct them for
the new install path; record the installer version/build if displayed. This is a reported
environment, not a promise for every setup. Follow your homebrew environment's
own installation instructions; CTR2WUP does not install or update console firmware.
WUP Installer is not needed to launch diagnostic WUHB files. The new original
WUP candidate requires Aroma signature patches (`01_sigpatches.rpx`); their
presence is not assumed. Installer/Menu behavior remains physically unverified.

1. Extract the kit and copy the **contents of `sd-card/`** to the SD root.
2. WUHBs go under **`sd:/wiiu/apps/`**. Launch the named app from Aroma.
3. Follow the app's controller prompts. **Minus** saves the current result and
   requests exit; tell us if the launcher/Menu return fails.
4. Collect results from **`sd:/ctr2wup/`**. ZIP the named `result-*.txt` files and
   any full traces requested by the guide, then attach the ZIP to a console-result
   issue or the current development conversation.

An RPX launcher can use the kit's `rpx/` files instead. RPX launch and Aroma WUHB
launch can follow different lifecycle paths; record which you used.

For GX Transfers 0.17.0, see [the two-step guide](gx-transfers.md). It needs no ROM,
key or extra asset file. For Asset Files 0.16.0, the original blob must also be
present at `sd:/ctr2wup/romfs-test.bin`. Save/result testing follows
[console-retest.md](console-retest.md). Completed long input guides need not be
repeated merely because a newer source version exists.

If a result is missing or the app hangs, use [troubleshooting](troubleshooting.md).

## Build from source

See [CONTRIBUTING.md](../CONTRIBUTING.md) for Python, host compiler and verified
devkitPro/WUT setup. `make check` runs the regression suites; `make prototype`
produces original RPX/WUHB diagnostics. Windows has a separate native toolchain
route and CI baseline; Linux SDK executables do not run on Windows.

## Your owned game input

The future GUI will accept decrypted dumps or locally prepared encrypted inputs.
Current engineering commands can [prepare supported inputs locally](encrypted-input.md)
and [export a small private executable intake](cia-intake.md). Their success does
not enable game execution. Do not attach keys, full ROMs, saves or commercial
asset blobs to GitHub issues. The first selected game is Super Mario 3D Land;
its USA executable and seven selected asset payloads are privately verified.
Observed runtime requirements and commercial execution remain open; see
[the current matrix](first-title.md).
