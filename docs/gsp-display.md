# Standard direct GSP framebuffer display (0.10.0)

The shared runtime advertises `gsp::Gpu`. This original implementation supports
single-process direct framebuffer submission through checked TLS IPC:

| Request | Checked behavior |
|---|---|
| AcquireRight `00160042` | flags0, one copy descriptor0 and current-process pseudo-handle `ffff8001`; acquires the local process's display right |
| ReleaseRight `00170000` | Releases the acquired right |
| SetLcdForceBlack `000b0040` | flags0/1; actually controls decoded output |
| SetBufferSwap `00050200` | screen0/1 and seven framebuffer words; validates source range before changing state |

Replies have one normal result word and the requested command ID, e.g.
`00050040`/result0 for SetBufferSwap. Duplicate session handles retain ownership;
closing the last GSP session drops the right. Descriptors remain for display after
release, until guest close or a later submission. Unsupported commands and
malformed/unsupported modes fault visibly, with endpoint/header recorded. Invalid
source memory records a precise memory fault before any display-state change.
These are scoped HLE policies, not retail error-code/timing conformance.

Framebuffers are fixed 2D top 400x240/bottom 320x240. Active index and display-select
must be equal 0/1, the unknown word 0, and the secondary address 0 or equal to primary.
Supported mode bits are formats 0..4, top 2D bit 6 and local-memory bit 8. Top requires
bit 6; bottom must clear it. Stereo, wide800, VRAM bit 9 and other mode bits fail.
Column stride must contain all 240 pixels and be at most 4096 bytes; padding is
permitted. The reader uses `base + x*stride + (239-y)*bytes_per_pixel` and checks
the entire source extent, including adjacent mappings, before writing host output.
It revalidates on every presentation, so freed/reprotected backing fails safely.
No guest address becomes a host pointer and no native-endian pixel cast is used.

The five formats are ABGR-byte RGBA8, BGR8, little-endian RGB565, RGB5A1 and RGBA4.
Channels expand by bit replication; alpha is preserved. Host output is numeric
`RRGGBBAA`, which the existing GX2 copy backend writes as explicit RGBA bytes.
Force-black output is opaque black. Tests use independent golden channels,
four distinct corners, padded strides, split mappings, overflow/permission faults
and unchanged output on failed preflight. CPU cache/GX2 surface synchronization
remains in the native presentation backend; this is not PICA cache emulation.

The original ARM program in `host/gsp_fixture.py` acquires SRV/HID/GSP, waits for
standard HID input, writes RGB565 CPU framebuffers and submits both screens.
A makes the top green, release makes it blue. Valid touch makes the bottom
magenta with one white pixel; release makes it cyan. The top's corners are white,
red, green and blue (top-left, top-right, bottom-left, bottom-right). Two frames
and cleanup execute 1382865 instructions and 27 SVCs, with zero fallback and
matching full state/memory through interpreter/original/external AOT. The fixture
also checks unsigned ARM LDRH/STRH with immediate preindexing and no writeback;
other ARM halfword forms remain unsupported in the bounded original backends.

`make -C tests/gsp check sanitize` runs 362 API assertions and the original
interpreter/AOT display self-test. `tools/validate-upstream.py --gsp-api --output
artifacts/my-display-checks` executes all nine external probes and writes
`gsp/frame-{1,2}-{top,bottom}.ppm` reference images. The external display test has
3603 assertions when writing all four images. Host, sanitizer, Clang and emulated
PPC results establish portable behavior, not Wii U execution or usable speed.

All three Wii U builds embed `gsp_selftest`; native mode displays
`HID/GSP RGB565 DISPLAY PASS CODE 0`. Y cycles native → original custom-I/O ARM
fixture → standard HID/GSP fixture. The GSP fixture uses original AOT in all
builds; v0.11 replaces the live direct guest with the shared-queue variant
described in `gsp-queues.md`. The existing external-AOT build still uses the actual external lifter
for its custom-I/O fixture. External HID/GSP execution is separately tested on
desktop/emulated PPC. The GSP status overlay is in the middle of the top image,
leaving all four corner markers visible. Plus resets both graphics guests, B
requests the current graphics guest's exit and Minus exits the application.
Input latency includes bounded 50,000-instruction host slices and complete CPU
frame fills. SD logs contain swaps, state, fallback, fault and shutdown outcomes.

Interface facts were checked against libctru
`9b55eda44cf80b971503991e0c78f9bc8fe50425`, `gspgpu.h`, `gspgpu.c`, `gfx.c`,
and the audited Zakuro display/format descriptions. No upstream implementation
was copied. Version 0.11 adds interrupt relay registration, shared framebuffer
queues and diagnostic VBlank; see `gsp-queues.md`. GX command queues,
DMA/display transfers, VRAM/physical-address translation,
PICA registers/draws/shaders/textures/TEV and graphics thread scheduling are
absent. Stock libctru graphics initialization needs several of these and is not
claimed to boot. This bridge is a reusable display foundation for the injector.

The next planned acceptance gate is physical Wii U input/display/stability and
the selected Mario revision's private executable intake. Without that evidence,
M3/M4/M5, commercial conversion, installer acceptance and the GUI remain open.

## Native display cache

Version 0.25.0 retains the previous original 400×240/320×240 CPU image for each
TV/GamePad destination. The immutable nearest-neighbor plan and exclusive output
ownership are required. Cold use, destination/pitch/capacity changes, guest
restart or foreground reacquisition force a complete rebuild/publication,
including black borders, pitch padding and allocation tail. Unchanged input
uses exact equality and needs no CPU image write or publication. A warm change
maps each source row's first/last changed columns into output spans, scales the
span once and copies repeated rows; dirty output rows coalesce into full-pitch
publication ranges. Unsampled downscale changes update history without output.

All returned ranges still pass through `DCFlushRange` and
`GX2Invalidate(CPU_TEXTURE)`. `GX2DrawDone` precedes CPU writes; both surfaces
still receive `GX2CopySurface` and normal WHB presentation every frame. The guide
continues to require a submitted draw and a fresh positive input. This cache does
not implement guest PICA resource coherency or native shader translation.

The portable cache bounds sources to 1,048,576 pixels, validates capacities,
rejects source/history/output overlap and address wrap before writes, and
preserves destination/history/damage on invalid arguments. Initialization failure
aborts visibly; it does not silently select a different renderer. Native source
history uses 691,200 bytes plus row ranges (12,480 bytes at the measured output
sizes with 32-bit `size_t`), cache structs and allocator overhead. Logs report
actual owned history/range bytes. There is no additional full-size output image.

`make -C tests/report check` compares all output bytes against an independent
integer nearest-neighbor reference: full-screen/unchanged/sparse changes,
downsampling, odd dimensions, borders/padding/tails, rebind/invalidation and
failure atomicity. `python3 tools/benchmark-display.py --cc gcc --output
artifacts/display-benchmark` compares six original 60-frame workloads against
the retained uncached scaler with exact bytes. Static content avoids 98.3% of
requested publication bytes; small changing counters on TV with static GamePad
avoid 90.2% combined. Full-screen motion has no byte saving. Host CPU timing
excludes generated-input work, comparison, publication APIs and GPU copies;
no timing threshold is used. Hosted GCC/Clang repeat the comparison.

Native `scale` and `publish` phases are separate. Compare their combined totals
and call counts to the completed 0.24.1 `scale` phase, which covered both.
`PUBLISH` trace records and `Scale cache` result lines expose per-screen full,
reused, scaled-pixel, requested-byte, range and heap counters. The
[focused guide](console-retest.md) covers picture freshness, Plus restart and
fresh launch/Minus. Physical correctness and speed for 0.25.0 remain pending;
prior scoped results do not automatically validate a changed display path.
