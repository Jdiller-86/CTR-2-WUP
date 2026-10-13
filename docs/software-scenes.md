# First software triangle scenes

v0.22.0 establishes the first original scene through a bounded PICA-format
loader, index, shader and raster path. Two indexed triangles form a square,
red at lower left and green at upper right. Both unsigned byte and unsigned
short index formats produce identical pixels. This is a software correctness
checkpoint for M4; full renderer acceptance remains open.

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
lifecycle. It does not replace the pending Audio Check/GX Transfers 0.20.0
launch-music regression or repeat the completed Save/RomFS guides.

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

Limits are 256 vertices per draw, 32 draws per list, 256 shader instructions,
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

The native Scene Check runs this CPU path, flips its owned image rows for display,
then uses the established GX2 surface-copy presentation. It does not submit a
PICA shader as a Latte shader. The standard guest GSP queue continues to use the
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
