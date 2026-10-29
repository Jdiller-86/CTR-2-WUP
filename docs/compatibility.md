# Compatibility

No commercial game profile is enabled. CTR2WUP's original diagnostics exercise
bounded runtime features; upstream desktop emulator compatibility does not imply
Wii U compatibility. The selected first-title target is
[Super Mario 3D Land, USA](first-title.md).

| Area | Implemented boundary | Remaining limit |
|---|---|---|
| Native Wii U platform | TV/GamePad output, VPAD/touch, owned AX audio, ProcUI lifecycle and SD reports | Native installed exit/relaunch and automatic SD recovery remain open |
| CPU | Checked ARM/Thumb interpreter, original AOT and external generated-C adapter | Incomplete ARM11/VFP coverage; exact revision profiles and useful workload costs remain open |
| Kernel/services | Single-process threads, memory/objects, TLS/SRV/CFG, HID, GSP, standalone APT/FS and bounded DSP PCM IPC | Unsupported commands fail; general applets, retail saves and DSP firmware remain open |
| Original compiled applications | Standard libctru startup, input, frames, saves, RomFS, transfers and bounded draws | Original programs do not establish retail startup or gameplay |
| Graphics | Five framebuffer formats, CPU-backed GX transfers, bounded GX2 vertex translation, all 14 base textures, nearest/bilinear six-stage TEV with a combiner buffer, alpha/logic/blend/write controls and D16/D24S8 depth/stencil | Physical GPU parity/rounding, broader native blends, general shaders, mixed filters/mipmaps, DOT3, depth interpolation and geometry remain open |
| Audio | DSP ADPCM, bounded CSTM blocks/seek, two-voice CSTM mixer, 24-voice original guest PCM HLE and finite native clips | General DSP firmware, guest ADPCM/rates, CSAR selection, CSTM loops and continuous synchronization remain open |
| Decrypted inputs | Base-application CIA, NCCH v2 CXI and NCSD cartridge inspection | No NAND loader, update/DLC merging or arbitrary-ROM execution |
| Encrypted inputs | Scoped local CIA/NCCH preparation with local key/seed files | Supported profiles only; no bundled keys or console extraction |
| RomFS | Canonical IVFC/metadata verification, bounded exports and read-only guest provider | No signature authentication or general game integration |
| WUP | Original plaintext test-signed candidate and separately configured encrypted packing | Original installation/Menu boot accepted; native exit remains failed and general-title acceptance is open |
| Windows | Python CLI, native host checks and separate CIA helper | Final injector GUI and end-to-end commercial conversion remain unfinished |

Stable program ID identifies future saves; executable/module hashes identify a
revision. Profiles must match exact revisions and explicit supported behavior.
Only the original `ctr2wup-framebuffer-fixture-v1` is enabled by the guest CLI.

Physical Wii U validation covers native controls/input, tone, stability, save
reload, transfers and bounded guest draws. Exact source, environment and scope
are recorded in the
[hardware evidence ledger](https://github.com/Jdiller-86/CTR-2-WUP/blob/main/tests/hardware-evidence.json).
Host tests and emulated PowerPC checks cover their stated contracts; they do not
establish physical display/audio quality, installation or console performance.
The optional translated GX2 draw path has separate validation scope; existing
software-rendered guest evidence does not establish its physical GPU parity.

New 3DS exclusives, stereo output, online services, cameras, microphone, DLC/update
merging and save import/export are outside the current scope.
