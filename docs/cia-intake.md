# Private executable intake

The intake tools produce a small private executable view for analysis when a
complete game dump cannot be transferred. They do not inject, install or run a
game. Keep the original file locally for later assets and conversion.

## Python route

For Python 3.12 or newer, run from the repository or install the matching wheel:

```sh
python -m host prepare-rom "private/owned-game.cci" --output "private/prepared-game"
python -m host prepare-intake "private/prepared-game/game.cxi" --output "private/intake"
python -m host analyze-title "private/intake/executable-view.cxi" --lifter --output "private/analysis"
```

`prepare-rom` selects and validates the main NCSD partition. Already-decrypted
input requires no keys; [encrypted preparation](encrypted-input.md) uses local
material. The standalone CIA helper below accepts CIA only.

An intake directory must be new. Its `CTR2WUP-intake.zip` contains a receipt and,
for supported decrypted input, an executable-only CXI below 30 MiB. RomFS, tickets,
certificates and keys are excluded. ExeFS may contain private executable/icon/
banner data, so share the archive only through an agreed private channel.
A report-only export identifies a blocking layer; it is not successful executable
intake. The original remains unchanged and existing outputs are refused.

## Standalone Windows CIA helper

The [Windows validation workflow](https://github.com/Jdiller-86/CTR-2-WUP/actions/workflows/windows-validation.yml)
provides `cia-helper-windows-<source commit>`, containing the checked,
self-contained **CTR2WUP-CIA-Intake Windows x64** package. It includes
its .NET runtime/notices and requires no Python or SDK. Drag a CIA onto
`CTR2WUP-CIA-Intake.exe`, or open it and paste the local path. The new
`<CIA filename>.ctr2wup-intake/` directory contains the private ZIP.

The helper checks CIA bounds, bitmap, TMD groups/content digests, ticket-title
agreement and stored NCCH/ExeFS hashes. It does not decrypt. Declared outer CIA
or inner NCCH encryption produces a bounded report-only result with the specific
blocking layer. Filename extensions and `.legit` do not prove encryption state.
The derived header is unsigned and explicitly has no RomFS; it is an analysis
view, not a complete or playable ROM.

## Supported profile

The Python engine accepts standard `0x2020` type 0/version 0 CIA, SHA256 header
layouts, version 1 TMD tables, sparse bitmaps and content-index 0 application
selection. Executable NCCH is version 2; supported decrypted CFA data partitions
may use version 0. Prototype/version 1 executables, malformed bounds/hashes,
updates, DLC and system titles are outside this route. Never alter a source
header to make an unsupported input appear supported.

The Python exporter also checks decompression and executable-revision identity.
The .NET exporter checks stored ExeFS bytes; subsequent Python analysis validates
code/segments/revision. Metadata allocations are bounded, content hashes stream,
and output remains below 32 MiB. Hash agreement establishes integrity, not
Nintendo signature authenticity or commercial compatibility.

## Build and validation

Use the SDK/runtime pins in `tools/CiaIntake/CiaIntake.csproj` and the helper
packaging tool. Keep build intermediates outside the source tree:

```sh
dotnet build tools/CiaIntake/CiaIntake.csproj -c Release -p:BaseIntermediateOutputPath=/absolute/artifacts/cia-obj/ -p:OutputPath=/absolute/artifacts/cia-linux/
python tools/validate-cia-helper.py --helper /absolute/artifacts/cia-linux/CTR2WUP-CIA-Intake.dll --output artifacts/cia-helper-report
```

Original fixtures cover sparse contents, both encryption layers, bounds/hashes/
identity failures, output preservation, compressed code and large RomFS omission.
The validator checks byte-for-byte agreement between the .NET and Python exports.
Published Windows bundles preserve exact runtime licenses and third-party notices.
