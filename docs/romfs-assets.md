# Current-process read-only RomFS assets (0.14.6)

This shared M3 foundation lets ordinary libctru `romfsInit` and newlib code read
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
322-byte image, C header and manifest; it labels IVFC authentication false.

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
PPC Linux**. The Compiled RomFS RPX/WUHB is built for Wii U; physical execution
is pending. The portable FS suite adds lifetime, copied-handle, sparse >4 GiB,
malformed-descriptor, read-failure atomicity and permission/alias checks.

```sh
source /workspace/ctr2wup-tools/environment/activate.sh
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
or timezone accuracy is claimed. See [cpu-stack.md](cpu-stack.md).

## Remaining work

Full NCCH RomFS/IVFC extraction and integrity checks, decryption, cartridge and
other archive selectors, extdata/retail saves, assets for the selected commercial
revision and Windows conversion integration remain unfinished. This diagnostic
does not enable a retail profile, render PICA scenes or establish console speed.
