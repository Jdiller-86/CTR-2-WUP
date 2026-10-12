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

The 0.17.0 kit contains **Native** and **GX Transfers** original diagnostics.
Earlier 0.14.6 contains the Save/RomFS follow-up kit; 0.16.0 adds Asset Files.
Use the kit requested by the relevant testing guide. Do not replace/delete save
snapshots or old reports while investigating a failure.

## Wii U prerequisites

The diagnostic launch route targets a homebrew Wii U with **Aroma** and an SD
card. The current tester reports a U.S. Wii U using latest firmware/Aroma/WUP
Installer GX2; exact version numbers have not been recorded. This is a reported
environment, not a promise for every setup. Follow your homebrew environment's
own installation instructions; CTR2WUP does not install or update console firmware.
WUP Installer is not needed to launch the diagnostic WUHB files.

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
its actual revision and requirements still await private analysis.
