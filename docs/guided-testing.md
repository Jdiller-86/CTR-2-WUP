# Guided Wii U tests and TXT results (0.14.6)

This page preserves the original 0.14.6 controls and reporting contract.
The current physical results and focused follow-up are in
[console-evidence.md](console-evidence.md) and [console-retest.md](console-retest.md).
Save/RomFS, launch audio and bounded guest draws have reviewed scoped results;
no long guide repetition is requested solely for a version bump.

## Launch

For Aroma, copy the kit's `sd-card/` contents to the SD root. Apps belong in
`sd:/wiiu/apps/`. An RPX-capable launcher can use the matching `rpx/` files.
Keep the existing `sd:/ctr2wup/` folder and saves. Use your current launcher.

Each app displays **BUILD 0.14.6**, **RESULT VERIFIED** and its new filename.
VERIFIED means the current 4 KiB TXT was closed and compared byte for byte after
reopening. It does not prove survival of sudden power loss. The separate
**TRACE SAVING / FAILED** status describes the longer trace, not the result file.
A trace failure is captured in the result. **RESULT STORAGE FAILED / SD ERROR**
pauses the guest; exit with Minus and report the operation/number and any files.

## Prompts

- Button/stick/touch inputs, guest PASS/exit and save reload are checked automatically.
- **R** confirms the displayed picture, alignment, sound or stability observation;
  **L** marks a problem. These are labeled user evidence.
- **ZL** skips a step as NOT TESTED; **ZR** marks a problem and advances.
- **Minus** ends the test and writes the available summary. Unfinished steps stay
  NOT TESTED. Results are also written at startup and throughout execution.
- Use **Plus** when prompted for guest restart/save reload. Y/unscheduled Plus
  remain reserved while a guide is active.

Only Native plays the platform tone on A. Compiled apps use A as guest input and
implement no guest audio. The interactive A prompt waits for a fresh decoded
green frame before allowing R and records latency separately from confirmation.
Native and interactive full guides include a ten-minute observation interval;
its completion is separate from the user's final stability confirmation.

Compiled Save preserves the existing diagnostic snapshot, checks its own guest
save/reload and asks for Plus. It may request one exit/relaunch for a fresh-process
check. Compiled RomFS automatically checks original asset reads and exits after
one step. No photo is needed to report these software checks.

## Files to return

All reports are under **`sd:/ctr2wup/`**:

```text
result-native-0.14.6-<ticks>-<suffix>.txt
result-compiled-interactive-0.14.6-<ticks>-<suffix>.txt
result-compiled-guarded-0.14.6-<ticks>-<suffix>.txt
result-compiled-save-0.14.6-<ticks>-<suffix>.txt
result-compiled-romfs-0.14.6-<ticks>-<suffix>.txt
```

These fixed-size plain-text results contain named PASS / FAIL / NOT TESTED guide
outcomes and their evidence, build/session identity, embedded-check status,
guest instructions/PC/fault/fallback, input counters, green-response latency,
diagnostic frame timing, save load/commit status and full-trace errors. Trailing
space padding is intentional. Later updates verify the reserved file identity;
missing or replaced reports are not silently recreated. Per-launch exclusive
names preserve older results.

The separate longer traces use `native-*.txt`, `compiled-interactive-*.txt`,
`compiled-guarded-*.txt`, `compiled-save-*.txt` and `compiled-romfs-*.txt`.
Their successful commits sync/close/flush the SD volume. Neither a cleanup marker
nor a saved result proves that the launcher finished returning; add that observation.

Upload the new result TXT and any matching full trace **in this chat**, directly
or in a ZIP below 32 MiB. For save testing, include the snapshot and any `.pending`
/ `.previous` siblings, preserving the originals. If nothing exists, say so and
include the short on-screen storage error. See `console-retest.md` for this round.
The console environment, launcher version, SD format/capacity and TV resolution
help when diagnosing failures; do not resend already supplied details.

Local tests verify report write/read-back, failed/missing/replaced-file handling,
interrupted process evidence, guide outcomes and lifecycle ordering. Builds use
the actual Wii U SDK. v0.14.6 result persistence and save updates are awaiting
physical evidence. These diagnostics do not verify PICA, guest audio, retail
saves, Mario gameplay or WUP installation.
