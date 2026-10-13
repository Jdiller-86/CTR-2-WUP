# CIA intake and the attachment transfer limit

**October 13 full input:** the user supplied a CCI described as decrypted,
but it exceeds the executor's 32 MiB transfer limit. No byte from that attachment
has been inspected; filename identity and decryption remain unverified. On the
Windows 11 PC with Python 3.12+, install the released CTR2WUP Python wheel, then:

```powershell
py -m host prepare-rom "C:\path\owned-game.cci" --output "C:\path\new-Mario-prepared"
py -m host prepare-intake "C:\path\new-Mario-prepared\game.cxi" --output "C:\path\new-Mario-intake"
```

Return `new-Mario-intake\CTR2WUP-intake.zip` and
`new-Mario-prepared\preparation-report.json` privately in this chat. The output
directory must be new. NCSD cartridge input uses the same content-based Python
route as `.3ds`; renaming or uploading the full CCI again is unnecessary. The
original file remains unchanged and stays on the PC for later RomFS/module work.
The archive contains a receipt and, for supported decrypted input, a derived
executable-only CXI below 30 MiB; it excludes RomFS, tickets, certificates and
keys. A report-only result still requires resolving its stated blocking error.
The standalone .NET CIA helper below does not accept CCI.

**Small intake received:** the two-step route supplied a valid, privately verified
USA Mario executable and matching revision receipt. The full CCI and RomFS/CRO
assets remain unavailable here. See [first-title findings](first-title.md).
The current inspector accepts checked decrypted v0 CFA data partitions alongside
v2 executable content; v1 prototypes, v0 executable/header claims, encryption,
bounds and integrity failures remain rejected. This compatibility fix does not
enable game execution.

The user's direct 0.20.0 export reported `unsupported_ncch_version` for version 0.
CFA data partitions use version 0, while supported application CXIs use version 2.
The two-step route selects and validates the supported main partition without
inspecting unsupported auxiliary layouts. An original v2-executable/v0-data
fixture verifies this route; the prepared main executable is now independently
verified as v2. The original auxiliary partition bytes were not transferred, so
its specific cause remains unverified. The strict decrypted CFA compatibility
fix is tracked separately under task #32. Do not alter the original header or infer
commercial compatibility from successful intake.

October 11 update: the user returned a valid report-only intake ZIP. The helper
reports `encrypted_cia_content`; no executable view was exported. This identifies
the next input as a decrypted executable/CIA copy. The receipt is retained
privately, and the original CIA bytes have not been independently inspected.

October 12: v0.15.0 now has a **local Python engine** prepare-rom stage and
supported key/seed database importer. With locally obtained material it can
prepare a checked main CXI, then feed that into the existing small export.
See [encrypted-input.md](encrypted-input.md). No real console export has been
tested, and the standalone .NET helper remains unchanged/decrypted-only.
Do not upload key files or a key store; preparation runs on the user's PC.

Updated October 7, 2026 (America/New_York). The user supplied
`0004000000054000 Super Mario 3D Land (CTR-P-AREE) (v0.3.0) (U).legit.cia`.
Its transfer into this execution workspace failed because the attachment exceeds
the tool's **32 MiB limit**. Its bytes, hashes, encryption status, region and
revision have not been inspected. The filename suggests the selected USA title;
it is not verification of content, authenticity or decryption.

CIA is an installation container. A supported, fully decrypted base-application
CIA can supply the same executable NCCH/ExeFS data as a CXI. Version 0.5.0 adds
strict bounded CIA metadata/content inspection, checked executable selection and
small private executable export. The pinned external lifter already accepts CIA.
Commercial compatibility remains disabled.

## Smallest local step on Windows

The separate **CTR2WUP-CIA-Intake** console utility is an engineering intake helper;
it does not inject, install or run a game. The Windows x64 distribution includes
its .NET runtime, licenses and third-party notices. It requires no Python, SDK,
separately installed .NET, donor title or encryption key.

1. Extract `CTR2WUP-CIA-Intake-Windows-x64-0.5.0.zip` into a folder.
2. Drag your CIA onto `CTR2WUP-CIA-Intake.exe`. Alternatively, open the EXE and
   paste the CIA's local path when prompted.
3. Upload the resulting **CTR2WUP-intake.zip**, found in the new
   `<original CIA filename>.ctr2wup-intake` folder beside the CIA.

The helper reads the original without modifying it and refuses existing outputs.
It hashes the source and validates CIA bounds/bitmap/TMD groups/content digests,
ticket-title agreement and executable NCCH/ExeFS stored hashes. It exports a
derived executable-only CXI and a JSON receipt into an archive smaller than
32 MiB. It omits RomFS/plain/logo assets, the ticket, certificates and keys.
ExeFS contains the private executable and may include game icon/banner material;
keep this archive private. The derived header is unsigned and explicitly has no
RomFS. It is an analysis view, not a complete or playable ROM.

If outer CIA or inner NCCH encryption is declared, the archive contains only the
small report with the specific blocking layer. Do not infer encryption from
`.legit` or any filename. A report-only export does not unlock executable
analysis; fully decrypted executable data will then be required. No decryption
or credential collection is performed. Malformed/unsupported files fail visibly.
The executable view limit is 30 MiB; metadata allocations are bounded and content
hashing is streamed. The full original file remains needed locally for later
RomFS assets/module investigation and eventual conversion.

The helper's .NET logic passes original-fixture tests on Linux. Its Windows x64
self-contained EXE is cross-published; native Windows execution remains
unverified. No user game byte or credential is included in the utility bundle.

## Shared Python engine

For developers with Python 3.12+:

```sh
python -m host inspect "private/game.cia"
python -m host prepare-intake "private/game.cia" --output "private/game-intake"
python -m host analyze-title "private/game-intake/executable-view.cxi" --lifter --output "private/game-analysis"
```

The engine supports standard `0x2020` type-0/version-0 CIA, SHA-256 signed-header
layouts, version-1 TMD tables, sparse content bitmaps, exact ticket/TMD identity,
and content-index-0 executable selection. Update/DLC/system titles are outside
the first-title route. CIA metadata inspection can report declared encryption;
full executable intake rejects encrypted content. TMD/section hashes establish
integrity, not authentic signatures or support for the game.

The Python export performs BLZ/code-revision inspection and verifies the derived
CXI preserves the original executable revision. The .NET exporter validates
stored ExeFS bytes; cloud host intake performs the subsequent decompression,
segment and revision checks. Both exporters produce identical derived CXI bytes
for the original tested fixtures. Neither exports credentials or RomFS payloads.

## Reproduction and evidence

`tests/test_cia.py` covers 16 original cases: decrypted/sparse contents,
both encryption layers, damaged hashes/bitmap/bounds, identity mismatch,
missing executable, update refusal, output preservation and RomFS-free export.
`tools/validate-cia-helper.py` executes 13 .NET cases, including a >32 MiB
original RomFS placeholder and compressed code, and checks byte-for-byte
agreement with the Python engine. No commercial test material is used.

Build the helper with .NET 10.0.401, using intermediate/output paths outside the
source tree. Windows publishing is pinned to the .NET 10.0.12 runtime:

```sh
dotnet build tools/CiaIntake/CiaIntake.csproj -c Release -p:BaseIntermediateOutputPath=/absolute/artifacts/cia-obj/ -p:OutputPath=/absolute/artifacts/cia-linux/
python tools/validate-cia-helper.py --helper /absolute/artifacts/cia-linux/CTR2WUP-CIA-Intake.dll --output artifacts/cia-helper-report
dotnet publish tools/CiaIntake/CiaIntake.csproj -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -p:EnableCompressionInSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true -p:BaseIntermediateOutputPath=/absolute/artifacts/cia-win-obj/ -p:PublishDir=/absolute/artifacts/cia-windows/
```

The .NET Windows runtime is MIT with additional preserved third-party notices,
copied from the exact restored runtime package. The helper archive includes
`LICENSE-DOTNET.txt` and `THIRD-PARTY-NOTICES-DOTNET.txt`; the SDK is not bundled.
CIA layout facts were checked against the pinned lifter's `src/rom.rs` and
documented container offsets. The reader/exporter code is original.
