# GSP shared framebuffer queues and VBlank relay (0.11.0)

This shared runtime increment uses standard GSP wire layouts to present the
original ARM graphics probe. It supports one local guest/client slot, two
screens, CPU framebuffer memory and a deterministic diagnostic clock. It does
not enable commercial conversion or stock libctru graphics initialization.

`RegisterInterruptRelayQueue` (`00130042`) accepts flags1, copy descriptor0 and
a live event handle, after AcquireRight. The successful `00130082` reply
contains result0, client ID0, copy descriptor0 and an owned read/write 4096-byte
shared-memory handle. Result0 means this HLE display is already configured;
the hardware-init result path and GPU register programming are unsupported.
Typed resource/stale-handle errors return a service result. Malformed wire
layouts and unsupported commands fault visibly. Duplicate registration fails.

The device retains the shared page and event independently of returned handles.
Mappings and waits also retain their objects. Closing the last copy of the
registering session or `UnregisterInterruptRelayQueue` (`00140000`) releases
the device references; an existing mapped page stays valid. Another session
cannot unregister the owner. Last-GSP-session close releases display rights.

The slot0 interrupt record has index/count/error/reserved bytes at offset0,
with 52 interrupt bytes at offset `0x0c`. Framebuffer headers are at `0x200`
and `0x240`: bit0 selects one of two seven-word entries, bit8 marks an update.
Queue entry selection is independent of the displayed buffer's active index.
The format/range checks and rotated decoders are described in `gsp-display.md`.
Other header bits and unsupported display modes are rejected.

At a due host scheduling entry, both dirty descriptors and readable source
extents are checked before either screen changes. Successful publication clears
only dirty bits, commits the latest pending descriptors, appends VBlank0/1 IRQs
2/3, and signals the retained event through the normal wait mechanism. Ring
wrap preserves pending entries. A full ring sets the error byte and records
dropped IRQs; it never overwrites pending interrupts. Invalid headers, source
permissions or modes leave the page and display unchanged and fault the guest.
Nonzero queued-GX headers at `0x800` fault rather than discard unimplemented work.

The interval is 16,666,667 ns. This is a diagnostic 60 Hz clock, not measured
3DS/Wii U timing. A late host call coalesces missed intervals into one IRQ pair
and the latest display update, while recording missed intervals. A repeated call
at the same time cannot deliver another pair. Deadline overflow faults. Releasing
display rights pauses processing; registration stays owned until unregistered.
Device writes invalidate the CPU exclusive monitor and executable aliases.

The original ARM probe (`host/relay_fixture.py`) maps the page, writes rotated
RGB565 screens, uses word LDREX/STREX to publish and drain queues, waits for the
relay, reads both interrupt IDs, then unregisters/unmaps/closes/exits. The same
instruction stream runs in interpreter, original AOT and the real pinned
external recompiler. The comparison deliberately inserts a second VBlank between
LDREX and STREX, ensuring a failed store retries without losing interrupts.
Two frames plus that interleaving execute 1,383,167 instructions and 34 SVCs
per path with zero fallback. The external comparison has 11,541 assertions. All state, mapped/shared bytes and pixel references
are compared. Generated original sources reproduce from the checked generator.

`make -C tests/relay check sanitize` passes 762 API assertions covering timing, atomic two-screen updates,
ring wrap/full cases, source failures, resource rollback, session ownership and
handle lifetime, alongside the original interpreter/AOT self-test.
`python3 tools/validate-upstream.py --relay-api --output artifacts/my-relay-check`
runs nine external probes plus relay API checks and exports four
`relay/frame-{1,2}-{top,bottom}.ppm` reference images. Compiler/target checks
include Clang and emulated big-endian PPC Linux; none is physical Wii U evidence.

All Wii U builds embed direct-display and relay self-tests. Y's standard HID/GSP
mode now runs the original queued guest. SD logs record relay registration,
VBlank, coalesced intervals, dropped IRQs, faults and clean shutdown.
See `console-testing.md` for physical checks and reference images.

Public interface facts were checked against pinned libctru
`9b55eda44cf80b971503991e0c78f9bc8fe50425` (`gspgpu.h/c`, `gfx.c`). No upstream
implementation was copied. Stock libctru additionally uses service threads,
LightEvent/address arbitration, barriers and hardware initialization paths that
this subset does not supply. GX commands, DMA/display transfers, physical/VRAM
translation, PICA rendering, DSP/audio and saves remain unimplemented. Wider
shared runtime development can continue without a ROM; exact Mario requirements
and physical acceptance still need private intake and console results.
