# Focused Wii U hardware round — 0.24.1

This two-app test build checks new exact ADD/MUL arithmetic on Wii U and measures
the corrected compiled-draw path. Its package manifest names the exact source
commit. It is a premerge hardware evaluation from PR #106; host/cross-build
checks do not establish a physical pass. Earlier Save/RomFS/audio/GX results
remain valid for their builds. Do not repeat those guides for this round.

## Copy to SD

1. Extract the downloaded hardware-round ZIP on Windows.
2. Copy the **contents of `sd-card/`** to your SD root. This adds two WUHBs under
   `sd:/wiiu/apps/`. Keep your existing `sd:/ctr2wup/` reports and saves.
3. Launch the apps through your normal Aroma homebrew launcher. Check that the
   on-screen build is **0.24.1**. These are homebrew launch files, not WUPs; they
   do not need a ROM, keys or an installer.

The first run creates unique result/trace files. **RESULT VERIFIED** means its
current 8 KiB result was closed, reopened and compared byte for byte. It does not
prove power-loss durability. If storage fails, guest input pauses; use Minus
and return any available files plus the displayed error. Do not delete or
replace older evidence to make a test pass.

## 1. Scene Check: arithmetic, picture and repeat launch

Open **CTR2WUP Scene Check 0.24.1**.

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

Open **CTR2WUP Guest Draws 0.24.1**.

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
presentation and SD result/commit operations. The execution target is a
**cooperative 8 ms** slice, at most 500,000 instructions/50 calls of 10,000 each;
one generated block or service call may overrun wall time. No guest clock is
fast-forwarded. Presentation timing is host API wall time, not GPU timestamp
profiling or game FPS; inline trace work can be included in execution time.

The previous 0.23.0 drawn presentation was logged at 57.33 seconds. This build's
speed is unknown until your run. If it remains at WAIT for about three minutes,
use **ZR** to record the problem, then **Minus** if responsive, and return the
files. Do not wait indefinitely or repeat a hung run.

## Optional HOME follow-up

After the required Minus tests, you may reopen Scene Check and try HOME once.
Record whether it returned, suspended, ignored HOME or hung; launcher behavior
is environment-dependent. This is separate from the automatic checks. Completed
results are already written, but successful launcher return needs your observation.

## Return one ZIP

1. After closing the apps, remove the SD normally and insert it in Windows.
2. In the extracted kit, double-click **`collect-hardware-results.cmd`**. It uses
   your installed Python 3 launcher. Enter the SD drive/root, such as **E:**.
3. Answer the short launcher/problem questions. Environment versions are optional;
   a blank answer is recorded as not reported. No screenshots are needed unless
   a visual failure needs explanation.
4. Send the generated **`CTR2WUP-results-0.24.1-<time>.zip`** back in this chat.

The collector reads only this version's `native-software-scene` and
`compiled-pica-draws` TXT files under `sd:/ctr2wup/`. It includes incomplete/failure
reports and checksums, leaves originals untouched, and excludes saves, ROMs,
keys and other versions. The return has a 24 MiB uncompressed budget and should
be well below the chat upload limit. If collection fails, send the error and
the matching TXT files manually; preserve any oversized trace separately.

If no files were created, say so and report the on-screen storage error. Exact
source/build identity, user observations and machine checks remain distinct;
this round cannot accept a full milestone, commercial gameplay or WUP installation.
