# Testing and evidence

Tests must establish the behavior named in their result. Use original fixtures
for shared development; keep commercial files and credentials under ignored
private directories. Unsupported CPU/services/GPU states must fail visibly.

## Desktop regression checks

```sh
make check
make prototype
```

`make check` runs Python and portable C suites, including ASan/UBSan. Separate
CI targets are `make check-python`, `make check-c` and normal-only
`make check-c-portable`. The latter is appropriate for native Windows GCC,
where the Linux sanitizer runtime is not assumed available.

Targeted graphics checks include:

```sh
make -C tests/gx check sanitize
python3 tools/validate-transfers.py --output artifacts/my-transfer-matrix
python3 tools/validate-libctru.py --application transfer --wiiu \
  --output artifacts/my-compiled-transfer
```

The compiled test uses pinned external libctru and the audited AOT toolchain.
Optional external color/layout references use a clean pinned Zakuro checkout;
that code remains test-only. Full console rendering is not inferred from it.

## Continuous integration

Automatic CI runs Python 3.12/3.14, GCC/Clang normal/sanitizer fixtures, native
Windows host/portable-C checks, workflow lint and an immutable-SDK Wii U build.
**Required CI** summarizes those results; a failed dependency cannot become a
successful aggregate. Artifacts include logs and original diagnostic binaries,
with source commits, hashes and notices. Read the actual run status and logs.

Manual Windows development validation adds the external AOT execution and WPF
SDK probe. WPF tooling readiness is not a finished GUI. Optional manual CodeQL
requires existing repository scanning eligibility; it does not enable a paid
plan and is not part of the required regression gate.

## Wii U evidence

Each diagnostic guides its needed inputs and records a TXT result independently
of the full trace. Collect the release/commit, app version, launch route, console
firmware/Aroma/installer versions where available, observed output, exit behavior
and named report files. The console-result issue form accepts this evidence.

Keep **automatic observations**, **user confirmations**, **failures**, **skips**
and **unrun checks** distinct. A saved result does not prove successful Menu
return. A valid visit-1 snapshot does not establish later update/reload. A reported
pass can guide continued development while hard logs remain pending.

No new long test is requested while the current tester is away. The existing
Save/RomFS follow-up, optional Asset Files and new GX Transfers round await
availability. See [getting started](getting-started.md) and each kit's guide.

## Acceptance

A full milestone closes only when its [complete criteria](milestones.md) have
recorded evidence. Commercial acceptance requires the exact selected revision,
boot/menu/gameplay/input/audio/save/reload/stability, actual WUP install/Menu
launch, useful Wii U timings and the final Windows GUI conversion workflow.
Passing unit tests and creating more diagnostics are partial implementation.
