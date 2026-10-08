# Checked external AOT integration

Version 0.3.0 uses the pinned external lifter's **C generation**, then statically
links the translated executable with an original CTR2WUP ABI/runtime on desktop
and Wii U. It no longer stops at analysis. The commercial conversion command
remains gated; this integration does not enable arbitrary game execution.

## Reproduction

```sh
bash tools/install-translator.sh
python3 -m host make-fixture --output artifacts/original-input
python3 -m host translate artifacts/original-input/ctr2wup-guest.cxi --output artifacts/lifted-code
python3 -m host run-fixture artifacts/original-input/ctr2wup-guest.cxi --backend upstream --output artifacts/lifted-frames
python3 -m host build artifacts/original-input/ctr2wup-guest.cxi --target guest-fixture --backend upstream --output artifacts/lifted-console
make upstream-check
python3 tools/validate-upstream.py --sanitize --output artifacts/my-upstream-sanitizers
```

On Windows use `tools/install-translator-windows.ps1` after installing official
Rust/rustup and Git; native GCC is required for execution. The generation receipt
binds the clean audited source pin, current original driver and binary hashes.
Source/header/driver changes require rebuilding or regeneration. Configure
`CTR2WUP_GENERATOR` only with a corresponding receipt; this is local build
provenance checking, not cryptographic authentication of an untrusted tool.
For a source archive extracted elsewhere, `CTR2WUP_LIFTER_SOURCE` selects the
clean pinned checkout and `CTR2WUP_GENERATOR` selects the existing external driver.
For external analysis from that location, `CTR2WUP_LIFTER` selects the audited
`3dsrecomp` executable.

The driver calls upstream public discovery/generation APIs and exposes every
discovered instruction as a resume point. It discards the emitted upstream
`recomp.h`. The Python adapter removes register-local/page-table/budget
optimizations from the pinned emitter's C and inserts an original instruction
boundary. Unexpected grammar fails the build. The replacement header is an
original implementation of interface facts, under this project's MIT license.
The lifter executable/source are external and are not bundled. Its missing
complete permission/copyright notice still blocks bundling its implementation.

## Runtime contract

v0.6.0 refreshes user read-only TLS from the CPU context on every execution entry.
Its original 89-instruction SRV/CFG startup probe matches interpreter/original/
external AOT state and memory (297 boundary assertions). It also rejects
unsupported CP15 cache operations and DMB/DSB/ISB, including emitter no-ops;
35 dedicated assertions cover executed faults and condition skips.
`--service-api` adds the 1,001-assertion malformed IPC/handle/resource suite to a
compiler/target run. All four integration probes pass on GCC/sanitizers, Clang
and emulated big-endian PPC Linux. See `shared-services.md` for exact interfaces
and limits; this does not provide a complete guest kernel or commercial profile.

v0.7.0 adds the original 151-instruction heap lifecycle probe. All three backends
match CPU/mapping/backing state at 1/7/23 instruction budgets, with zero fallback
and unchanged executable generations. Guest stores fault identically on
protected/freed pages. The 1,778 boundary assertions and 307 heap API assertions
pass GCC/sanitizers, Clang and emulated PPC Linux. `--heap-api` adds the latter
suite; combine it with `--service-api` for a 14-check run of all five probes and
both API suites. See `guest-memory.md` for exact supported memory semantics.

- Registers and guest addresses are `uint32_t`; page pointers remain unused.
  Every memory access takes the checked little-endian callback path; span
  bypasses are disabled. No 4 GiB host address reservation is required.
- The attach step checks ABI 4, generation 3, entry ordering and FNV code-piece
  hashes against executable mappings. CRO/module attachment is rejected.
- State is visible at every instruction. Exact budgets resume mid-function;
  SVCs unwind to the existing guest scheduler, which owns sleep/events/exit.
- Callback failures stop immediately through a bounded host `setjmp`/`longjmp`
  boundary. Fault diagnostics retain the instruction PC and failing address;
  no following instruction or store executes. Multi-transfer instructions may
  retain earlier transfers if a later access faults. The external instruction
  counter includes an attempted faulting instruction; successful execution
  matches the interpreter's count.
- Executable mapping/protection changes and code writes, including writable
  aliases, invalidate the snapshot. Ordinary data mappings preserve it.
  Guest writes clear the single-guest exclusive monitor. Multi-core/multi-thread
  exclusive semantics are not implemented.
- Missing blocks and emitted interpreter requests fail visibly by default.
  Opt-in C attachment fallback executes only the existing bounded interpreter
  and records every fallback instruction. It cannot supply general ARM/VFP HLE.
- Scalar VFP words/double assembly are endian-safe. Only basic default-control
  arithmetic/moves/compare are currently exercised. Non-default FPSCR controls
  and vectors fail. Complete NaN/denormal/exception/rounding/conversion behavior
  is unverified; absent helpers cause compilation failure rather than success
  stubs. Builds disable contraction/fast math. This is not full VFP support.
- Native guest call depth is limited to 64; deeper calls unwind to dispatch.

## Verified evidence

The original 85-instruction framebuffer guest produces two touched/button
frames, 1,382,534 executed instructions, four SVCs and an orderly exit with zero
fallback. Full registers/flags and all loaded memory regions agree with the
interpreter after uneven budgets; framebuffer files match byte-for-byte.
The separate original 30-instruction ARM/Thumb probe verifies scalar single and
double arithmetic, compare/flags, stack transfer, state switch and one-instruction
resume. Its register/VFP results also match Unicorn 2.1.4.

The external integration suite passes **566 framebuffer/boundary assertions
and 34 ARM/Thumb/VFP assertions** with GCC, ASan/UBSan, Clang and statically linked
32-bit big-endian PowerPC Linux GCC under QEMU's 750 model. This is emulated Linux
evidence, not Espresso performance or Wii U execution. Real devkitPPC/WUT
RPX/WUHB linking also succeeds; physical input/rendering/exit tests are pending.

For this guest the generated C is about 24 KiB, with one function and 85 resume
entries. There is no 4 GiB reservation or million-entry page table. The checked
desktop run uses roughly 0.02 seconds of CPU for two frames on this machine;
this is a regression measurement, not a game or console speed claim. Code-size,
memory, compiler peak RSS and fallback timings for the selected commercial
revision remain awaiting its private analysis and hardware measurements.

Versioned reports are under `artifacts/reports/upstream-*` and the main validation
report. Emulated PowerPC/Clang checks can be repeated from the cloud host with:

```sh
python3 tools/validate-upstream.py --cc powerpc-linux-gnu-gcc --cc-prefix 'bash /workspace/ctr2wup-tools/environment/dev-container.sh' --run-prefix 'bash /workspace/ctr2wup-tools/environment/dev-container.sh qemu-ppc -cpu 750' --static --output artifacts/my-ppc-check
```

Source `tests/recomp/abi.s` generated `abi.arm.bin` with Clang 19's
`--target=arm-none-eabi -mcpu=arm1176jzf-s -mfpu=vfp -mfloat-abi=softfp` and
`llvm-objcopy -O binary --only-section=.text`; the object has no relocations.

v0.8.0 adds the 148-instruction original shared/sync probe (639 assertions)
and `--kernel-api` (197 assertions). All six probes pass GCC/sanitizers, Clang
and emulated PPC; the compiler/target runs include the kernel API (14 checks).
See `kernel-objects.md` for single-thread scope and exact lifetime rules.

v0.9.0 adds the standard-HID consumer (185 instructions, 18 SVCs, 858 boundary
assertions) and `--hid-api` (336 assertions). The validator runs seven probes;
compiler/target runs with HID API contain 16 checks. See `hid-input.md` for the
shared-ring contract and live original-AOT Wii U input consumer.

v0.10.0 adds an original standard-HID/GSP RGB565 graphics probe: 1382865
instructions, 27 SVCs, zero fallback; 3603 assertions including four reference
PPMs. `--gsp-api` adds 362 format/rights/bounds assertions. All eight probes plus
that API give 18 compiler/target checks. The original ARM halfword subset is
exercised in the graphics program. See `gsp-display.md` for exact limits.

v0.11.0 adds the ninth external probe, `relay`: 1,383,167 instructions and 34
SVCs per path, zero fallback, full-state/pixel parity including a VBlank between
LDREX/STREX. It exports four `relay/frame-{1,2}-{top,bottom}.ppm` images.
`--relay-api` adds timing, ring, lifetime, rejection and resource rollback checks.
Nine probes plus this API produce 20 compiler/target checks. See `gsp-queues.md`
for the single-client diagnostic timing and remaining stock startup/GX/PICA gaps.

v0.12 adds `stack` and `stack_reject`, bringing external probes to eleven.
The original nested ARM/Thumb program takes 47 instructions/one SVC; 352
assertions compare normal execution and six partial-word fault paths across
external/original AOT and interpreter. The 48-assertion rejection probe checks
six empty/unpredictable Thumb forms, including ones the emitter erases.
`--multiple-api` adds the stack/transfer API suite (24 compiler/target checks
total). An ARM LDM abort restores an early-emitter writeback; prior successful
loads remain visible. See `cpu-stack.md` for constraints and independent evidence.
