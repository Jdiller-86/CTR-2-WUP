# GSP command queue and CPU-backed GX transfers (0.17.0)

This M3/M4 foundation lets ordinary libctru GX calls clear/copy/convert buffers
through the standard shared command ring. It supplies no PICA draw renderer or
native GX2 transfer acceleration. No game patch or commercial data is involved.

## Supported boundary

Client ID0 uses the retained 4 KiB relay page, 15 32-byte command slots at 0x820
and the header at 0x800. `TriggerCmdReqQueue` requires the owning registered GSP
session and GPU right. Commands commit synchronously in order, advancing index/
count only after successful execution. A failed command and its later entries
remain pending, with a visible guest fault; any successful prefix stays committed.
Documented stop/should-stop states are retained, not silently erased.

| Command | Supported behavior | Current limits |
|---|---|---|
| DMA (0) | Staged byte copy and DMA completion | Nonempty, at most 4 MiB |
| Memory fill (2) | 16/24/32-bit LE repeating pattern, either/both banks | Trigger required; 8-byte endpoints; whole patterns; at most 4 MiB per bank |
| Display transfer (3) | All five display formats, linear/tiled/same layout, vertical flip | Equal dimensions in 1..1024; tiled multiples of 8; no scaling/crop/raw-display copy |
| Texture copy (4) | Raw copy with independent 16-byte-unit line widths/gaps | Flags8, lengths in whole 16-byte units; bounded spans; gaps preserved |
| Cache regions (5) | Checked coherent CPU-backed ranges and barrier | No asynchronous GPU/DSP cache model |
| PICA command list (1) | Visible unsupported fault | Shaders/TEV/draw execution remains unfinished |

DMA accepts the virtual addresses defined by the GSP command wire. This is not
a general physical/VRAM translator. All source/destination spans, permissions,
alignment, arithmetic and allocations are checked before each command changes
bytes. Staging makes guest overlap safe. Device output may not alias relay backing,
including through another mapping. Bulk byte APIs validate complete ranges across
adjacent mappings and update exclusive/code generations, including executable
storage aliases; caller-owned host buffers cannot alias guest storage.

PSC0/PSC1/PPF/DMA completions use the standard relay interrupt ring. A two-bank fill
sends one PSC0, following the pinned Azahar behavior; Zakuro's two-IRQ variant is
not assumed equivalent. Full rings retain drop/error accounting and do not signal
an event for an interrupt that was not enqueued. CPU-backed timing is synchronous;
actual hardware latency, cache/coherency and scheduling accuracy remain unverified.

## Original application and verification

`tests/homebrew/transfer.c` uses stock CRT/libctru GX APIs. It checks both/single
fill banks, raw copy with asymmetric gaps and untouched gap bytes, cache regions,
DMA bytes, tiled RGBA8 -> linear RGB565 conversion, and green/red/green top with
blue bottom images. It waits through standard PSC/PPF/DMA/VBlank events, then
unmounts/exits through ordinary library code.

Its checked AOT run exits after **1,487,784 instructions**, **zero fallback**,
**16 GX operations** (3 DMA, 8 fill, 3 display, 1 texture copy, 1 cache), and exact
three-frame pixel checks. Three deliberately unavailable startup services remain
recorded. C normal/sanitizer checks cover failure atomicity, permissions, aliases,
generations, queue wrap/stop/prefix behavior, malformed headers and full interrupts.
The image matrix compares **400 original vectors** across C/Python and pinned
external color/Morton primitives; emulated big-endian PPC hashes match.
The external full transfer engine and physical Wii U were not executed by those
reference comparisons. Required checks and WUT builds are recorded separately.

## Optional console round

The 0.17.0 kit places `ctr2wup-libctru-transfer.wuhb` in `sd-card/wiiu/apps/`.
Copy the contents of `sd-card` to SD root and launch **CTR2WUP GX Transfers 0.17.0**
in Aroma. This test needs **no ROM, key or extra blob**.

1. Watch the TV change green/red/green with a blue GamePad image. The guide asks
   **R** for the observed picture or **L** for a problem. Do not confirm a picture
   you did not observe; faults/skips stay distinct.
2. The app checks the guest's own GX PASS and exit automatically. Press **Minus**
   when complete and separately report whether it returned to Menu/launcher.

Results are under **`sd:/ctr2wup/`**, named
**`result-compiled-gx-transfer-0.17.0-*.txt`**, plus any full trace. They include
operation counts and the last GX command/fault. ZIP those TXT files together and
attach the ZIP to a console-result issue or the development conversation. Preserve
existing saves/reports. Completed long native/input guides need not be repeated.
Actual SD/lifecycle/pictures and console timings remain pending until that round.
