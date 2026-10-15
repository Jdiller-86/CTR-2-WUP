# Audio Streams: guided Wii U test

This v0.26.0 test plays original synthesized notes through the checked CSTM,
ADPCM, rate-conversion/mixing and native stereo output path. Allow about five
minutes. It does not execute guest DSP firmware or establish game compatibility.
Physical sound and lifecycle results are still pending.

## Copy and launch

1. Download the `wiiu-audio-round` artifact linked from the tested PR/run and
   extract it. Keep its `manifest.json` with the return files; it identifies the
   exact source. The kit contains only original diagnostics and reference notes.
2. In FTP **binary** mode, copy `sd-card/wiiu/apps/ctr2wup-audio-stream-check.wuhb`
   to `SD:/wiiu/apps/`. Close FTP before starting the app. Launch **CTR2WUP Audio
   Streams 0.26.0** through Aroma. Use the RPX alternative only if that is your
   normal homebrew route. No WUP installation is required for this diagnostic.
3. Set audible volume on TV and GamePad. The launcher's music should stop. The
   screen shows each instruction, playback status and the result-file name.

## Follow the eight prompts

Use **A** to play/replay, **R** for the expected sound and **L** for a problem.
The app waits for its playback to finish before accepting a fresh R press.
**ZL** skips a step; **ZR** records a problem. Skips remain NOT TESTED.

| Prompt | What to do and listen for |
| --- | --- |
| Launch silence | Confirm the launcher's music stopped; no note starts on its own. |
| Left channel | Press A. Hear three rising notes on the left of both outputs; the right remains silent. R confirms after playback. |
| Right channel | Press A. Hear three falling notes on the right; the left remains silent. |
| Two-voice mix | Press A. Hear both patterns together on both outputs, with the expected gaps. |
| Source rate change | Press A. Hear the left pattern faster and higher than the first test. |
| Pause and resume | Press A, then X **during a note**. Wait briefly, then X again. Sound must stop while paused and continue. If you miss the note, A replays. |
| Seek to second phrase | Press A. Playback starts at the second note; the first note is absent. |
| New playback session | Press Plus. Both patterns play again in a new session; confirm after completion. |

The `reference/` WAV files let you hear the exact original scenes on a computer.
They help identify a wrong pitch, missing channel or unexpected noise. The app
also checks each rendered scene's independent PCM hash before native playback;
that check does not replace your hearing.

After completion press **Minus**. Confirm you return to the launcher/Menu and
sound stops. For a focused lifecycle check, launch once more, start a note and
use HOME through your normal return route. Confirm exit and silence. If HOME
suspends and returns to this app, the current attempt is invalidated: press A
again when prompted. Record what happened; do not count an interrupted attempt
as a listening pass. An incomplete second guide is useful lifecycle evidence.

## Send back the files

After the app has closed, reconnect FTP. Download the matching files from
`SD:/ctr2wup/`:

- `result-native-audio-streams-0.26.0-<session>-<attempt>.txt`
- `native-audio-streams-<session>-<attempt>.txt`

Keep **both result and full trace for every launch**, including a failed or
interrupted one. Trace names omit the version. The result names its full trace;
collision suffixes can differ. Zip the files together with `manifest.json` and
send that ZIP here. No console command, SD removal or collector CMD is needed.
Do not include saves, ROMs, keys or unrelated diagnostic archives.

Add your firmware/Aroma version, launch route, whether TV and GamePad channels
sounded correct, any noise/gap/pitch problem, and whether Minus/HOME returned
normally and silently. A photo is useful only if the app cannot save its files.
The optional `collect-audio-results.py` works on a **local FTP download folder**
containing `ctr2wup/`; manual ZIP return is equally valid.

The fixed-size result preserves all eight named outcomes and session/control
counts. The trace records source identity, rendered PCM hashes, source/output
frames, decode/clipping counters, render time, AX routing/cursors/ownership and
cleanup through main return. Missing or failed checks remain visible for review.

This test is part of the audio development checkpoint, not full
[M1 platform acceptance](milestones.md#m1). Installed-WUP, persistence/recovery
and full stability evidence have their own requirements. The earlier source-bound
0.25.0 display-cache comparison remains a separate pending hardware round.
