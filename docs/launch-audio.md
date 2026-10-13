# Launch music check (0.20.0)

The latest Save, RomFS, SD asset and GX tests passed their scoped checks. The
follow-up Save trace confirms persistence across closing and reopening the app.
Those checks do not need repeating. See [the evidence review](console-evidence.md).

The Wii U launch sound continued throughout the apps. This build takes a fresh
native AX audio context on launch and releases it on exit/foreground changes.
Compiled guests do not acquire a tone voice or implement 3DS audio. Host checks
and Wii U cross-builds verify the code; hearing silence needs this short round.

## Run these two apps

1. Copy the ZIP's `sd-card/` contents to your SD root. Keep `sd:/ctr2wup/` and
   all its reports and saves. Launch from Aroma as before; no WUP installation
   or ROM/key is needed.
2. Open **CTR2WUP Audio Check 0.20.0**. Turn up TV and GamePad volume. At the
   launch-music prompt press **R** if it is quiet, or **L** if the launch sound
   still plays. At the next prompt press **A** for the short native tone, then
   **R** if it plays through both TV and GamePad, or **L** to record a problem.
   Press **Minus** to exit.
3. Open **CTR2WUP GX Transfers 0.20.0**. At the two picture prompts confirm
   with **R**, or report an incorrect picture with **L**. The TV should show
   green, then red, then green; the GamePad stays blue. No test tone should play.
   Press **Minus** to exit.

If an app hangs or displays an audio/storage error, stop that run and return any
files it saved. Repeating a failure is not needed to produce a report.

## Send the results here

The apps display **RESULT VERIFIED** and the result filename. Plain-text files
are in **`sd:/ctr2wup/`**:

- `result-native-audio-0.20.0-*.txt`
- `result-compiled-gx-transfer-0.20.0-*.txt`
- Matching `native-audio-*.txt` and `compiled-gx-transfer-*.txt` full traces, if present.

Upload those new files in this chat, together or as a ZIP below 32 MiB. Add one
short note stating whether launch music stopped in **both** apps and whether
**Minus** returned normally in both. Pictures are optional. The completed
Save/RomFS checks need no new upload.

## Purpose and limits

Audio Check records launch-music silence and the native tone on both outputs.
GX confirms that the same startup does not disrupt a compiled original guest
or its transfer pictures. Reports record AX initialization, inherited-context
resets and cleanup counters. An initialized AX context alone cannot prove audible
silence. These tests do not establish 3DS guest audio, PICA draws, commercial
gameplay or an installed WUP.
