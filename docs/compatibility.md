# Compatibility

| Input/workflow | Current state | Evidence and limit |
|---|---|---|
| Original native diagnostic | Partial console evidence; Minus exit confirmed | v0.14.1 native report records all ten guided checks/nine embedded self-tests and cleanup; v0.14.3 Minus exit now user-confirmed; full installation/compiled stability remains open |
| Synthetic ARM/Thumb fixture | Desktop verified bounded subset | Interpreter/generated-C state matches; linked into diagnostic RPX, console run pending |
| Original framebuffer NCCH fixture | Desktop execution verified; awaiting Wii U | Checked loader, sleep/event SVC runtime, ARM-written frames/input/exit, AOT/interpreter matches; console RPX/WUHB built; explicit harness I/O, not HID/GSP/PICA |
| Original TLS/SRV/CFG ARM startup probe | Desktop verified; awaiting Wii U | Standard TLS and restricted service commands; interpreter/original/external AOT match; original self-test embedded in diagnostics |
| Original ARM heap lifecycle probe | Desktop verified; awaiting Wii U | Heap/linear CPU allocation, free/protection/query; three-backend match; embedded self-test; GPU/DSP physical mapping absent |
| Original ARM shared/sync probe | Desktop verified; awaiting Wii U | Local shared aliases, typed lifetimes, mutex/semaphore/event waits; three-backend match; single-thread scope |
| Original standard-HID ARM consumer | Desktop verified; awaiting Wii U | HID copied handles, read-only ring, pad/touch events; three-backend match; live VPAD-to-ARM diagnostic; motion/New-family input absent |
| Original standard-HID/GSP graphics fixture | Desktop verified; awaiting Wii U | ARM writes rotated RGB565 and submits both screens through direct GSP IPC; three-backend full-state/pixel match; embedded self-test; PICA absent |
| Original shared-GSP/VBlank graphics fixture | Desktop verified; awaiting Wii U | Relay registration/shared framebuffer queues, exclusive retry, IRQ ring and cleanup match three backends; original AOT runs in standard diagnostic mode; GX/PICA absent |
| Original ARM/Thumb nested stack-call fixture | Desktop verified; awaiting Wii U | 47 instructions/one SVC, restored registers/stack, three-backend state/memory match, independent ARM1176 trace, partial-fault parity; embedded diagnostic |
| Original three-thread fixture | Desktop verified; awaiting Wii U | Three-backend CPU/TLS/ownership/wait/quantum parity; embedded 345-instruction/eight-switch self-test; single emulated CPU, no priority inheritance |
| Original compiled libctru applications | Seven original applications verified; interactive/guarded v0.14.5 completion user-reported | Supplied text logs are older; new independent TXT results await hardware evidence. Normal CRT/newlib, threads/TLS/HID/GSP/FS/APT and Level-3 RomFS reads pass; retail saves/GX/PICA/audio absent |
| Original compiled savedata program | Desktop save/reload verified; console first-write evidence, later failure reported | Uploaded snapshot has two valid visit-1 records; its private desktop copy reloads to visits 2 and 3. v0.14.6 backup/publication/rollback fixes a tested rename assumption; console update/reload acceptance pending |
| Original compiled RomFS program | Desktop/sanitized/emulated PPC verified; console pending | Original Level-3 image, root/nested/Unicode reads, seek/EOF, directory listing and write denial; canonical NCCH/IVFC preparation and file-provider checks added in v0.16; no signature authentication or commercial asset integration |
| Decrypted base-application CIA | Intake supported | Checked index-0 NCCH, metadata/bitmap/content hashes and loader offsets; small executable export supported |
| Decrypted NCCH v2 `.cxi` | Inspection supported | Bounds, sections/ExeFS hashes, code decompression and metadata tested with original fixtures |
| Decrypted NCSD `.3ds` | Inspection supported | Cartridge partition bounds and all present NCCH partitions checked; no NAND loader |
| Supported encrypted CIA/NCSD/NCCH | Local preparation implemented; original fixtures verified | prepare-rom validates recovered main CXI using local key/seed files; standard/1/0a/0b NCCH, scoped CIA wrapping; no actual user-key/Windows acceptance or commercial conversion |
| 3DS key/seed exports | Scoped local import implemented | Plaintext AES key DB retail NCCH KeyX, seeddb and declared decrypted title-key DB; no console extraction, boot9 import or encrypted key DB support |
| Main RomFS assets | Canonical full IVFC/metadata preparation implemented | Private blob/manifest; bounded names/chains/extents and streamed hashes; file-backed original guest proof; actual game assets/console SD remain unverified |
| PICA base textures | All 14 format decoders implemented | Original C/Python/reference vectors; no texture sampling, GX2 scene rendering or commercial support |
| Resolved vertex arrays and indices | Bounded portable decoding implemented | Explicit streams/input permutation, unsigned 8-bit/16-bit indexed gather, transactional range/float/alias checks; no PICA loaders, fixed attributes, shaders or draws |
| Original owned software scenes | Exact desktop scenes verified; focused Wii U cross-build available | PICA-format loader/index/fixed/uniform/MOV shader and bounded opaque triangles; atomic rollback; bounded standard guest GSP integration; no sampling/general TEV/depth or GPU shader translation; hardware evidence pending |
| Original compiled GX transfer program | Sanitized desktop/reference checked; console pending | 16 standard operations, exact frames; scoped DMA/fill/transfer/copy/cache; PICA draw command faults |
| `.3dsx`, update/DLC merges | Unsupported | No corresponding loader/pipeline; renamed input is not accepted as NCCH |
| Any commercial revision | Unsupported/unverified | USA Mario executable/revision now privately verified; local outer asset inventory is matched, but selected archive bytes/nested modules remain uninspected; required HLE/GPU/audio/save implementation and Wii U gameplay evidence remain absent |
| WUP installation | Unverified/unavailable | Original encrypted writer and independent verification pass synthetic-input checks; installer acceptance and actual installable output remain pending |

Stable inspection identity uses the 3DS program ID for save identity. Executable
revision includes code and extended-header hashes, separate from stable identity.
This is preparation for future caches/profiles, not a currently usable save system.

Only `ctr2wup-framebuffer-fixture-v1`, containing original code and an exact layout,
is enabled by the guest CLI. The separate original startup probe is executed by
developer tests and embedded diagnostics, alongside the heap/shared/HID/GSP probes.
No commercial title profile is enabled. Profiles must match an exact
executable/module revision and recorded console evidence. Upstream authors'
desktop compatibility reports never set CTR2WUP's console-verified status.

The October 11 native report provides scoped physical evidence for its embedded
CPU/services/heap/objects/HID/GSP/relay/stack/thread self-tests. It does not
establish full live guest graphics, save update/reload, full lifecycle acceptance or commercial
compatibility. Full console acceptance remains open; see `console-retest.md`.

Deferred: New 3DS exclusives, stereo output, online services, cameras, microphone,
DLC/update merging, save import/export, arbitrary-ROM compatibility and Windows
GUI. Native output copying is not a working PICA renderer.

ARMv6 interpreter SXTB/SXTH/UXTB/UXTH rotations/conditions/aliases pass 2,048
independent reference cases. This is a bounded CPU extension; full compiled
interpreter startup still rejects STRD. The delivered guarded app uses AOT.


October 12 reported console environment is U.S. firmware/Aroma/WUP Installer GX2,
each described by the user as latest. Exact versions are not yet captured. Earlier
v0.14.5 Native/Guarded/Compiled successes are retained as reported evidence while
the user is away. They do not establish v0.15 decryption, v0.14.6 SD/RomFS, retail
compatibility or actual WUP installation. No completed milestone is added.
