# Guided Wii U testing and TXT reports (0.14.5)

Use the **0.14.5 console testing kit** for the current test round. It supplies
four original homebrew diagnostic variants with prompts and automatic plain-text
recording. No ROM is required and these builds are RPX/WUHB, not WUP packages.

**Current round: [one short guarded report-recovery retest](console-retest.md).**
The first old report is reviewed; the frozen launch saved no file. Native Minus
exit is user-confirmed. Guarded stability and crash recovery remain unverified.
The same `console-retest.md` is in the kit.

## Start

1. Extract the kit on your PC. For Aroma, copy the contents of `sd-card/` to
   your SD card root. The four WUHB files belong in `sd:/wiiu/apps/`.
2. For this round use **Guarded AOT 0.14.5** once per the short retest. Leave
   **Native**, **Compiled AOT** and **Compiled Save** for later.
   For complete acceptance rounds, follow each app's on-screen guide.
3. For an RPX-capable launcher, use the matching files under `rpx/` through
   that launcher's normal workflow. Use your existing homebrew environment.

The apps reserve a new report at launch and show **TXT SYNC**, its filename,
and the upload folder. **TXT REPORT FAILED / SD ERROR** means
the file could not be created or written; report that error and upload any
available partial file. Logging cannot work without accessible writable SD storage.

v0.14.5 displays **TXT SYNC** after startup commits succeed. Critical checkpoints
sync and close the file, flush the mounted SD volume and reopen the existing file;
startup, guest start and periodic summaries commit too. A storage failure pauses
guest execution and reports the failing operation, errno and FSA status. Syncing
adds diagnostic overhead and cannot guarantee SD-controller power-loss recovery.

## Prompts and controls

- Requested button presses, four directions on each stick, five touch regions,
  border rejection, guest PASS/exit, and snapshot reload are recorded automatically.
- **R** confirms a picture, touch-alignment, sound, or stability observation;
  **L** records a problem. These confirmations apply to the displayed prompt.
- **ZL** skips the current step and records **NOT TESTED**.
- **ZR** records a problem for the current step and advances.
- **Minus** finishes early or closes a completed test. Unfinished steps stay
  **NOT TESTED**; the app writes its available session summary before closing.
  Reports are written throughout the run, rather than waiting for Minus.

Only the native app plays the diagnostic tone on A. The compiled apps reserve A
for guest input and show progress counters. They do not implement guest audio.
The A-button prompt waits for a new green guest frame and records its latency;
R remains a confirmation of what you saw, distinct from decoded-pixel evidence.

The native and interactive tests include a ten-minute countdown. Their final
stability prompt asks whether you observed problems. The interval and your
confirmation are separate evidence, not a claim of representative game speed.
Y mode changes and unscheduled Plus restarts are reserved until the guide finishes;
they will not disrupt a button test. Follow the prompted Plus action when testing
guest restart or save reload.

The save test checks the initial guest save/reload, prompts for Plus to reload,
then requests **one exit and fresh launch** if needed. The next launch detects
the guest's successful initial reload and reports **Fresh-launch reload confirmed**.
Existing snapshots are preserved; no reset/deletion is required. A failure to
read or commit a snapshot is logged rather than replaced with an empty save.

## Results and delivery

Reports are in **`sd:/ctr2wup/`**:

```text
native-<session>-<suffix>.txt
compiled-interactive-<session>-<suffix>.txt
compiled-guarded-<session>-<suffix>.txt
compiled-save-<session>-<suffix>.txt
```

Each report contains the application/build identity, raw diagnostic messages,
named guided steps and their **PASS / FAIL / NOT TESTED** results, your controller
confirmations, runtime faults, input events, save messages, duration/counters,
and a final session summary. Session IDs use console ticks rather than wall-clock
dates; collisions try another suffix instead of replacing a report.
Records are flushed as they are written. An interrupted run can leave useful
partial evidence: absence of the final `===== END OF REPORT:` marker means
resource cleanup was not recorded. Exit checkpoints follow that marker; neither
the marker nor `main_return` proves successful launcher return. Include your
brief return/stall/freeze observation. Flush and close do not establish power-loss durability.

After the tests, exit with Minus and copy the **new `.txt` files** from that
folder to your PC. **Upload them in this chat**, individually or in one ZIP
below 32 MiB. Include both save-test launch reports when available. On save
failure, also return `filesystem-test.snapshot` and its `.pending` sibling if
present; keep the originals. Exclude unrelated personal files and credentials.

With the first upload, include Windows version, Wii U model/region/firmware,
homebrew environment and launcher versions, SD format/capacity, and TV resolution.
Pictures are optional for illustrating a visual failure; normal results are
recorded in the TXT files. Add a brief note if the generic controller confirmation
does not describe a problem precisely enough.

These tests establish scoped platform/runtime behavior. They do not validate
PICA game rendering, guest DSP/audio, commercial saves, Mario gameplay, or WUP
installation. Display/audio observations are user confirmations, distinct from
software assertions. The new guide/report code passes desktop and sanitizer
checks and is cross-compiled; actual console evidence is still pending.
