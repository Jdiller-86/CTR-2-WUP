# Current-process read-only RomFS assets

This shared runtime interface lets ordinary libctru `romfsInit` and newlib code read
an explicitly supplied **decrypted Level-3 RomFS view**. No game patch or host
path exposure is involved. The original fixture contains three tiny files and
no commercial content.

## Implemented boundary

The platform initializes a zeroed `ctr_fs_romfs` with a borrowed read callback,
context and 64-bit byte length, then mounts it before guest execution. It owns
the provider until all mounts and retained file handles close; active views
cannot be reinitialized/closed. The callback must read the requested range or
return failure. The runtime stages reads before publishing guest bytes so a
failed/partial provider cannot leak partially written output.

FSUSER `OpenFileDirectly` (0x08030204) recognizes archive 3, the empty archive
path, 12-byte all-zero binary current-process file selector and read-only flags.
It validates static descriptors and guest buffers. FSFILE supports Read, GetSize,
Flush and Close. Writes/resizes are denied. Handles retain ownership across
copies; whole-buffer permissions, IPC physical aliases, EOF and overflowing
64-bit offsets are checked. Requests are bounded to 1 MiB, while the provider's
file size may exceed 4 GiB. Unmounted/unknown archives fail visibly.

Unmodified libctru resolves directories, filenames and Unicode **inside guest
code** by reading Level-3 metadata. This is not a new implementation of generic
FSUSER directory commands. `tools/generate-romfs-fixture.py` creates the original
322-byte image, SHA-256 C header, manifest and synthetic IVFC wrapper. Hash
consistency does not establish signature authenticity.

## Original application and checks

`tests/homebrew/romfs.c` runs normal CRT/APT/FS/newlib startup and checks:

- root file bytes;
- nested binary reads, seek and EOF;
- a Japanese filename;
- directory listing through `opendir`/`readdir`;
- rejection of a write-open, then ordinary unmount and process exit.

It emits `CTR2WUP compiled RomFS PASS` only after all checks. Desktop and
ASan/UBSan runs exit after 49,550 instructions with zero interpreter fallback.
The same original generated program also passes in **QEMU emulated big-endian
PPC Linux**. The optional native build uses the same runtime. The portable FS suite adds lifetime, copied-handle, sparse >4 GiB,
malformed-descriptor, read-failure atomicity and permission/alias checks.

```sh
python3 tools/validate-libctru.py --application romfs --wiiu \
  --output artifacts/reports/my-romfs
make -C tests/filesystem check sanitize
```

Normal libctru filename/time handling exposed three shared prerequisites:
a read-only shared time page, the defined ARM STM writeback form with its base
lowest in the list, and ARMv6 U=1/A=0 unaligned single-data accesses. These were
implemented in the runtime and checked against ARM1176 references, without
changing the program's library implementation. Time starts at an explicitly
synthetic Unix-zero epoch and advances with the guest clock; no real console RTC
or timezone accuracy is claimed. See [cpu.md](cpu.md).

## Host asset preparation

`prepare-rom` verifies the complete canonical three-level IVFC tree whenever
RomFS exists. `prepare-assets` performs the same full verification after checked
decrypted container inspection, validates Level-3 metadata, and exports a private
blob and manifest:

```sh
python3 -m host prepare-assets "private/prepared/game.cxi" \
  --output "private/mario-assets"
```

The output contains `romfs.bin` (Level-3 bytes) and `asset-manifest.json`, including
source/code revision, full-tree integrity, blob SHA-256 and virtual file offsets,
sizes and hashes. Directory parents/siblings/hash chains, Unicode components,
cycles/orphans, duplicate names, ranges, alignment and bounded budgets are
checked. Virtual names never become host extraction paths. Unsupported IVFC
layouts and corruption outside the old NCCH superblock check fail explicitly.
All declared physical blocks are checked, including zero final-block padding.
NCCH's checked superblock must cover the complete master table. This establishes
hash consistency, not authentic signatures or commercial compatibility.
Output publication is atomic and refuses replacement; sources remain unchanged.

When the blob exceeds the transfer limit, use the
[standalone private sample helper](private-asset-samples.md) to share only explicit
hash-checked guest files and a small receipt. It works with existing schema-1
reports without reinstalling the host wheel.

## File-backed provider

`ctr_romfs_file` opens an explicitly configured immutable blob, streams its
SHA-256 and requires the expected manifest hash before mounting. It lends the
same read-only callback contract to guest FS; libctru still resolves filenames.
It closes after guest mounts/handles release and creates no extra SD mount/client.
The current standard-C `fseek`/`ftell` provider is limited to **LONG_MAX** on the
target (2 GiB minus one byte on 32-bit PPC), despite the abstract provider's
64-bit/sparse-file support. Backing must remain immutable while mounted; this
is an open-time hash check, not per-read authenticated storage. Actual SD startup
cost, file access and lifecycle behavior still require console evidence.

Build the optional original Asset Files diagnostic with:

```sh
python3 tools/validate-libctru.py --application romfs --wiiu --romfs-file \
  --output artifacts/reports/my-romfs-file
```

The validator checks identical complete guest results for RAM/file providers and
rejects a corrupted blob before execution. `tools/validate-asset-pipeline.py`
reuses its `romfs/` directory to prove original encrypted CIA -> local preparation
-> full IVFC -> private asset blob -> actual compiled libctru file access, with
transient synthetic keys and zero interpreter fallback.

## Limits

Commercial asset/revision integration, other cartridge/archive selectors,
extdata/retail saves and Windows conversion integration remain unfinished. Signature authenticity is unverified. This diagnostic does
not enable a retail profile, render PICA scenes or establish console speed.
