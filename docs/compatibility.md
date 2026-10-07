# Compatibility

| Input/workflow | Current state | Evidence and limit |
|---|---|---|
| Original native diagnostic | Experimental; awaiting console | Real devkitPPC/WUT RPX and WUHB compile; no physical execution evidence |
| Synthetic ARM/Thumb fixture | Desktop verified bounded subset | Interpreter/generated-C state matches; linked into diagnostic RPX, console run pending |
| Original framebuffer NCCH fixture | Desktop execution verified; awaiting Wii U | Checked loader, sleep/event SVC runtime, ARM-written frames/input/exit, AOT/interpreter matches; console RPX/WUHB built; explicit harness I/O, not HID/GSP/PICA |
| Decrypted NCCH v2 `.cxi` | Inspection supported | Bounds, sections/ExeFS hashes, code decompression and metadata tested with original fixtures |
| Decrypted NCSD `.3ds` | Inspection supported | Cartridge partition bounds and all present NCCH partitions checked; no NAND loader |
| Encrypted dump | Unsupported | Clear rejection; no decryption/key acquisition implemented |
| `.3dsx`, CIA, update/DLC merges | Unsupported | No corresponding loader/pipeline; renamed input is not accepted as NCCH |
| Any commercial revision | Unsupported/unverified | No supplied dump, complete guest HLE/GPU/audio/save implementation or Wii U gameplay evidence |
| WUP installation | Unverified/unavailable | Structure validator implemented; no licensed packer integrated or installable output |

Stable inspection identity uses the 3DS program ID for save identity. Executable
revision includes code and extended-header hashes, separate from stable identity.
This is preparation for future caches/profiles, not a currently usable save system.

Only `ctr2wup-framebuffer-fixture-v1`, containing original code and an exact layout,
is execution-enabled. No commercial title profile is enabled. Profiles must match an exact
executable/module revision and recorded console evidence. Upstream authors'
desktop compatibility reports never set CTR2WUP's console-verified status.

Deferred: New 3DS exclusives, stereo output, online services, cameras, microphone,
DLC/update merging, save import/export, arbitrary-ROM compatibility and Windows
GUI. Native output copying is not a working PICA renderer.
