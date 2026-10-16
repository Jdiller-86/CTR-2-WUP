# Build on Windows

Windows development tooling is verified. The finished injector GUI, native Wii U
conversion and playable WUP output remain under development.

## Prerequisites

- Python 3.12 or newer and Git.
- MSYS2 UCRT64 with native GCC and Make for host-generated C and portable tests.
- Official devkitPro Windows tooling with devkitPPC/WUT for Wii U RPX/WUHB builds.
- Rust 1.95.0 for the optional audited external lifter.
- .NET SDK 10.0.401 for the separate CIA intake helper and WPF tooling probe.

The Linux Docker SDK installer does not produce native Windows dependencies.
Use the official Windows SDK route and preserve download/signature verification.
Native `gcc` executes host fixtures; the PowerPC cross-compiler cannot run them.
Keep the two compilers distinct in PATH.

## Source CLI and original fixtures

Run these commands from the repository root, using `python` or your local Python
launcher. Install the declared crypto extras before exercising encrypted inputs:

```sh
python -m pip install '.[packaging,decryption]'
python -m host --help
make check-python PYTHON=python
make check-c-portable
python -m host make-fixture --output "artifacts/Windows fixture 日本語"
python -m host run-fixture "artifacts/Windows fixture 日本語/ctr2wup-guest.cxi" --output "artifacts/Windows frames 日本語"
```

Run Make commands in UCRT64 with native GCC available. `make check-c-portable`
runs normal C suites; Linux ASan/UBSan results are separate evidence.
The vertex suite includes every unsigned 8-bit/16-bit index value and original
multi-stream/interleaved gathers; it uses portable byte loads rather than native
unaligned index casts. These checks do not prove Wii U rendering or conversion.
Generated fixtures are original and require no ROM, console key or commercial
asset. See [encrypted inputs](encrypted-input.md) for private local key handling.

The software scene suite also runs under portable Windows GCC. It checks
PICA-format loader/MOV shader/raster behavior independently of a Wii U build.
With the verified native SDK installed, `python -m host build --target scene-check
--output artifacts/scene-check` builds the focused original console diagnostic.
See [software scenes](software-scenes.md) for the accepted subset and evidence limits.

The Python wheel contains the CLI, translator and preparation modules. It does
not bundle the SDK, GUI or native runtime/build recipes. Extract the matching
source distribution for Wii U builds; `CTR2WUP_PROJECT_ROOT` can point an installed
CLI to that distribution.

## Optional external generation

In PowerShell, use `tools/install-translator-windows.ps1`. It builds the audited
source pin with Rust 1.95.0 and writes a provenance receipt. Then use the native
GCC/Make environment:

```sh
python tools/validate-upstream.py --service-api --heap-api --kernel-api --hid-api --gsp-api --relay-api --fs-api --apt-api --thread-api --multiple-api --output artifacts/windows-upstream
python -m host run-fixture "artifacts/Windows fixture 日本語/ctr2wup-guest.cxi" --backend upstream --output "artifacts/Windows AOT frames 日本語"
```

The manual **Windows development validation** workflow reproduces these checks
and the WPF SDK probe, preserving logs as Actions artifacts.
[Upstream integration](upstream-integration.md) records ABI, pin and notice limits.
Optional Unicorn/Zakuro/reference comparisons require their own pinned tools;
the Windows baseline is not evidence that those optional comparisons ran there.

## CIA intake helper

The `CTR2WUP-CIA-Intake` console utility has a separate self-contained
Windows x64 bundle. It needs no Python/SDK/runtime installation and exports
private analysis data. See [CIA intake](cia-intake.md) for controls and build pins.
It is separate from the planned injector GUI.

## Product boundary

Host tests, original fixtures and a WPF tooling probe do not implement the final
injector. Native Wii U conversion, locally prepared owned inputs and the complete
GUI-to-installed-game workflow require their own demonstrated behavior. See
[compatibility](compatibility.md) and [architecture](architecture.md).
