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
| Texture | Texture0, one base level, all 14 decoded formats, 8..1024 per dimension |
| Coordinates | Vertex semantics 12/13, exact Q16 UV in −16..16, affine interpolation with W=1 |
| Sampler | Matching nearest or bilinear min/mag, LOD/bias zero; clamp-to-edge, repeat or mirrored repeat independently on S/T |
| Combiner | Six stages; Texture0, binary primary color, per-stage RGBA8 constants and Previous; independent RGB/alpha operands, operations and 1/2/4 scale |
| Fragment policy | Binary opaque primary color; final-alpha tests, 16 logic operations and RGBA write masks; no lighting/depth/stencil/blending |

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

Bilinear sampling uses `coordinate × extent − ½`, wraps or clamps each of four
taps independently, and interpolates RGBA with the exact affine rational weights.
One final round-half-up produces each byte; intermediate rows are not rounded.
Portable integer limbs preserve the full numerator on 32-bit PowerPC. This is an
explicit software numeric contract; physical PICA/GX2 rounding is not established.

## Texture combiners

All six TEV stages accept separate RGB/alpha replace, modulate, add, signed add,
interpolate, subtract, multiply-add and add-multiply. Sources are primary color,
Texture0, each stage's RGBA8 constant and the preceding stage. RGB operands select
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
DOT3, combiner-buffer reads/writes, lighting sources, Texture1–3 and stage-zero
Previous reject explicitly. Active constant sources require a completely defined
constant register; reserved operand/function/scale fields reject even after a draw.

Textures resolve entirely inside the stable geometry snapshot or one of at most
32 separate owned snapshots, with 4 MiB total additional source bytes. External
spans cannot overlap geometry or each other; partial geometry aliases and target
feedback fail. The [guest adapter](guest-pica-draws.md#resource-and-transaction-contract)
checks allocation identity and permissions before snapshotting.

## Fragment output

After TEV, alpha testing compares the final alpha byte with an unsigned reference
using NEVER, ALWAYS, EQUAL, NOT_EQUAL, LESS, LEQUAL, GREATER or GEQUAL. A failed
test preserves the destination. Passing fragments apply one of all 16 PICA logic
operations independently to each byte, then write only enabled RGBA channels.
Logic operations read the destination produced by earlier draws in the same list.
The fragment counter measures geometric coverage, including rejected alpha.

| Register | Accepted state |
| --- | --- |
| `0x100` | Zero: ordinary fragment mode, blending disabled |
| `0x102` | Logic operation 0..15 |
| `0x104` | Alpha enable bit 0, comparison bits 4..6, reference bits 8..15 |
| `0x107` | RGBA write bits 8..11; depth test/write disabled |
| `0x112` | Read gate 0 or 15; destination-dependent logic requires 15 when writes are enabled |
| `0x113` | Whole-framebuffer write gate 0 or 15; zero preserves every channel |

All six registers must be completely defined for bound targets. The owned scene
API retains its COPY/full-write/disabled-alpha default only when none is supplied.
Partial byte writes merge with retained state; unsupported fields reject even
after the final draw. CLEAR, COPY, SET and COPY_INVERTED do not require destination
reads. Partial read/write gate encodings, blending, depth and stencil fail explicitly.
Register and comparison facts follow pinned
[PICA fields](https://github.com/azahar-emu/azahar/blob/987f18405073960622e1043bb44005989135c6e1/src/video_core/pica/regs_framebuffer.h)
and [libctru enums](https://github.com/devkitPro/libctru/blob/9b55eda44cf80b971503991e0c78f9bc8fe50425/libctru/include/3ds/gpu/enums.h#L162).
The full read/write gate follows the
[citro3d framebuffer producer](https://github.com/devkitPro/citro3d/blob/9f21cf7b380ce6f9e01a0420f19f0763e5443ca7/source/framebuffer.c#L49);
undocumented partial gate behavior is not approximated.

## Native GX2 path

The native adapter uploads decoded RGBA to an owned pitched GX2 texture, reversing
rows so the original guest UV needs no CPU rewrite. Its Latte pixel program uses
base-level normalized sampling and the selected point/linear sampler. An original
scalar Latte encoder implements the admitted combiner chain using nine GPRs and
bounded shader storage. Pixel uniforms carry stage constants; explicit byte
quantization and floor division preserve the software stage boundaries.
CPU cache flushes and texture invalidation precede drawing; retirement precedes
reuse/free. Failed waits quarantine GPU allocations until graphics shutdown.
Complete software preflight and exact GPU readback precede guest commit.
GX2 receives explicitly translated logic/comparison enums and the effective
channel mask. Alpha thresholds match the shader's byte normalization, including
the 255 endpoint; the API model checks both clamped and unclamped exports.

Mipmaps, mixed min/mag filters, projective/perspective coordinates, DOT3,
combiner buffers, resource caching and depth/blend/stencil remain unsupported. Physical sampling parity
and useful GPU performance remain unverified. The full-target byte comparison
stays exact, including for bilinear draws; differing hardware rounding rejects
the list without guest publication or software fallback.

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
