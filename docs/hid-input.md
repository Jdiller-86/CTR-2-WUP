# Standard HID input subset (0.9.0)

The reusable runtime advertises `hid:USER` through SRV. GetHandles request
`000a0000` replies with header `000a0047`, result0, copy descriptor `14000000`
and six typed handles: one shared page and Pad0/Pad1/accelerometer/gyroscope/debug
events. The latter three events have no producer. Motion enable/disable and all
other HID commands fail visibly. `hid:SPVR`, IRRST and New-family input are absent.

The device owns six internal handles for its lifetime; requests return independent
copies. Initialization and copy exhaustion roll back allocations. The shared page
has read-only guest rights; its mapping, copies and waiters obey the checked kernel
lifetime rules. Closing a returned handle cannot destroy the device's backing.

`ctr_hid_update` publishes Old-family physical button bits 0..11, signed Circle Pad
axes in -156..156, and valid touch coordinates in 320x240. Circle Pad direction
bits 28..31 use a radius40 threshold and diagonal sectors. Touch bit20 is derived
by the client from the touch entry, as in the public interface. ZL/ZR, C-stick,
accelerometer, gyro, raw analog calibration and slider are unsupported. Invalid
buttons, axes, touched coordinates and nonincreasing timestamps cause no update.

The eight-entry little-endian pad ring has index at `10`, current buttons at `1c`
and 16-byte entries from `28`: held, added, removed, packed signed XY. The touch
index is at `b8`; eight-byte entries from `c8` contain packed unsigned XY and
validity. Both indexes advance together. On index0 wrap, current/previous reset
timestamps at `0`/`8` and `a8`/`b0` use the 268111856 Hz guest tick scale. Pad0 and
Pad1 one-shot events signal after publication. The single guest runs on the same
host thread, so publication has no concurrent partial reader. Multi-thread device
timing and physical 3DS equivalence require further evidence.

The original ARM consumer in `host/hid_fixture.py` reads TLS, registers SRV,
acquires HID handles, maps the page read-only, waits for Pad0, reads the standard
rings and writes a diagnostic snapshot. Two controlled updates followed by
unmap/handle cleanup/exit execute 185 instructions and 18 SVCs. Interpreter,
original AOT and actual external AOT match registers, mapped memory, shared bytes,
object references and event state at uneven instruction budgets, with zero fallback.
The external comparison has 858 assertions; 336 API assertions check wire replies,
read-only rights, transitions, wrap timestamps, validation and resource rollback.
GCC/sanitizer, Clang and emulated big-endian PPC results do not prove console input.

All three Wii U builds embed `hid_selftest` and a separate live original-AOT ARM
consumer. VPAD buttons map to physical 3DS bits, the left stick scales to ±156,
and calibrated touch passes through the checked bottom-screen viewport. In native
mode, `ARM HID`, `ARM CIRCLE`, `ARM TOUCH` and sample counts show values read by ARM
through shared memory, rather than direct host input. SD logs contain
`HID live state=...` and `hid_live_shutdown`. Diagnostic Y/Plus/Minus remain host
controls; Minus exits before publishing its press. The right stick is displayed
by native diagnostics but is not exposed as a New-family C-stick.

Reproduce with `make -C tests/hid check sanitize`, `tools/generate-hid-fixture.py`
and `tools/validate-upstream.py --hid-api --output artifacts/my-hid-checks`.
Interface facts were checked against devkitPro/libctru commit
`9b55eda44cf80b971503991e0c78f9bc8fe50425`, `services/hid.h`, `services/hid.c`
and IPC descriptor definitions. The implementation and fixture are original.
This does not claim conventional graphical homebrew boot or Mario compatibility.
