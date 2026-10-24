# Software triangle scenes

The bounded software renderer accepts PICA-format vertex/index state, shaders
and triangle lists. It provides a correctness reference for [compiled guest
draws](guest-pica-draws.md) and the optional translated GX2 backend. Both paths
retain the same narrow scene contract; general PICA rendering remains unsupported.

## Execution boundary

`runtime/core/ctr_scene.c` accepts an explicit stable geometry span, optional
owned texture spans, matching PICA physical bases and an owned linear RGBA8 surface. Row zero is the
bottom. It never dereferences a guest/physical address. Every command list
stages the state and entire color/depth targets before committing; late errors
preserve previous registers, counters and both buffers. Failure reports describe an attempted
staged prefix, not committed draws. Aliased reports are left untouched.

The bounded path supports:

- PICA byte masks and finalized command records through the existing inspector;
  array loader offsets, component formats, alignment, padding, stride and input
  permutations; fixed attributes; indexed and nonindexed complete triangle lists.
- Shader code/descriptor uploads, complete packed float24 or float32 uniform
  uploads, entry point, input mapping and compact output masks/semantic maps.
  MOV/ADD/MUL support independent source swizzles, negation, write masks,
  initialized temporary registers and uniforms. ADD/MUL accept only exact
  normal float24/signed-zero results; canonical NOP and END terminate a linear program.
- Position XYZW and color RGBA outputs; W exactly 1, clip XYZ in the accepted
  volume, exact 1/16-pixel coordinates, viewport origin/extent, winding cull,
  include/exclude scissor, degenerate triangles and integer edge evaluation.
  Each triangle has a constant opaque endpoint color: RGBA lanes are 0 or 1,
  alpha 1. Final TEV alpha can reject fragments; logic/blend operations and
  channel masks control color writes; the paired-target API adds bounded D16
  depth and D24S8 stencil comparisons/actions.
- Optional UV0/1/2 outputs (semantics 12/13, 14/15 and 22/23), exact Q16 in −16..16 with W=1;
  affine nearest/bilinear sampling of all 14 base formats with independent clamp/repeat/
  mirror S/T per independently enabled Texture0/1/2. Texture2 can select UV1.
  Six TEV stages combine the three textures, primary, constants, Previous and PreviousBuffer
  with independent RGB/alpha operations, component operands and 1/2/4 scale.
  See [texture and combiner rules](textures.md).
- All eight final-alpha comparisons, all 16 bytewise logic operations and RGBA
  write masks with checked read/write gates; see [fragment output](textures.md#fragment-output).
- Separate RGB/alpha blending with five equations, 15 factors, retained constants
  and one final division/clamp. Native admission and rounding limits are defined
  in the [blend contract](textures.md#blending).

Limits are 256 vertices per draw, 32 draws per list, 512 vertex shader code words,
128 descriptors, 96 float uniforms, 12 attributes/loaders, a 4 MiB geometry span,
32 separate texture spans with 4 MiB total source bytes, a 1 MiB color target and
a 512 KiB D16 or 1 MiB D24S8 target, with matching dimensions no larger than
512×512. Separate texture spans cannot overlap geometry or each
other. No primitive is carried across lists. Unknown state, inexact shader
arithmetic, other opcodes, relative addressing, geometry, clipping, perspective,
varying primary color, mixed min/mag filters, mipmaps, DOT3,
broader depth
fail visibly. This is not a general PICA renderer.

`runtime/core/ctr_shader.c` accepts only finite normal values exactly
representable as PICA float24, plus signed zero. NaN/infinity, denormals,
out-of-range exponents and values requiring rounding fail. There is no float24
rounding approximation. MOV/ADD/MUL capture source lanes before writing
in-place swizzles and rejects undefined lanes or missing code/END.

### Vertex code capacity

Vertex code storage contains **512 words**. Entries 0–511 are valid;
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

### Exact ADD and MUL

Component-wise ADD and MUL share the bounded software execution path. Both operands
use independent descriptor swizzles/negation and initialized input/temporary
lanes; the first operand may also select a float uniform. Destination masks
select the lanes actually read and written, and in-place temporary execution
captures all operands before publishing any lane.

Only exact finite normal float24 results and signed zero are in scope. Integer
dyadic arithmetic rejects results needing rounding, underflow/overflow and
non-finite/denormal inputs instead of using a desktop float approximation.
Opposite-sign exact cancellation yields positive zero; multiplication preserves
the XOR of operand signs; adding two negative zeros preserves negative zero.
Zero does not bypass validation of the other operand. A masked-off lane reads
neither operand. Source 2 has the published five-bit input/temporary encoding;
source 1 has seven bits and can address all 96 float uniforms. The unused third
operand fields are ignored, while reserved descriptor bit 31 and relative
addressing remain unsupported.

Independent arbitrary-precision Python `Fraction` arithmetic agrees on all
**167,730** original numeric cases: 64,262 exact accepted results and 103,468
explicit numeric rejections. Coverage includes every normal exponent pair/sign,
special values, exact/inexact cancellation and a fixed original significand corpus.
In particular, `1 + 2^-62` fails rather than rounding to 1; `1 - 2^-17` is exact,
while `1 + 2^-17` is rejected. Multiplication rejects inexact low product bits.
The reference validates this deliberately exact subset, not hardware rounding.

Two additional scenes encode inverse-transformed original vertices and execute
`r0=c0; r1=v0*r0; o0=c1+r1`, using scale 2 and a nonzero XY offset. Both index
formats reproduce the analytic square exactly, executing 30 instructions per
two-draw scene. MOV scenes still execute 18. Late numeric, undefined-source and
unsupported-state failures preserve retained shaders, pixels and counters;
guest checks also preserve memory generations, exclusive state and pending IRQ/
queue completion after an earlier staged draw. These checks validate the exact supported numeric subset.

Run the independent numeric check directly with:

```sh
python tools/validate-shader-arithmetic.py --output artifacts/arithmetic-reference-NEW
```

The normal scene validator also runs it, including under sanitizers in CI.
Relative addressing, dot products, branches and hardware rounding remain
unsupported. Native Scene Check renders this CPU path and
presents its owned images through GX2 surface copies; it does not translate the
PICA shader into a Latte shader.

## GX2 shader and backend contract

`runtime/core/ctr_gx2_shader.c` encodes the accepted linear MOV/ADD/MUL/NOP/END
program as explicit little-endian Latte instruction bytes, preserving operand
and output-map semantics. Source lanes are captured before writes. Storage is
padded to 256 bytes; the native adapter uses aligned shader memory and reserves
a vertex budget above the encoder's **53 GPRs**, rising to **54/55/56** when
UV0/1/2 is the highest exported coordinate set.

| Vertex ABI | Registers or upload |
| --- | --- |
| Inputs `v0`–`v15` | GPR 1–16, fetched as float4 |
| Temporaries `r0`–`r15` | GPR 17–32 |
| PICA outputs `o0`–`o15` | GPR 33–48 |
| Captured source vectors | GPR 49–50 |
| Position and primary-color exports | GPR 51 and 52 |
| Optional UV0/1/2 | GPR 53/54/55.xy; dense PARAM exports after color carry xy01, with native semantic IDs 1/2/3 |
| Float uniforms `c0`–`c95` | Constant-file vectors 0–95 in UniformRegister mode; `GX2SetVertexUniformReg(index * 4, 4, bits)` |

`ctr_scene_execute_target_backend` preflights every actual vertex with the exact
float24 evaluator, then passes original inputs and state to synchronous
`begin`/`draw`/`end` callbacks. Compilation does not expand the numeric domain.
PARAM0 exports primary color; complete UV pairs export in coordinate order.
Readback must match the entire staged target, including padding and trailing
bytes, before commit. Callback failures and mismatches report
`CTR_SCENE_BACKEND` and abort without fallback. The paired
`ctr_scene_execute_targets_backend` variant uses optional `begin_depth` and
`end_depth` callbacks; legacy color-only backends reject depth before submission.
Both readbacks must match, including padding and trailing bytes. The
[depth/stencil profiles](textures.md#d16-depth) define quantization and native admission.

`runtime/wiiu/gx2_scene.c` implements GX2 surfaces, shaders, uniforms,
viewport/cull/scissor and readback. Textured draws bind each unit's owned RGBA8
texture and point/linear sampler; an original Latte pixel program samples base level zero
and evaluates the six-stage byte-domain TEV chain with delayed RGB/alpha buffer
publication. Reversed rows preserve PICA V origin
without changing guest UV. CPU flush/texture invalidation precede drawing.
The adapter restores the presentation context after each callback and retires
GPU references before reuse/free; failed retirement quarantines allocations
until graphics shutdown. Physical parity remains unverified, with preflight and
readback enabled; this path makes no performance claim.

### Native program cache

`runtime/core/ctr_gx2_cache.c` holds four vertex and eight pixel records in one
CPU allocation bounded by 384 KiB. Full canonical byte keys use explicit
little-endian fields, source/build identity and versioned encoder, numeric and
renderer profiles. Vertex identity includes all code/descriptors and defined
masks, entry and active output mapping. Pixel identity includes decoded TEV
topology, buffer update masks, buffered-program selection, enabled-unit mask,
Texture2 coordinate routing and depth profile. FIFO eviction is deterministic; duplicate stores retain their
original position.

Uniform values and stage/buffer colors remain dynamic. A vertex hit checks
current used lanes for definition and exact float24 representation in original
instruction encounter order; every draw uploads current constants and rebinds
complete state. Invalid state still fails explicitly. Allocation failure bypasses
the cache and uses the same checked translator. Cache statistics and translation
failures are separate from GPU allocation and retirement counters.

Records own their bytes and immutable metadata, with no guest or GPU pointers.
Hits copy into transaction-owned aligned GPU buffers after retirement; CPU-only
eviction needs no GPU wait. Records survive list completion, abort and foreground
release, while graphics shutdown or explicit cache clear frees the owner. Clearing
retains cumulative counters. Complete preflight, ownership checks and exact paired
readback still precede guest publication. Texture/target caching, source-resource
generation invalidation and physical performance remain outside this cache.

The optional backend `draw_textures` callback receives three unit-indexed borrowed
descriptors and an enable mask. Legacy `draw` callbacks retain their Texture0
contract; unsupported multi-unit draws reject during complete software preflight
before any native callback. All active resources remain inside the existing
32-snapshot/4-MiB source budget.

The independent validator decodes emitted GPU bytes and compares original
programs against arbitrary-precision rational expectations and the CPU evaluator:

```sh
python tools/validate-gx2-shaders.py --sanitize --output artifacts/gx2-shader-reference-NEW
```

ISA facts follow pinned
[Latte instruction definitions](https://github.com/decaf-emu/decaf-emu/blob/e6c528a20a41c34e0f9eb91dd3da40f119db2dee/src/libgpu/latte/latte_instructions.h)
and the
[WUT position/color shader interface](https://github.com/devkitPro/wut/tree/937f0d6901f5e2a72672154a674ec47b63e2f417/samples/cmake/content).
The encoder and decoder are original; no emulator implementation or external
assembler is bundled. These checks do not establish console execution or speed.

## Validation and references

Run the focused contract suites and independent reference validator:

```sh
make -C tests/scenes check sanitize
make -C tests/guest-draws check sanitize
python tools/validate-scenes.py --output artifacts/scene-evidence \
  --nihstro-dir /path/to/clean/pinned/nihstro
```

The evidence tool executes MOV and arithmetic scenes in both index formats and independently calculates the
expected square regions. Exact comparison, with zero tolerance, checks every
RGBA byte. Each image has 1,128 red and 1,176 green pixels; two draws execute
18 MOV or 30 arithmetic shader instructions. The validator saves command/resource bytes, raw RGBA,
PNG previews and hashed JSON receipts. No commercial input is used.
The receipt also records one desktop process CPU-time sample per render,
including renderer allocation/validation/commit. These tiny scenes and sanitizer
costs do not predict game frame rate or physical Espresso performance.

Original C tests cover exact float24 conversion against mathematical values,
MOV/ADD/MUL swizzles/negations/write masks, inputs/temporaries/uniforms, packed uploads,
loader padding, output maps, cull/scissor, both index sizes, nonindexed winding,
aliases, bounds and whole-list rollback. Normal and ASan/UBSan checks are part
of GCC/Clang CI; portable checks also run on Windows.

The optional test adapter checks 282,626 encodings against unmodified **nihstro**
headers at `f4d8659decbfe5d234f04134b5002b82dc515a44` (BSD-3-Clause,
Tony Wasserka). It validates instruction/operand encoding only. Azahar's pinned
PICA register/output/loader descriptions provide semantic review; its current
float24 model records approximate arithmetic, so it is not claimed as a hardware
numeric oracle. No emulator code is copied, linked or bundled with the runtime.

The image below is actual desktop output from the original renderer, flipped
to top-first display orientation. It is not Wii U or 3DS hardware evidence.

![Desktop software scene: green upper right and red lower left](images/software-scene.png)

Broader shader/fragment behavior, independent hardware raster observations and
measured GX2 costs remain development work.
The original software scenes do not establish commercial compatibility.
