# Current console follow-up

The next focused hardware round is being prepared under scoped #87/M2. It will
provide an arithmetic Scene Check and bounded/profiling Guest Draws, accurate
drawn-frame confirmation, per-phase metrics and a Windows return-log collector.
This is a development plan, not a published kit or physical acceptance.

The Save, embedded RomFS, SD Asset Files and GX checks have reviewed physical
results. The additional Save 0.14.6 trace confirms persistence across closing and
reopening the app. Correct screens and normal Minus return have the user’s
confirmation. See [the evidence review](console-evidence.md).

The October 13 [launch-audio check](launch-audio.md) and Guest Draws follow-up
have reviewed passes, with the user confirming pictures, silence and correct
exit. The rendered draw scene takes 57.33 seconds in the trace. Its visual
prompt accepted an answer before that scene, so the next focused work is
presentation gating and measured diagnostic progress. See
[the evidence review](console-evidence.md).
There is no need to repeat Save/RomFS or the long native controls/stability guide
solely for a version bump. Keep older reports, snapshot and backup siblings.

A new draw-performance build will require its own focused hardware comparison;
this run cannot establish a future build's speed. No further test is requested
for the unchanged audio, Save or RomFS routes.
These original RPX/WUHB diagnostics have separate acceptance from installed WUPs,
commercial gameplay and full milestones.
