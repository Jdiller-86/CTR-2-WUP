# Checked external AOT integration

The pinned external lifter generates C that links statically against an original
CTR2WUP ABI/runtime on desktop and Wii U. Its desktop runtime is not a Wii U
dependency. Commercial conversion stays gated by exact supported profiles.

## Reproduction

```sh
bash tools/install-translator.sh
python3 -m host make-fixture --output artifacts/original-input
python3 -m host translate artifacts/original-input/ctr2wup-guest.cxi --output artifacts/lifted-code
python3 -m host run-fixture artifacts/original-input/ctr2wup-guest.cxi --backend upstream --output artifacts/lifted-frames
python3 -m host build artifacts/original-input/ctr2wup-guest.cxi --target guest-fixture --backend upstream --output artifacts/lifted-console
make upstream-check
python3 tools/validate-upstream.py --sanitize --output artifacts/upstream-check
```

Windows uses `tools/install-translator-windows.ps1`, official Rust/rustup/Git and
native GCC. The receipt binds a clean audited source pin, original driver and
binary hashes. Source/header/driver changes require rebuilding or regeneration.
`CTR2WUP_LIFTER_SOURCE` selects the clean pinned checkout;
`CTR2WUP_GENERATOR` selects its driver with the corresponding receipt;
`CTR2WUP_LIFTER` selects the external analysis executable. This is local build
provenance, not authentication of an untrusted binary.

The driver uses upstream public discovery/generation APIs and exposes every
discovered instruction as a resume point. It discards upstream `recomp.h`.
The adapter removes pinned-emitter register-local/page-table/budget optimizations
and inserts an original instruction boundary; unexpected grammar fails the build.
The replacement header implements interface facts under MIT. The lifter source/
executable remain external; incomplete upstream permission/copyright notices
prevent bundling that implementation.

## Runtime contract

- Registers/guest addresses are `uint32_t`. Every access takes checked
  little-endian callbacks; page pointers/span bypasses are unused. No 4 GiB
  host reservation is needed.
- Attach checks ABI 4, generation 3, entry ordering and FNV code-piece hashes
  against executable mappings. CRO/module attachment is rejected.
- State is visible at every instruction. Exact budgets resume mid-function;
  SVCs unwind to the shared scheduler for sleep/events/exit. TLS refreshes from
  the current CPU at every execution entry.
- Callback faults unwind immediately through bounded `setjmp`/`longjmp`, retaining
  instruction PC/address. Later effects cannot execute. Multiple transfers may
  retain earlier words; abort bookkeeping restores an emitter's early LDM base.
  The external counter includes an attempted faulting instruction.
- Executable mapping/protection changes and writes through any alias invalidate
  snapshots. Ordinary data mapping preserves them. Writes/context switches
  invalidate the single emulated CPU's exclusive monitor.
- Missing blocks and emitted interpreter requests fail by default. Explicit C
  attachment fallback uses only the bounded interpreter and records its count.
- Native guest call depth is bounded to 64; deeper calls unwind to dispatch.
  A 64-dispatch no-progress guard prevents empty/unwinding blocks from consuming
  unlimited host time without guest budget progress.

Thread contexts preserve CPU/TLS/raw VFP/FPSCR state. Checked ARMv6 DMB/DSB,
media/SMLAxy and scalar FP word transport support ordinary compiled startup;
unsupported CP15/cache operations, including emitter NOPs, fail visibly.
[CPU](cpu.md), [services](services.md), [memory](guest-memory.md) and
[compiled homebrew](compiled-homebrew.md) define shared behavior.

## Floating-point limits

Scalar single/double assembly is endian-safe. Basic default-control arithmetic,
moves and comparisons have original reference probes. Nondefault arithmetic
controls/vectors and incomplete NaN/denormal/exception/rounding/conversion cases
remain unsupported or unverified; absent helpers cause compile failure.
Builds disable contraction/fast math. Integer work may retain nondefault FPSCR.

VPUSH/VPOP and exercised VLDM/VSTM are raw bit transport under CRT flush/default-
NaN controls. Ranges/counts are checked before effects; payload bits and FPSCR
are preserved. This enables checked compiled calls without claiming full VFP.

## Validation

`tools/validate-upstream.py` compares original framebuffer, TLS/services, heap,
synchronization, HID/GSP/relay, stack, thread, media and FP-context programs.
Optional `--service-api`, `--heap-api`, `--kernel-api`, `--hid-api`, `--gsp-api`,
`--relay-api`, `--fs-api`, `--apt-api`, `--thread-api` and `--multiple-api` add focused
malformed-input/ownership checks. Complete CPU/mapped/shared bytes, exact pixels,
faults and counters are compared across backends at uneven instruction budgets.

GCC/Clang sanitizer, native Windows and emulated big-endian PowerPC checks
establish their stated execution behavior. QEMU Linux is not Wii U hardware,
and tiny synthetic timings do not predict useful game performance. Optional
Unicorn ARM1176 checks provide an independent instruction reference.
