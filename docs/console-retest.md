# Focused console retest (0.14.2)

The October 11 guided native report records ten passing steps, all nine software
self-tests, no input errors or guest faults, about thirteen minutes of operation,
and completed application resource cleanup. The user reports a freeze after
Minus instead of returning to the launcher. The report was saved; its end marker
does **not** prove launcher return. The save app was not tested.

The user also reports that the newer compiled app shows red/blue, then freezes
when A plays a native tone, without a green frame. No log from that newer
compiled run was supplied. The older uploaded `.log` identifies a v0.14.0 compiled
app despite its `native-` filename and contains a guest PASS; it is separate
evidence and cannot explain the newer freeze.

## What changed

- Menu-launched titles now request `SYSLaunchMenu` and keep processing ProcUI
  until system exit. HBL/regional Mii Maker routes retain WUT's relaunch path.
  Requests are idempotent. The former forced-stop path did not request menu exit.
- SD files use WUT's existing newlib mount; the app no longer mounts/unmounts
  that same volume through a second WHB client. UDP logging is off by default;
  developer builds can opt in with `UDP_LOG=1`.
- Compiled apps neither initialize/shut down AX nor play the native A-button
  tone. A exercises guest input only; guest audio remains unsupported. This
  isolates the reported trigger; it does not establish the freeze's cause.
- A/B/Plus transitions start bounded checkpoint logging around HID, guest
  execution, pixel decoding, scaling and GX2 rendering. Host frame and guest
  instruction counters are visible. Exit stages include checked closed-file
  checkpoints after report close and WHB process shutdown. The runtime's final
  exit and actual launcher return still require your observation.

## Short retest

1. Extract the **0.14.2** kit. Replace the older three WUHB files using the
   `sd-card/` contents on your SD root, or use its matching RPXs with your existing
   RPX launcher. Keep the old logs. New runs reserve different report filenames.
2. Launch **Native Diagnostic** and press **Minus** as soon as the test screen
   appears. Check whether the menu/launcher returns normally. The unfinished
   guide will record **CANCELLED / NOT TESTED**; that is expected for this short
   exit check. Do not repeat the full ten-minute guide yet.
3. If native exit succeeds, launch **Compiled libctru Test**. Wait for red top /
   blue bottom, press **R** to confirm, then **hold A**. There should be no native
   tone. Watch for green and whether the visible host frame / guest instruction
   counters keep increasing. Allow up to a minute; these are slow CPU-framebuffer
   diagnostics, not a game-speed test. If the app responds but green never
   arrives, use **ZR** to record the problem and **Minus** to exit. If it freezes
   again, stop this round and preserve any partial report.
4. If the button response succeeds, use the guide for touch and **B** guest exit.
   Skip the long stability interval with **ZL** and close with **Minus**. Check
   menu/launcher return. Leave the save app for the next round until these two
   failures are resolved.

Upload the new **`sd:/ctr2wup/native-*.txt`** and
**`sd:/ctr2wup/compiled-interactive-*.txt`** files **in this chat**, individually
or together in a ZIP below 32 MiB. Include partial files after a freeze. Add one
brief note saying whether each app returned to the menu or froze; software cannot
log what happens after its own final exit. Include the launcher/environment
version if known. Photos are optional for a visual problem.

Desktop checks and cross-compilation validate these changes locally. Their
effect on either reported console freeze remains **awaiting this retest**.
No full milestone, commercial-game execution or WUP acceptance is claimed.
