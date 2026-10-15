# Display, GSP queues and transfers

The shared runtime advertises `gsp::Gpu` for one local client. It uses checked
TLS IPC and CPU-backed framebuffer memory. Software draws are documented
[separately](guest-pica-draws.md); this transport does not provide native GX2
shader translation, general VRAM mapping or asynchronous GPU coherency.

## Direct framebuffer submission

| Request | Checked behavior |
|---|---|
| AcquireRight `00160042` | flags 0, copy descriptor 0, pseudo-handle `ffff8001`; acquire local display right |
| ReleaseRight `00170000` | Release acquired right |
| SetLcdForceBlack `000b0040` | flags 0/1; control decoded output |
| SetBufferSwap `00050200` | screen 0/1 plus seven framebuffer words; validate before publication |

Replies contain the command ID and one result word. Duplicate session handles
retain ownership; final session close drops the right. Descriptors remain until
resubmission or process close. Invalid ranges and malformed/unsupported modes
fault before display state changes. These are scoped HLE policies, not retail
error-code or timing conformance.

Top is 400×240 and bottom 320×240. Active index and display-select must equal 0/1;
the unknown word is 0 and secondary address is 0 or primary. Formats 0..4, top 2D
bit 6 and local-memory bit 8 are supported. Top requires bit 6; bottom clears it.
Stereo, wide 800, VRAM bit 9 and other modes fail. Column stride contains 240 pixels
and is at most 4096 bytes. The rotated reader uses
`base + x*stride + (239-y)*bytes_per_pixel`, checking the complete source extent
on every presentation, including adjacent mappings and permission changes.

Formats are ABGR-byte RGBA8, BGR8, LE RGB565, RGB5A1 and RGBA4. Channels expand
by bit replication; alpha is preserved. Host output is numeric `RRGGBBAA`; the
native backend writes explicit RGBA bytes. Force-black is opaque black.

## Relay and VBlank

`RegisterInterruptRelayQueue` (`00130042`) requires AcquireRight, flags 1,
copy descriptor 0 and a live event. Reply `00130082` returns result 0, client 0 and
an owned RW 4096-byte shared-memory handle. Result 0 means HLE display is already
configured; hardware GPU-register initialization is unsupported.

The device retains the shared page/event independently of returned handles.
The owner session may unregister (`00140000`); final owner-session close drops
device references, while mapped pages remain valid. Other sessions cannot
unregister it. Slot 0 has index/count/error/reserved bytes at 0 and 52 IRQ bytes at
`0x0c`. Framebuffer headers at `0x200`/`0x240` use bit 0 for entry selection and
bit 8 for update. Other bits/modes fail; queue selection is independent of active
buffer index.

A due scheduling call validates both dirty descriptors and source extents before
either screen changes. Publication clears dirty bits, commits descriptors,
appends VBlank0/1 IRQs 2/3 and signals the retained event. Full IRQ rings retain
pending entries, set error and record drops. Invalid input preserves page/display.
Device writes invalidate exclusive state and executable aliases.

The diagnostic interval is 16,666,667 ns, not measured hardware timing. Late calls
coalesce missed intervals into one IRQ pair/latest update and record misses;
repeat timestamps cannot emit another pair. Overflow faults. Releasing rights
pauses processing without destroying registration.

## GX command ring

Client 0 uses 15×32-byte slots from `0x820` and a header at `0x800`.
`TriggerCmdReqQueue` requires the owning session/right. Commands commit
synchronously in order and advance index/count only on success. The failing
entry and later work remain pending; an already committed prefix survives.
Stop/should-stop state is retained.

| Command | Implemented boundary |
|---|---|
| DMA (0) | Staged byte copy; nonempty, at most 4 MiB |
| PICA list (1) | Finalized no-draw configuration plus the bounded [guest draw adapter](guest-pica-draws.md) |
| Fill (2) | LE16/24/32-bit pattern, either/both banks, trigger, 8-byte endpoints and whole patterns; at most 4 MiB/bank |
| Display transfer (3) | Five formats; linear/tiled/same layout and vertical flip; equal 1..1024 dimensions; tiled multiples of 8 |
| Texture copy (4) | Flags 8; whole 16-byte units; independent line widths/gaps; preserve gaps |
| Cache regions (5) | Checked coherent CPU-backed ranges/barrier |

DMA accepts the virtual addresses on its wire interface; this is not general
physical translation. Commands preflight spans, permissions, alignment, arithmetic
and allocations. Staging permits source/destination overlap. Output cannot alias
relay backing through any guest mapping. Complete-range memory APIs cover adjacent
mappings and invalidate exclusive/code generations; host buffers cannot alias
backing. Transfers do not scale/crop or implement asynchronous GPU/DSP caches.

PSC0/PSC1/PPF/DMA completions use the IRQ ring. Two-bank fill emits one PSC0;
full rings record drops and do not signal for IRQs that were not enqueued.

## Native display cache

The backend retains original 400×240/320×240 CPU images per destination with an
immutable nearest-neighbor plan and exclusive output ownership. Cold use,
destination/pitch/capacity change, restart and foreground reacquisition force
full rebuild, including black borders, padding and allocation tail. Exact
unchanged input needs no CPU write/publication. Changed source rows map their
first/last changed columns to output spans; repeated rows copy the scaled span.
Dirty output rows coalesce into full-pitch publication ranges. Unsampled downscale
changes update history without output.

`GX2DrawDone` precedes CPU writes. Publication uses `DCFlushRange` and
`GX2Invalidate(CPU_TEXTURE)`; both surfaces still receive `GX2CopySurface` and
normal WHB presentation every frame. Source/history/output overlap, wrap,
capacities and the 1,048,576-pixel limit are checked before writes. Failure
preserves destination/history/damage. Initialization failure is explicit.
Logs expose full/reused/scaled-pixel/requested-byte/range/heap counters and
separate scale/publication timing. Full-screen motion has no publication saving.

## Validation

```sh
make -C tests/gsp check sanitize
make -C tests/relay check sanitize
make -C tests/gx check sanitize
make -C tests/report check
python3 tools/validate-transfers.py --output artifacts/transfers
python3 tools/benchmark-display.py --cc gcc --output artifacts/display
```

Original ARM and libctru programs check complete state, IRQ/lifetime ownership,
exact frames, exclusives, partial queue commits and failure atomicity. Independent
pixel/format/layout references cover corners, padding, aliasing and split mappings.
Public interfaces follow pinned libctru
`9b55eda44cf80b971503991e0c78f9bc8fe50425`; no implementation was copied.
Host timings do not establish Wii U performance.
