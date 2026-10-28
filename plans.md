# Development plan

Build a Windows injector that converts a supported, owned 3DS revision into a
playable Wii U Menu title. The shared runtime and conversion engine come first.
Commercial games and the GUI are not supported yet.

## Current work

1. **Connect guest audio** ([#25](https://github.com/Jdiller-86/CTR-2-WUP/issues/25)).
   The checked CSTM/ADPCM stream mixer exists. Implement the guest DSP/service
   connection and timing; native tone playback does not cover guest audio.
2. **Extend native resource reuse** ([#30](https://github.com/Jdiller-86/CTR-2-WUP/issues/30)).
   Bounded vertex translation, independent Texture0/1/2 sampling and coordinates,
   six-stage RGB/alpha TEV with a combiner buffer, alpha/logic/blend operations,
   write masks, bounded D16/D24S8 depth/stencil and CPU program caching are
   implemented. Bounded source versions, immutable resource snapshots and decoded
   CPU texture reuse cover writes and aliases, including completed targets used
   by later lists. Retired native texture uploads use exact current image keys.
   Add native target pooling and depth interpolation toward
   observed first-game requirements. Retain
   current-state validation, exact software preflight, GPU retirement and full
   readback. Physical parity and costs remain unverified.
3. **Resume installed-title lifecycle after console evidence**
   ([#15](https://github.com/Jdiller-86/CTR-2-WUP/issues/15)). Installation, native
   boot and SD preparation are proven; Minus/HOME return still hangs. Retrieve
   retained system logs before another repair. The SDK correction did not fix
   exit, and the saved SD trace ends before the handoff. Console work is deferred;
   no reinstall, repeated failure run or baseline repeat is requested.

Remaining console evidence does not block CPU, graphics or audio development.

## Milestones

Detailed scope, dependencies and evidence are tracked in the linked issues and
[Project](https://github.com/users/Jdiller-86/projects/2). The
[dynamic graph](https://jdiller-86.github.io/CTR-2-WUP/progress/) summarizes live status.

| Milestone | Existing foundation | Remaining outcome |
| --- | --- | --- |
| [M1: Platform](https://github.com/Jdiller-86/CTR-2-WUP/issues/1) | Reviewed screens, input, tone, stability, scoped exit/save, original installation and installed boot | Installed-title exit/relaunch and automatic SD recovery |
| [M2: CPU](https://github.com/Jdiller-86/CTR-2-WUP/issues/2) | ARM/Thumb/VFP AOT, checked ABI/memory and original fixtures | Exercised instruction coverage, fallback/traces and useful Espresso performance |
| [M3: Runtime](https://github.com/Jdiller-86/CTR-2-WUP/issues/3) | Compiled original guests, kernel/services, filesystem and mixer | Required services/modules, guest DSP and runtime acceptance |
| [M4: Graphics](https://github.com/Jdiller-86/CTR-2-WUP/issues/4) | GSP/PICA, software reference, cached GX2 programs, versioned source snapshots, decoded CPU and retired native texture reuse, independent Texture0/1/2 materials, six-stage TEV, alpha/logic/blend/write controls and D16/D24S8 depth/stencil | Physical GPU parity, broader filtering/fragment state, native target pooling and measured costs |
| [M5: First game](https://github.com/Jdiller-86/CTR-2-WUP/issues/5) | Verified private Mario 3D Land revision and selected asset analysis | Implement observed gaps and prove boot, gameplay, audio and saves |
| [M6: Conversion](https://github.com/Jdiller-86/CTR-2-WUP/issues/6) | Intake/decryption, AOT builds, original WUP packing and integrity checks | Complete supported-game pipeline, Windows execution and installed rebuild/save continuity |
| [M7: GUI](https://github.com/Jdiller-86/CTR-2-WUP/issues/7) | Windows toolchain and workflow design | Responsive Select → Configure → Inject → WUP over the proven engine |
