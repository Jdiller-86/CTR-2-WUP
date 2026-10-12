# Private filesystem and standalone application lifecycle (0.14.6)

This is shared runtime implementation, independent of any game patch. The original
compiled program uses normal libctru/newlib `fopen`, `fread`, `fwrite`, `fflush`,
`fseek`, `ftell`, `fclose`, `unlink` and the standard save-archive commit request.
It creates a record, rereads it, uses a Japanese filename, deletes a temporary
file, commits, exits, then reloads and updates the record in a fresh process.
The host independently checks both saved files and their exact second-run bytes.
Corrupt snapshots are refused and preserved, rather than replaced with empty saves.

The portable runtime opens `fs:USER`, initializes its session and supports:

| Interface | Commands |
|---|---|
| FSUSER | Initialize / InitializeWithSdkVersion; OpenArchive / CloseArchive; OpenFile; CreateFile / DeleteFile; GetFreeBytes; ControlArchive(commit); scoped read-only OpenFileDirectly |
| FSFILE | Read / Write; GetSize / SetSize; Flush / Close |
| Explicit archives | Savedata (4), private virtual SDMC (9), and current-process read-only RomFS view (3), supplied by the platform |

No guest path names a host file. The platform owns a volume until guest teardown;
mounts and file sessions retain its lifetime. Archive IDs are generation checked
and bound to their service session. File sessions survive archive closure and
are retained across kernel-handle duplication. Closing a file session invalidates
all copies; final kernel-handle close also releases it. Deleting a live file is
explicitly refused in this bounded profile. Archive/file/handle exhaustion rolls
back ownership, including a create request when a file session cannot be allocated.

Volumes have 16 root files, at most 1 MiB per file and 4 MiB total data. Request
buffers are limited to 1 MiB. ASCII and valid UTF-16 paths are decoded to exact,
case-sensitive UTF-8 names; traversal, nested directories, device syntax,
separators and invalid Unicode are refused. Sparse extensions are zero filled;
reads clamp at EOF without truncating 64-bit offsets. Read/write rights and
read-only volumes are enforced. Guest buffers are checked across the entire
requested range, including physical aliases of IPC storage, before file or guest
output changes. Unsupported wire formats and commands fault visibly.

Writes change the buffered volume. Flush and commit succeed for a writable
volume only when its platform persistence callback succeeds. A failed flush can
leave dirty buffered data but does not claim a successful save. The original
snapshot adapter uses explicit little-endian framing, bounded whole-input
validation and a CRC32 corruption check. CRC32 provides no authenticity. It
publishes through an exclusive sibling `.pending` file. v0.14.6 validates any
existing primary, moves it to an absent `.previous`, then renames the stage into
the vacant primary name. Successful publication removes the old backup. A failed
publish attempts rollback; if rollback fails, both siblings remain available.
This avoids relying on POSIX replacement semantics: WUT passes rename directly
to FSA. The exact console failure stage is unknown; a no-overwrite filesystem
model reproduces the assumption and exercises the corrected publisher.

If the primary is missing, a validated `.previous` can be loaded with the explicit
RECOVERED status. A corrupt primary is never masked by a backup or reset. Stale
`.pending` or ambiguous existing backup blocks another update visibly; preserve
both for investigation. There is no concurrent-writer or power-loss durability
guarantee. Failure/abrupt-process/rollback/recovery cases run under sanitizers.

The user's uploaded snapshot is CRC-valid, with `/progress.dat` and `/記録.dat`
at visit 1. A private copy advances to visits 2 and 3 in the updated desktop
runner while the original remains unchanged. Console update/reload acceptance
is pending. The guest now names its failing phase; the independent result TXT
records guest message, load status and persistence errno even if full tracing fails.

Read-only Level-3 RomFS assets are implemented separately; ordinary libctru
handles nested names and directory listing in guest code. See
[romfs-assets.md](romfs-assets.md). NCCH/IVFC extraction/authentication, cartridge
assets, generic FSUSER directories, rename/subfiles, extdata, secure values,
retail save formatting and commercial integration remain unfinished.

## Standalone APT

The platform explicitly enables one standalone application (`APPID_APPLICATION`,
0x300). Only `APT:U` is advertised; privileged `APT:S`/`APT:A` remain unavailable.
GetLockHandle returns copied handles to a retained recursive mutex. Initialize
returns two real retained events; Enable queues one empty wakeup and signals the
resume event. NotifyToWait, GlanceParameter and ReceiveParameter run the checked
local state machine. Glance preserves the pending parameter, Receive consumes it.
Checked static descriptors support the shell-open query and enable-transition
unlock. CheckNew3DS returns the configured Old-family policy. PrepareToClose /
CloseApplication terminate the process; Finalize unregisters it while copied
objects retain their lifetime. This lets the unmodified libctru event thread,
LightEvent/arbitration waits, normal initialization and exit code actually run.

The original boot checks `aptIsActive`, `aptShouldClose` and `aptMainLoop` in guest
code. A single-instruction trace captures successful APT and FS requests; remaining
lookup failures are exactly `APT:S`, `APT:A`, and optional `ptm:sysm`. The compiled
runner explicitly mounts an empty read-only diagnostic virtual SDMC. This is not
access to the user's SD card. There is no shell lid, HOME menu, sleep, cross-title
launch, shared font, applet, CPU-time quota or New 3DS implementation. Unsupported
operations fail; `complete_service_startup` remains false in reports.

## Build and console check

```sh
make -C tests/filesystem check sanitize
make -C tests/apt check sanitize
python3 tools/validate-libctru.py --application filesystem --wiiu \
  --output artifacts/reports/my-save-test
python3 tools/validate-libctru.py --application boot --trace-startup \
  --output artifacts/reports/my-startup-trace
```

The additional `ctr2wup-libctru-filesystem.rpx` / `.wuhb` executes the same original
ARM program through external AOT. Launch with an accessible mounted SD card; the existing diagnostic creates
its `ctr2wup` directory on the WUT-owned SD mount.
On a fresh diagnostic save it must display/log `SAVE 1 PASS` and `state=exited`.
Plus restarts from the snapshot and must display/log `RELOAD PASS`; exit/relaunch
must also reload. The private path is
`sd:/ctr2wup/filesystem-test.snapshot`. Keep the existing file and any
`.pending` / `.previous` for investigation if the test fails; never replace a failed load with
an empty save. Minus exits the host. This is an original diagnostic save and
RPX/WUHB, not a Mario save or an installable WUP. The initial snapshot is received; successful physical update/reload is pending.
Use the current [console retest](console-retest.md) and return its new result TXT.
