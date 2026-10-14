# First software triangle scenes

v0.22.0 establishes the first original scene through a bounded PICA-format
loader, index, shader and raster path. Two indexed triangles form a square,
red at lower left and green at upper right. Both unsigned byte and unsigned
short index formats produce identical pixels. This is a software correctness
checkpoint for M4; full renderer acceptance remains open.

Version 0.23 connects this owned renderer to [standard compiled guest draws](guest-pica-draws.md).
The focused native check below remains a separate original diagnostic.

## Focused Wii U check

Build with `python -m host build --target scene-check --output artifacts/my-scene-check`.
The matching CI run also supplies `wiiu-scene-check-<source commit>`, containing
RPX/WUHB, build receipt, checksums, source identity and notices. Use its WUHB
from `sd:/wiiu/apps/` with the supported homebrew launcher, or its RPX with
Homebrew Launcher. This is an original homebrew check, not an installable WUP.

The TV shows the byte-index scene; GamePad shows the short-index scene. Both
should show a solid square with red lower left, green upper right and a dark
border. The TV guide asks for two observations. Press **R** for correct or
**L** for a problem; **ZL** skips and **ZR** records a failure. **Minus** closes
the application and preserves results. Send the new plain-text files from
`sd:/ctr2wup/` and report whether Minus returned to the launcher. The automatic
pixel self-test and render timings enter the small result and full trace;
human display observations remain separate. An automatic failure cannot pass
the picture guide.

This check uses the existing native audio initialization and report/exit
lifecycle. The reviewed October 13 Audio Check/GX Transfers 0.20.0 and
Save/RomFS results remain valid for their builds; no unchanged guide repeat is requested.

## Execution boundary

`runtime/core/ctr_scene.c` accepts an explicit stable resource span, matching
PICA physical base and an explicit owned linear RGBA8 surface. Row zero is the
bottom. It never dereferences a guest/physical address. Every command list
stages the state and entire target before committing; late errors preserve
previous registers, counters and pixels. Failure reports describe an attempted
staged prefix, not committed draws. Aliased reports are left untouched.

The bounded path supports:

- PICA byte masks and finalized command records through the existing inspector;
  array loader offsets, component formats, alignment, padding, stride and input
  permutations; fixed attributes; indexed and nonindexed complete triangle lists.
- Shader code/descriptor uploads, complete packed float24 or float32 uniform
  uploads, entry point, input mapping and compact output masks/semantic maps.
  MOV supports swizzle, negation, write masks, initialized temporary registers
  and uniforms; canonical NOP and END terminate a bounded linear program.
- Position XYZW and color RGBA outputs; W exactly 1, clip XYZ in the accepted
  volume, exact 1/16-pixel coordinates, viewport origin/extent, winding cull,
  include/exclude scissor, degenerate triangles and integer edge evaluation.
  Each triangle has a constant opaque endpoint color: RGBA lanes are 0 or 1,
  alpha 1. Fragments replace the target with depth and blend disabled.

Limits are 256 vertices per draw, 32 draws per list, 512 vertex shader code words,
128 descriptors, 96 float uniforms, 12 attributes/loaders, a 4 MiB resource span
and a 1 MiB target with dimensions at most 512×512. No primitive is carried
across lists. Unknown pipeline/framebuffer/texture/TEV/depth registers,
arithmetic, relative addressing, geometry, clipping, perspective, varying color,
interpolation, sampling and blending fail visibly. This is not a general PICA
renderer or a GPU shader translator.

`runtime/core/ctr_shader.c` accepts only finite normal values exactly
representable as PICA float24, plus signed zero. NaN/infinity, denormals,
out-of-range exponents and values requiring rounding fail. There is no float24
arithmetic or rounding approximation. MOV captures source lanes before writing
in-place swizzles and rejects undefined lanes or missing code/END.

### Vertex code capacity

The merged capacity increment in [PR #98](https://github.com/Jdiller-86/CTR-2-WUP/pull/98)
expands code storage from 256 to **512 words**. Entries 0–511 are valid;
execution must encounter an actual END before leaving word 511. The upload
cursor may retain the exhausted value 512, but another code word fails without
wrapping or committing any part of that list. All eight upload ports, repeated
parameters and consecutive-register batches share this checked cursor.
The 128 descriptors and MOV/NOP/END semantics remain bounded separately.

The vertex-specific limit follows the pinned
[vertex upload reference](https://github.com/azahar-emu/azahar/blob/d409087405bc299b88ff35dd5b96b6e2698d5929/src/video_core/pica/pica_core.cpp#L738).
Its larger shared shader container also accommodates geometry storage; it does
not establish vertex capacity or enable geometry here. Only interface facts
were reviewed; no reference implementation is copied or linked.

Original tests execute every entry, upload every slot, compare relocated high
entry scenes against the same analytic pixels, and force overflow after staged
draws. They verify whole-list state/pixel preservation, retained guest storage,
memory generations, exclusive state and pending queue/IRQ behavior.
Run `make -C tests/scenes check sanitize` and
`make -C tests/guest-draws check sanitize` to reproduce them.

With devkitPPC GCC 16.1.0, `-O2 -mcpu=750 -meabi -mhard-float`, the program
object grows from 3,556 to 4,836 bytes and scene state from 8,952 to 10,232 bytes.
Each retained/staged scene therefore adds 1,280 heap bytes. Compiler
`-fstack-usage` reports unchanged static frames: shader execution 640 bytes,
scene execution 928 bytes, guest adapter 272 bytes. These are cross-compiler
measurements, not physical memory/performance observations. Required Windows,
Python, sanitizer, Wii U and documentation hosted checks pass. No commercial
shader is executed by these original probes.

The native Scene Check runs this CPU path, flips its owned image rows for display,
then uses the established GX2 surface-copy presentation. It does not submit a
PICA shader as a Latte shader. The v0.22 standard guest GSP queue continues to use the
separate configuration-only PICA executor and rejects draws.

## Evidence and reference limits

Run `make check`, `make prototype`, then:

```sh
python tools/validate-scenes.py --output artifacts/scene-evidence \
  --nihstro-dir /path/to/clean/pinned/nihstro
```

The evidence tool executes both index formats and independently calculates the
expected square regions. Exact comparison, with zero tolerance, checks every
RGBA byte. Each image has 1,128 red and 1,176 green pixels; two draws execute
18 shader instructions. The validator saves command/resource bytes, raw RGBA,
PNG previews and hashed JSON receipts. No commercial input is used.
The receipt also records one desktop process CPU-time sample per render,
including renderer allocation/validation/commit. These tiny scenes and sanitizer
costs do not predict game frame rate or physical Espresso performance.

Original C tests cover exact float24 conversion against mathematical values,
all MOV swizzles/negations/write masks, inputs/temporaries/uniforms, packed uploads,
loader padding, output maps, cull/scissor, both index sizes, nonindexed winding,
aliases, bounds and whole-list rollback. Normal and ASan/UBSan checks are part
of GCC/Clang CI; portable checks also run on Windows.

The optional test adapter checks 12,290 encodings against unmodified **nihstro**
headers at `f4d8659decbfe5d234f04134b5002b82dc515a44` (BSD-3-Clause,
Tony Wasserka). It validates instruction/operand encoding only. Azahar's pinned
PICA register/output/loader descriptions provide semantic review; its current
float24 model records approximate arithmetic, so it is not claimed as a hardware
numeric oracle. No emulator code is copied, linked or bundled with the runtime.

The image below is actual desktop output from the original renderer, flipped
to top-first display orientation. It is not Wii U or 3DS hardware evidence.

![Desktop software scene: green upper right and red lower left](images/software-scene-v022.png)

Remaining acceptance requires physical Wii U picture/timing/exit reports,
independent hardware/reference raster observations, broader shader/fragment
behavior, guest resource integration and a license-cleared GX2 shader route with
measured costs. No commercial compatibility or full milestone PASS is implied.
