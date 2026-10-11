# First supported-title candidate: Super Mario 3D Land

October 11 update: the user is preparing console tests and has supplied the
small CIA helper receipt. It reports outer-encrypted executable content and
contains no executable view. Decrypted executable intake is still needed; no
commercial code or assets have been analyzed. See `guided-testing.md` for the
new controller-driven TXT test workflow.

October 8 (America/New_York): the user is away from the CIA's computer. Development
continues on shared ROM-independent TLS/SRV/CFG startup support and original
fixtures (`shared-services.md`). These are general startup interfaces, not
requirements observed in the inaccessible Mario executable or a Mario patch.

Selected by the user on 2026-10-08 UTC. A CIA attachment was subsequently supplied
on October 7 (America/New_York), but exceeds the workspace's 32 MiB transfer
limit. Its filename suggests USA title `0004000000054000`, `CTR-P-AREE`,
`v0.3.0`; these are unverified filename claims. Its bytes/encryption/hash/revision
have not been inspected. `cia-intake.md` supplies a small local executable export
helper for this transfer constraint. No commercial profile is enabled, and there is
no reproduced CTR2WUP boot/gameplay/save or Wii U performance result.
The MVP is still the Windows GUI's Inject → per-game WUP → Wii U Menu workflow.

## Evidence and requirements to investigate

The pinned Zakuro source provides concrete leads rather than a compatibility
guarantee. At commit `76f17bf76cd9ea238a94f37d389fe701ab05a673`:

- `zakuro-core/src/services/mod.rs` and `services/mii_selector.rs` include tests
  explaining that the game's saved Mii must agree with its CFG console identity.
  APT/Mii selection, CFG identity and save/reload must therefore be consistent;
  an empty generic applet response is unsuitable.
- `zakuro-gpu/src/raster/hardware.rs` notes small color-buffer CPU readbacks
  while loading a course. GPU completion and guest-visible memory coherence
  need real handling, beyond presenting a final framebuffer.

These are source observations that have not been reproduced using the user's
revision. The desktop Vulkan implementation is not a GX2 implementation. No
upstream executable/assets/caches or implementation from these locations was
copied into CTR2WUP.

| Area | Intake/implementation evidence needed |
|---|---|
| Revision | NCCH/NCSD identity, region/version, input/executable hashes and module inventory |
| CPU | ARM/Thumb/VFP coverage, CP15/TLS, exceptions, exact generated-code/RSS/memory costs; measured fallback |
| Kernel/IPC | Required SVCs, threads/synchronization, `srv:` routing and traced command shapes |
| Declared services | Read the private dump's extended-header allowlist; confirm actual use by tracing |
| Input/display | HID shared memory/events, GSP/GX2 command/state translation, TV/GamePad mapping |
| GPU | Vertex shaders, textures/TEV/depth/stencil and color-buffer readback/coherency needed by observed scenes |
| Filesystem/modules | RomFS assets, save archive operations and CRO/CRS use if present |
| Audio | Required DSP protocol/voices and console output; compare against game behavior |
| APT/CFG/Mii | Repeatable console/Mii identity, applet completion and save continuity |
| Packaging | Original encrypted WUP, install/Menu launch, stable identity across rebuild, uninstall |

## Private intake

Keep a fully decrypted `.cia`, `.3ds` or `.cxi` under ignored `private/`; no keys are
needed for already decrypted input. Avoid update/DLC merging for the first exact
revision. The following produces metadata and declared capabilities, not a
supported or playable conversion:

```sh
python3 -m host analyze-title private/game.cxi --lifter --output private/first-title-intake
```

The report preserves exact hashes, segment/stack/BSS layout, declared service
access and section inventory. The external lifter analysis remains private.
Do not infer implemented/observed IPC from the service allowlist. Later extraction
and module/shader analysis will use this revision to prioritize concrete runtime
work. The synthetic intake commands and CIA/executable-export fixtures are
verified; actual commercial intake remains pending the smaller private export.

Proposed manual route, to agree after identifying the dump: cold boot, title
menu, start a new save, enter/play the first course, test movement/jump/touch as
used, finish/save, exit and relaunch, then rebuild the same title ID and confirm
save continuity. Record firmware, homebrew environment, installer, screen/audio
behavior, frame/instruction timings and fault/fallback logs. This route is not
claimed to have been executed.

While private executable intake is pending, v0.7.0 adds shared heap/linear CPU
allocation, free/protection/query and an original three-backend ARM probe. These
are reusable startup interfaces, not an inferred Mario patch/profile. Its actual
memory needs, thread/shared-memory behavior and GPU/DSP mappings still require
the supplied revision and runtime evidence. See `guest-memory.md`.
