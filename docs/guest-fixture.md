# Controlled NCCH guest execution

This original, redistributable ARM fixture is the first executable guest pipeline:
NCCH inspection and hash checks → checked segment extraction → original generated
C → portable guest loader/SVC runtime → raw TV/GamePad framebuffer presentation.
This fixture uses an explicit harness I/O contract; it is not
a stock 3DS graphical application and does not implement HID, GSP or PICA.

## Reproduce from source

Use Python 3.12+, native GCC for desktop execution, and the installed official
devkitPPC/WUT SDK for console builds. Run from the complete source root:

```sh
python3 -m host make-fixture --output "artifacts/original guest"
python3 -m host run-fixture "artifacts/original guest/ctr2wup-guest.cxi" --output "artifacts/guest AOT" --frames 2 --buttons 1 --touch --touch-x 100 --touch-y 80
python3 -m host run-fixture "artifacts/original guest/ctr2wup-guest.cxi" --output "artifacts/guest interpreted" --frames 2 --buttons 1 --touch --touch-x 100 --touch-y 80 --mode interpreter
python3 -m host build "artifacts/original guest/ctr2wup-guest.cxi" --target guest-fixture --output "artifacts/guest console"
```

Each output directory must be new. Failure preserves compiler/execution logs and
removes incomplete staging. `run-fixture` writes real guest-generated `top.ppm`
(400×240), `bottom.ppm` (320×240), generated C, the native runner, logs and a JSON
report. The installed wheel includes the bounded translator, but C compilation
requires the full source; set `CTR2WUP_PROJECT_ROOT` to that source root.

`tools/generate-guest.py` deliberately regenerates the tracked original fixture
sources after fixture changes. Console builds compare these sources against the
inspected input and reject stale results. Setup does not regenerate source.
Only the exact original code, segment layout and program ID are execution-enabled.
Unknown commercial/homebrew profiles fail before publishing an output.

## Guest loading and I/O contract

The unsigned, decrypted NCCH v2 has program ID `000400000ff00f02`, 340 bytes of ARM
text at `0x00100000`, one 4 KiB data page at `0x08000000`, and 691,200 bytes of BSS
for framebuffers. Text is read/execute, rodata is read-only, data/BSS and stack/TLS
are read/write. Padding/BSS, the 16 KiB stack below `0x10000000`, and the TLS page
at `0x1ff82000` are zeroed. Initial SP is `0x10000000`; entry uses user ARM mode.
No guest pointer becomes a host pointer. Allocation is bounded to 64 MiB; page
alignment, descriptor sizes, permissions, overlaps and wraparound are checked.
The general loader supports BSS immediately after non-page-aligned initialized
DATA. TLS has user read-only CP15 access; [services](services.md) defines the
standard transport. This fixture still uses harness input, not HID/GSP.

| Data-page offset | Harness/guest meaning |
|---|---|
| `0` | Host supplies buttons; bit 0 changes the top framebuffer color |
| `4`, `8`, `12` | Host supplies touch X, Y and active flag; guest checks bounds |
| `16` | Guest increments completed-frame count after writing both buffers |
| `20` | Host requests exit; guest calls ExitProcess at its next loop entry |
| `32` | Original debug message for OutputDebugString |

Top pixels begin at `0x08001000`; bottom pixels follow 400×240 RGBA8 pixels.
The ARM program fills both images with STR loops, writes a white touch pixel,
increments its frame count, sleeps for 1 ms and repeats. Presentation reads
little-endian pixels through checked mappings and converts them to the host's
RGBA representation. These addresses are a fixture contract, not emulated 3DS
shared-memory/HID/GSP addresses. No GPU draw commands are executed.

## Runtime and validation

The fixture uses ExitProcess, SleepThread, OutputDebugString and checked
instruction budgets. The shared runtime also implements the thread/object/
service interfaces documented in [CPU](cpu.md) and [services](services.md).
Unknown SVCs fault visibly. Virtual time is monotonic and ticks use268111856 Hz.

Two controlled frames with button1/touch(100,80) execute1,382,534 instructions,
zero fallback and four SVCs before exit. Interpreter/original/external AOT checks
compare every register, loaded byte and framebuffer. Top RGB is `10 bb 22`;
bottom is `77 22 bb` with a white touch pixel. The generator binds fixture identity;
no commercial profile can be enabled by renaming an input.

The optional native target starts in guest mode. Its host controls show on-screen;
Plus restarts, B requests guest exit and Minus exits the application. Input is
sampled between bounded50000-instruction slices. Full ARM-written frames span
several display iterations; this correctness fixture does not predict game speed.
Hardware procedures live in the downloaded diagnostic package.
