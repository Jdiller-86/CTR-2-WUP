# First supported-title candidate: Super Mario 3D Land

**Current M5 work: In progress, full acceptance open.** The executable,
preparation/inventory reports and seven selected asset payloads are available
for private analysis. Input absence no longer blocks this work. Static resource
findings guide shared implementation; observed boot/gameplay and useful Wii U
performance remain unverified. Project #2 owns the live status.

**October 13 verified executable intake:** the smaller private export contains
an independently inspected v2 NCCH executable. Stored hashes, decompression and
exact executable-revision identity match its receipt. Product `CTR-P-AREE`,
program ID `0004000000054000`, English title and SMDH region mask 2 establish the
USA candidate from bytes, rather than its filename. The remaster field is 0;
the filename's `v0.3.0` is not a verified CIA title version.

The decompressed executable is 3,096,576 bytes. The pinned lifter's static
analysis succeeds; the extended-header allowlist declares 27 services, including
`dsp::DSP` and `ldr:ro`. These are declared capabilities, not traced runtime IPC
or supported gameplay. Exact hashes and all commercial analysis stay private.

**October 13 asset reports received:** the preparation report's output hash and
revision match both the executable intake and the asset manifest. The two local
reports contain identical full IVFC verification evidence. Inventory paths,
file hashes, offsets/sizes, totals and source/revision bindings were cross-checked
here. The original CCI and RomFS payload bytes remain on the user's PC; their
full hashes and integrity are reported local evidence, not recomputed here.

The manifest lists 1,756 files in 19 directories and a 298,080,796-byte Level-3
blob: 1,666 `.szs` entries, 88 `.bcstm` entries, one `.bcsar` entry and one
`.bsm` entry. Two shader-named `.szs` files total about 20 KiB. No outer `.cro`/`.crs`
entry is listed. Selected nested structures are reviewed below; the remaining
inventory is uninspected here. This does not prove
absence of dynamic modules or identify archive formats from extensions alone.

## October 13 selected asset bytes verified

The received ZIP contains exactly seven requested files, totaling 15,304,637
bytes, plus a bounded receipt. Every file size/hash matches the earlier manifest;
receipt hashes match the unchanged preparation/asset reports, executable revision
and prepared-main identity. Selected bytes are independently checked here. The
full original CCI/RomFS hash and IVFC verification remain reported local evidence.

| Selected content | Checked structure | Shared work it informs |
| --- | --- | --- |
| Three compressed archives | Yaz0 decompression yields bounded NARC allocation tables; four DVLB members, one CGFX member and one BYML member | Reusable bounded decompression/container readers; never turn guest names into arbitrary host paths |
| Four layout shader members and the primitive shader binary | Five DVLB code/descriptor/DVLE table sets fit their files; five vertex entries and one geometry entry | Arithmetic, comparisons/addressing, bounded control flow and possible geometry support; actual entry selection is untraced |
| CGFX shader resource | Its DATA block fits the file; three embedded DVLB regions have valid bounded code/tables and twelve vertex entries | CGFX dictionary/resource binding remains to be validated; code sizes 472, 428 and 264 exceed the current 256-word runtime limit |
| System class table | Little-endian BYML version 1 header and table/root offsets fit the member | Validate typed nodes and bounds when consumed; class names do not establish service use |
| Sound archive | CSAR version 2 STRG/INFO/FILE blocks fit the supplied file | Resolve archive references/banks/sequences/waves; nested signature candidates are not a validated complete inventory |
| Two short streams | CSTM version 2 INFO/SEEK/DATA blocks; both declare non-looping, stereo DSP ADPCM at 32 kHz | Original codec/block/history/seek probes and guest DSP/audio integration; no decoded-audio or playback acceptance yet |

The raw shader code includes ADD, DP3/DP4, MUL, floor/max/reciprocal, address moves,
comparisons, conditional/loop/jump control, multiply-add and geometry emit families.
Opcode-family facts were checked against the pinned nihstro encoding reference
`f4d8659decbfe5d234f04134b5002b82dc515a44`; no reference implementation is bundled.
This is a static code inventory, including potentially unreachable words, rather
than proof that every instruction executes on the proposed gameplay route.

## Current feature and test-route matrix

| Area | Present evidence | Next acceptance |
| --- | --- | --- |
| Exact revision/input | Independently verified executable and selected bytes; complete local inventory reports | Retain private provenance; broader modules/assets and original full-container integrity remain bounded by available bytes |
| Shader runtime | Actual resources exceed MOV-only semantics and include code beyond 256 words | Original numeric/control/capacity probes, safe upload/rollback and selected-entry traces before claiming support |
| Fragment/rendering | Original canonical software draws pass their scoped routes | Capture actual required TEV/texture/depth/blend/coherency state; implement shared features and native GX2 translation |
| Guest audio | Two sampled streams declare DSP ADPCM; archive structure exists | Verify codec/history/seek behavior with originals, required DSP protocol/mixing and later physical game audio |
| Services/modules/saves | Declared DSP/loader capabilities and static executable analysis | Trace actual IPC/module/save demands; do not implement all declarations as silent successful stubs |
| Gameplay/conversion | Proposed private route and Windows product contract | Boot/menu/course/input/audio/save/reload, useful Wii U costs and complete WUP/GUI acceptance |

No additional copy of these seven samples is needed. Continue the reusable shader,
container, audio and execution-budget work, then refine the matrix from observed
execution. Additional private files are requested only for a specific missing
structure. No commercial profile is enabled.

The helper accepts the supplied schema-1/0.20.0 reports and needs only Python
3.12+. It checks the complete blob and selected file hashes, while preserving
sources and existing outputs. The selected upload is now reviewed above;
implementing the transfer route and static inspection do not complete M5 #32/#33.

## Historical intake notes

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
work. The synthetic intake/export fixtures and the received private executable are
verified. Full assets/modules and observed runtime requirements remain pending.

Proposed manual route, to agree after identifying the dump: cold boot, title
menu, start a new save, enter/play the first course, test movement/jump/touch as
used, finish/save, exit and relaunch, then rebuild the same title ID and confirm
save continuity. Record firmware, homebrew environment, installer, screen/audio
behavior, frame/instruction timings and fault/fallback logs. This route is not
claimed to have been executed.

Earlier, while executable intake was pending, v0.7.0 added shared heap/linear CPU
allocation, free/protection/query and an original three-backend ARM probe. These
are reusable startup interfaces, not an inferred Mario patch/profile. Its actual
memory needs, thread/shared-memory behavior and GPU/DSP mappings still require
private runtime evidence from the now-verified revision. See `guest-memory.md`.
