# Renderer foundation

Version 0.23 adds [compiled original guest draws](guest-pica-draws.md) through
standard GSP, tracked owned resources and canonical tiled targets. The CPU
software MOV subset retains shader/state and commits target bytes/completion
transactionally. Broader shaders/fragment state, native GX2 translation and
physical renderer acceptance remain open. The sections below record earlier
foundation boundaries at their named versions.

The reviewed private title samples now identify shader arithmetic/control and
code-capacity work: three bounded embedded shader regions declare 472, 428 and
264 code words, exceeding the former 256-word limit; selected resources also
include a geometry entry. These are static resource findings, not traced active
game shaders. [The title matrix](first-title.md#current-feature-and-test-route-matrix)
connects them to shared scoped work; no commercial shader bytes are published.

Merged PR #98 expands vertex code capacity to 512 words with original high-entry,
upload and late rollback probes plus native storage/stack measurements. Required
platform checks pass; Project reconciliation remains pending. See
[the capacity boundary](software-scenes.md#vertex-code-capacity); arithmetic,
control flow, geometry and commercial execution still need separate evidence.

v0.22.0 executes the [first original software scenes](software-scenes.md)
through checked PICA-format loader/index state, fixed attributes and uniforms,
MOV/NOP/END shaders and a bounded opaque triangle raster path. Both index formats
match independently calculated pixels exactly. The focused native Scene Check
builds and records timings/automatic results, but physical picture/exit evidence
remains pending. Guest GSP draws and general shader/fragment/GX2 translation
remain unsupported; full M4 acceptance is open.

v0.21.0 extends the [resolved vertex decoder](vertex-attributes.md) with checked
unsigned 8-bit/16-bit little-endian indexed gathers. Duplicates and index order
are preserved. Complete referenced ranges, finite float values and full-span
aliases are checked before output changes. This supplies array preparation for
the future rendering boundary; it enables no PICA loader register, fixed
attribute, shader, primitive assembly or draw. Exhaustive index tests use
original bytes and explicitly retain physical renderer acceptance as pending.

`renderer.pica.decode_commands` inspects complete little-endian PICA command
records, byte masks, repeated/consecutive register writes, and eight-byte
padding. It bounds input, output count, and the 1,024 wire identifiers, and rejects
extended/reserved header encodings rather than guessing. Inspection returns
records; it does not mark those records supported for execution.

This is original code. The documented basic command layout was checked against
the pinned Azahar command-header/processing behavior recorded in the dependency
audit; no Azahar implementation is copied or linked.

v0.18 adds the native [PICA command front end](pica-commands.md), explicit raw
viewport/scissor configuration and finalized standard queue processing. It is
verified against original pinned-libctru command construction and a compiled
guest; actual draws, shaders and GX2 submission remain unsupported.

`renderer.framebuffer` tests little-endian RGB565 decoding, RGB8 PPM export,
aspect-preserving viewport calculation, and border touch rejection. The native
Wii U backend has its own compiled display/input implementation; Python results
do not establish GX2 behavior.

v0.10 implements direct GSP framebuffer submission, rotated portable decoding of
all five linear display formats, and an original ARM HID-to-RGB565 display guest
in all Wii U builds. See `gsp-display.md` for scope and desktop reference images.
Console behavior remains pending. This does not execute PICA draws.

Pending: asynchronous GPU transfer/coherency, broader vertex and
geometry processing/shaders, texture sampling/mips/resource lifetime, lighting, TEV, broader guest draw
submission, caching, hardware/reference scene comparisons, and console timings.
The Python `execute_draw` inspection path remains explicitly unsupported; the
portable/native guest adapter implements only the documented software subset. Milestone 4 is incomplete.


v0.16.0 adds an original portable base-texture decoder in
`runtime/core/ctr_texture.c` and a Python reference in `renderer/texture.py`.
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
Format 6 follows Azahar's RG8 convention `[byte1, byte0, 0, 255]`; Zakuro's
`[0, byte1, byte0, 255]` differs and is explicitly excluded from that comparison.
External emulator code is test-only in separate checkouts, never copied into or
linked with the runtime/delivery. Actual PICA sampling/render scenes and GX2
resource/coherency/performance checks still need Wii U/reference evidence.


v0.17.0 implements a bounded standard GSP GX ring and CPU-backed DMA/fill/display
conversion/raw texture copy/cache checks. Equal-sized unscaled transfers support
all five display formats, layout/swizzling and flip. Stock libctru produces exact
three-frame outputs with zero fallback. PICA lists/draws remain unsupported; GPU
resource/sampling/TEV/shader/coherency work and actual console scene/timing proof
are still required. See [gx-transfers.md](gx-transfers.md).
