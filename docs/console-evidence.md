# Guarded console evidence review

Received October 12, 2026 UTC (October 11 in America/New_York).
The raw TXT and SHA-256 receipt are retained privately, outside source bundles.

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
The next requested evidence is [one short guarded retest](console-retest.md).
