# Next acceptance gate and resume steps (0.10.0)

Implementation continued through the verified shared-memory/synchronization,
standard HID input and standard direct GSP display foundations. Checkpoints are
pushed to the requested repository. The next planned acceptance step cannot be
verified with the resources currently available:

- No Wii U is attached. RPX/WUHB cross-compilation and emulated PPC Linux cannot
  establish GX2/VPAD/AX behavior, runtime costs, latency or ten-minute stability.
  All three current builds include the original self-tests and the standard
  HID/GSP display guest needed for this test; no ROM is required.
- The supplied Mario CIA exceeds the attachment transfer tool's 32 MiB limit.
  No bytes, encryption state, exact executable revision or runtime requirements
  have been inspected. The user is away from the computer containing that CIA.
  The delivered Windows intake helper can export a small private executable
  view when available; an encrypted result requires decrypted executable data.

These block physical M3 acceptance and the selected-title P2/P3/M4/M5 route.
More generic kernel/graphics code can be written independently, but it does not
replace the missing acceptance evidence or determine which game-specific shaders,
services, modules and reference scenes the next title implementation requires.
The current direct display subset does not supply stock libctru startup,
multi-thread scheduling, GSP relay/shared/GX queues, VBlank, PICA, audio or saves.
No commercial profile or playable Mario build is enabled.

Resume in this order when the evidence is available:

1. Run the v0.10 diagnostic RPX/WUHB and the standard HID/GSP mode per
   `console-testing.md`. Record every self-test outcome, corner/channel/rotation
   correctness, touch mapping, input latency, audio/lifecycle, ten-minute stability
   and SD logs, with firmware/homebrew/launcher versions. Fix reproducible console
   failures before expanding the runtime or drawing performance conclusions.
2. Use `CTR2WUP-CIA-Intake` with the original local CIA and privately return its
   small `CTR2WUP-intake.zip` per `cia-intake.md`. Analyze actual hashes/segments/
   allowlists and pinned-lifter coverage; establish the exact Mario revision.
3. Capture a private boot/unsupported trace and select the exercised next kernel,
   graphics queues/threads, FS/APT, modules, DSP/audio and save interfaces. Implement
   those common runtime features with targeted original fixtures. Use trusted
   PICA scene/reference data to implement the required GX2 path, then verify boot,
   gameplay, save/reload and stability on Wii U.
4. Validate the existing original packer's candidate metadata/ticket/FST policy
   using provenance-established local credentials and an actual diagnostic WUP
   installation/Menu launch/removal. Synthetic offline inputs cannot prove this.
5. Finish verified Windows conversion/caching/rebuild and then the UWUVCI-style
   GUI Inject workflow. The final product remains a per-game installable WUP
   generator; current diagnostic bundles are RPX/WUHB, not installable WUPs.

Native Windows execution is also pending. The earlier unapproved manual workflow
remains local; no workflow was published or dispatched. Source/build artifacts and
versioned reports are maintained under `artifacts/`, as recorded in `STATUS.md`.
