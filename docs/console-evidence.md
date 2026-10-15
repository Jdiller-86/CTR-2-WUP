# Console evidence review

## October 15 correction: reuse the native platform evidence

The new M1 suite incorrectly described the full native controls and timed
stability checks as missing. The reviewed Native 0.14.1 report already has all
ten guided steps PASS, nine software checks PASS and zero input errors/faults.
The original guide includes both screens, buttons, both sticks, touch
positions/border/alignment, both-output tone, the ten-minute interval and final
observation. Its native step definitions and 600,000 ms threshold match the
current guide. The later user-reported 0.14.5 successes also explicitly
superseded the request to repeat long input guides. **No full native guide or
ten-minute repeat is requested.**

The [reviewed M1 evidence ledger](m1-evidence.json) preserves #12 result
collection as Done and records #14 controls/audio as Done for that native
baseline, including the later 0.20.0 silence/tone regression. The 0.24.1 return
already covers repeated Minus/HOME exits with observed returns; Save 0.14.6
already establishes exact fresh-process persistence. For #13, only the
automatic SD interrupted-publication/error-recovery probe remains physically
unverified; combine it with the eventual installed-title run.

The original Minus hang and older save failures remain in the history; their
existence does not erase the later scoped successes. New display-cache behavior
and Audio Streams still have no physical acceptance. The 0.27.0 original
plaintext/self-signed WUP candidate now removes the common-key, production-
certificate and donor prerequisite. Signature-patched Aroma is required; actual
installation and Menu launch/exit/relaunch still need the new focused evidence.
This ledger does not accept the full milestone or apply
live Project changes automatically. Earlier blanket statements below about
missing controls, timed stability or normal Minus/HOME evidence are superseded
by this correction.

## October 14 evening: arithmetic, repeated guest draws and measured scaling

Received October 15 UTC (October 14 in America/New_York). The later complete
folder ZIP resolves all requested trace/repeat evidence for this hardware round.
It contains five source-matched full traces and five 8 KiB result files; the
four reports from the earlier collector ZIP are byte-identical. All CRCs and
per-file hashes verify. The corrected collector/analyzer processes the actual
native filenames with no missing-pair warnings. The request for more files is
resolved; no additional test run is requested for this round.

The returned 0.24.1 collector ZIP has four 8 KiB result files. ZIP/receipt
checksums and every result's clean source identity agree with tested kit source
`4eeee0ba3a839f934c6bb7d6313f5fcb657782c5` (the source tree at PR #106 head
`d292d762dca03b03815c74435bda5535def7e023`). Raw files and the intake receipt
remain in ignored `private/console-evidence-20261015/`; only aggregates are
published. The user reports firmware **5.5.6 U**, **Aroma Beta 27**, no separate
launcher, normal Minus returns, optional HOME return and no observed problems.

| Diagnostic | Reviewed physical evidence | Remaining scope |
|---|---|---|
| Scene Check 0.24.1, two launches | Both exact ADD/MUL analytic pixel/index checks pass; picture and shared diagonal each PASS; normal Minus exit reported on both launches | Scoped CPU software raster/GX2-copy check, not native shader translation or full lifecycle acceptance |
| Guest Draws 0.24.1, first run | Own PASS/exit, 4 draws/4 triangles/97,200 fragments/36 shader steps, zero fallback/fault; submitted serial 2 and user picture PASS | Guide cancelled after 2 PASS; Plus repeat NOT TESTED. User confirms exiting before pressing Plus, rather than a failed restart |
| Guest Draws 0.24.1, later Plus follow-up | Matching report/trace verify 2 guest sessions, 2 own PASS messages, submitted serial 2 in each session and all 3 guide steps PASS, with no failures/skips/fallback/fault | Drawn latency is 91.190 s initially and 91.613 s after Plus; timing remains diagnostic rather than game FPS |
| Scene Check 0.24.1, third launch | Automatic arithmetic checks pass; guide remains at step 1; ProcUI transitions 0/2/3 and complete shutdown through `main_return` | User identifies this as the optional HOME return; picture prompts were intentionally not repeated |

The first two arithmetic launches report `pixel_selftest=0`, both index renders
`ok`, 30 shader steps per render and 0.666–0.951 ms render times. Guest Draws
executes 17,532,421 instructions; this physical count differs from the host
reference because it is a separate live-time run. All four reports reach
`resources_closed_before_system_transition` with host exit result 0 and no
reported storage error. That checkpoint is before the final system transition;
successful launcher return comes from the user's observation.

The full traces now show ordered guest/audio/graphics/report cleanup, ProcUI and
WHB shutdown and `main_return` for **all five launches**. Both completed Scene
Checks and both Guest Draws launches use Minus; the HOME-associated Scene Check
exits through the foreground-release/exiting ProcUI path. Launcher return still
has the separate user confirmation; `main_return` alone is not a Menu screenshot.

Picture confirmation now has direct chronological evidence. The first draw is
submitted before the visual-ready marker and fresh R answer in both launches.
In the completed follow-up, submission is at 92.250 s of the host session and
the ready marker at 92.325 s; the picture answer follows at 122.831 s. Plus then
requests session 2, its own scene is submitted at 218.665 s of the host session,
and its own guest PASS/exit precede step 3 PASS and guide completion. The final
report records 17,537,612 instructions for the second guest session, zero
fallback/fault and unchanged 4/4/97,200/36 draw counters. The original cancelled
first run is retained as history, rather than overwritten by the successful run.

The Guest Draws submitted-scene latency is **91,549 ms** and mean diagnostic
host frame is **146,527 us**. These results do not establish an improvement over
the dated 57,330 ms 0.23.0 trace: the builds, logging and timestamp origins differ,
and this is not a controlled CPU/game benchmark. The recorded host phase counters
identify a concrete next optimization target:

| Host phase | Calls | Total seconds | Mean ms/call | Maximum ms/call |
|---|---:|---:|---:|---:|
| Guest execution/HLE | 824 | 6.515 | 7.906 | 46.146 |
| Pixel decode | 2 | 0.108 | 53.908 | 53.923 |
| Scale and source/cache publication | 824 | 108.999 | 132.281 | 133.886 |
| GPU wait API | 824 | 0.006 | 0.007 | 0.205 |
| Presentation APIs | 824 | 2.280 | 2.767 | 3.224 |
| Result/trace commit storage | 239 | 3.171 | 13.270 | 16.051 |

Scaling accounts for about 90% of the sum of these recorded phase times. The
two completed native launches also spend about 132 ms per scale call without
running the compiled draw guest. This supports optimizing the measured native
scale/publication path before increasing guest budgets. These are cumulative
host wall-time counters with diagnostic overhead, not GPU timestamps or a full
CPU/GPU attribution. The 46.146 ms maximum guest phase also confirms that the
8 ms cooperative target is not a hard per-frame deadline.

The completed two-session launch corroborates the bottleneck: mean host frame
146.579 ms, mean scale/publication 132.218 ms, about 89.9% of summed recorded
phase time. Guest execution/HLE totals 13.021 s with a 48.636 ms maximum phase.
Final slice stops are 1,168 soft-time, 4 new-frame, 7 waiting and 440 stopped;
there is no slice-contract failure. Each guest still records 3 IPC lookup
failures, so zero fallback/fault must not be described as zero service failures.

**Collection defect:** the shipped collector expected a version in trace names;
the native writer uses `application-<session>-<attempt>.txt`. It therefore
included results but silently missed full traces. The corrected collector
accepts the actual trace names only after matching application, build and clean
source headers, and warns about absent paired traces. The analyzer also warns
when an older collector's return lacks them. The original return remains valid
for its four result files; the later folder ZIP supplies the full chronology,
slice-stop details and new completed repeat report. The actual-file collector/
analyzer round trip verifies all ten payloads without missing-pair warnings.
The file-retrieval request is closed. The initial ZIP's cancelled guide remains
the first run's history. No further files or reruns are requested for this round.
Full stability/recovery, installed WUP, commercial gameplay and all seven full
gates remain open.

## October 13: launch audio, GX transfers and compiled PICA draws

The new private ZIP contains three full traces and two independent 4 KiB result
files. Audio Check 0.20.0 records both launch-music silence and the native tone
as PASS in its full trace; its separate result file was not included. GX Transfers
0.20.0 and Guest Draws 0.23.0 each record both guide steps PASS in matching result
and trace files, with no failed or skipped steps. All three traces reach
`main_return`; the user separately confirms no launch music and correct exit.
This is scoped hardware evidence, not full milestone acceptance.

| Diagnostic | Reviewed physical evidence | Timing and limits |
|---|---|---|
| Audio Check 0.20.0 | Silence prompt and native tone on both outputs PASS; five tone requests; clean shutdown | No guest DSP/audio claim; standalone result TXT absent from this upload |
| GX Transfers 0.20.0 | Original guest PASS/exit, 17 GX operations, four paired presentations, zero fallback/fault | Final transfer presentation at 6,882 ms; mean diagnostic host frame 145,948 us |
| Guest Draws 0.23.0 | Own PICA draw PASS/exit, four triangles, 97,200 fragments, 36 shader steps, zero fallback/fault; user confirms pictures | Rendered second presentation at 57,330 ms; session 70,021 ms; mean host frame 153,189 us |

The physical draw run executed 17,457,061 instructions. The host reference count
is 17,420,794; they are different runs, not an exact instruction-count match.
The user estimates roughly two minutes before the draw picture appeared.
The trace's first presentation at 1,235 ms is the startup framebuffer, not the
rendered scene. These aggregate timings include diagnostic overhead and do not
measure a game frame rate or isolate CPU rasterization, scaling, GPU waits or SD
publication costs.

The visual prompt accepted R at 10,221 ms, before the drawn second presentation.
Its guide PASS alone therefore cannot prove the final triangle picture was
confirmed at the right time. The user's separate confirmation and the later
guest's own target/readback PASS remain evidence. A focused follow-up must gate
the visual answer on the drawn presentation, measure individual phases and
review the fixed 50,000-instruction-per-refresh budget. No audio, Save or RomFS
repeat is requested solely for this evidence update.

The user is available with Windows 11, an SD reader and current mainstream Wii U
firmware/Aroma; exact versions were not supplied. Raw files and their SHA-256
receipt remain in ignored private storage. Full lifecycle/recovery, installed
WUP, guest audio, broader graphics and useful game performance remain open.

## October 12 follow-up: current reports and fresh-launch save

Five completed named result files were reviewed from the latest private ZIP.
The user confirms correct screens and normal Minus return. All apps continued
playing Wii U launch music; that remains an open audio defect. Raw uploads,
snapshots and receipts stay in ignored private storage.

| Test | Purpose | Reviewed result |
|---|---|---|
| Save 0.14.6 | Write original save bytes and reload the SD snapshot | Two steps PASS; follow-up trace confirms fresh-launch reload |
| Embedded RomFS 0.14.6 | Read nested/Unicode assets and reject writes | One step PASS in each of two launches |
| SD Asset Files 0.16.0 | Read a tiny original asset blob from SD | One step PASS |
| GX Transfers 0.18.0 | Copy/fill/transfer pixels and finalize no-draw PICA configuration | Two steps PASS; 17 GX operations, 11 configuration writes |

Every new result reports an exited guest, zero interpreter fallback and no guest
fault. The 88-byte snapshot has a valid CRC and both original visit-2 records.
The follow-up Save trace identifies 0.14.6, loads the existing snapshot in its
first guest session, records `RELOAD PASS`, completes both steps without failures
or skips and states **Fresh-launch save reload confirmed: YES**. Cleanup reaches
`main_return`; Menu return also has the user's separate confirmation. No new
Save or RomFS run is requested.

The ZIP retains older failed Save 0.14.5 traces and an empty Guarded file, separate
from current successes. A nonempty Guarded 0.14.5 trace records all eight guide
steps PASS. This does not identify the cause of older failures or establish
universal freeze recovery.

Next: [the short launch-audio check](launch-audio.md). Exact platform versions,
Home/APT routes, installed WUP acceptance, actual PICA draws and commercial title
requirements remain open. Scoped successes do not accept a full milestone.

## Earlier intake history

Received October 12, 2026 UTC (October 11 in America/New_York).
The raw TXT and SHA-256 receipt are retained privately, outside source bundles.

## Latest folder upload and v0.14.5 feedback

The user reports Native, Guarded and Compiled completed with no errors on
0.14.5, including the latest input/exit round. No new reports were found; Save
failed and left a snapshot. This is useful **user-reported evidence**, separate
from the uploaded file identities:

| Uploaded file | Identified evidence |
|---|---|
| `native-00ba9c0725b6c820-00.txt` | Native 0.14.3; cancelled first guide step, nine self-tests PASS, complete exit checkpoints |
| `native-00ba9bdc28fd74db-00.txt` | Native 0.14.2; cancelled first guide step, nine self-tests PASS, stops at graphics cleanup begin |
| `native-00ba9b456393b96e-00.txt` | Native 0.14.1; previously received ten guide/nine self-test passes |
| `native-ba9b26dfd4988b.log` | Compiled 0.14.0 despite its filename; previously received guest PASS |
| `compiled-guarded-00ba9c07ed661313-00.txt` | Guarded 0.14.3; same first delayed-green run reviewed below |
| `filesystem-test.snapshot` | 88-byte valid CTRFS001, archive 4, correct CRC, two exact 12-byte visit-1 records |

None of the text headers says 0.14.5. The snapshot has no build identity or failure
stage. It proves an initial write; it does not establish later save/reload or
report generation. All six originals and SHA-256 receipts are preserved under
ignored `private/console-followup-20261011/`, excluded from source/diagnostic kits.
A private desktop replay advances its copy to visits 2 and 3 with zero fallback;
the original remains unchanged. No private bytes are published.

v0.14.6 adds independent named TXT results with exact closed-file read-back and
save publication that does not require rename replacement. Unit/sanitizer tests
cover failures and rollback, but these are **not confirmed causes/fixes for the
console behavior**. The exact missing-report and save failure stages remain
unknown. The next requested evidence is [Save then RomFS](console-retest.md).
The v0.14.5 successes supersede the old request to repeat the long input guides;
no new claim of commercial or full milestone acceptance follows.

## Historical guarded 0.14.3 report

The uploaded report identifies **compiled-guarded 0.14.3**. Its complete shutdown
and guide results match the user's first, delayed-green run. It does not capture
the second launch that froze; the user confirms that launch saved no partial file.

| Observation | Recorded evidence |
|---|---|
| Initial picture | User confirmed PASS |
| A input | One press; held A reached the standard HID publication |
| Button-response guide | User marked FAIL; this remains a failure |
| Guide completion | Cancelled during touch step: 1 passed, 1 failed, 6 unfinished |
| Embedded checks | All nine returned PASS |
| Post-input checkpoints | 64 complete ordered frames, including two pixel-decode pairs |
| Guest execution | 6,029,629 instructions, no interpreter fallback or guest fault |
| Display | Five matched presentations; no reported device failure or dropped IRQ |
| Host-frame timing | Average 285,194 us, range 261,544–326,732 us; about 3.5 frames/s |
| Guarded AOT | Maximum call depth 2; 24 recorded unwinds |
| Shutdown | Resource cleanup, RELEASE_FOREGROUND, EXITING, ProcUI/WHB shutdown and `main_return` checkpoints |

The user separately confirms launcher return. A `main_return` marker alone does
not prove that the Wii U Menu finished loading. The log has three IPC request
failures, so zero guest faults must not be described as zero service failures.
Frame-boundary stack headroom is not a measurement at the deepest native call.

The long host-frame time supports investigating response delay. This version
does not log decoded frame colors or A-to-green latency, so the exact delay and
green-frame timing cannot be derived from these counters. Continued execution
in this run cannot identify the cause of an intermittent failure on another run.

v0.14.5 adds checked file sync/close and mounted-volume sync before continuing
past critical checkpoints. Desktop process-termination tests verify scoped
recovery, and SDK builds verify the actual imports. SD controller durability,
FSA behavior during a console freeze, and the original freeze remain unverified.
That earlier retest request is superseded by the latest feedback and v0.14.6
save/result-file round above.
