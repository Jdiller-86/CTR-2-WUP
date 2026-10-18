# Development plan

Build a Windows injector that converts a supported, owned 3DS revision into a
playable Wii U Menu title. The shared runtime and conversion engine come first.
Commercial games and the GUI are not supported yet.

## Current work

1. **Extend GX2 guest rendering** ([#29](https://github.com/Jdiller-86/CTR-2-WUP/issues/29)).
   Bounded vertex translation, Texture0 sampling and replace/modulate TEV are
   implemented. Extend filtering, combiner and depth/blend coverage, then address
   observed first-game requirements. Keep exact software preflight/readback while
   expanding; physical parity and resource-cache costs remain unverified
   ([#78](https://github.com/Jdiller-86/CTR-2-WUP/issues/78)).
2. **Connect guest audio** ([#25](https://github.com/Jdiller-86/CTR-2-WUP/issues/25)).
   The checked CSTM/ADPCM stream mixer exists. Implement the guest DSP/service
   connection and timing; native tone playback does not cover guest audio.
3. **Finish installed-title lifecycle** ([#15](https://github.com/Jdiller-86/CTR-2-WUP/issues/15)).
   The keyless, self-signed plaintext WUP candidate and automatic SD recovery
   probe exist. Fresh installation and actual installed boot are proven with the
   complete XML/artwork/movie profile. Resolve native exit finalization, SD mount
   path validation and predecessor-update failure, then verify the two focused
   exit/recovery launches. Reuse bounded MCP
   enumeration and the reviewed
   SD logging and native display/audio/exit helpers.
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
| [M4: Graphics](https://github.com/Jdiller-86/CTR-2-WUP/issues/4) | GSP/PICA, software reference, GX2 vertex translation, base textures and replace/modulate TEV | Physical GPU parity, broader filtering/fragment state, coherency and measured costs |
| [M5: First game](https://github.com/Jdiller-86/CTR-2-WUP/issues/5) | Verified private Mario 3D Land revision and selected asset analysis | Implement observed gaps and prove boot, gameplay, audio and saves |
| [M6: Conversion](https://github.com/Jdiller-86/CTR-2-WUP/issues/6) | Intake/decryption, AOT builds, original WUP packing and integrity checks | Complete supported-game pipeline, Windows execution and installed rebuild/save continuity |
| [M7: GUI](https://github.com/Jdiller-86/CTR-2-WUP/issues/7) | Windows toolchain and workflow design | Responsive Select → Configure → Inject → WUP over the proven engine |
