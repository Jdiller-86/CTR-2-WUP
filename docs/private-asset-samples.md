# Share selected private asset samples

Use this helper when a complete RomFS is too large to transfer. It creates one
small private ZIP from **explicit guest paths** in an existing `prepare-assets`
manifest. Python **3.12 or newer** is sufficient; no wheel installation, SDK,
cryptography package or project checkout is required.

## Download

From a successful [CI run](https://github.com/Jdiller-86/CTR-2-WUP/actions/workflows/ci.yml),
download **`private-asset-sample-tool-<source commit>`** and extract it.
The artifact contains `export-asset-samples.py`, this guide and the license.
Repository access is required; the helper contains no commercial files.

Alternatively, open
[the standalone source](https://github.com/Jdiller-86/CTR-2-WUP/blob/main/tools/export-asset-samples.py)
and use **Download raw file**. Save it as `export-asset-samples.py`.

## Run on Windows

Keep the existing `romfs.bin`, `asset-manifest.json` and matching
`preparation-report.json` on your PC. In PowerShell, change to the directory
containing the helper and adjust these **original example paths**:

```powershell
py .\export-asset-samples.py `
  --manifest ".\Original-assets\asset-manifest.json" `
  --preparation-report ".\Original-prepared\preparation-report.json" `
  --romfs ".\Original-assets\romfs.bin" `
  --paths "/levels/config.bin" "/levels/設定.txt" `
  --output ".\Original-asset-samples.zip"
```

Guest paths start with `/`, use forward slashes and must match the manifest
exactly, including case. They are not PC filenames. Select the requested files;
there is no directory, wildcard or automatic discovery export. Use `python3`
instead of `py` on Linux/macOS.

On success, JSON reports `"ok": true`, the selected file count and ZIP size.
Share only the new sample ZIP through the agreed private channel. It contains
`files/<guest path>` entries and `asset-sample-report.json`. It does not contain
the full manifest, blob, preparation report, original container or keys.

## Checks and limits

The helper checks matching preparation/asset identity, executable revision,
prepared-main hash and reported full-IVFC evidence. It validates every inventory
path, extent, hash syntax and byte total, then hashes the complete local blob and
every selected file. It rechecks source bytes and file identity before publishing.
Commercial hashes and receipt contents stay inside the private ZIP.

Selections are limited to **64 files and 30 MiB uncompressed**, with a completed
ZIP **strictly below 32 MiB**. Reports are bounded to 8 MiB, inventories to 10,000
files and blobs to 4 GiB. Unicode/case aliases, traversal, overlapping extents,
Windows reserved names, symbolic-link inputs and existing outputs fail visibly.
The ZIP is staged locally and published without replacement; failed staging is
removed and sources remain unchanged.

If an output already exists, choose a new ZIP filename. For a hash/provenance
error, use reports and blob from the same preparation and asset run; do not edit
hashes to make the command pass. A size error requires separate smaller batches.

The helper verifies **blob and selected-file hash consistency**. It cannot
reverify the original container or complete IVFC tree from these inputs, authenticate
signatures, identify nested formats or establish console/game compatibility.
The receipt records those limits explicitly. Inspect selected bytes before
asserting archive, shader, audio or dynamic-module requirements.
