# ARM/Thumb stack and register transfers (0.12.0)

The shared CPU runtime now executes ordinary function stack frames and transfers
of multiple registers. This is ROM-independent work toward M2/M3/P3; it does not
enable a commercial profile, threading or the GUI Inject-to-WUP pipeline.

Supported instructions:

- User-mode ARM LDM/STM in IA, IB, DA and DB addressing modes, with checked
  optional base writeback and ascending register/address order.
- ARM LDM of PC, including stack returns, with ARM/Thumb interworking.
- Thumb16 PUSH/POP of low registers and optional LR/PC; POP PC interworks.
- Thumb16 LDMIA/STMIA. LDM with its base in the list suppresses writeback;
  STM with its base in the list is supported only when the base is first.
- Register BLX calls in the bounded original translator, matching existing
  interpreter support. The target is captured before LR changes, including
  BLX LR, and Thumb callers receive an odd return address.

The interpreter decodes these forms into a checked word-transfer helper.
Original AOT emits constant decoded parameters and never calls the interpreter.
Condition skips preserve registers/memory while consuming one instruction.
Transfers preserve flags except the execution-state bit on a loaded-PC return.
Address arithmetic wraps at 32 bits; every word still enforces mapping, alignment
and read/write permissions. No guest address becomes a host pointer.

An access fault keeps the faulting instruction PC and the original base; earlier
successful register loads or memory stores remain visible. These CPU operations
are not atomic service publications. The external emitter writes an ARM LDM
base early; the checked ABI captures and restores that base on a memory fault.
It clears that abort bookkeeping at subsequent instruction/execution boundaries.
CPU logs contain the failed address/opcode; guest fault detail retains the PC.

Empty lists, PC as base, stores of PC, user register banks/SPSR returns and ARM
writeback with its base in the list are rejected before effects. Thumb STM with
a non-first base and BLX PC are rejected. The external instruction guard rejects
the same empty/unpredictable transfer forms, including empty Thumb operations
that the emitter otherwise turns into successful no-ops. This is an explicit
safe subset, not a claim to implement every ARM11 abort/unpredictable behavior.
Thumb long-immediate calls and broader ARM/VFP/privileged behavior remain open.

The original checked NCCH (`host/stack_fixture.py`) calls an ARM function, then
a Thumb function that calls ARM again. It saves/restores register frames, stores
and reloads register groups, skips one conditional transfer from an unmapped
base, returns to ARM, checks a restored stack and exits. Its 47 instructions and
one SVC produce independent golden data, with zero fallback and matching CPU/
mapped bytes across interpreter, original AOT and the real external lifter at
uneven budgets. Separate external cases compare six partial-word fault paths
and reject six empty/unpredictable Thumb forms without effects.

`make -C tests/multiple check sanitize` checks golden modes, interworking,
partial word faults, permission/alignment failures, unsupported forms, wrapped
addresses and the interpreter/original-AOT nested call self-test.
`python3 tools/validate-upstream.py --multiple-api --output artifacts/my-stack`
runs eleven external probes plus this API (24 checks).

`tools/reference-multiple.py --output artifacts/my-reference` independently
compares 220 instruction cases through interpreter and original AOT with pinned
Unicorn 2.1.4 configured as ARM1176. It checks all registers, CPSR and all 4096
data bytes, then verifies the nested program's 47-instruction trace, golden output
and restored stack. Unicorn and a native GCC/shared-library toolchain are optional
developer requirements; production has no Unicorn dependency. In the managed
environment, set `PYTHONPATH=/workspace/ctr2wup-tools/environment/python-packages`
for this optional tool. The reference report is
`artifacts/reports/multiple-oracle-final-v12/report.json`.

All Wii U builds embed `ARM/THUMB STACK CALLS` and `stack_selftest`.
Compilation, reference checks and emulated PPC prove only the stated checks.
Physical PowerPC correctness, workload speed, conventional homebrew/commercial
startup and the user's selected Mario revision remain unverified.
