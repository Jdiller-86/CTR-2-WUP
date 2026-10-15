# Original homebrew WUP packaging

## Current original install-test profile (0.27.0)

`host.package_homebrew` adds original plaintext **`0x2000`** contents and a
self-generated test-signing chain for a signature-patched homebrew environment.
It requires **no common key, production certificate, donor title or game input**.
The independent verifier checks content/FST/XML/identity and the generated
signature chain. This establishes package integrity and test-signature
consistency; it does not establish Nintendo authenticity or physical installation.

The candidate targets **WUP Installer GX2 under Aroma with signature patches**,
including `wiiu/environments/aroma/modules/setup/01_sigpatches.rpx`. The module
is not bundled and its presence is not assumed. Installer acceptance and actual
Wii U Menu launch/exit/relaunch remain unverified until a physical return.
The explicit diagnostic ID **`0005000043543257`** is not a reserved safe range:
check the console for an unrelated title with that ID before installation.

The focused `native-install` diagnostic checks its actual title identity and
runs safe original SD recovery probes in `sd:/ctr2wup/install-check/`.
[One short guide](m1-acceptance.md) covers two brief new installed-title launches;
prior native controls/tone/ten-minute, ordinary exits and save reload are reused.
The kit exposes one `START-HERE.md` and one `sd-card/` deployment tree. Source,
checksums, receipts and notices belong under `support/`, with no extra tester steps.

## Existing encrypted research profile

The encrypted `0x2001` writer remains supported with its separately configured
local inputs. Those inputs are unnecessary for the new original plaintext test.

Version 0.4.0 implements an original, scoped encrypted writer and a separate
decrypting verifier. **No package has been installed or launched on a physical
Wii U.** That encrypted offline fixture uses invented test certificates and a
synthetic key; its pass does not establish installer acceptance. Commercial
conversion remains disabled. The original plaintext candidate above is a separate
new physical install-test route.

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

`host.package_integrity` independently reads the result without calling the
writer's FST, inventory or serialization functions. It unwraps the key, verifies
every padded plaintext hash, bounds/tree/parents/names/content assignments/file
ranges and zero padding, checks decrypted XML against TMD identity and launch
entry, and optionally compares every file hash and directory with source staging.
Certificate layout and CP/XS issuer agreement are checked; a trusted root and
signature authentication are **not** established. The existing structural
validator still works without a key, with an explicitly narrower result.

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
values are omitted from packaging reports. Do not paste keys into command-line
arguments or conversations. The explicit policy acknowledges unsigned homebrew;
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

The registry covers only this user's local allocations. Same-owner rebuilds may
update the package revision while retaining the title ID; a different owner
cannot take it. Serialize concurrent registry writes. TMD digests and randomized
ciphertext are package revisions; title/save identity remains the explicit ID.
Rebuild/save continuity, installation, Menu launch, exit and uninstall await
physical-console tests. No installation is automated.

## Evidence and dependency audit

20 new offline tests exercise streaming across chunk boundaries, empty/Unicode
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

The MIT-root makefst candidate bundles GPLv2-or-later PolarSSL, MIT ezxml and
Unlicense miniz. Its code was not incorporated or executed. Earlier research
focused on the encrypted profile; the new plaintext/test-signing route above is
an original implementation and supplies no makefst production certificate. The nuspacker/CNUS_Packer
checkouts have no identified license grant and remain research references only.
See `foundation-audit.md` for pins and licensing constraints.
