# Testing

Use original fixtures for shared development. Keep commercial inputs, derived
code and credentials private. Unsupported CPU/service/GPU states fail visibly.
A check establishes the behavior it exercises, not general game compatibility.

## Local checks

```sh
make check
make prototype
```

`make check` runs Python and portable C suites, including ASan/UBSan with leak
detection. Focused targets are `make check-python`, `make check-c` and
`make check-c-portable`; the last runs normal C checks for native Windows GCC.
Run affected checks while editing, then the checks appropriate to the final
change. A documentation-only update needs documentation validation.

Independent format/pixel/numeric references are documented beside their
contracts. Validators write original fixtures, compiler/source identity and
bounded reports under a new output directory in `artifacts/`.

## Hosted checks

CI covers supported Python versions, GCC/Clang sanitizer runs, Windows host/
portable C, workflow lint and an immutable-SDK Wii U build. Required CI reports
its dependencies' actual status. Documentation builds strictly and checks the
publishable site. Logs and original artifacts bind to a source commit.

Optional Windows deep validation exercises the external AOT adapter and WPF SDK
probe; an SDK probe does not implement the final GUI. Emulated PowerPC checks
byte-order/ABI behavior without establishing physical Wii U performance.

## Hardware diagnostics

Each diagnostic package contains a deployment guide, an SD-card tree and a
result folder with build identity and full logging.
Ordinary diagnostic logs are isolated under `sd:/ctr2wup/results/`; the installed
candidate uses `sd:/ctr2wup/install-check/`. Saves and asset blobs stay outside
these return folders.

Automatic checks, human observations, failures, skips and unrun steps stay
separate. A saved report does not prove Menu return; unit tests do not prove
picture/audio quality or installation. The compact
[hardware ledger](https://github.com/Jdiller-86/CTR-2-WUP/blob/main/tests/hardware-evidence.json)
records baseline scope and provenance. Detailed acceptance criteria and results
are tracked in [GitHub issues](https://github.com/Jdiller-86/CTR-2-WUP/issues).
