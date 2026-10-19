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
| Combiner | TEV0 replaces with Texture0 or multiplies it by constant binary primary color; later stages pass Previous |
| Fragment policy | Zero TEV operands/scales, opaque primary color, full writes, copy logic, no lighting/depth/stencil/blending |

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

Textures resolve entirely inside the stable geometry snapshot or one of at most
32 separate owned snapshots, with 4 MiB total additional source bytes. External
spans cannot overlap geometry or each other; partial geometry aliases and target
feedback fail. The [guest adapter](guest-pica-draws.md#resource-and-transaction-contract)
checks allocation identity and permissions before snapshotting.

## Native GX2 path

The native adapter uploads decoded RGBA to an owned pitched GX2 texture, reversing
rows so the original guest UV needs no CPU rewrite. Its Latte pixel program uses
base-level normalized sampling, the selected point/linear sampler and optional
primary-color multiplication.
CPU cache flushes and texture invalidation precede drawing; retirement precedes
reuse/free. Failed waits quarantine GPU allocations until graphics shutdown.
Complete software preflight and exact GPU readback precede guest commit.

Mipmaps, mixed min/mag filters, projective/perspective coordinates, general TEV,
resource caching and depth/blend remain unsupported. Physical sampling parity
and useful GPU performance remain unverified. The full-target byte comparison
stays exact, including for bilinear draws; differing hardware rounding rejects
the list without guest publication or software fallback.

## Validation

```sh
python tools/validate-textures.py --output artifacts/texture-reference-NEW
python tools/validate-textured-scenes.py --sanitize --output artifacts/textured-scene-reference-NEW
```

Independent original reference checks cover format decoding, exact scene pixels,
wrap/origin rules, modulation and transactional failures. The native API model
also captures actual submitted pixel-program bytes for independent decoding;
it does not emulate GPU execution. The optional clean Zakuro decoder comparison
uses pin `76f17bf76cd9ea238a94f37d389fe701ab05a673`. Format 6 follows RG8
`[byte1, byte0, 0, 255]`; Zakuro's differing channel convention is excluded.
External reference implementations remain test-only, outside delivered runtime code.
