# Vertex attribute decoding

`runtime/core/ctr_vertex.c` supplies a bounded, original byte-stream decoder for
the array-backed vertex formats needed by the PICA rendering pipeline. This is a
portable preparation step. No PICA loader register, guest draw, shader or GX2
draw is enabled by adding this module.

## Supported contract

The caller resolves a layout into at most twelve attributes and twelve streams.
Each attribute identifies its stream, offset within a vertex, component count
and destination shader-input slot. Destination slots are unique and range from
0 to 15, so the layout expresses an explicit input permutation. A stream supplies
an owned byte buffer, its checked byte length, a first-vertex offset and a stride
of 1 to 255 bytes. The attribute's entire payload must fit within that stride.
Unaligned host buffers and component offsets are handled through byte loads;
the caller remains responsible for resolving actual PICA loader alignment and
padding rules before providing this explicit layout.

| Format ID | Source component | Conversion |
|---|---|---|
| 0 | Signed 8-bit byte | Exact, unnormalized float32 integer value |
| 1 | Unsigned 8-bit byte | Exact, unnormalized float32 integer value |
| 2 | Signed 16-bit little-endian short | Exact, unnormalized float32 integer value |
| 3 | Little-endian IEEE binary32 | Finite bit pattern preserved on the verified host |

Each attribute supplies one to four components. Missing components start at
`(0, 0, 0, 1)`, so a one-component attribute produces `(x, 0, 0, 1)` and an
explicit fourth component replaces the default W. Unconfigured input slots also
contain that deterministic vector but remain absent from the returned `enabled`
mask; they are not claimed as supplied or fixed PICA attributes.

PICA has **no unsigned-16 attribute format**. IDs outside 0–3, NaN/Infinity
encodings and incompatible host float representations fail visibly with
`unsupported`. Float32 is only the interchange representation here: the module
does not implement PICA float24 quantization, arithmetic or shader execution.
The tests include signed zero, finite limits, subnormals and values that would
need separate rounding for PICA float24. These are decoder checks, not hardware
numeric conformance evidence.

## Bounds and failure behavior

A call decodes at most 4,096 vertices; each supplied source span is limited to
4 MiB. The decoder checks every supplied stream, layout, destination capacity,
first/last-vertex arithmetic and every finite-float encoding before it changes
any output. A malformed later attribute or vertex therefore cannot publish a
valid prefix. The decoder allocates no heap memory and never follows a guest
address. The caller supplies owned, stable buffers.

Output may not overlap attribute descriptors, stream descriptors or any part of
a supplied source span, including an unused tail. Read-only streams may overlap
one another. Adjacent input/output spans are permitted. A null output with zero
capacity validates the same layout, bytes and float policy without publishing
vertices. A zero-vertex range validates the configuration without reading source
components or changing output.

Errors distinguish invalid arguments/layout, unsupported formats/values,
truncated source ranges, configured limits, insufficient capacity, aliases and
arithmetic overflow. The output remains unchanged on every error. This contract
does not interpret fixed attributes, retained input state,
multi-loader overwrite semantics or implicit padding IDs; those require their
own checked PICA-state adapter.

## Indexed gathering

`ctr_vertex_decode_indexed` uses the same resolved attribute/stream contract and
adds a caller-owned `ctr_vertex_indices` descriptor. Its format IDs are 0 for
unsigned 8-bit elements and 1 for unsigned 16-bit little-endian elements. A byte
offset locates the first index in the supplied span; unaligned index addresses
are decoded with byte loads. `first_index` and `vertex_count` select elements,
not byte offsets. Each element selects a source vertex independently in every
stream, preserving requested order and duplicates.

For example, indices `[2, 0, 2, 1]` produce source records 2, 0, 2 and 1, with
their input permutation and component defaults intact. Stream offsets resolve
the source vertex base. The API does not add `first_index` to a decoded index,
interpret PICA's vertex-offset register, or treat 0xff/0xffff as restart markers.
The future state adapter must resolve actual guest addresses and loader rules.

The decoder validates the entire selected index byte range and every referenced
attribute range before publication. It checks finite float encodings only in
referenced vertices: an unreferenced NaN/Infinity is never read and does not
reject an otherwise valid gather. A malformed final index or referenced float
leaves all output unchanged. Validation-only calls obey those same rules.

The same 4,096-output and 4 MiB-per-source limits apply to indices. Index
descriptors and complete supplied index byte spans, including unselected bytes,
must not overlap output. Read-only index and attribute source spans may overlap.
Zero output count validates descriptors/layout without reading index elements
or source components. Checked first/last-element, width and byte-offset
arithmetic rejects overflow before a load. The path allocates no heap memory
and enables no primitive assembly, PICA command, shader or draw.

## Origin and validation

The numeric format IDs and descriptor nibble packing were checked against
the pinned [libctru enums](https://github.com/devkitPro/libctru/blob/9b55eda44cf80b971503991e0c78f9bc8fe50425/libctru/include/3ds/gpu/enums.h)
at `9b55eda44cf80b971503991e0c78f9bc8fe50425`:
`GPU_ATTRIBFMT(i,n,f)` packs `(n-1)` above the two format bits. Register names
and the twelve loader banks are in the same revision's
[register definitions](https://github.com/devkitPro/libctru/blob/9b55eda44cf80b971503991e0c78f9bc8fe50425/libctru/include/3ds/gpu/registers.h).
Published register information is available at
[3dbrew](https://www.3dbrew.org/wiki/GPU/Internal_Registers).
The signed-short, missing-component and loader distinctions were also inspected
in [Azahar's reference loader](https://github.com/azahar-emu/azahar/blob/d409087405bc299b88ff35dd5b96b6e2698d5929/src/video_core/pica/vertex_loader.cpp)
at `d409087405bc299b88ff35dd5b96b6e2698d5929`. No emulator implementation is copied,
compiled, linked or distributed with the runtime. The C decoder and all test
data are original.

The unsigned byte/short index distinction and indexed-versus-nonindexed vertex
offset behavior were inspected in that same pinned reference's
[pipeline register definitions](https://github.com/azahar-emu/azahar/blob/d409087405bc299b88ff35dd5b96b6e2698d5929/src/video_core/pica/regs_pipeline.h)
and [draw input selection](https://github.com/azahar-emu/azahar/blob/d409087405bc299b88ff35dd5b96b6e2698d5929/src/video_core/pica/pica_core.cpp).
This decoder consumes explicit spans rather than implementing those registers.

Run `make -C tests/vertex check sanitize`. The suite checks every signed-short
value, all byte values, all sixteen format/component descriptor combinations,
lane defaults, shuffled input slots, twelve-stream layouts, nonzero first vertex,
unaligned views, exact finite-float bits, late rejection without partial output,
limits, range arithmetic and descriptor/source aliases. Indexed cases exercise
every unsigned byte/short value, including 0xff/0xffff, reordered/repeated
interleaved and twelve-stream inputs, nonzero first-index/byte offsets,
unreferenced nonfinite data, late atomic rejection and full index-span aliases.
The combined suite has **364,123 original assertions**. Root `make check-c` and
the portable Windows target include this suite, and the native Wii U source list
cross-compiles the module. Desktop/sanitizer passes and cross-compilation do not
establish Espresso execution or any rendered scene. Milestone M4 remains open.
