# Compatibility

| Input/workflow | Current state | Evidence and limit |
|---|---|---|
| Original native diagnostic | Experimental; awaiting console | Real devkitPPC/WUT RPX and WUHB compile; no physical execution evidence |
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
| Original compiled libctru applications | Scoped desktop execution verified; interactive console build available | Normal CRT/newlib/library startup, real threads/LightLock/C TLS, standard HID, graphics thread/shared GSP display; exact CPU-written pixels; APT/FS lookup failures persist, GX/PICA/audio/saves absent |
| Decrypted base-application CIA | Intake supported | Checked index-0 NCCH, metadata/bitmap/content hashes and loader offsets; small executable export supported |
| Decrypted NCCH v2 `.cxi` | Inspection supported | Bounds, sections/ExeFS hashes, code decompression and metadata tested with original fixtures |
| Decrypted NCSD `.3ds` | Inspection supported | Cartridge partition bounds and all present NCCH partitions checked; no NAND loader |
| Encrypted dump | Unsupported | Clear rejection; no decryption/key acquisition implemented |
| `.3dsx`, update/DLC merges | Unsupported | No corresponding loader/pipeline; renamed input is not accepted as NCCH |
| Any commercial revision | Unsupported/unverified | Mario CIA supplied but transfer is blocked; required HLE/GPU/audio/save implementation and Wii U gameplay evidence remain absent |
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

Deferred: New 3DS exclusives, stereo output, online services, cameras, microphone,
DLC/update merging, save import/export, arbitrary-ROM compatibility and Windows
GUI. Native output copying is not a working PICA renderer.
