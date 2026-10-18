# Wii U application protocol

Reuse the compact [installed application's](https://github.com/Jdiller-86/CTR-2-WUP/blob/main/runtime/wiiu/install_check.c)
platform helpers. Install Check and Capture share
[display](https://github.com/Jdiller-86/CTR-2-WUP/blob/main/runtime/wiiu/diagnostic_display.c),
silent AX and the [live ProcUI adapter](https://github.com/Jdiller-86/CTR-2-WUP/blob/main/runtime/wiiu/test_lifecycle_wiiu.h).
Initialize checked SD evidence before graphics; initialize silent audio and the
shared display before lengthy diagnostic work. Keep guest execution, tone voices
and prototype display caches out of an installation/lifecycle-only application.
WUT owns the SD mount: never add a competing mount or unmount it. Use binary
writes, checked sync/close and exact read-back; installation diagnostics also
verify the physical `/vol/external01` file through FSA. Preserve the first error,
show its stage/code and block dependent operations when logging fails. Display
the precise return path and filename. Keep confirmed failure, observed presence
and inconclusive preflight distinct; log the actual bounded evidence behind each
classification. A successful API call alone does not establish its data's meaning.
Validate native paths from the real `fs:/vol/external01` mount root, then each
descendant; devoptab's virtual `fs:/vol` ancestor need not support directory stat.
Keep traversal, symlink and wrong-device rejection enabled.

For plaintext WUPs, keep each file in its own padded content stream with FST
offset zero, following the [packaging profile](packaging.md). Bind installation
logging to the exact generated file inventory and candidate digest. Integrity
checks alone do not establish MCP read compatibility or installed-title behavior.
Require the independent `wup-menu-v3` check for new installation candidates:
complete typed Menu metadata, unsigned COS `cmdFlags`, coherent title/group/OS
identity, complete uncompressed TGA 2 artwork and the original `bootMovie.h264`.
Carry the RPX FILEINFO SDK into `app.xml`; verify its CRC and executable/XML
agreement from the packed bytes. A zero placeholder is not a native startup profile.
ACP reads the movie independently of XML and icons: verify its presence, bounded
size and pinned bytes in the WUP, installed read-back and distributed wheel.
Independently decode the original clip and match native channel movie geometry.
Readable XML and a correct Menu tile do not establish complete launch resources.
Preserve the historical
integrity-only reader for older evidence. An installed title may be updated only
after its version and every compiled predecessor hash match on the selected
device; never infer ownership from a title ID or delete a title to bypass this check.
Changed revisions use distinct TMD content IDs; FST streams and ticket rights use
the separate content indices. Keep both independently verified. Put each payload
revision in a compiled directory so FTP merging cannot mix old stream filenames.
After installation, read back all installed file hashes and log the raw ACP
metadata result before prompting for Menu launch. Installation success and
readable launch metadata are distinct from actual execution.

Use one graphics owner: the native GX2/font/scaling path. Avoid OSScreen and GX2
ownership conflicts; check the pinned SDK's actual API semantics. Always acquire
the [silent AX context](https://github.com/Jdiller-86/CTR-2-WUP/blob/main/runtime/wiiu/diagnostic_audio.c)
to end inherited launcher music, even without playback. Release voices before
AX shutdown; release/reacquire audio on foreground changes. Poll ProcUI directly;
read VPAD and draw only in the foreground. Keep IOS buffers aligned and alive,
publish callback results atomically, and drain accepted asynchronous requests
after cancellation before releasing resources.

The shared [exit helper](https://github.com/Jdiller-86/CTR-2-WUP/blob/main/runtime/wiiu/test_lifecycle.c)
has scoped acceptance through the Aroma-managed route; native installed-title
return remains unresolved. Its early graphics cleanup must not be treated as a
proven native ordering contract. The maintained [Wii U SDL driver](https://github.com/devkitPro/SDL/blob/a8f1e43a4a9d70cbae397433e1a3dd40113679aa/src/video/wiiu/SDL_wiiuvideo.c)
retains graphics/release callbacks while draining ProcUI, then shuts down GX2 and
ProcUI. This difference is an investigation lead, not an established hang cause.
Request the correct Menu/HBL route once, acknowledge saves, process foreground
release and wait for EXITING. Close application reports/FSA clients before the
transition; do not reopen them during it.
Leave process-wide filesystem finalization to WUT's CRT; application cleanup
closes only its own files and clients. Persist the final application I/O barrier
before the transition; later native checkpoints use OSReport only. A cleanup
marker never proves actual Menu return. Compare the complete startup/cleanup
path against the accepted tool's exact source before changing platform behavior;
keep route-specific hypotheses out of the reusable protocol until established.
When the saved application trace ends before a native stall, inspect retained
system logs rather than requiring another identical failure.
[WiiUCrashLogDumper](https://github.com/wiiu-env/WiiUCrashLogDumper/tree/3bb31ca67a6b015cf79f6ef9397f4d039ae60192)
reads SLC `/sys/logs` and copies logs to `sd:/wiiu/crash_logs/`; it does not require
reinstalling or rerunning the failed title. Logs can contain unrelated console
data and remain private. Their absence does not establish a successful exit.
Before delivery, run required runtime/sanitizer/native checks and verify the
source-bound artifact. Cross-builds and API doubles do not establish hardware
behavior. Request only evidence for changed or unresolved paths, with one short
guide and one FTP return folder; retain applicable reviewed hardware evidence.
