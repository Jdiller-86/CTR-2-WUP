# M1 native platform acceptance: guided Wii U test

This is the complete evidence route for [M1](milestones.md#m1). One full Native
guide covers pictures, controls, touch, the original platform tone and a timed
ten-minute session. Two short fresh launches cover persistence and Minus/HOME
exit. The separate installation section is mandatory for the full milestone.
Expect approximately 15–20 minutes for the homebrew section, plus installation.
Send the TXT files and the short answer card below; no PC command is required.

**A complete report is a candidate for review, not an automatic milestone pass.**
The reviewer marks every criterion PASS, FAIL, INCOMPLETE or BLOCKED. M1 can be
accepted only after all criteria have complete reviewed physical evidence.

## Before starting

1. Use the supplied **M1 acceptance** kit. Read `manifest.json` or the kit's
   start page for its version, source commit and suite ID. The Native app's
   displayed version must agree. Keep this kit's manifest with the return.
2. Copy the kit's `sd-card/` contents to the SD root using FTP **binary** transfer
   mode. M1 Check goes under `sd:/wiiu/apps/`. Keep existing `sd:/ctr2wup/` files,
   saves and earlier evidence. The tests use original diagnostic data only.
3. Charge the GamePad, connect both displays, enable normal TV and GamePad sound
   and note the current firmware, Aroma version and launch route. A separate
   launcher version is needed only when you use a separate launcher; otherwise
   record **Aroma Wii U Menu / no separate launcher**.
4. Do not run an SD test while FTP is changing its files. Close FTP before each
   launch. Leave the SD inserted and the console powered normally throughout.

The kit must declare `recovery.available: true` with protocol `scratch-v1` for
the persistence/recovery test. A kit without that probe is explicitly blocked
for this criterion. An ordinary successful save cannot substitute for recovery.

The kit also contains **Audio Streams 0.26.0**, a separate five-minute test of
the new stream/mixer path. Use `AUDIO-STREAMS.md` for that optional development
round; use **M1 Check** for the full platform guide below. Its listening return
uses `audio-manifest.json`, while the M1 return uses `manifest.json`.

## 1. Native: the full guide, once

Open **CTR2WUP M1 Check 0.26.0** from this kit (not an older Native icon). It shows a unique result filename and **RESULT
VERIFIED**. That means a closed file was reopened and compared byte for byte;
it does not promise survival of sudden power loss. **TRACE SAVING** describes
the separate longer log. Note the filename, or download it after this run.

Follow the on-screen guide:

| Step | What to do | Evidence |
| --- | --- | --- |
| Picture | TV has colored bands; GamePad has the checkerboard and text. Both images are stable and aligned. Press R if correct or L if wrong. | Your observation |
| Buttons | Press A, B, X, Y, L, R and each D-pad direction when requested. | All ten presses recorded automatically |
| Sticks | Move each stick fully up, down, left and right, one stick at a time. | Each direction recorded automatically |
| Touch positions | Touch all four image corners and the center. Touch inside the displayed image. | Five positions recorded automatically |
| Border | Touch a black side border outside the image. | Rejected border touch recorded automatically |
| Touch alignment | Check the marker follows your finger accurately. R if correct; L if wrong. | Your observation |
| Native tone | Press A. Hear the short tone on both the TV and GamePad. R if both work; L if either fails. | Tone request and your observation |
| Stability | Leave the app running while the ten-minute countdown finishes. Watch for freezes, flicker, stopped counters or unexpected audio. | Actual timed interval plus your observation |
| Final observation | R if no problem occurred; L if a problem occurred. | Your observation |

**R means “I checked and it passed”; it is not a Next button.** ZL leaves a
step NOT TESTED. ZR records a failure. Use these honestly; a partial or failed
run is useful evidence and stays in the return. Do not repeat a failed run
until you have kept its files and described what happened.

Confirm the Wii U launch music stopped after Native opened. The tone is the
original native platform test; it does not prove 3DS DSP service compatibility.

The automatic SD scratch probe checks its own sentinel bytes, a simulated
interrupted publication with a readable previous copy, rejection of ambiguous
pending data, an intentional invalid write path and a successful valid retry.
It operates in `sd:/ctr2wup/m1-scratch-v1-<source>/`. It never modifies your
game saves or the earlier diagnostic snapshot. No SD removal, forced power-off
or manual corruption is part of this test. Preserve any error and stop that
section if the probe fails.

After **GUIDED TEST COMPLETE**, press **Minus**. Wait for the actual launcher
or Wii U Menu. Say whether it returned correctly. The log's `main_return`
marker records program cleanup, so your return observation is also required.

## 2. Native: short fresh-launch persistence and repeat exit

Open the **same Native app again from its icon**. Plus is an in-app restart and
does not count as a fresh process. The new launch must show a different result
filename. Its SD probe must report that the sentinel from the preceding launch
was loaded and verified, then repeat the recovery/error checks successfully.

Check both pictures and wait for the storage status. You do **not** need to
repeat the ten-minute guide. Press **Minus**, wait for the launcher/Menu and
record that it returned. This run's unfinished guide steps are expected; the
reviewer requires the full guide only in section 1.

## 3. Native: short HOME lifecycle check

Open Native once more, check that pictures and live counters appear, then press
**HOME** and finish the normal return/close route offered by your environment.
Record any HOME menu choice you used and where you returned. Wait for a normal
launcher/Menu return. Do not force-close or power-cycle an unresponsive test
just to turn it into a pass. If it hangs, report the last screen, wait duration
and any recovery action; keep the partial logs.

This third launch needs a third distinct filename. No ten-minute repetition is
required. If a particular launcher has no supported HOME exit, record that
limitation: the route remains incomplete until a reviewed supported lifecycle
route has evidence.

## 4. Installed original diagnostic: required for full M1

**An RPX or WUHB copied to `wiiu/apps` does not satisfy this section.** Opening
a WUHB from an Aroma Menu icon still launches homebrew; it does not demonstrate
installation of a WUP title.

Check the kit manifest's package status before proceeding. If it says
`package.available: false`, **stop this section and return the other results**.
The reviewer reports M1's installed-title criterion as BLOCKED. The missing
prerequisite is a real, source-matched original WUP with reviewed local packaging
material/provenance, explicit nonreserved title identity checked against your
installed titles, and independently verified content. An offline package made
with synthetic keys/certificates is not an install-test package. See
[packaging prerequisites](packaging.md).

When a reviewed real package is supplied:

1. Use your **WUP Installer GX2**. Record its displayed version; if no version
   is shown, record the installer app/build or its source instead. Record the
   required patched environment and your installation target (system memory or
   USB). Compare
   the expected title name and ID with the supplied package report. Resolve any
   collision before installing; do not overwrite an unrelated title.
2. Install **that original diagnostic WUP** through WUP Installer GX2.
   Record success or the exact displayed error. Keep its source-bound package
   report/hash locally; return its report identity, not encryption material.
3. From the **Wii U Menu**, launch the installed title. Confirm its expected
   title/name and both screens. Note its result filename. Press Minus and
   confirm normal Wii U Menu return.
4. From the Menu, launch the **same installed title again**. Note the new result
   filename, check both screens, then Minus and normal Menu return again.

No ten-minute guide repetition is requested for these two installation runs.
Runtime logs must identify the installed title ID rather than a homebrew
launcher title. Actual installer success, Menu launch, visible title identity
and final return remain your observations; package integrity alone proves none
of them. This is an original native diagnostic, not a commercial-game package.

## Return files over FTP

After exiting all tests, open FTP. Copy the following from `sd:/ctr2wup/` to
one new local folder, preserving the exact filenames and bytes:

- Each current kit `result-native-m1-<version>-<session>-<attempt>.txt`, including
  any failed or incomplete attempt. This is the kit manifest's `native_app`.
- Each paired `native-m1-<session>-<attempt>.txt` full trace. **Trace filenames do
  not contain the version.** The result's `Full trace` line names its exact
  partner; the attempt suffixes may differ.
- The kit's `manifest.json` and this filled answer card, or send the answers in
  your chat message. For an installed run, identify the reviewed package
  report and its hash/title ID.

ZIP this small folder and attach it here. No collection command is necessary.
Do not include ROMs, game assets, keys, certificates, package contents, game
saves or other `ctr2wup` data. The scratch sentinel itself is not needed. If no
report exists, say **no file created**, include the visible storage error and
send any partial trace. Do not delete earlier failure evidence.

```text
M1 SUITE ID:
On-screen version / manifest source:
Firmware (previously 5.5.6 U; confirm or correct):
Aroma (previously Beta 27; confirm or correct):
Launch route / separate launcher version, or “no separate launcher”:
SD format / capacity:
TV output mode:

1 Full Native result filename:
TV / GamePad correct? Tone heard on TV / GamePad?
Touch marker aligned? Launch music stopped?
Full ten-minute countdown completed without a problem?
Minus returned to which screen? Any problem?

2 Fresh repeat result filename:
Storage probe loaded preceding sentinel and passed?
Minus returned to which screen? Any problem?

3 HOME result filename:
HOME menu choice, if any:
Returned to which screen? Any problem?

4 WUP supplied by kit, or BLOCKED/no real package:
WUP Installer GX2 version (or app/build/source if not displayed):
Required patched environment / installation target (system memory or USB):
Expected package report/hash / title name / title ID:
Installation outcome or displayed error:
First installed result filename / launched from Wii U Menu / both screens / Minus return:
Second installed result filename / launched from Wii U Menu / both screens / Minus return:

Other attempted launches / issues / recovery actions (or none):
```

Blank is **unknown**, not a pass. A skipped prompt stays incomplete; an observed
failure stays failed until reviewed. A problem-free later run does not erase an
earlier hang, publication failure or installer rejection.

## Reviewer: criterion coverage and verdict

Previously reviewed Save/RomFS/GX/arithmetic/Guest Draws results remain valid
for their unchanged scoped checks. Do not request their long guides again just
because the version changed. This round fills M1's missing full native controls,
timed stability, fresh-process recovery and installed-title evidence. The new
SD probe is original scratch data; it does not accept retail save behavior.

| M1 task | Required evidence in this suite |
| --- | --- |
| `m1-results` | Clean exact-source/version/session identity in result and trace body (`M1_SESSION`); unique result; closed-file read-back counter; paired full trace; all returned current-build attempts accounted for |
| `m1-lifecycle` | Distinct full/Minus-repeat/HOME launches; ordered cleanup through `main_return`; tester confirms actual returns; source-bound sentinel survives a fresh process; deterministic interrupted-publication/error-path probe passes |
| `m1-controls-audio` | All ten native named steps PASS; at least 600,000 ms measured in stability step; zero input errors; original tone initialized/requested; explicit both-screen/touch/both-output/silence/stability observations |
| `m1-package-launch` | Independently verified real original WUP, local provenance and collision review; exact installer/environment; actual install; matching native title ID; two distinct Menu launches and observed exits |
| `m1-platform-acceptance` | Complete versioned record of all above with exact environment and no unexplained failure; maintainer reviews and explicitly accepts the full gate |

Keep the raw return and review under ignored private evidence. Convert the
answer card to the reviewer's structured observations, preserving uncertainties
as absent fields and reported failures as `false`. Bind roles to the exact
filenames, never just to the newest file. The tool reads a completed local FTP
folder; it does not access the console or infer a return from cleanup markers.

```sh
python tools/review-m1-results.py PRIVATE/ctr2wup \
  --manifest PRIVATE/manifest.json \
  --observations PRIVATE/m1-observations.json \
  --expected-source THE_INDEPENDENTLY_RECORDED_KIT_SOURCE \
  --output PRIVATE/m1-review.json --checklist PRIVATE/m1-review.md
```

`PASS` means the named criterion has reviewable evidence. `INCOMPLETE` means a
required observation, file, time interval or fresh run is absent. `FAIL` means
reported failure, conflicting identity or failed checks. `BLOCKED` means the
kit lacks a required actual implementation/package prerequisite. The generated
report always keeps `full_milestone_acceptance: false`; only a maintainer can
review the whole record and apply the corresponding live Project transition.

The structured observation root contains `suite_id`, `environment`, `runs`,
`native_observations`, `installation` and `reviewed_attempts`. Run roles are
`native-full`, `native-repeat`, `native-home`, `installed-first` and
`installed-repeat`; each records `result`, `exit_control`, `returned_ok` and
`no_unexplained_problem`. Installed roles also require `launched_from_menu`.
Native observations are explicit booleans `tv_correct`, `gamepad_correct`,
`touch_aligned`, `tone_tv`, `tone_gamepad`, `launch_music_stopped` and
`stability_no_problems`. Installation observations are `installation_succeeded`,
`title_identity_correct` and `installed_both_screens_correct`. Unassigned returned
attempts need a nonempty reviewer explanation keyed by their exact filename in
`reviewed_attempts`; their failures remain visible in the inventory. Do not
publish raw reports or private notes. Publish reviewed aggregate findings only.
