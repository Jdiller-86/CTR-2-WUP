# Build on Windows

The v0.5.0 `CTR2WUP-CIA-Intake` console utility has a separate self-contained
Windows x64 bundle requiring no Python/SDK/runtime installation. It exports
private analysis data and is not the final GUI/injector. See `cia-intake.md` for
use, build/runtime pins and the unverified native-Windows acceptance check.

The source CLI targets Python 3.12+ and uses the standard library, argument-list
subprocesses, Windows process-group cancellation and Unicode paths. It has been
executed on Linux, including a checkout/output path with spaces and Japanese
characters. A clean Windows machine has not been tested; this is a pending
acceptance check, not a completed Windows product.

## Prerequisites

Install Python 3.12 or newer and the official devkitPro Windows installer. Use
devkitPro's supplied MSYS2 environment to install the signed `wiiu-dev` packages
(devkitPPC, WUT, Wii U tools) and GNU Make. Preserve normal Pacman signature and
TLS verification. Set `DEVKITPRO` and `DEVKITPPC` to that installation, and put
the compiler, tools and Make in PATH. The Linux Docker SDK is an ELF toolchain;
it cannot be used as native Windows executable dependencies.

From the extracted source root, in that environment:

```sh
python -m host doctor
python -m unittest discover -s tests -v
python -m host inspect "C:/My Games/my decrypted dump.cxi"
python -m host build --target prototype --output "C:/My Builds/CTR2WUP diagnostic"
python -m host make-fixture --output "C:/My Builds/original guest"
python -m host run-fixture "C:/My Builds/original guest/ctr2wup-guest.cxi" --output "C:/My Builds/guest frames"
python -m host build "C:/My Builds/original guest/ctr2wup-guest.cxi" --target guest-fixture --output "C:/My Builds/guest console"
```

The output directory must not exist. It receives the real RPX, WUHB if available,
build log and versioned build report after successful artifact validation. Builds
never install titles on a console. Commercial `build` reports an unsupported
profile with a nonzero exit and produces no placeholder package.

`run-fixture` requires a native Windows GCC available as `gcc`; the devkitPPC
cross-compiler cannot execute desktop tests. Its native runner uses relative
ASCII output filenames inside the Unicode working directory.

The Python wheel includes the host CLI and bounded Python translator. It
does not bundle the SDK, a Windows GUI, or the native C source/build recipes. For
prototype/guest building use the full source distribution, or set
`CTR2WUP_PROJECT_ROOT` to it when calling an installed `ctr2wup` entry point.

The optional external lifter requires a Windows Rust 1.95.0 toolchain and a C
compiler. See the audited source pin and unresolved notice/ABI requirements in
`foundation-audit.md`. `tools/install-translator.sh` provisions Linux only; it is
not advertised as a Windows installer. The bounded Python translator itself
requires only Python and emits portable C for the supplied synthetic fixtures.

Version 0.3.0 adds `tools/install-translator-windows.ps1` for the external
generation driver. It requires official Rust/rustup and Git, builds the clean
audited source with Rust 1.95.0 and writes the provenance receipt. Use
`run-fixture --backend upstream` and `tools/validate-upstream.py` with native GCC.
The script/workflow are prepared; they have not yet executed on Windows.
The manual workflow at `.github/workflows/windows-validation.yml` also tests
actual external ARM/Thumb/VFP execution and Unicode output, then builds a WPF
tooling probe. Publishing/one run await the earlier requested approval.

Version 0.4.0 adds optional encrypted packaging. From source, run
`python -m pip install '.[packaging]'` and follow `packaging.md` for local file-based inputs.
`pack-title` and `verify-package` pass Linux Unicode/space-path and installed-wheel
checks; native Windows packaging remains unverified. The prepared workflow now
installs the crypto dependency and runs its original offline unit tests too.

## Windows checks still required

v0.6.0 adds the portable TLS/SRV/CFG startup suite. With native GCC/Make, run
`make -C tests/services check` and `python tools/validate-upstream.py --service-api
--output artifacts/windows-startup-check`. These source checks have Linux and
emulated PowerPC evidence; no native Windows result is claimed. The separate
v0.5.0 CIA intake helper remains the unchanged ROM-export utility.

v0.7.0 adds `make -C tests/heap check` and
`python tools/validate-upstream.py --heap-api --service-api --output
artifacts/windows-memory-check`. These exercise allocation/free/protection/query
and actual external AOT. Native Windows acceptance remains pending.

v0.8/v0.9/v0.10 add `make -C tests/kernel check`, `make -C tests/hid check`,
`make -C tests/gsp check`, and `python tools/validate-upstream.py --kernel-api
--hid-api --gsp-api --output artifacts/windows-device-check`. The latter checks
actual external execution and emits original HID/GSP display reference images.
Native Windows and physical Wii U results remain pending.

Doctor and clean prototype compile; inspection of NCSD/NCCH and malformed inputs;
spaces/Unicode on both source and output paths; cancellation terminating compiler
children; atomic publication and refusal to overwrite outputs; installable-wheel
commands; interpreter/AOT guest-frame matches; and console RPX launch. The WPF GUI is deferred until execution and
packing gates are demonstrated, as required by the brief.
