# Focused Wii U display-cache comparison — 0.25.0

**New build, physical comparison pending:** the completed 0.24.1 round remains
accepted for its scoped arithmetic/picture/restart/exit checks; all its files
are received. Version 0.25.0 changes native scaling and CPU-to-GX2 publication.
This short two-app run checks picture freshness, restart and timing for that
changed path. Do not repeat Save/RomFS/audio/GX guides. The
[baseline review](console-evidence.md) records 91.190–91.613 s draw latency and
132.218–132.281 ms mean scale/publication on firmware 5.5.6 U / Aroma Beta 27.

The manifest identifies the exact clean source of the kit built for PR #106,
which is now merged. The existing kit retains its original source identity.
Host pixel/traffic comparisons and cross-builds do not establish Wii U speed or
picture correctness. See the [cache contract](gsp-display.md#native-display-cache).

## Copy to SD

1. Extract the downloaded hardware-round ZIP on Windows.
2. Copy the **contents of `sd-card/`** to your SD root. This adds two WUHBs under
   `sd:/wiiu/apps/`. Keep your existing `sd:/ctr2wup/` reports and saves.
3. Launch the apps through your normal Aroma homebrew launcher. Check that the
   on-screen build is **0.25.0**. These are homebrew launch files, not WUPs; they
   do not need a ROM, keys or an installer.

You can upload those same two WUHB files by FTP to `sd:/wiiu/apps/` instead of
removing the SD card. Use **binary transfer mode**. The FTP server may expose the
SD under a different root name; select its SD-card mount. Let uploads finish
before launching the apps. Preserve the old reports; upload only the two new WUHBs.

The first run creates unique result/trace files. **RESULT VERIFIED** means its
current 8 KiB result was closed, reopened and compared byte for byte. It does not
prove power-loss durability. If storage fails, guest input pauses; use Minus
and return any available files plus the displayed error. Do not delete or
replace older evidence to make a test pass.

## 1. Scene Check: arithmetic, picture and repeat launch

Open **CTR2WUP Scene Check 0.25.0**.

- Automatic checks render the original exact ADD/MUL transform in both index
  formats and compare every expected pixel on the console. TV and GamePad show
  a square with **red lower left, green upper right and a dark border**. TV has
  the guide overlay; GamePad shows the unobscured scene.
- Follow the two on-screen picture/diagonal questions. **R** means correct,
  **L** means a problem. A submitted scene and passing automatic checks are
  required before a positive observation can be recorded.
- At **GUIDED TEST COMPLETE**, press **Minus** and note whether you returned to
  the launcher. Open the **same app once more**, repeat the short guide, then
  Minus again. Keep both launches' files; this checks fresh initialization and exit.

The reference picture is `reference/arithmetic-scene.png`. This is a CPU
software renderer with GX2 display copies, not native GPU shader translation.

## 2. Guest Draws: accurate scene, timing and in-app repeat

Open **CTR2WUP Guest Draws 0.25.0**.

- Wait for the red/green rectangles on **both** screens. The initial startup
  image is deliberately **not** accepted. The guide stays in **WAIT** until a
  paired, decoded frame with the expected draw counters has been submitted.
- When the picture prompt is ready, make a fresh **R** press if correct, or
  **L** if wrong. R held/pressed during startup cannot pre-confirm a later scene.
- The compiled guest checks its own pixels/readback and exits automatically.
  Press **Plus when prompted** to run it again; wait for the new run's automatic
  PASS/exit and submitted scene. Do not press B to force an exit.
- At **GUIDED TEST COMPLETE**, press **Minus** and note launcher return.

No stopwatch or manual timing entry is needed. The logs record per-run drawn
submission latency, instructions/fallback/faults, slice stop reasons, and host
wall-time totals/maxima for execution/HLE, pixel decoding, scaling, GPU wait,
presentation and SD result/commit operations. New `Scale cache TV/DRC` and
`PUBLISH` records count full/reused updates, scaled pixels, requested publication
bytes/ranges and heap storage. Compare **new scale + publish** against the old
0.24.1 `scale` phase, which included both operations. Both screens still receive
a GX2 surface copy every frame; these counters do not measure GPU bandwidth. The execution target is a
**cooperative 8 ms** slice, at most 500,000 instructions/50 calls of 10,000 each;
one generated block or service call may overrun wall time. No guest clock is
fast-forwarded. Presentation timing is host API wall time, not GPU timestamp
profiling or game FPS; inline trace work can be included in execution time.

The 0.24.1 baseline took 91.190–91.613 seconds. This build's
speed is unknown until your run. Check that counters and guide text keep changing,
the new guest scene appears after Plus, and neither screen retains stale pixels. If it remains at WAIT for about three minutes,
use **ZR** to record the problem, then **Minus** if responsive, and return the
files. Do not wait indefinitely or repeat a hung run.

## Optional HOME follow-up

After the required Minus tests, you may reopen Scene Check and try HOME once.
Record whether it returned, suspended, ignored HOME or hung; launcher behavior
is environment-dependent. This is separate from the automatic checks. Completed
results are already written, but successful launcher return needs your observation.

## Return one ZIP

### FTP, without running the collector

1. Finish the tests and close the diagnostic apps before downloading logs. Then
   open your FTP server and browse to the SD card's `ctr2wup/` directory.
2. In **binary transfer mode**, download **all** files matching
   `result-native-software-scene-0.25.0-*.txt` or
   `result-compiled-pica-draws-0.25.0-*.txt`. Also download each matching session's
   trace: `native-software-scene-<session>-<attempt>.txt` or
   `compiled-pica-draws-<session>-<attempt>.txt`. Trace filenames have no version;
   use the `Full trace: ... file=...` field in each result to identify the file.
   Include both Scene Check launches and incomplete runs.
3. Keep the original filenames. Wait until the FTP queue finishes, then select
   the downloaded TXT files in Windows and **Compress to ZIP file**. Send that
   ZIP in this chat. The `.cmd` and Python are optional for this route.
4. In your message, report whether Minus returned to the launcher after **both
   Scene Check launches** and after **Guest Draws**, plus any visual, freeze or
   storage problem. Include the optional HOME outcome only if you tried it.
   Firmware/Aroma/launcher versions are useful if known.

Return incomplete/failure logs too. Do not delete the originals on the Wii U,
and do not include saves, ROMs, keys or unrelated files. The raw-log ZIP can be
processed using the tested kit's source identity; a collector receipt is not
required. No screenshots are needed unless a visual failure needs explanation.

### Optional collector, from FTP downloads or an SD reader

The collector also works on an ordinary local folder. If you prefer its
checksummed receipt and observation prompts, download the matching files into
`C:\CTR2WUP-return\ctr2wup\`, then run **`collect-hardware-results.cmd`** from
the extracted kit. At its SD-root prompt enter **`C:\CTR2WUP-return`** (the
parent of `ctr2wup`), even though the files came over FTP. Keep the kit's
`manifest.json` beside the collector. Use the collector from the kit you tested.

This kit's collector includes unversioned traces after checking their
application/build/source headers and warns about missing pairs. The tool also
supports the completed 0.24.1 round when given that round's manifest; it never
combines different versions into a new round.

With an SD reader, close the apps, remove the SD normally and enter its drive
root instead, such as **E:**. The collector uses your installed Python 3 launcher.
Answer the short launcher/problem questions, then send its generated
**`CTR2WUP-results-0.25.0-<time>.zip`** back in this chat. A blank observation is
recorded as not reported.

The collector reads only this version's `native-software-scene` and
`compiled-pica-draws` TXT files under `sd:/ctr2wup/`. It includes incomplete/failure
reports and checksums, leaves originals untouched, and excludes saves, ROMs,
keys and other versions. The return has a 24 MiB uncompressed budget and should
be well below the chat upload limit. If collection fails, send the error and
the matching TXT files manually; preserve any oversized trace separately.

If no files were created, say so and report the on-screen storage error. Exact
source/build identity, user observations and machine checks remain distinct;
this round cannot accept a full milestone, commercial gameplay or WUP installation.
