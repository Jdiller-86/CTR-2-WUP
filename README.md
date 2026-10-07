# CTR2WUP

A staged research project for ARM11-to-Wii U translation. The current working
deliverable includes native Wii U diagnostics and a controlled ARM guest
RPX/WUHB, a Windows-compatible Python CLI, checked NCSD/NCCH loading, and a
tested portable CPU/AOT and single-guest SVC/event runtime.
Commercial ROM-to-WUP conversion is not implemented or claimed.

Read `STATUS.md`, `docs/milestones.md` and `docs/foundation-audit.md` before
continuing. `docs/project-brief.md` preserves the user-supplied requirements.

## Linux/cloud quick start

```sh
bash tools/install-wiiu-sdk.sh
python3 -m host doctor
make check
python3 -m host build --target prototype --output "artifacts/my diagnostic"
python3 -m host make-fixture --output "artifacts/my fixture"
python3 -m host run-fixture "artifacts/my fixture/ctr2wup-guest.cxi" --output "artifacts/my guest frames"
python3 -m host build "artifacts/my fixture/ctr2wup-guest.cxi" --target guest-fixture --output "artifacts/my guest RPX"
```

The setup script uses the official devkitPro image pinned by digest, extracts a
local SDK, and preserves verification. Run it in the existing isolated checkout;
do not create a Git worktree unless explicitly requested. No live services need
to remain running. Optional audited host lifter:

```sh
bash tools/install-translator.sh
python3 -m tests.host_fixtures artifacts/fixtures
python3 -m translator.upstream artifacts/fixtures/synthetic.cxi
```

Inspect a fully decrypted user-owned dump with `python3 -m host inspect PATH`.
Rejects malformed/encrypted data and reports compatibility honestly. Use
`python3 -m host validate-package DIRECTORY` for limited WUP structure checks;
the report separates integrity, signatures and installation readiness.

Artifacts under `artifacts/` are generated and Git-ignored. Keep ROMs, extracted
assets, translated commercial code, saves, packages, traces and keys under
ignored `private/` directories. The delivered diagnostics/fixtures contain only
original homebrew code. No keys, commercial assets or proprietary SDK are bundled.

See `docs/build-windows.md` for the Windows prerequisites and pending clean-machine
checks, `docs/console-testing.md` for the physical-console checklist, and
`THIRD_PARTY_NOTICES` for dependency and licensing boundaries.
`docs/guest-fixture.md` explains the real ARM execution pipeline and its explicit
input/raw-framebuffer contract. It does not provide HID/GSP/PICA or stock 3DS
graphical homebrew support. Both Wii U applications await physical execution.
