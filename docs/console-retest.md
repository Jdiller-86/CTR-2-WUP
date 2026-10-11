# Menu-transition and compiled-input retest (0.14.3)

The first native TXT records ten guided checks and nine embedded self-tests
passing, with resource cleanup reached. The latest user clarification is more
specific: **Minus reaches the Wii U Menu splash, whose music continues, but the
menu never loads**. This is a transition stall, not a complete console freeze.
The newer compiled app still freezes the whole system at **hold A**, without a
green frame. The user confirms that **no native tone plays** in that run, so
removing the tone did not resolve it. No TXT from these latest runs was supplied.
The older v0.14.0 compiled log with a guest PASS is separate evidence. Save testing
remains deferred.

## Changes in this kit

- The app polls ProcUI directly. The previous `WHBProcIsRunning` helper can call
  `ProcUIShutdown` before app cleanup, conflicting with WUT's documented EXITING
  order. Minus now stops rendering, closes guest/audio/graphics/report resources,
  clears stale graphics callbacks and preserves the save acknowledgement, then
  requests menu transition. It waits for EXITING before shutting down ProcUI.
  HBL/Mii Maker relaunch also occurs after resource cleanup and ProcUI shutdown.
- Input/rendering pause outside the foreground. Owned native AX audio stops on
  foreground release and restarts on acquire; compiled apps never own AX audio.
- The kit adds **Guarded AOT**: the same compiled guest/runtime with native call
  nesting limited to two, alongside the standard limit of 64. The existing AOT
  unwind/dispatcher resumes execution; this is not an interpreter or a game patch.
  Both produce identical expected frames/input/exit on desktop. This comparison
  may narrow stack/recursion-related behavior; its console effect is unverified.
- App names show **0.14.3**, and the guarded app has its own filename/report
  prefix. Bounded input checkpoints record PC/instructions/frame swaps, maximum
  AOT call depth, unwind count and stack headroom sampled at frame boundaries.
  This is not a measurement of deepest native stack use. Exit checkpoints record
  cleanup, callback removal, request, lifecycle state and final shutdown order.

WUT remains the SD mount owner; UDP logging is off by default. Reports are saved
throughout the run. An end marker or `main_return` does not prove actual menu
return; record that observation briefly when uploading.

## Short retest

1. Extract the **0.14.3** kit and copy the contents of `sd-card/` to the SD root.
   Replace the older files. The menu labels now include the version. RPX users
   can use the matching files under `rpx/` with their existing launcher.
2. Launch **CTR2WUP Native 0.14.3**. Press **Minus** once the diagnostic screen
   appears. The guide records CANCELLED / NOT TESTED, which is expected for this
   short exit check. Check whether the actual menu/launcher loads, beyond its
   splash. No ten-minute repeat is needed. **If it still stalls, stop this round.**
3. If native exit succeeds, launch **CTR2WUP Guarded AOT 0.14.3**. Wait for red top /
   blue bottom, confirm with **R**, then hold **A**. There should be no tone.
   Watch for green and moving host/guest progress counters; allow up to one minute.
   If responsive but incorrect, use **ZR**, then **Minus**. If it freezes, stop
   the round and preserve the partial report.
4. If guarded input succeeds, repeat the short A check with **CTR2WUP Compiled
   AOT 0.14.3**, then **Minus**. Comparing the two reports will help locate the
   remaining failure. Skip long stability with **ZL** if reaching that step.
   Leave **Compiled Save** for a later round until exit and input are stable.

## Return the results

Upload the new TXT files **in this chat**, individually or in a ZIP below 32 MiB:

```text
sd:/ctr2wup/native-*.txt
sd:/ctr2wup/compiled-guarded-*.txt
sd:/ctr2wup/compiled-interactive-*.txt
```

Include partial files and any TXT from the preceding failed build if available.
Add one brief note per run: **menu returned / menu splash stalled / system froze**,
and whether A produced green. Include the launcher/environment version if known.
Photos are optional. The save app need not be run for this retest.

These changes pass local tests and cross-compilation. Their effect on the reported
console failures still needs this retest. No full milestone, commercial gameplay
or WUP installation is accepted.
