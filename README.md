# CTR2WUP

The product target is a Windows GUI injector like UWUVCI AIO: select a decrypted
3DS game, configure title/artwork, click Inject and install its per-game WUP on
Wii U. Super Mario 3D Land is the first selected candidate; its dump is pending.
The current working
deliverable includes native Wii U diagnostics and a controlled ARM guest
RPX/WUHB, a Windows-compatible Python CLI, checked NCSD/NCCH loading, and a
tested portable CPU/AOT and bounded single-core guest-thread runtime. Version 0.3.0 connects
the external lifter's generated C to an original checked ABI and static PowerPC
build. Version 0.4.0 adds an original encrypted homebrew package writer and
independent decrypting/FST/file verification, tested with synthetic local inputs.
It also retains original title staging and private title intake.
Version 0.5.0 adds decrypted CIA intake and a separate self-contained Windows
console helper for exporting small private executable analysis data. The supplied
Mario CIA exceeds the workspace's 32 MiB transfer limit; see `docs/cia-intake.md`.
Version 0.6.0 adds shared CP15 TLS/SRV/CFG startup support and an original ARM
service probe embedded in the Wii U diagnostics; see `docs/shared-services.md`.
Version 0.7.0 adds checked heap/linear allocation, partial free, permissions and
queries, with an original ARM lifecycle self-test; see `docs/guest-memory.md`.
Version 0.8.0 adds shared-memory aliases and single-thread synchronization
objects/waits; see `docs/kernel-objects.md`.
Version 0.9.0 adds standard HID shared input/events and a live ARM consumer in all
Wii U diagnostics; see `docs/hid-input.md`.
Version 0.10.0 adds standard direct GSP framebuffer submission, five format
decoders and an ARM HID-to-display prototype; see `docs/gsp-display.md`.
Version 0.11.0 adds shared GSP framebuffer queues, diagnostic VBlank/IRQ delivery
and an original ARM queued display guest; see `docs/gsp-queues.md`.
Version 0.12.0 adds checked ARM/Thumb stack and multiple-register transfers,
BLX calls and an original nested-call diagnostic; see `docs/cpu-stack.md`.
Version 0.13.0 adds guest threads/TLS/VFP contexts, locks/arbitration/quotas,
compiled libctru startup and verified standard input/CPU-framebuffer applications.
An additional interactive RPX/WUHB runs that original compiled application;
see `docs/guest-threads.md` and `docs/compiled-homebrew.md`.
Version 0.14.0 adds checked private FS archives/files, original save/reload
through normal libctru/newlib, standalone APT startup/exit and a fifth compiled
save diagnostic RPX/WUHB; see `docs/filesystem.md`.
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
python3 -m host run-fixture "artifacts/my fixture/ctr2wup-guest.cxi" --backend upstream --output artifacts/external-frames
python3 -m host build "artifacts/my fixture/ctr2wup-guest.cxi" --target guest-fixture --backend upstream --output artifacts/external-console
make upstream-check
```

Inspect a fully decrypted user-owned dump with `python3 -m host inspect PATH`.
Rejects malformed/encrypted data and reports compatibility honestly. Use
`python3 -m host validate-package DIRECTORY` for limited WUP structure checks;
the report separates integrity, signatures and installation readiness.

Artifacts under `artifacts/` are generated and Git-ignored. Keep ROMs, extracted
assets, translated commercial code, saves, packages, traces and keys under
ignored `private/` directories. The delivered diagnostics/fixtures contain only
original homebrew applications with disclosed linked support and preserved
licenses. No keys, commercial assets or proprietary SDK are bundled.

See `docs/build-windows.md` for the Windows prerequisites and pending clean-machine
checks, `docs/console-testing.md` for the physical-console checklist, and
`THIRD_PARTY_NOTICES` for dependency and licensing boundaries.
`docs/guest-fixture.md` explains the real ARM execution pipeline and its explicit
input/raw-framebuffer contract. The separate compiled libctru workflow supplies
standard CPU-framebuffer homebrew evidence; PICA remains absent. All Wii U
applications await physical execution.
The separate HID/GSP fixture uses standard input and direct display services;
GSP GX commands and PICA rendering are still absent; relay/framebuffer queues
and a diagnostic VBlank clock are now implemented in the shared runtime.
See `docs/upstream-integration.md` for checked ARM/Thumb/scalar-VFP evidence and
`docs/first-title.md` for the Mario intake. `stage-title` produces original title
staging, not a WUP. The GUI follows the working conversion/packing pipeline.
See `docs/packaging.md` for `pack-title`, `verify-package`, the optional crypto
dependency and local key/certificate requirements. Installer acceptance remains
unverified; the offline test credentials cannot create an installable title.
