# Private filesystem and standalone lifecycle

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
publishes through an exclusive sibling `.pending` file. The loader validates any
existing primary, moves it to an absent `.previous`, then renames the stage into
the vacant primary name. Successful publication removes the old backup. A failed
publish attempts rollback; if rollback fails, both siblings remain available.
This avoids relying on POSIX replacement semantics: WUT passes rename directly
to FSA. A no-overwrite filesystem model checks this platform constraint and the
publication/rollback paths.

If the primary is missing, a validated `.previous` can be loaded with the explicit
RECOVERED status. A corrupt primary is never masked by a backup or reset. Stale
`.pending` or ambiguous existing backup blocks another update visibly; preserve
both for investigation. There is no concurrent-writer or power-loss durability
guarantee. Failure/abrupt-process/rollback/recovery cases run under sanitizers.

The guest names its failing phase; the independent result TXT records guest
messages, load status and persistence errno even if the full trace fails.

Read-only Level-3 RomFS assets are implemented separately; ordinary libctru
handles nested names and directory listing in guest code. See
[romfs-assets.md](romfs-assets.md). NCCH/IVFC preparation is a separate host stage. Generic FSUSER directories,
rename/subfiles, extdata, secure values,
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

## Validation

```sh
make -C tests/filesystem check sanitize
python3 tools/validate-libctru.py --application filesystem --wiiu --output artifacts/filesystem
```

The original compiled program exercises UTF-16 paths, newlib file I/O, explicit
commit and fresh-process reload. Independent snapshot inspection and API tests
cover corruption, interrupted publication, resource lifetime, bounds/aliases and
failure preservation. Native diagnostics use only an original private save;
these checks do not establish retail-save compatibility or power-loss durability.
Preserve primary, `.pending` and `.previous` files when investigating a failure.
