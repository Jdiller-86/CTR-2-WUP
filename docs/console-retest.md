# Guarded report-recovery retest (0.14.5)

Native Minus exit works, according to the user. The received v0.14.3 guarded
report captures the first run, which displayed green late and exited. It records
continued execution/rendering, a failed button-response observation and complete
shutdown; it does not explain the next launch's freeze. The user confirms that
frozen launch saved nothing. **There is no old partial report to recover.**
See [the scoped evidence review](console-evidence.md).

## One short test

1. Extract the **0.14.5 console testing kit** and copy its `sd-card/` contents to
   the SD root, replacing the previous diagnostic builds. An RPX launcher may
   instead use the matching `rpx/ctr2wup-libctru-guarded.rpx`.
2. Launch **CTR2WUP Guarded AOT 0.14.5** once. Leave Native, Compiled AOT and
   Compiled Save for later. No ROM is needed.
3. If **REPORT STORAGE FAILED** appears, press **Minus** to exit. Report the
   displayed `SD ERROR`, operation and `FSA` number; send any TXT that exists.
   Guest execution pauses on a reported storage failure.
4. Confirm the red top / blue bottom picture with **R**. At the A prompt, hold
   **A** until **NEW GREEN FRAME DETECTED** appears and the picture looks green,
   then confirm with **R**. If it still says **WAITING FOR GREEN** after about
   30 seconds, mark the problem with **L**.
5. Press **Minus** to exit after that observation. The remaining guide steps will
   be marked unfinished; this short diagnostic is not a full acceptance run.
   Stop after this launch, including if it freezes, and return the result before
   trying another launch.

## Return the result here

Upload the newly created **`compiled-guarded-*.txt`** from:

```text
sd:/ctr2wup/
```

Send it **in this chat**, directly or in a ZIP below 32 MiB. If several guarded
reports exist and the new one is unclear, send them together; each header records
its application/build. Include any incomplete file after a reboot.
Add a short note: whether green appeared, whether Minus returned to Menu, or
whether the console froze. If no file exists again, say so—do not repeat the test
to create one. A visible storage error only needs its short text/numbers.

## What changed

Each critical checkpoint now **syncs the file, closes it, flushes the mounted SD
volume, then reopens the existing file** before proceeding. Startup and periodic
summaries also commit; missing files are never silently recreated. The report
names the checkpoint, commit sequence and sync policy. Failure preserves the
first error and pauses guest execution. WUT continues to own the SD mount.

This is stronger than flushing a C buffer, but physical crash recovery still
needs this console test. Syncing adds diagnostic overhead. If the filesystem or
SD controller itself hangs, a checkpoint can remain missing despite these steps.
There is no verified freeze fix yet.

The kit also retains v0.14.4's cached image scaling, fresh-green confirmation,
latency logging and tested idle-AOT-dispatch guard. Those checks pass locally;
they have not established console speed or intermittent stability. The supplied
RPX/WUHB diagnostics are not commercial-game WUPs.
