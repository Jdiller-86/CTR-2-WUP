# Batch CIA 3DS Decryptor investigation

Reviewed October 12, 2026 for CTR2WUP 0.16.0. **Its decryption methods can fit
CTR2WUP's input-preparation stage. Retain the project's original checked engine;
accept independently validated decrypted output as an additional input route.**
Decryption alone does not translate ARM, provide 3DS services, render PICA draws
on GX2 or produce a playable Wii U WUP.

## Sources reviewed

| Project | Pinned source | Finding |
|---|---|---|
| [matiffeder's original batch script](https://github.com/matiffeder/3DS-stuff) | `af35b08a94f1662bd4ad23ccb35d2ab52f18712d` | MIT script; orchestrates external decrypt, ctrtool and makerom executables |
| [Redux](https://github.com/xxmichibxx/Batch-CIA-3DS-Decryptor-Redux) | `2fb1ac549a4006dd5261b8545e943527960ceeff` | Rewritten batch wrapper; bundled binaries/seed DB; no license file in reviewed tree |
| [Source-based ctrdecrypt fork](https://github.com/R-YaTian/ctrdecrypt) | `1763c5ebf04de24e8cdd672a391aae84dc45eef3` | GPL-3.0; readable AES/container/seed implementation |
| [ctrdecrypt upstream](https://github.com/shijimasoft/ctrdecrypt) | `a0d170c2153fe4054bd241ef4605ea64c1f3cc99` | GPL-3.0 source, referenced by later batch packages |
| [rcyggdra rewrite](https://github.com/rcyggdra/decryptor) | `9984cd001bcd249af04d19eb9d015fd2ab742d76` | MIT root label, but its two Rust decryption files match the GPL fork byte-for-byte |

The [original release thread](https://gbatemp.net/threads/batch-cia-3ds-decryptor-a-simple-batch-file-to-decrypt-cia-3ds.512385/)
is linked by the reviewed README; it was not used as executable provenance.
Original `decrypt.exe` is opaque in the reviewed packages. Reading later Rust
sources does not establish the exact behavior/source of those Windows binaries.
Repository text and tree metadata were inspected; **no bundled executable,
seed/key database, downloader or actual game was downloaded or run**.
Source literals were redacted from the local audit excerpts. No external
implementation or credential constant was incorporated into CTR2WUP.

## What the software actually does

The original script loops over CIA/3DS inputs. `ctrtool` inspects CIA title/content
metadata, a separate decrypt engine produces decrypted NCCH partitions, and
`makerom` rebuilds decrypted CIA or NCSD/CCI output. Base-game CIA conversion
commonly ends in a **`.cci`**, not a CXI. `.cci` and `.3ds` can both contain NCSD;
the magic/layout, rather than renaming, identifies the format. `makerom`'s
`-ignoresign` does not create authentic Nintendo signatures or a Wii U package.

The readable later engine uses CIA AES-CBC ticket/title-key/content decryption,
NCCH AES-CTR section counters, hardware-style KeyX/KeyY derivation, primary versus
secondary ExeFS keys, and optional SHA-256 title-seed checks/derivation. It includes
shared platform/common-key constants; it does not require a unique personal
console key for every ordinary retail title. Some input types still need a
particular title key or seed. The exact required material depends on the dump.

That engine also tries Nintendo seed endpoints when a seed is missing locally,
logs derived key-related data, and supports forms outside CTR2WUP's current
base-title scope. Those behaviors have not been adopted. CTR2WUP's current route
uses explicitly local file imports, redacted diagnostics and no automatic key/
seed download. The final GUI will expose local preparation using that same engine.

## Fit with CTR2WUP

| Mechanism | Current CTR2WUP position | Decision |
|---|---|---|
| CIA outer AES-CBC and ticket/title key | Implemented with local file references and full TMD plaintext-content hash checks | Keep existing original implementation |
| NCCH section AES-CTR / mixed ExeFS keys | Implemented for v2 and standard/1/0a/0b methods | Same method; no need to launch the batch wrapper |
| Seed DB / seed-derived KeyY | Local import, seed verification and derivation implemented | Keep local/private source; GUI import remains planned |
| Decrypted NCSD/CCI output | Accepted by magic; main NCCH preparation and asset checks available | Useful optional handoff from a user-run decryptor |
| Rebuilding a CIA/CCI with makerom | Unnecessary for the injector's main executable/asset preparation | Do not add a dependency just to repack the source |
| Batch convenience | Can inform eventual queued input/progress handling | Use controlled engine jobs, structured results and isolated outputs |
| Fixed-key or NCCH v0/v1, updates/DLC/system/DSi titles | Unsupported in CTR2WUP's current preparation profile | Separate implementation/evidence needed; no new compatibility claim |
| Windows executables | Not run in this Linux/cloud audit | Native Windows/source-build/provenance tests remain pending |

The original batch performs broad wildcard deletion of NCCH intermediates in
its working directory. Redux documents non-ASCII filename limitations and
cleanup of decrypted CIAs even when CCI conversion fails. These are concrete
integration problems for a GUI that must preserve user inputs and support spaces/
Unicode. CTR2WUP already uses bounded parsing, isolated staging, immutable source
checks and atomic non-overwriting publication.

## Reuse and licensing decision

The original MIT wrapper can be adapted with its notice, but adds little to the
existing orchestration. Bundling `decrypt.exe` as an unexplained binary would
leave source/provenance and behavior unresolved. Redux's reviewed tree does not
supply a blanket redistribution grant for its wrapper and bundled dependencies.
The readable Rust engine is GPL-3.0; use would require the applicable source/
license obligations and an explicit integration design. A fork's MIT badge is
not evidence that inherited GPL code was relicensed: the reviewed `main.rs` and
`ctrutils.rs` match the GPL fork's full SHA-256 hashes. This audit does not make a
legal conclusion about the publisher; it records why that label is insufficient
for choosing a dependency.

**Recommended adaptation:** keep CTR2WUP's original decryption/preparation code,
use published crypto/layout facts as cross-checks, and validate a user's already
prepared CIA/CCI/CXI through the same inspection/full-IVFC/revision path. A future
source-built external backend is technically possible, but no integration need
or licensing/provenance resolution is established by this audit.

## Verified handoff and remaining proof

An original synthetic decrypted `.cci` passed **inspection -> prepare-rom ->
full IVFC -> prepare-assets -> executable-only intake**. The prepared blob matched
its original bytes, the executable revision survived and the source stayed
unchanged. This verifies the supported NCSD/CCI handoff, **not execution of Batch
Decryptor or its Windows binaries**, and not an actual Mario dump.

A local handoff can use:

```sh
python -m host prepare-rom "MyGame-decrypted.cci" --output "private/prepared"
python -m host prepare-intake "private/prepared/game.cxi" --output "private/intake"
python -m host prepare-assets "private/prepared/game.cxi" --output "private/assets"
```

The standalone v0.5 Windows CIA helper expects CIA; a `.cci` is handled by the
Python engine above, not by renaming it to CIA. When resources are available,
the small private intake can be supplied without uploading keys or a full asset
blob. Actual user-key exports, Mario input, Windows execution and console gameplay
remain unverified. No further resource is needed from the user while away.

Audit receipts: `artifacts/reports/assets-textures-v016/decryptor-audit/`, including
source pins, license/source comparisons and `cci-interoperability.json`. External
source excerpts, binaries and key databases are excluded from delivery.
