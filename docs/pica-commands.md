# PICA command front end

The original front end processes a small **no-draw configuration** command list
through the standard GSP queue. It stores raw viewport/scissor register values,
applies byte masks and recognizes the finalization emitted by libctru. This is a
configuration boundary. It produces no pixels, runs no shader and submits no GX2 draw.

The scene adapter also adapts [bounded compiled guest draws](guest-pica-draws.md)
through the standard queue. The configuration-only API below remains unchanged;
the separate scene/guest adapter handles its explicitly defined shader/target subset.

## Wire inspection

`renderer.pica.decode_commands` and `runtime/core/ctr_pica.c` inspect explicit
little-endian bytes. A record contains its first parameter, header, extra
parameters and padding to an eight-byte boundary. The header contains:

| Bits | Meaning |
|---|---|
| 0–15 | Identifier; this implementation accepts wire IDs `0x000–0x3ff` |
| 16–19 | Four byte-enable bits |
| 20–27 | Extra parameter count; one record has 1–256 parameters |
| 28–30 | Unused; unsupported encodings are rejected |
| 31 | Increment register for each parameter, otherwise repeat it |

Inspection supports the wire range without claiming those registers can execute.
Padding is skipped and never becomes a write. Nonzero padding tolerance is our
inspection contract; the audited libctru producer writes zero padding. It is not
physical hardware evidence. Native/host budgets are at most 4 MiB and 65,536
writes; callers may choose smaller budgets. Range overflow, truncated records,
unused header bits and exceeded budgets fail visibly.

Facts were checked against [GBATEK's command-list description](https://problemkaputt.de/gbatek-3ds-gpu-internal-registers-command-lists.htm)
and clean libctru commit `9b55eda44cf80b971503991e0c78f9bc8fe50425`,
`gpu.h`, `gpu.c` and `registers.h`. Its builder splits API arrays into records of
at most 256 parameters. Its implementation is compiled only into a separate
reference test binary; no emulator or external GPU implementation is embedded
in the native front end.

## Supported execution

`ctr_pica_execute` starts with a software snapshot initialized to zero; this does
not model hardware reset values. Its supported ordinary registers are:

| IDs | Raw configuration |
|---|---|
| `0x41–0x44` | Viewport width/inverse width/height/inverse height |
| `0x65–0x67` | Scissor mode/position/dimensions |
| `0x68` | Viewport origin |

The stored words retain byte-mask semantics. Numeric float decoding, clipping
and raster behavior are not implemented. A submitted list must have a length
divisible by sixteen bytes and end with one or two single-parameter, full-mask
`FINALIZE` (`0x10`) writes of `0x12345678`, matching `GPUCMD_Split` alignment.
Finalization is handled as a completion marker and never stored as an ordinary
register. Configuration after a marker, missing/invalid markers, streamed marker
parameters and more than two markers fail.

All other registers fail, including with a zero byte mask. Draw triggers,
shader/uniform/table uploads, command jumps, caches, textures and render-target
state are not treated as generic successful register writes.

The whole list and counters are checked before committing any register change.
Input/state/output/report aliases fail before mutation. The commit pass cannot
change unread input. This avoids a 4 KiB automatic state copy; the current
devkitPPC compiler reports a 176-byte `ctr_pica_execute` stack frame. That is a
compiler observation, not a measurement of the full Wii U call stack.

## Standard guest queue

GX command 1 stages the checked guest command-buffer bytes before processing.
Its address must be eight-byte aligned; length must be nonzero, a multiple of
sixteen and within the bounds above. Unimplemented GSP state flags and nonzero
reserved words fail. The cache-request bit uses the existing coherent CPU-backed
memory contract; asynchronous GPU coherency remains unfinished.

A successful no-draw list emits one synchronous P3D completion (IRQ 5). The
counter of tail-marker writes is separate from that command completion. This
does not model physical interrupt timing or assert that hardware emits the same
interrupt count. Failure preserves the pending queue entry, subsequent work and
PICA register snapshot. Successful earlier queue entries remain committed.

The TXT report records completed lists, writes, markers and the last parser's
result/register/offset. Guest-memory failures retain the existing memory-fault
report. These observations are distinct from picture/launcher confirmations.

## Validation

- `make check` includes the PICA C suite and integrated GSP rejection/completion
  tests, normal and with ASan/UBSan. The standalone suite exercises capacity,
  aliases, counter overflow, the full wire-ID boundary and maximum write budget.
- `tools/validate-pica.py --sanitize --output <new-directory>` compares **353
  original cases**, including real pinned libctru construction, native inspection,
  the independent Python decoder and checked no-draw state expectations. All 353
  vector receipts match an emulated big-endian PowerPC Linux run; this does not
  establish physical Espresso execution.
- The original normal-CRT/libctru GX application sends a builder-produced
  finalized configuration list and waits for P3D. Its AOT execution exits after
  **1,489,172 instructions**, with **zero fallback**, 17 GX commands and the
  expected masked viewport word. Its green/red/green pictures still come from
  CPU-backed fills/transfers, not PICA draws.

The [compiled guest draw adapter](guest-pica-draws.md) executes its separately
bounded software subset. General shaders, textures/TEV, geometry, native GX2
translation and full renderer compatibility remain unsupported.
