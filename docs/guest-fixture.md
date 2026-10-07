# Controlled NCCH guest execution (0.2.0)

This original, redistributable ARM fixture is the first executable guest pipeline:
NCCH inspection and hash checks → checked segment extraction → original generated
C → portable guest loader/SVC runtime → raw TV/GamePad framebuffer presentation.
Desktop execution and Wii U cross-compilation pass. Physical Wii U execution is
awaiting testing. This fixture uses an explicit harness I/O contract; it is not
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
Non-page-aligned data/BSS layouts are explicitly rejected for now. The TLS page
exists, but CP15 TLS access is not implemented.

| Data-page offset | Harness/guest meaning |
|---|---|
| `0` | Host supplies buttons; bit0 changes the top framebuffer color |
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

## Implemented SVC subset

One guest CPU/thread runs under instruction quotas; callers supply monotonic
virtual time in nanoseconds. No host thread is created for a guest thread.

| SVC | Implemented behavior/register contract |
|---|---|
| `03` ExitProcess | Guest enters exited state |
| `0a` SleepThread | `r0:r1` nanoseconds; zero yields, positive sleeps; negative/overflow faults |
| `17` CreateEvent | Reset type `r1`: OneShot0 or Sticky1; result `r0`, handle `r1`; Pulse unsupported |
| `18` SignalEvent | Handle `r0`, result `r0`; wakes the controlled waiter |
| `19` ClearEvent | Handle `r0`, result `r0` |
| `23` CloseHandle | Handle `r0`, result `r0`; retains objects owned by other handles/waiter |
| `24` WaitSynchronization1 | Handle `r0`, timeout `r2:r3`; zero polls, positive times out, negative waits indefinitely |
| `27` DuplicateHandle | Input handle `r1`, result `r0`, new handle `r1` |
| `28` GetSystemTick | Virtual time scaled to 268,111,856 Hz; low/high `r0:r1` |
| `3d` OutputDebugString | Checked pointer `r0`, length `r1`, maximum 256 bytes; success result `r0=0` |

There are 32 event objects and 64 generation-tagged handles. Stale/closed handles,
timeouts and exhaustion return explicit results. Unknown SVCs, including IPC
SendSyncRequest, fault visibly. Multi-thread scheduling, timers, memory-control
SVCs, IPC and FS/APT/CFG/HID/GSP services remain unimplemented. Interface facts
were checked against audited research sources and the public libctru SVC header;
their implementation was not copied.

## Console controls and expected evidence

Launch `ctr2wup-guest-fixture.wuhb` with a compatible Aroma setup or the RPX with
an RPX-capable launcher. It starts in guest mode. The native diagnostic also
contains this guest; Y toggles modes. Plus restarts the guest; B asks the guest
to exit; Minus exits the host application. A changes the next guest top color
and plays the native test tone. A calibrated touch changes the bottom color
and draws one white guest pixel. Hold A/touch through a completed guest frame.

The TV has a small host status overlay; the remaining image and GamePad image
come from guest ARM memory writes. Quotas allow 50,000 guest instructions per
display iteration, so a full guest frame spans multiple display iterations.
This deliberately bounded correctness test does not measure game performance.
Record both outputs, increasing guest frame count, successful color/touch/exit/
restart, and `sd:/ctr2wup/native-*.log`; follow `console-testing.md`.

## Verified desktop evidence

Two AOT frames with button1 and touch(100,80) execute 1,382,534 guest instructions,
zero fallback instructions, four SVCs and one debug message, then exit normally.
Top RGB is `10 bb 22`; bottom RGB is `77 22 bb`, with a white touch pixel.
Interpreter and AOT frames have identical SHA-256 values:

```text
top.ppm     267878f6f7665b30bf43465c48d9b55094e8fe7d8fc694585e1afe74c30bae81
bottom.ppm  6cadca972cf65a4842d8d8a73bd6a4b806bf1706a2c5b826cd136d21a371d06d
```

The original fixture SHA-256 is
`004d03a5a0eca2679f2efbf6f887590fbf51ed984649056e78702c43af968210`.
The separate executable revision is
`38820d9091dcf0eb863b5c74c04536c9e765aaf39f4719f017205298015a3472`.
Kernel/loader tests cover faults, events, sleeps, ticks and actual interpreter/AOT
guest state/memory matches, both normally and under ASan/UBSan. All hardware,
stock 3DS homebrew, commercial gameplay, saves and WUP claims remain open.
