# Delayed-green and intermittent-freeze follow-up (0.14.4)

**Native Minus exit now works, according to the user.** In the first guarded
v0.14.3 run, holding A did not freeze the console. Green appeared late, after the
user marked L and advanced; the app then exited normally. A second launch froze
the whole Wii U when holding A. The guarded app is therefore **not stable**.
No TXT from these two runs has been supplied. The save app remains untested.

## Next action: return the existing reports

Before another console run, upload **both v0.14.3 guarded reports** from:

```text
sd:/ctr2wup/compiled-guarded-*.txt
```

Upload them **in this chat**, individually or together in a ZIP below 32 MiB.
Include the partial file from the frozen run. Session filenames use console
clock ticks; include both even if you cannot tell which is which. Reports should
identify the application/build and last input/guest/render checkpoint.
Add your brief note that the first run had delayed green and exited, while the
second froze. No photos or transcription are needed for ordinary diagnosis.
There is no need to repeat the already-passing native exit test or try the save
app yet. The second run's last checkpoint is needed to distinguish a guest-side
stall from presentation or platform failure.

## What 0.14.4 changes

- The old guide offered confirmation immediately after recording A. It now waits
  for a **fresh, fully green decoded guest frame after input** before enabling R
  confirmation. It shows **WAITING FOR GREEN**, logs A-to-green delay and then
  asks you to confirm the visible picture. L/ZR can still record a problem.
  Decoded pixels and your observation are distinct evidence.
- Image scaling caches coordinate maps and copies repeated rows. It produces the
  same nearest-neighbor RGBA bytes, black borders and padding while removing
  millions of repeated 64-bit divisions per display frame. Exact desktop pixel
  tests pass; the actual Wii U speed improvement is **not measured yet**.
- An AOT dispatch that repeatedly returns without executing an instruction can
  previously loop forever without consuming its budget. After 64 such dispatches
  the runtime now returns a visible missing-block fault. The old engine fails a
  regression deadline and the new engine passes. This defect has **not been
  identified as the cause of the console freeze**. GPU/platform hangs are outside
  this software guard.
- Live timing replay drives held/released A before execution, with both call
  limits and host clock steps from 1 ms to 1 s, including the console's 50,000
  instruction budget. It completes red/green/red/green and guest exit on desktop.
  At the old 285 ms cadence, green is delayed about 7.1 s; at a 1 s cadence with
  late input, about 47 s. Initial blank buffers are counted separately; these
  cases have no mixed frames. This explains a possible delay, not the hard freeze.

The kit retains **Native**, **Guarded AOT**, **Compiled AOT**, and **Compiled Save**
RPX/WUHB variants; menu names show **0.14.4**. Reports remain under `sd:/ctr2wup/`.
If a later retest is requested, copy the kit's `sd-card/` contents to the SD root
and replace older files, or use its matching RPXs with your existing launcher.
Follow the new waiting/confirmation prompt and preserve partial files on failure.

Local tests and cross-compilation pass. Compiled console stability and save/WUP
acceptance remain open. The kit is a diagnostic update, not a verified freeze fix
or an installable commercial-game WUP.
