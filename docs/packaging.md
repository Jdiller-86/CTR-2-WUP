# Original homebrew WUP packaging

## Plaintext original homebrew

`host.package_homebrew` adds original plaintext **`0x2000`** contents and a
self-generated test-signing chain for a signature-patched homebrew environment.
Each staged file has its own padded content stream and FST offset zero. The
ticket grants the complete 1024-index homebrew bitmap; packages are bounded to
1024 contents including the FST. Empty top-level folders retain an empty stream
with their original ownership.
It requires **no common key, production certificate, donor title or game input**.
The independent verifier checks content/FST/XML/identity and the generated
signature chain. This establishes package integrity and test-signature
consistency; it does not establish Nintendo authenticity or physical installation.

The candidate targets **WUP Installer GX2 or the bundled MCP capture under Aroma
with signature patches**,
including `wiiu/environments/aroma/modules/setup/01_sigpatches.rpx`. The module
is not bundled and its presence is not assumed. Physical installer acceptance
and Wii U Menu launch/exit/relaunch are unverified.
The explicit diagnostic ID **`0005000043543257`** is not a reserved safe range:
check the console for an unrelated title with that ID before installation.

The `native-install` diagnostic checks its actual title identity and the production
`ctr_fs_snapshot_*` adapter using isolated original data. The first Menu launch
leaves a deliberately interrupted publication (**PREPARED**); the second recovers
the committed `.previous`, rejects the uncommitted `.pending` and verifies a new
publication (**RECOVERED**). Later launches only recheck completion. Source-bound
`install-v2`/`snapshot-v2` receipts and recovery files stay under
`sd:/ctr2wup/install-check/<source-commit>/`; return the whole parent folder over
FTP. This proves deterministic software interruption recovery, not power-loss
durability. The package supplies one guide, one `sd-card/` tree and the existing
capture app compiled with the candidate's exact file hashes and manifest digest.
One selected destination is sufficient. The capture records installation before
the two Menu launches; an installation failure stops the sequence. Source,
notices and checksums remain under `support/`.

```sh
python -m host stage-title PATH.rpx --title-id YOUR_APPLICATION_ID --name "My title" --output private/staging
python -m host pack-homebrew private/staging --output private/package
python -m host verify-package private/package --expected-staging private/staging
```

The producer generates an ephemeral RSA4096 Root and RSA2048 CA/CP/XS test chain.
No private key is serialized. The independent verifier checks signatures and
content consistency while explicitly retaining an untrusted-root result.

`tools/package-install-capture.py --output artifacts/install-capture` builds the
standalone Aroma diagnostic for the historical seven-file package pinned in
`packaging/install-capture.json`. The generated M1 handoff instead binds the same
app to its current per-file package. Both require a complete compiled inventory,
a writable SD report and an absent title ID before one explicit target selection
can start installation. Package pins cannot be changed through an SD manifest.
Read-only MCP package parsing runs independently of install eligibility. Title
inventory reports `absent`, `present` or `inconclusive`, with before/returned/after
counts and bounded capacity. Only an actual matching record establishes presence;
the global count and enumerated count need not match. Absence requires a successful
list below capacity and stable global counts within the supported bound. A full
buffer, invalid response or changing global count blocks writes. An exact-ID
lookup additionally blocks an observed match; its errors alone cannot prove absence.
Only the fixed target's matching metadata is logged, never unrelated titles.
Startup evidence precedes graphics. Each binary log record is closed and synced,
then checked against its intended byte count and SHA-256 through stdio and a
separate FSA read of `/vol/external01`. An unavailable or mismatched report blocks
installation; filesystem acknowledgements do not prove power-loss durability.
Native GX2 display and silent AX ownership follow the diagnostic frontend.
Raw IOS submission/callback codes and MCP progress are saved under
`sd:/ctr2wup/install-check/install-capture/`. Cancellation drains the callback
before releasing buffers. The app preserves the WUP and existing titles; its
receipt establishes installation diagnostics, not M1 completion.

## Encrypted research profile

The encrypted `0x2001` writer remains supported with its separately configured
local inputs. Those inputs are unnecessary for plaintext homebrew packing.

The original encrypted writer has a separate decrypting verifier. Offline fixtures
use synthetic inputs; they cannot establish trusted signatures or installer
acceptance. Commercial conversion remains disabled.

## Implemented profile

`host.package_writer` packs original `code/content/meta` staging into four
encrypted `0x2001` contents: FST, code, content and meta. It supports application
IDs beginning `00050000`, explicit version/group/OS fields, UTF-8 names, nested
and empty directories, empty files and at most 1 GiB per content group. Files
start at 32-byte boundaries; contents are padded with zeros to 32 KiB.

AES-128-CBC uses the content index followed by zero bytes as its IV. A fresh
random title key is wrapped with the locally supplied common key using the title
ID as IV. TMD content records contain SHA-1 of padded plaintext plus 12 zero
bytes; SHA-256 protects the content-record group and content-info tables. The
ticket contains one content-access bitmap. Common-key index 0, unpersonalized
tickets, RSA-2048/SHA-256 header layouts and **absent signatures** are the only
implemented profile. Hashed `0x2003`/H3 writing, disc/update/shared contents,
authentic signatures and retail-console installation are unsupported.

The writer checks RPX structure and XML identity, rejects symlinks and unsafe or
case-colliding paths, refuses existing outputs, detects changed inputs, checks
disk space and keeps unfinished output in a temporary directory. It streams
content with 1 MiB buffers; bounded FST/name metadata is held in memory. FST
logical cluster placement, ownership, COS permissions/resources and metadata/
artwork are research settings awaiting installer/console evidence.

`host.package_integrity` independently reads both profiles without calling the
writer's FST, inventory or serialization functions. It unwraps the key, verifies
every padded plaintext hash, bounds/tree/parents/names/content assignments/file
ranges and zero padding, checks decrypted XML against TMD identity and launch
entry, and optionally compares every file hash and directory with source staging.
Certificate layout and CP/XS issuer agreement are checked; a trusted root and
signature authentication are **not** established. The existing structural
validator still works without a key, with an explicitly narrower result.
The reader retains the historical four-content plaintext profile for existing
evidence. New per-file plaintext packages require one file at offset zero in each
file stream, exact padded lengths, path-derived ownership and complete ticket
rights; the encrypted research writer retains its four grouped contents.
Directory-only streams must be referenced, correctly owned and entirely zero.

## Local inputs and commands

Install the licensed crypto dependency in your Python environment:

```sh
python -m pip install '.[packaging]'
```

Copy `packaging/local-inputs.example.json` into an ignored private configuration
directory. Its paths resolve relative to the configuration file:

```json
{
  "schema_version": 1,
  "signature_policy": "homebrew_unsigned",
  "common_key_file": "common.key",
  "certificate_file": "title.cert"
}
```

The common-key file must contain exactly 16 raw bytes. The certificate file is a
user-managed binary chain with its parents and one RSA-2048 CP and XS signer.
Establish its provenance locally. No key, certificate or donor is downloaded or
provided by CTR2WUP. Keep these files outside the staged folders; their paths and
values are omitted from packaging reports. Keep keys out of command-line
arguments, logs and public reports. The explicit policy acknowledges unsigned homebrew;
it does not establish which patched installer/environment will accept the result.

```sh
python -m host stage-title PATH.rpx --title-id YOUR_APPLICATION_ID --name "My title" --output private/my-staging
python -m host pack-title private/my-staging --config private/packaging/local-inputs.json --output private/my-package
python -m host verify-package private/my-package --common-key-file private/packaging/common.key --expected-staging private/my-staging
python -m host validate-package private/my-package
```

`stage-title` creates original XML/checker artwork and an empty `content/`; it
produces staging, not a WUP. `pack-title` publishes the `.app`, TMD, ticket,
certificate and report only after successful independent decrypted/file checks.
Its `installable_wup` field explicitly awaits installer and console validation.
The report separates decrypted integrity, unauthenticated signatures and unrun
installation. It never claims an authentic or proven installable title.

## Title identity

IDs are explicit; no globally safe homebrew range is assumed. Staging/packing
does not reserve an ID or inspect installed console titles. Check your console
inventory and local projects before using an ID. The existing explicit local
registry command remains available after structural validation:

```sh
python packaging/validate.py PATH --registry private/title-registry.json --reserve "my project identity"
```

The registry covers only local allocations. Same-owner rebuilds may
update the package revision while retaining the title ID; a different owner
cannot take it. Serialize concurrent registry writes. TMD digests and randomized
ciphertext are package revisions; title/save identity remains the explicit ID.
Rebuild/save continuity, installation, Menu launch, exit and uninstall await
physical-console tests. No installation is automated.

## Validation and dependencies

Offline tests exercise streaming across chunk boundaries, empty/Unicode
paths, OpenSSL independently decrypting the ticket and all contents, wrong keys,
ciphertext corruption, malicious FSTs with repaired hashes, source mutation,
unsafe inputs, cancellation and output preservation. `tools/validate-packaging.py`
also packages the real WUT-built RPX, verifies all staged files and exercises both
CLI paths. Its synthetic inputs/package exist only in a temporary directory;
the retained report records that they cannot authenticate a console title.

The encryption dependency is **cryptography 50.0.0**, Apache-2.0 OR BSD-3-Clause;
CTR2WUP selects the BSD-3-Clause grant and preserves its notice in
`docs/licenses/cryptography-LICENSE.BSD`. The library and its native dependencies
remain external Python packages; no crypto binary is bundled here.

Binary layout/alignment/hash facts were reviewed against makefst's `fst.h`,
`tmd.h`, `tik.h`, `certs.h` at
`e363897e29109e6f1125d60376822c3768d3c2d4` and the previously audited Java/C#
packer format descriptions. The writer/reader are original implementations;
no source, binary, XML template, certificate or key from those packers is adopted.
This is a scoped research profile, not a full Wii U package-format implementation.
The per-file plaintext layout follows makefst's stream assignments in
[`main.c`](https://github.com/shinyquagsire23/makefst/blob/e363897e29109e6f1125d60376822c3768d3c2d4/main.c#L250).
NUSPacker also assigns `app.xml` and `cos.xml` to separate streams in its
[content rules](https://github.com/ihaveamac/nuspacker/blob/797b84af32e5bb0c353577a7a3f67e236f7b7ee3/src/com/tim/nuspacker/nuspackage/packaging/ContentRules.java#L123).
These are format references; neither packer's code or assets are incorporated.

The MIT-root makefst candidate bundles GPLv2-or-later PolarSSL, MIT ezxml and
Unlicense miniz. Its code was not incorporated or executed. The plaintext/test-signing route is an original implementation and supplies no
makefst production certificate. The nuspacker/CNUS_Packer
checkouts have no identified license grant and remain research references only.
Dependency pins and shipped license notices remain in the source and third-party notices.
