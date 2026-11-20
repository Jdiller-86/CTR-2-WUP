# Development plan

Build a Windows injector that converts a supported, owned 3DS revision into a
playable Wii U Menu title. The shared runtime and conversion engine come first.
Verified playable commercial games and the GUI are not supported yet.

## Current work

The [experimental Mario USA test builder](docs/first-title.md#build-a-private-test-wup)
connects ROM intake, assets, AOT/native compilation and verified WUP publication
([#189](https://github.com/Jdiller-86/CTR-2-WUP/issues/189),
[#38](https://github.com/Jdiller-86/CTR-2-WUP/issues/38)), with optional pinned
Azahar 2126.2 Mii RomFS (`--mii-romfs`), WUT CRT startup layout and uncompressed
FILEINFO COS code reservation sizing for large AOT executables, and a
`host build --cemu` local Cemu 2.6 test profile. Next, verify behavior from the
owned ROM locally and on console, then resolve remaining observed execution gaps.
Original-input build and Cemu checks do not establish physical Wii U Mario boot
or gameplay.

The initial ARMv6K/Thumb interpreter and original AOT path cover integer/media/DSP
arithmetic, normal signed/unsigned memory addressing, typed exclusives, long calls,
compact aligned jump-table dispatch, 16 process-local threads (two TLS pages), and
scalar VFPv2 arithmetic, direct truncating conversions and register transport
([#17](https://github.com/Jdiller-86/CTR-2-WUP/issues/17)). Added HLE services cover
`SRV::EnableNotification`, `NDM`, expanded `CFG` blocks, `mic:u`, `hid:USER`
accelerometer/gyro, headless APT Mii Selector, and savedata/Mii RomFS access.
Remaining CPU work is broader reference/FP boundary coverage, external-backend
FP control parity, observed title gaps and Espresso performance; the complete
CPU gate is not accepted by this baseline.

Initial [CRO/CRS loading](docs/modules.md) supports packaged fixed-address modules,
checked symbols/relocations and interpreter/AOT load/call/unload. Broader module
forms and actual title requirements remain open in
[#24](https://github.com/Jdiller-86/CTR-2-WUP/issues/24).

1. **Connect guest audio** ([#25](https://github.com/Jdiller-86/CTR-2-WUP/issues/25)).
   The checked CSTM/ADPCM mixer and bounded original NDSP PCM service exist,
   including queued voices, bounded linear/direct PCM rates, mono/looping ADPCM,
   interrupts and exact frame deadlines. Verified embedded DSP1 HLE binding adds
   signed mono PCM8, 1/512–4× rate ratios, fractional-rate ADPCM, sequential Q15/Q14
   filters and dual auxiliary buses. Incremental DSP-to-48k conversion,
   an eight-block AX queue and live original ARM playback support bounded continuous
   production, pause/resume, underrun silence and draining toward observed game
   requirements. Finite original playback, automatic pause/resume and resource
   release have console evidence; continuous guest/AX synchronization and game
   audio still need physical Wii U acceptance.
2. **Extend native resource reuse** ([#30](https://github.com/Jdiller-86/CTR-2-WUP/issues/30)).
   Bounded vertex translation, independent Texture0/1/2 sampling and coordinates,
   six-stage RGB/alpha TEV with a combiner buffer, alpha/logic/blend operations,
   write masks, bounded D16/D24S8 depth/stencil and CPU program caching are
   implemented, alongside game-path extensions for dedicated read-only VRAM
   backing, command-buffer jumps, immediate strips, expanded vertex shader ISA
   (control flow, subroutines, `MOVA`/`DP3`/`DPH`/`RSQ`/float24 rounding),
   homogeneous clipping/perspective, directional lighting, fog, 16-bit color
   targets, independent D24S8 gates, mip chains/mixed filters, and a Cemu 2.6
   paired readback/48-slot texture-cache profile. Bounded source versions,
   immutable resource snapshots and decoded CPU texture reuse cover writes and
   aliases, including completed targets used by later lists. Retired native
   texture uploads use exact current image keys. Add native target pooling and
   depth interpolation toward observed first-game requirements while retaining
   current-state validation, exact software preflight, GPU retirement and full
   readback. Physical Wii U GPU parity and hardware costs remain unverified.
3. **Isolate the installed-title exit conflict**
   ([#15](https://github.com/Jdiller-86/CTR-2-WUP/issues/15)). Installation, native
   boot and SD preparation are proven; Minus/HOME return still fails. Retained
   logs reach `main_return`, then panic in GiveMiiYouTube's socket finalizer through
   Aroma's exit hooks. Isolate that plugin before changing application shutdown.
   Plugin-free Menu return, relaunch and SD recovery remain unaccepted.

Remaining console evidence does not block CPU, graphics or audio development.

## Milestones

Detailed scope, dependencies and evidence are tracked in the linked issues and
[Project](https://github.com/users/Jdiller-86/projects/2). The
[dynamic graph](https://jdiller-86.github.io/CTR-2-WUP/progress/) summarizes live status.

| Milestone | Existing foundation | Remaining outcome |
| --- | --- | --- |
| [M1: Platform](https://github.com/Jdiller-86/CTR-2-WUP/issues/1) | Reviewed screens, input, tone, stability, scoped exit/save, original installation and installed boot | Installed-title exit/relaunch and automatic SD recovery |
| [M2: CPU](https://github.com/Jdiller-86/CTR-2-WUP/issues/2) | ARM/Thumb/VFP AOT, checked ABI/memory and original fixtures | Exercised instruction coverage, fallback/traces and useful Espresso performance |
| [M3: Runtime](https://github.com/Jdiller-86/CTR-2-WUP/issues/3) | Compiled original guests, kernel/16-thread/TLS runtime, `SRV`/`NDM`/`CFG`/`mic:u`/HID motion/headless Mii/filesystem, mixer and verified-component DSP1/PCM8/ADPCM/aux audio | Required title/module behavior, full guest audio and runtime acceptance |
| [M4: Graphics](https://github.com/Jdiller-86/CTR-2-WUP/issues/4) | GSP/PICA, software reference, cached GX2 programs, versioned snapshots, decoded CPU and retired native texture reuse, independent Texture0/1/2 materials, mip chains/mixed filters, six-stage TEV, alpha/logic/blend/write controls, 16-bit targets, D16/D24S8 gates, directional lighting/fog and Cemu 2.6 readback profile | Physical Wii U GPU parity, native target pooling and measured costs |
| [M5: First game](https://github.com/Jdiller-86/CTR-2-WUP/issues/5) | Verified private Mario 3D Land revision, ROM-to-WUP/Cemu test builder, optional Mii RomFS, WUT startup layout and initial HLE service/graphics bring-up | Resolve remaining observed gaps and prove boot, gameplay, audio and saves on Wii U |
| [M6: Conversion](https://github.com/Jdiller-86/CTR-2-WUP/issues/6) | Intake/decryption, AOT builds, original WUP packing and integrity checks | Complete supported-game pipeline, Windows execution and installed rebuild/save continuity |
| [M7: GUI](https://github.com/Jdiller-86/CTR-2-WUP/issues/7) | Windows toolchain and workflow design | Responsive Select → Configure → Inject → WUP over the proven engine |
