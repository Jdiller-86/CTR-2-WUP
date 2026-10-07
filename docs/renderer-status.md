# Renderer foundation

`renderer.pica.decode_commands` inspects complete little-endian PICA command
records, byte masks, repeated/consecutive register writes, and eight-byte
padding. It bounds input, output count, and the register bank, and rejects
extended/reserved header encodings rather than guessing. Inspection returns
records; it does not mark those records supported for execution.

This is original code. The documented basic command layout was checked against
the pinned Azahar command-header/processing behavior recorded in the dependency
audit; no Azahar implementation is copied or linked.

`renderer.framebuffer` tests little-endian RGB565 decoding, RGB8 PPM export,
aspect-preserving viewport calculation, and border touch rejection. The native
Wii U backend has its own compiled display/input implementation; Python results
do not establish GX2 behavior.

Pending: actual framebuffer display transfers/rotation/coherency, vertex and
geometry processing, shaders, textures/ETC/swizzling, lighting, TEV, draw
submission, caching, trusted reference scene comparisons, and console timings.
`execute_draw` raises an explicit unsupported error. Milestone 4 is incomplete.
