# First-title technical target

The selected commercial target is **Super Mario 3D Land, USA**. Its executable
and selected resources inform reusable runtime work. No commercial profile,
boot/gameplay result or supported conversion is enabled.

## Identity and input

The independently inspected NCCH has product `CTR-P-AREE`, program ID
`0004000000054000`, region mask 2 and remaster field 0. Its decompressed executable
is 3,096,576 bytes. Exact revision/code hashes and all commercial analysis remain
private. The source filename's version is not a verified CIA title version.
The extended-header allowlist declares 27 services including `dsp::DSP` and
`ldr:ro`; declarations do not prove executed IPC or module loading.

The reported asset inventory contains 1,756 files in 19 directories
and a 298,080,796-byte Level-3 RomFS blob: 1,666 `.szs`, 88 `.bcstm`, one `.bcsar`
and one `.bsm`. Full source/blob integrity is recorded by local preparation
reports. Structural analysis covers seven selected payloads totaling 15,304,637
bytes, verified against the private manifest and executable revision. These
findings apply to the selected resources, not the complete container.

## Resource findings

| Resource | Verified structure | Runtime implication |
|---|---|---|
| Three archives | Yaz0 yields bounded NARC tables; selected DVLB, CGFX and BYML members | Bounded decompression/container handling; names remain virtual |
| Shader members | Five vertex entries and one geometry entry across selected DVLB sets | Arithmetic, comparison/addressing and control flow; active entries remain untraced |
| CGFX shader resource | Bounded DATA block and three embedded DVLB regions, 12 vertex entries | Resource binding; code sizes 472/428/264 fit the implemented 512-word vertex capacity |
| System class table | LE BYML version 1, bounded header/table/root offsets | Typed-node parsing; class names do not establish service use |
| Sound archive | CSAR version 2 STRG/INFO/FILE blocks fit the file | Archive references/banks/sequences/waves still require interpretation |
| Two streams | CSTM version 2 INFO/SEEK/DATA; non-looping stereo DSP ADPCM at 32 kHz | Checked codec/blocks/mixing exist; selection and guest DSP ADPCM integration remain open |

Raw shader words include ADD, DP3/DP4, MUL, floor/max/reciprocal, address moves,
comparisons, conditional/loop/jump, multiply-add and geometry emit families.
This static inventory includes potentially unreachable code. Encoding facts use
nihstro `f4d8659decbfe5d234f04134b5002b82dc515a44`; no implementation or commercial
shader bytes are bundled. The software renderer supports only its
[documented subset](software-scenes.md).

## Execution requirements

Observed execution must determine required IPC/modules, shader entries, fragment
state, save behavior and audio scheduling. Upstream Zakuro source observations
at `76f17bf76cd9ea238a94f37d389fe701ab05a673` identify two leads: saved Mii/CFG
identity consistency, and small color-buffer CPU readbacks during course loading.
These have not been reproduced with this exact revision. Generic applet success
or framebuffer presentation cannot substitute for correct identity/coherency.

Private analysis uses:

```sh
python3 -m host analyze-title private/game.cxi --lifter --output private/first-title-analysis
```

The report binds hashes, segments/stack/BSS and declared capabilities. Keep all
code, assets, generated commercial C, traces and saves under private storage.
[Compatibility](compatibility.md) describes implemented shared features;
[plans.md](https://github.com/Jdiller-86/CTR-2-WUP/blob/main/plans.md) tracks development priorities.
