# Project completion assessment

Updated **October 14, 2026**, against source **0.23.0** and the
[product plan](development-plan.md). The [live dashboard](https://jdiller-86.github.io/CTR-2-WUP/progress/)
reads Project #2; this article records the engineering assessment.

## Estimated implementation: approximately 20%

The completion target is a Windows GUI that converts one verified owned 3DS
revision into a WUP, followed by Wii U installation, Menu launch, gameplay,
audio, save/reload, rebuild continuity and useful measured performance. Broad
game compatibility follows the first supported revision.

**Approximately 20%** remains a rough implementation judgment. It is independent
of test counts, commits, versions and Project task totals, and predicts no date.
**0/7 full milestones are accepted.** Scoped implementation and diagnostic passes
do not accept a complete hardware/title/Windows gate. See [the full criteria](milestones.md).

## What supports the estimate

| Area | Verified foundation | Main remaining acceptance |
| --- | --- | --- |
| Preparation | Bounded container inspection/decryption, executable intake, full local RomFS verification and selected-file provenance | Reusable nested readers, observed runtime requirements and clean Windows commercial conversion |
| CPU/runtime | Checked ARM/Thumb/VFP/AOT subsets, owned memory, eight single-core guest contexts and scoped services | Exercised game CPU/HLE coverage, modules, guest DSP/audio and retail saves |
| Homebrew | Nine original normal-CRT/libctru applications; save/assets, GX and bounded PICA draws | Complete platform lifecycle/recovery, broader runtime and measured Wii U performance |
| Graphics | Exact original software frames, MOV/NOP/END and checked 512-word vertex storage | Arithmetic/control, TEV/sampling, coherency, general rendering and native GX2 translation |
| Conversion/GUI | Original synthetic package verification and specified GUI workflow | Actual WUP installation/Menu launch, complete Windows conversion and final GUI acceptance |

The October 13 Audio Check/GX Transfers 0.20.0 and Guest Draws 0.23.0 results
have reviewed scoped passes: expected pictures, silence and normal exit. Earlier
Save/RomFS results remain valid. The draw trace reaches its second presentation
at 57.33 seconds and exposed premature picture confirmation; accurate gating and
phase profiling remain scoped follow-ups. New cross-builds do not create new
console evidence or require repeats of unchanged passed guides.

## Largest unfinished work

- Broader shaders, fragment state, native PICA-to-GX2 translation, resources and coherency.
- Required game services/modules, guest audio and retail saves.
- Mario boot/menu/gameplay/audio/save/reload, stability and useful Wii U costs.
- Complete Windows conversion, cancellation/rebuild/save continuity and installed WUP acceptance.
- The final packaged GUI over the demonstrated conversion engine.

## Evidence and inputs still needed

The USA Mario executable, matching preparation/inventory reports and all seven
selected asset payloads are verified privately. Selected Yaz0/NARC, DVLB/CGFX/
BYML and CSAR/CSTM structures inform shared shader and DSP ADPCM work. Input
absence no longer blocks M5/intake/requirements analysis. Broader nested modules
and actual runtime use remain unverified; no commercial profile is enabled.
See [the current title matrix](first-title.md).

Record exact firmware/Aroma/installer versions, full platform recovery/stability,
original WUP installation/Menu launch, commercial runtime results and clean
Windows conversion. Hosted Windows tests and source/cross-build checks supply
their stated evidence without filling these gates.

Source, maintained documentation and the dashboard share the public
[CTR-2-WUP repository](https://github.com/Jdiller-86/CTR-2-WUP).
[Current status](../STATUS.md) records verified deployment and management receipts;
Project fields own live states and waiting resources. Historical checkpoints are
retained in [development history](development-history.md) and Git history.
