# Base texture decoding

The original portable base-texture decoder is
`runtime/core/ctr_texture.c`, with a Python reference in `renderer/texture.py`.
All 14 PICA format IDs are implemented, including RGBA/RGB packed formats,
luminance/alpha, RG8/HILO, ETC1 and ETC1A4. Base dimensions are powers of two
in 8..1024; 8×8 Morton tiles decode into explicit row-major RGBA bytes without
an implicit display-origin flip. Little-endian source words do not depend on
host byte order. Compressed differential blocks and buffer ranges are validated
before output changes; invalid ETC2-style modes, truncation and overlap fail.
Mip tails, texture filtering/wrapping, caches, GPU allocation and draw submission
remain unfinished. No PICA renderer or commercial compatibility is enabled.

Golden layouts/selectors/alpha/clamping and failure atomicity have C normal/
sanitizer and Python checks. `tools/validate-textures.py` compares 42 original
images with the independent Python decoder and 39 with the clean audited
Zakuro texture module at `76f17bf76cd9ea238a94f37d389fe701ab05a673`.
Format 6 follows Azahar's RG8 convention `[byte1, byte0,0,255]`; Zakuro's
`[0, byte1, byte0,255]` differs and is explicitly excluded from that comparison.
External emulator code is test-only in separate checkouts, never copied into or
linked with the runtime/delivery. Actual PICA sampling/render scenes and GX2
resource/coherency/performance checks still need Wii U/reference evidence.
