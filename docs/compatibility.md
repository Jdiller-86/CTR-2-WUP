# Compatibility

The 0.24.1 hardware snapshot in PR #106 packages exact arithmetic scenes and a
correctly gated, measured original guest draw route. Its cooperative execution
policy now has scoped physical evidence: two exact arithmetic launches and the
full Guest Draws/Plus-repeat guide, with all five traces reaching `main_return`
and reported Minus/HOME return. Scenes take 91.190–91.613 s and native
scale/publication is the dominant measured phase. All requested files are
received. No general game compatibility is implied. See
[the evidence review](console-evidence.md) and [focused test round](console-retest.md).

Merged PR #106 / version 0.25.0 adds a [native display cache](gsp-display.md#native-display-cache)
with independent exact host checks and workload reductions. Its focused physical
picture/restart/timing comparison is pending; full motion has no publication-byte
saving and both GX2 display copies remain. Earlier builds keep their scoped passes.

| Input/workflow | Current state | Evidence and limit |
|---|---|---|
| Original native diagnostic | Reviewed controls/audio/stability baseline retained | Native 0.14.1 ten-step/ten-minute guide and nine self-tests, later focused silence/tone and 0.24.1 Minus/HOME returns remain accepted in scope; automatic SD recovery and installed-title evidence remain open |
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
| Original compiled libctru applications | Nine original applications verified; scoped console results reviewed | Normal CRT/newlib, threads/TLS/HID/GSP/FS/APT, saves/assets, GX and bounded draws; complete lifecycle/stability, general PICA, retail saves and guest audio remain open |
| Original compiled savedata program | Desktop and scoped fresh-launch console reload verified | Reviewed 0.14.6 TXT/trace confirms original save write and fresh-process reload; earlier 0.14.5 failures remain recorded; retail saves and broader recovery remain open |
| Original compiled RomFS program | Desktop/sanitized/emulated PPC and scoped console reads verified | Reviewed embedded and SD-provider original nested/Unicode read, seek/list and write-denial results; no signature authentication or commercial asset integration |
| Decrypted base-application CIA | Intake supported | Checked index-0 NCCH, metadata/bitmap/content hashes and loader offsets; small executable export supported |
| Decrypted NCCH v2 `.cxi` | Inspection supported | Bounds, sections/ExeFS hashes, code decompression and metadata tested with original fixtures |
| Decrypted NCSD `.3ds` | Inspection supported | Cartridge partition bounds and all present NCCH partitions checked; no NAND loader |
| Supported encrypted CIA/NCSD/NCCH | Local preparation implemented; original fixtures verified | prepare-rom validates recovered main CXI using local key/seed files; standard/1/0a/0b NCCH, scoped CIA wrapping; no actual user-key/Windows acceptance or commercial conversion |
| 3DS key/seed exports | Scoped local import implemented | Plaintext AES key DB retail NCCH KeyX, seeddb and declared decrypted title-key DB; no console extraction, boot9 import or encrypted key DB support |
| Main RomFS assets | Canonical full IVFC/metadata preparation and bounded sample export implemented | Private blob/manifest and explicit-file ZIP; matching reported provenance, full-blob/selected-file hashes and safe publication; original SD-provider results reviewed; seven selected payloads and container/shader/audio structures are reviewed; broader nested resources and observed runtime use remain open |
| PICA base textures | All 14 format decoders implemented | Original C/Python/reference vectors; no texture sampling, GX2 scene rendering or commercial support |
| Resolved vertex arrays and indices | Bounded portable decoding implemented | Explicit streams/input permutation, unsigned 8-bit/16-bit indexed gather, transactional range/float/alias checks; no PICA loaders, fixed attributes, shaders or draws |
| Original owned software scenes | Exact desktop scenes verified; focused Wii U cross-build available | PICA-format loader/index/fixed/uniform/MOV/ADD/MUL shader and bounded opaque triangles; 512-word capacity and exact integer arithmetic have original numeric/pixel/rollback probes; merged PR #105 and scoped 0.24.1 arithmetic console passes; no rounding/sampling/general TEV/depth or GPU shader translation |
| Original compiled GX transfer program | Sanitized desktop/reference and scoped console results reviewed | 0.20.0 guest PASS/exit and exact transfer pictures; 17 GX operations, scoped DMA/fill/transfer/copy/cache and no-draw configuration; full renderer acceptance remains open |
| Original compiled guest PICA draws | Exact desktop frames and scoped 0.23.0/0.24.1 console results reviewed | 0.24.1 confirms correct picture timing and all three guide steps including a new Plus session's own draw/PASS/exit, with four triangles, 97,200 fragments and zero fallback/fault. Scenes take 91.190–91.613 s; 132.218–132.281 ms mean native scale/publication is the next measured target. General shaders/TEV/GX2 remain open |
| DSP ADPCM codec reference | Merged PR #109; desktop/rational/sanitizer/Windows/PPC checks pass | Explicit coefficients/history, signed PCM16, partial frames, continuation/seek and atomic failure. Scoped CSTM v2 block/SEEK reader exists; CSAR, guest DSP IPC/mixing and physical audio acceptance remain open |
| CSTM block/SEEK reader | Scoped #113 / PR #114; original desktop/reference checks | LE v2 non-looping mono/stereo DSP ADPCM; checked payload/padded channel addressing, coefficients and block histories. General SEEK intervals/loops, track mixing, guest DSP and console playback remain open |
| Original audio streams | Scoped #115 / PR #116 host/reference/native checks | Two CSTM voices, 48 kHz stereo integer conversion/mix, gains, pause/seek/EOF; original finite AX diagnostic and guided listening. No guest DSP firmware/IPC, continuous callback streaming or physical audio acceptance |
| Original installed-title check | Scoped #119 under #37/M6; physical acceptance pending | Keyless plaintext/self-signed WUP candidate and standalone native-install app; two brief Menu launches include the automatic SD probe. Prior controls/stability/results/lifecycle evidence is reused, without another full guide |
| `.3dsx`, update/DLC merges | Unsupported | No corresponding loader/pipeline; renamed input is not accepted as NCCH |
| Any commercial revision | Unsupported; active M5 analysis | USA Mario executable and seven selected payloads have verified provenance; selected container/shader/audio structures are identified, with 512-word code capacity implemented and shader semantics/DSP ADPCM gaps recorded; broader modules, actual HLE/GPU/audio/save use and Wii U gameplay remain unverified |
| WUP installation | Original candidate; physical acceptance unverified | Plaintext 0x2000/self-generated test signatures need no common key, production certificate or donor. Aroma 01_sigpatches.rpx is required and not assumed installed. Explicit identity/collision and independent integrity checks do not establish Nintendo authenticity or GX2/Menu success |

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
establish full lifecycle acceptance or commercial compatibility. Later original
save/assets/GX/draw and launch-audio results are reviewed in
[console evidence](console-evidence.md); [remaining gates](current-roadblock.md)
records the specific pending acceptance instead of requesting blanket repeats.

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
