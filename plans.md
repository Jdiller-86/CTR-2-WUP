# Development plan

Build a Windows injector that converts a supported, owned 3DS revision into a
playable Wii U Menu title. The shared runtime and conversion engine come first.
Commercial games and the GUI are not supported yet.

## Current work

1. **Extend GX2 guest rendering** ([#29](https://github.com/Jdiller-86/CTR-2-WUP/issues/29)).
   The bounded vertex translator and transactional GPU backend are implemented;
   physical shader/raster parity remains unverified. Extend texture and fragment
   state from original scene fixtures, then implement requirements observed in
   the first game. Keep software preflight and readback checks during expansion.
2. **Connect guest audio** ([#25](https://github.com/Jdiller-86/CTR-2-WUP/issues/25)).
   The checked CSTM/ADPCM stream mixer exists. Implement the guest DSP/service
   connection and timing; native tone playback does not cover guest audio.
3. **Finish original-title installation** ([#37](https://github.com/Jdiller-86/CTR-2-WUP/issues/37)).
   The keyless, self-signed plaintext WUP candidate and automatic SD recovery
   probe exist. Actual GX2 installation and two Menu launches are still unverified.
   This hardware-only gap does not block CPU, graphics or audio development.

## Milestones

Detailed scope, dependencies and evidence are tracked in the linked issues and
[Project](https://github.com/users/Jdiller-86/projects/2). The
[dynamic graph](https://jdiller-86.github.io/CTR-2-WUP/progress/) summarizes live status.

| Milestone | Existing foundation | Remaining outcome |
| --- | --- | --- |
| [M1: Platform](https://github.com/Jdiller-86/CTR-2-WUP/issues/1) | Reviewed screens, input, tone, stability, exit and save/reload | Installed-title launch and automatic SD recovery evidence |
| [M2: CPU](https://github.com/Jdiller-86/CTR-2-WUP/issues/2) | ARM/Thumb/VFP AOT, checked ABI/memory and original fixtures | Exercised instruction coverage, fallback/traces and useful Espresso performance |
| [M3: Runtime](https://github.com/Jdiller-86/CTR-2-WUP/issues/3) | Compiled original guests, kernel/services, filesystem and mixer | Required services/modules, guest DSP and runtime acceptance |
| [M4: Graphics](https://github.com/Jdiller-86/CTR-2-WUP/issues/4) | GSP/PICA, software reference, bounded GX2 vertex translation and guarded GPU backend | Physical GPU parity, broader fragment/texture coverage, coherency and measured costs |
| [M5: First game](https://github.com/Jdiller-86/CTR-2-WUP/issues/5) | Verified private Mario 3D Land revision and selected asset analysis | Implement observed gaps and prove boot, gameplay, audio and saves |
| [M6: Conversion](https://github.com/Jdiller-86/CTR-2-WUP/issues/6) | Intake/decryption, AOT builds, original WUP packing and integrity checks | Complete supported-game pipeline, Windows execution and installed rebuild/save continuity |
| [M7: GUI](https://github.com/Jdiller-86/CTR-2-WUP/issues/7) | Windows toolchain and workflow design | Responsive Select → Configure → Inject → WUP over the proven engine |
