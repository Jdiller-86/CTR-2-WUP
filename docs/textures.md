# Textures

`runtime/core/ctr_texture.c` decodes all 14 PICA base formats: RGBA/RGB packed
formats, luminance/alpha, RG8/HILO, ETC1 and ETC1A4. Power-of-two dimensions are
8..1024; 8×8 Morton storage becomes explicit row-major RGBA bytes. Source words
are little-endian on every host. Bounds, overlap and compressed differential
blocks are validated before output changes; mip tails and ETC2 modes are rejected.

## Sampling contract

The [scene renderer](software-scenes.md) and optional native GX2 backend accept:

| State | Supported boundary |
| --- | --- |
| Texture | Independently enabled Texture0/1/2, one base level, all 14 decoded formats, 8..1024 per dimension |
| Coordinates | UV0 semantics 12/13, UV1 14/15, UV2 22/23; exact Q16 in −16..16, affine interpolation with W=1 |
| Sampler | Per-unit matching nearest or bilinear min/mag, LOD/bias zero; independent clamp-to-edge, repeat or mirrored repeat on S/T |
| Combiner | Six stages; Texture0/1/2, binary primary color, per-stage RGBA8 constants, Previous and PreviousBuffer; independent RGB/alpha operands, operations and 1/2/4 scale |
| Fragment policy | Binary opaque primary color; final-alpha tests, logic/blend operations, RGBA masks and bounded D16/D24S8 depth/stencil; no lighting |

PICA wrap IDs are 0/2/3 for clamp/repeat/mirror; parameters store T at bits 8–10,
S at 12–14, with bit 5 set only for ETC1/ETC1A4. Filter bits 1/2 must both be
clear (nearest) or set (linear), following pinned
[libctru parameters](https://github.com/devkitPro/libctru/blob/9b55eda44cf80b971503991e0c78f9bc8fe50425/libctru/include/3ds/gpu/enums.h#L8).
Mixed min/mag, border wrap, mip filters and other sampler modes fail.
Nearest sampling floors the interpolated coordinate times the texture extent,
then wraps or clamps. PICA V=0 addresses the last raw storage row. This origin
follows pinned [tex3ds image/UV packing](https://github.com/devkitPro/tex3ds/blob/6d5e8681027d59a13fba95b8ee223e324125704e/source/tex3ds.cpp#L350)
and [atlas coordinates](https://github.com/devkitPro/tex3ds/blob/6d5e8681027d59a13fba95b8ee223e324125704e/source/atlas.cpp#L74).
The decoder preserves raw row order; sampling performs the origin conversion.

Register `0x80` bits 0/1/2 enable the units. Texture2 uses UV2, or UV1 when bit 13
is set; each required coordinate pair must be exported completely. Extra UV
exports must also be complete and valid. Semantic 16 is Texture0 W and remains
unsupported. `0x64` selects UV output mode; `0x6F` is
`3 | (exported_uv_mask << 8)` for the bounded position/color shader ABI.
These fields follow the pinned [libctru shader producer](https://github.com/devkitPro/libctru/blob/9b55eda44cf80b971503991e0c78f9bc8fe50425/libctru/source/gpu/shbin.c#L124).

| Unit | Dimensions | Parameters | LOD | Address | Format |
| --- | --- | --- | --- | --- | --- |
| 0 | `0x82` | `0x83` | `0x84` | `0x85` | `0x8E` |
| 1 | `0x92` | `0x93` | `0x94` | `0x95` | `0x96` |
| 2 | `0x9A` | `0x9B` | `0x9C` | `0x9D` | `0x9E` |

Active descriptors require complete byte masks. Addresses use eight-byte units;
LOD stays zero. Sources 3/4/5 require the corresponding enabled texture.

Bilinear sampling uses `coordinate × extent − ½`, wraps or clamps each of four
taps independently, and interpolates RGBA with the exact affine rational weights.
One final round-half-up produces each byte; intermediate rows are not rounded.
Portable integer limbs preserve the full numerator on 32-bit PowerPC. This is an
explicit software numeric contract; physical PICA/GX2 rounding is not established.

## Texture combiners

All six TEV stages accept separate RGB/alpha replace, modulate, add, signed add,
interpolate, subtract, multiply-add and add-multiply. Sources are primary color,
Texture0/1/2, each stage's RGBA8 constant, the preceding stage and PreviousBuffer. RGB operands select
color, alpha or replicated R/G/B, optionally complemented; alpha operands select
A/R/G/B with optional complement. Each stage snapshots its inputs before writing
any output channel, including cross-channel Previous operands.

The byte-domain contract truncates product/interpolation division by 255.
Signed add subtracts 128. Clamp the operation result to 0..255, multiply by the
selected 1/2/4 scale, then saturate to 255 before the next stage. Add-multiply
also clamps its sum before multiplication. These explicit software rules do not
establish physical PICA rounding. Register/operand facts follow pinned
[libctru enums](https://github.com/devkitPro/libctru/blob/9b55eda44cf80b971503991e0c78f9bc8fe50425/libctru/include/3ds/gpu/enums.h#L313)
and [PICA register fields](https://github.com/azahar-emu/azahar/blob/987f18405073960622e1043bb44005989135c6e1/src/video_core/pica/regs_texturing.h#L288).
DOT3, lighting sources, Texture3 and stage-zero
Previous reject explicitly. Active constant sources require a completely defined
constant register; reserved operand/function/scale fields reject even after a draw.

### Combiner buffer

Source `0xD` reads PreviousBuffer. Each fragment starts with a zero visible buffer
and a pending buffer initialized from `0xFD`, with R/G/B/A in successive
low-to-high bytes. After every stage, the pending value becomes visible, then
selected output channels replace pending RGB or alpha. Thus stage 0 reads zero,
stage 1 reads the initial color and stage 2 sees selected stage-0 output.
Passthrough stages also advance publication. Register `0xE0` bits 8–11 select
stage-0–3 RGB updates; bits 12–15 independently select alpha updates.

Active buffer state requires completely defined initial and update registers;
partial byte writes may complete retained state. Entirely undefined zero buffer
state remains the inactive legacy default. Fog and reserved `0xE0` bits reject.
Ordering is corroborated by the pinned
[software stage path](https://github.com/azahar-emu/azahar/blob/987f18405073960622e1043bb44005989135c6e1/src/video_core/renderer_software/sw_rasterizer.cpp#L699)
and separate [shader generator](https://github.com/azahar-emu/azahar/blob/987f18405073960622e1043bb44005989135c6e1/src/video_core/shader/generator/glsl_fs_shader_gen.cpp#L439);
the [producer controls](https://github.com/devkitPro/citro3d/blob/9f21cf7b380ce6f9e01a0420f19f0763e5443ca7/source/texenv.c#L46)
confirm the selector masks. These facts do not establish physical GPU parity.

Textures resolve entirely inside the stable geometry snapshot or one of at most
32 separate owned snapshots, with 4 MiB total additional source bytes. External
spans cannot overlap geometry or each other; partial geometry aliases and target
feedback fail. The [guest adapter](guest-pica-draws.md#resource-and-transaction-contract)
checks allocation identity and permissions before snapshotting.
Three decoded RGBA images use at most 12 MiB; native pitched uploads are bounded
to 8 MiB per unit and 24 MiB combined.

## Decoded-image cache

An optional guest-owned CPU cache retains eight decoded images within 24 MiB,
including owned encoded keys and RGBA payloads. Decoder profile, format,
dimensions and exact encoded bytes identify a record. Sampling, address and
coordinate routing remain current draw state. All active Texture0/1/2 images
stay pinned through each draw; unpinned entries evict in bounded round-robin
order. Invalid input preserves cached records; allocation failure may evict
unpinned records but cannot publish guest state. Counters saturate safely.

`ctr_scene_execute_targets_cached` accepts the explicit owner; existing scene
entry points retain one-use decoded ownership. Cache records contain no guest
or GPU pointers. Current source validation, native uploads, GPU retirement and
complete readback remain separate requirements.

## Fragment output

After TEV, alpha testing compares the final alpha byte with an unsigned reference
using NEVER, ALWAYS, EQUAL, NOT_EQUAL, LESS, LEQUAL, GREATER or GEQUAL. A failed
test preserves the destination. Passing fragments apply one of all 16 PICA logic
operations or separate RGB/alpha blending, then write only enabled RGBA channels.
Both routes read the destination produced by earlier draws in the same list.
Optional stencil/depth tests precede color writes. The fragment counter measures
geometric coverage, including alpha, stencil or depth rejection.

| Register | Accepted state |
| --- | --- |
| `0x100` | Ordinary mode; 0 selects logic, `0x100` selects blending |
| `0x101` | RGB/alpha equations in bits 0..2/8..10; source/destination factors at 16/20/24/28 |
| `0x102` | Logic operation 0..15 |
| `0x103` | Blend constant RGBA in successive low-to-high bytes |
| `0x104` | Alpha enable bit 0, comparison bits 4..6, reference bits 8..15 |
| `0x105` | D24S8 stencil enable bit 0, comparison bits 4..6, write mask/reference/input mask in successive bytes |
| `0x106` | D24S8 stencil-fail/depth-fail/pass actions in bits 0..2/4..6/8..10 |
| `0x107` | Depth test bit 0, comparison bits 4..6, RGBA write bits 8..11, depth write bit 12 |
| `0x112` | Read gate 0 or 15; destination-dependent operations require 15 for enabled writes |
| `0x113` | Whole-framebuffer write gate 0 or 15; zero preserves every channel |

The six base registers must be completely defined for bound targets. Blending
also requires all bytes of `0x101`; selected constant factors require all of
`0x103`, including when writes are masked. The owned scene
API retains its COPY/full-write/disabled-alpha default only when none is supplied.
Partial byte writes merge with retained state; unsupported fields reject even
after the final draw. CLEAR, COPY, SET and COPY_INVERTED do not require destination
reads. Blend dependencies are checked per enabled RGB/alpha side: destination
factors other than ZERO and source factors that consume destination bytes require
the full read gate. ONE/ZERO replacement can use read gate zero. Partial read/write
gate encodings and other fragment modes fail explicitly. Stencil state requires
an owned D24S8 target; D16 and color-only pipelines retain zero stencil state.
Register and comparison facts follow pinned
[PICA fields](https://github.com/azahar-emu/azahar/blob/987f18405073960622e1043bb44005989135c6e1/src/video_core/pica/regs_framebuffer.h)
and [libctru enums](https://github.com/devkitPro/libctru/blob/9b55eda44cf80b971503991e0c78f9bc8fe50425/libctru/include/3ds/gpu/enums.h#L162).
The full read/write gate follows the
[citro3d framebuffer producer](https://github.com/devkitPro/citro3d/blob/9f21cf7b380ce6f9e01a0420f19f0763e5443ca7/source/framebuffer.c#L49);
undocumented partial gate behavior is not approximated.

### Blending

The portable merger supports separate RGB/alpha ADD, SUBTRACT, REVERSE_SUBTRACT,
MIN and MAX, with all 15 PICA factors: zero/one, source/destination color or alpha,
constants, their complements, and source-alpha-saturate. For source byte S,
destination D and byte factors Fs/Fd, combine `S×Fs` and `D×Fd` with the selected
equation, divide once by 255 with integer truncation, then clamp to 0..255.
Products are not rounded separately. MIN/MAX compare the weighted products.
Source-alpha-saturate gives `min(Sa,255−Da)` for RGB and 255 for alpha.
The explicit numeric contract follows the pinned
[framebuffer equation reference](https://github.com/azahar-emu/azahar/blob/987f18405073960622e1043bb44005989135c6e1/src/video_core/renderer_software/sw_framebuffer.cpp#L294)
and [factor lookup](https://github.com/azahar-emu/azahar/blob/987f18405073960622e1043bb44005989135c6e1/src/video_core/renderer_software/sw_rasterizer.cpp#L595);
it does not establish physical PICA rounding.

### D16 depth

The paired-target API accepts an owned linear little-endian D16 surface with
bottom row zero, matching color dimensions and an independent stride. The guest
adapter resolves `0x11c` in eight-byte units and converts the same 8×8 Morton
layout as color. Format `0x116=0` selects D16; gates `0x114/0x115` accept 0 or 2.
Depth testing requires read gate 2. Writes require bit 12 and write gate 2,
including when the depth-test enable is off. A failed alpha or depth comparison
preserves both targets; color masks do not suppress enabled depth writes.
All eight comparisons use the incoming quantized depth against stored D16.

The numeric profile requires W=1, constant Z per triangle in [−1,0], Z buffering
(`0x06d=1`), scale −1 (`0x04d=0xbf0000`) and zero offset (`0x04e`). Software
quantization truncates `−Z×65535` exactly. Paired native backends initially admit
only Z=0 or −1, avoiding unestablished fractional GPU conversion. W buffering,
varying depth, other transforms and three-byte D24 reject explicitly. Field and
ordering facts follow the pinned [framebuffer reference](https://github.com/azahar-emu/azahar/blob/987f18405073960622e1043bb44005989135c6e1/src/video_core/pica/regs_framebuffer.h)
and [depth test](https://github.com/azahar-emu/azahar/blob/987f18405073960622e1043bb44005989135c6e1/src/video_core/renderer_software/sw_rasterizer.cpp#L860);
these do not establish physical rounding.

### D24S8 stencil

Format `0x116=3` selects four-byte little-endian D24S8: depth occupies bytes 0–2
and stencil byte 3. It uses the same Morton indices and paired-target transaction
as D16. Gates `0x114/0x115` accept 0 or 3; enabled stencil or depth testing requires
read gate 3. Write gate zero suppresses depth and stencil writes independently of
color. Both stencil registers must be completely defined, including valid
inactive configuration. D24S8 storage is limited to 1 MiB, alongside a 1 MiB
color target; D16 retains its separate 512 KiB limit.

Alpha rejection preserves both targets. Stencil compares masked reference against
masked stored stencil with the eight comparison functions. Stencil-fail,
depth-fail and pass each select KEEP, ZERO, REPLACE, saturating increment/decrement,
INVERT or wrapping increment/decrement. The write mask merges the result with
stored stencil. A failed stencil/depth test can update stencil while preserving
color and depth; pass actions run even when depth testing is disabled. Color masks
do not suppress stencil preparation.

The canonical constant-Z profile is shared with D16. Software truncates
`−Z×16777215`; native inputs remain Z=0 or −1. Packed layout follows the pinned
[PICA fields](https://github.com/azahar-emu/azahar/blob/987f18405073960622e1043bb44005989135c6e1/src/video_core/pica/regs_framebuffer.h)
and independent [R600 depth-format mapping](https://github.com/chaotic-cx/mesa-mirror/blob/f1f246cfda65eff82fba3be1caf2d23bdeda60cc/src/gallium/drivers/r600/r600_state.c#L113).
These interface facts do not establish physical Wii U parity.

## Native GX2 path

The native adapter binds each unit's decoded RGBA through an owned pitched GX2 texture,
reversing rows on upload so guest UV needs no CPU rewrite. Its Latte pixel program uses
base-level normalized sampling and the selected point/linear sampler. An original
scalar Latte encoder implements the admitted combiner chain using nine GPRs,
or eleven with separate visible/pending buffers for the legacy Texture0 route.
Materials using Texture1/2 use fifteen GPRs with separate sampled values and
compact coordinate inputs. Resource and sampler selectors retain the unit IDs;
Texture2's coordinate route is independent of its sampler binding. Shader storage
remains bounded.
Pixel uniforms carry stage constants and initial buffer bytes; explicit byte
quantization and floor division preserve the software stage boundaries.
The [native program cache](software-scenes.md#native-program-cache) reuses compiled
topology while validating current TEV state and uploading current constants.
Immutable snapshots, decoded CPU images and retired native uploads have separate
cache owners. Samplers bind from current descriptors on every draw.
CPU cache flushes and texture invalidation precede drawing; retirement precedes
reuse/free. Failed waits quarantine GPU allocations until graphics shutdown.
Complete software preflight and exact GPU readback precede guest commit.
GX2 receives explicitly translated logic/comparison enums and the effective
channel mask. Alpha thresholds match the shader's byte normalization, including
the 255 endpoint; the API model checks both clamped and unclamped exports.
Blending translates ADD/SUBTRACT/REVERSE_SUBTRACT factors explicitly, with separate
alpha controls and constants. Native MIN/MAX admits ONE/ONE factors only;
source-alpha-saturate in the destination selector rejects. Logic draws disable
blending, and every draw rebinds complete fixed state. Fixed-function fractional
rounding remains unverified: a one-byte difference still rejects the entire list.
The original compiled guest uses integral ONE/ONE RGB addition and ONE/ZERO alpha,
checks a buffer material before display, D16 writes after alpha rejection and D24S8 stencil preparation, masked
rendering and rejected overdraw while retaining the same displayed images.

Native D16/D24S8 targets and their linear staging surfaces both carry depth usage.
The adapter disables HiZ, sets the explicit Z scale/offset and uses late depth
testing so alpha rejection precedes writes. With testing disabled but writes
requested, ALWAYS implements the write path. Both complete readbacks must match,
including packed stencil, padding and trailing bytes, before either target commits.
Stencil reference, input/write masks and named actions bind on every draw with
matching front/back controls; disabled stencil resets these controls.
Surface/copy facts follow the
pinned [GX2 implementation reference](https://github.com/decaf-emu/decaf-emu/blob/e6c528a20a41c34e0f9eb91dd3da40f119db2dee/src/libdecaf/src/cafe/libraries/gx2/gx2_surface.cpp#L430).

Mipmaps, mixed min/mag filters, projective/perspective coordinates, DOT3,
native target pooling and broader depth remain unsupported. Physical sampling/blending/depth/stencil parity
and useful GPU performance remain unverified. The full-target byte comparison
stays exact, including for bilinear draws; differing hardware rounding rejects
the list without guest publication or software fallback.

### Native upload reuse

Six native records retain at most 24 MiB of pitched RGBA8 images, with an 8 MiB
per-image limit. After prior draw/context references retire, exact reversed-row
pixel comparisons and dimensions identify reusable read-only images. Source
address, sampler and UV route remain current state. Source spans overlapping
retained images or cache metadata reject; all active units pin their images
before another lookup can evict them. Unpinned records evict in bounded
round-robin order. Cache-owner allocation failure uses the checked transaction
upload path; image allocation failures reject explicitly.

Successful list completion retains only retired images. Abort drains active
references before clearing them; idle abort clears retired images without GX2
calls. Failed waits quarantine the transaction and all referenced cached images
until retirement succeeds or graphics shutdown releases them. CPU program-cache
clears do not touch GPU images. Native logs report hits, misses, uploads,
evictions, allocation failures, bytes and pins. Complete preflight and exact paired
readback remain mandatory; reuse establishes no physical parity or useful cost.

## Validation

```sh
python tools/validate-textures.py --output artifacts/texture-reference-NEW
python tools/validate-textured-scenes.py --sanitize --output artifacts/textured-scene-reference-NEW
```

Independent original reference checks cover format decoding, exact scene pixels,
wrap/origin rules, combiner operations/operands/scales and transactional failures. The native API model
also captures actual submitted pixel-program bytes for independent decoding;
it does not emulate GPU execution. The optional clean Zakuro decoder comparison
uses pin `76f17bf76cd9ea238a94f37d389fe701ab05a673`. Format 6 follows RG8
`[byte1, byte0, 0, 255]`; Zakuro's differing channel convention is excluded.
External reference implementations remain test-only, outside delivered runtime code.
