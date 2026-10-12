# Console evidence review

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
