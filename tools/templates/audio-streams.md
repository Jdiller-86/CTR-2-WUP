# Audio Streams

Build `@VERSION@` · source `@SOURCE_COMMIT@`.
Run this only when a change to audio playback needs console verification.
This checks original synthesized notes, not guest DSP firmware or games.

1. FTP the kit's `sd-card/` contents to the SD root in binary mode. Preserve
   existing files, close FTP, then launch **CTR2WUP Audio Streams** in Aroma.
   Set audible TV and GamePad volume. No WUP installation is required.
2. Follow the eight on-screen prompts. **A** plays, **R** confirms the expected
   sound after playback and **L** records a problem. **ZL** skips; **ZR** records
   a problem and advances. The reference WAVs in `reference/` play the expected
   sounds on a computer.

| Prompt | Expected result |
| --- | --- |
| Launch silence | The launcher's music stops; no note starts automatically. |
| Left channel | Three rising notes on the left of TV and GamePad; right silent. |
| Right channel | Three falling notes on the right; left silent. |
| Two-voice mix | Both patterns together, with the expected gaps. |
| Source rate change | The left pattern sounds faster and higher. |
| Pause and resume | Press X during a note, wait, then X: sound stops and resumes. A replays if needed. |
| Seek | Playback starts at the second note. |
| New session | Plus starts both patterns again in a new session. |

3. Press **Minus** after the guide. Confirm normal return and silence. For the
   lifecycle check, relaunch, start a note and try HOME through your usual return
   route. If HOME suspends and returns to the app, record that outcome; an
   interrupted listening attempt is not a pass.
4. After closing the app, FTP the whole `sd:/ctr2wup/results/` folder to your computer,
   ZIP it and return that ZIP. Reports contain source identity; do not select
   or pair individual logs. Include failed and incomplete runs.

Add a short note about TV/GamePad sound, Minus/HOME return and any noise, gap,
pitch or storage problem. Mention firmware/Aroma only if changed. No collector
command or SD removal is needed. Keep failure logs; do not rerun to obtain a
pass. Do not add games or keys.
