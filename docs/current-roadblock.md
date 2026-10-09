# Next acceptance gate and resume steps (0.14.0)

Implementation continued through the verified shared-memory/synchronization,
standard HID input, direct/shared GSP display, diagnostic VBlank, checked
ARM/Thumb stack/call foundations and normal compiled libctru startup/thread/input/
CPU-framebuffer applications, normal private FS save/read/fresh-process reload
and standalone APT event/wakeup/shutdown. Checkpoints are
pushed to the requested repository. The next planned acceptance step cannot be
verified with the resources currently available:

- No Wii U is attached. RPX/WUHB cross-compilation and emulated PPC Linux cannot
  establish GX2/VPAD/AX behavior, runtime costs, latency or ten-minute stability.
  All three diagnostic builds include original self-tests and standard HID/GSP
  display. A fourth interactive build runs the actual compiled libctru graphics
  thread/input/application exit path. A fifth savedata build tests actual SD snapshot commit and fresh-launch reload.
  These are the next M3 acceptance tests; no ROM
  is required. AOT emulated PPC passes cannot substitute for native Wii U state,
  FPSCR/byte order, GX2 display, input or lifecycle evidence.
- The supplied Mario CIA exceeds the attachment transfer tool's 32 MiB limit.
  No bytes, encryption state, exact executable revision or runtime requirements
  have been inspected. The user is away from the computer containing that CIA.
  The delivered Windows intake helper can export a small private executable
  view when available; an encrypted result requires decrypted executable data.

These block physical M3 acceptance and the selected-title P2/P3/M4/M5 route.
More generic kernel/graphics code can be written independently, but it does not
replace the missing acceptance evidence or determine which game-specific shaders,
services, modules and reference scenes the next title implementation requires.
The runtime now supplies scoped compiled libctru startup and single-core guest
thread scheduling. v0.14 supplies bounded standalone APT and explicitly configured private FS
saves. It does not supply applets, ROMFS/cartridge assets, retail save semantics,
GX commands, PICA or guest audio. v0.11 resumed
ROM-independent development with one local relay/shared framebuffer queue and
diagnostic VBlank. Missing evidence blocks acceptance and title-specific decisions;
it does not block further reusable CPU/kernel/services/graphics development.
v0.12 continues CPU register/stack transfers and nested ARM/Thumb calls with
independent reference tests; see `cpu-stack.md`.
v0.13 continues through real CRT/libctru/newlib code, per-thread CPU/TLS/VFP/FPSCR,
address arbitration, quota queries, CPU barriers/media/signed halfword accumulate
and exact rendered/input results. See `guest-threads.md` and
`compiled-homebrew.md`. Missing physical acceptance and private title intake
are external roadblocks; wider FS/APT/GX/PICA work remains unfinished, not automatically impossible.
v0.14 completed further independent FS/APT work: the original newlib program
commits and reloads exact save bytes in a fresh process; the APT library event
thread and exit run. The next selected-game executable/asset integration needs
the unavailable private dump/trace. Physical SD persistence and interactive
behavior cannot be validated without the console. See `filesystem.md`.
No commercial profile or playable Mario build is enabled.

Resume in this order when the evidence is available:

1. Run the v0.14 diagnostic, compiled interactive and savedata RPX/WUHB per
   `console-testing.md`. Record every self-test outcome, corner/channel/rotation
   correctness, touch mapping, input latency, audio/lifecycle, ten-minute stability
   and SD logs, with firmware/homebrew/launcher versions. Fix reproducible console
   failures before drawing performance conclusions. Common runtime implementation
   can proceed independently; it must retain explicit unsupported states and
   original fixtures rather than guess the selected game's requirements.
2. Use `CTR2WUP-CIA-Intake` with the original local CIA and privately return its
   small `CTR2WUP-intake.zip` per `cia-intake.md`. Analyze actual hashes/segments/
   allowlists and pinned-lifter coverage; establish the exact Mario revision.
3. Capture a private boot/unsupported trace and select the exercised next kernel,
   GX queues/PICA, ROMFS/archive/retail-save, applet, module and DSP/audio interfaces. Implement
   those common runtime features with targeted original fixtures. Use trusted
   PICA scene/reference data to implement the required GX2 path, then verify boot,
   gameplay, save/reload and stability on Wii U.
4. Validate the existing original packer's candidate metadata/ticket/FST policy
   using provenance-established local credentials and an actual diagnostic WUP
   installation/Menu launch/removal. Synthetic offline inputs cannot prove this.
   The new compiled interactive RPX now also passes the complete encrypted
   offline proof; its staging is `artifacts/libctru-title-staging-v13/`. No
   synthetic package or key/certificate is retained or delivered for installation.
5. Finish verified Windows conversion/caching/rebuild and then the UWUVCI-style
   GUI Inject workflow. The final product remains a per-game installable WUP
   generator; current diagnostic bundles are RPX/WUHB, not installable WUPs.

Native Windows execution is also pending. The earlier unapproved manual workflow
remains local; no workflow was published or dispatched. Source/build artifacts and
versioned reports are maintained under `artifacts/`, as recorded in `STATUS.md`.
