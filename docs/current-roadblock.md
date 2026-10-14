# Remaining acceptance gates (0.23.0)

The product remains a Windows GUI that converts owned 3DS games into individual
Wii U WUP titles. Approximately 20% implementation and 0/7 full gates accepted
remain the assessment. Original diagnostics are separate from commercial play.

The user is available with Windows 11, an SD reader and Aroma. Reviewed October 13
Audio Check/GX Transfers 0.20.0 and Guest Draws 0.23.0 results establish scoped
passes and user-confirmed silence, pictures and exit. Save/RomFS already have
reviewed results. No blanket repeat is requested. See [evidence](console-evidence.md).

Current requirements:

1. **Draw progress and accurate evidence:** the rendered second presentation
   takes 57.33 seconds in the trace; the visual guide accepted an answer before
   that scene. Gate confirmation on the drawn frame, measure individual phases
   and investigate the fixed guest-instruction-per-refresh budget. Aggregate
   host refresh time is not a GPU/CPU breakdown or a game-performance result.
2. **Mario sample bytes and runtime requirements:** local preparation/asset
   reports now agree with the verified USA executable and describe 1,756 files.
   Raw CCI/RomFS bytes remain on the PC. The
   [standalone sample helper](private-asset-samples.md) works with the existing
   reports without a wheel update. Export a bounded selected sample ZIP to
   inspect shader/system/audio containers and possible nested modules; do not
   infer formats or runtime use from filenames or declared services alone.
   [First-title findings](first-title.md) preserve these evidence boundaries.
3. **Platform acceptance:** complete documented repeat launch, Home/APT,
   stability, SD recovery and original WUP install/Menu-launch checks with exact
   environment/build identity. The three successful sessions do not complete
   those full criteria.
4. **Renderer/runtime implementation:** broader shaders, TEV/sampling, native
   GX2 translation, coherency, dynamic modules, guest audio and retail saves
   remain open. Independent original-fixture development can continue.
5. **Conversion/GUI:** prove the full native Windows conversion and WUP
   install/rebuild/save continuity, then complete GUI acceptance.

GitHub management and publication are working through trusted-main Actions.
Full Project statuses and gate acceptance remain authoritative; the scoped
physical passes do not close parent tasks or full milestones automatically.

## Historical handoffs

### v0.17.0 handoff

The final product remains an UWUVCI-style **Windows GUI → Inject → per-game WUP →
Wii U Menu gameplay**. The CLI and original diagnostics are its shared foundations.
No commercial profile, playable Mario build or finished GUI is enabled.

The user reports that **Native, Guarded AOT and Compiled AOT completed without
errors on 0.14.5**, including the previously problematic input/exit round.
No new logs were found, so these are user-reported observations. The uploaded
text logs are older; the valid two-record visit-1 snapshot establishes an initial
write, but Save failed at an unspecified stage. Exact causes of the missing logs
and save failure remain unconfirmed. See [console-evidence.md](console-evidence.md).

## Work completed independently

v0.14.6 adds a separate closed/read-back-verified TXT result, portable save
publication with rollback/recovery, and current-process read-only RomFS asset
transport. An original normal-CRT/libctru program reads nested/Unicode assets,
seeks, lists directories and denies writes on desktop, under sanitizers and in
an emulated big-endian PPC Linux process. Shared diagnostic time and ARM data
semantics needed by that unmodified program are independently checked.
See [romfs-assets.md](romfs-assets.md) and [filesystem.md](filesystem.md).

## Next actions

1. **Physical saves/results/assets:** run Compiled Save 0.14.6, then the quick
   Compiled RomFS test if exit works. Upload their new `result-*-0.14.6-*.txt`,
   any full traces, and the snapshot/siblings as described in
   [console-retest.md](console-retest.md). No long native/input repeat is needed.
   Read-back and emulated PPC checks cannot establish real SD behavior or Wii U stability.
2. **Mario executable intake:** the supplied CIA exceeds the transfer tool's
   32 MiB limit. The received small intake ZIP reports an outer-encrypted
   executable and contains no code. Use the new local key/seed preparation route in `encrypted-input.md`, or
   run the existing helper on an already-decrypted user-owned copy and privately provide the small executable export per
   [cia-intake.md](cia-intake.md). Its exact revision/service/module requirements
   remain unknown; the helper excludes assets, which will also need private access.
3. **Commercial renderer/runtime:** use that revision and private reference
   traces to implement required PICA-to-GX2 draws, services, modules, audio and
   retail saves, then prove boot/gameplay/save/reload/performance on Wii U.
   Canonical NCCH/IVFC verification, local decryption, private asset preparation
   and base texture decoding are now implemented. Signature authentication,
   commercial integration and actual PICA draws remain open.
4. **Installable WUP:** establish local packaging input provenance and record
   actual diagnostic install, Menu icon/launch/exit and removal without replacing
   an existing title. Synthetic offline packer checks do not prove installer acceptance.
5. **Windows and GUI:** validate the real conversion pipeline, caches/rebuild/
   cancellation and clean Windows execution, then implement the specified GUI.
   Reviewed Windows CI and validation workflows are included and authorized for
   publication; their actual hosted results must establish Windows acceptance.

No console is attached. Further generic CPU/service/renderer work is possible;
it cannot replace these physical gates or reveal the selected game's actual
requirements. Current delivery is a five-app diagnostic RPX/WUHB kit and source,
not an installable commercial WUP. Approximately 20% implementation and 0/7 full
acceptance gates remain the current assessment.


October 12: the user is again away and authorizes continuing with earlier verbal
confirmations. Their U.S. Wii U is reported to use latest firmware, Aroma and WUP
Installer GX2; exact versions remain unknown. v0.15.0 completes further independent
local ROM preparation/key-import work and an original encrypted execution pipeline.
No personal key is requested here. When available, local preparation can generate
the small executable intake without uploading keys. The existing v0.14.6 console
kit still supplies the Save/RomFS report round; this host release does not require
repeating completed long guides. Actual game bytes/requirements, renderer scenes,
local key-export/Windows proof and console/install acceptance remain external gates.


v0.16.0 completes further independent asset/texture work. The new optional
Asset Files diagnostic uses a small original SD blob and existing guided TXT
reporting; its physical file I/O/lifecycle test awaits the user's return.
No completed long test must be repeated now. Further generic work is possible,
but it cannot settle actual Mario requirements, GX2 reference scenes, console
performance, native Windows or installed WUP acceptance without those resources.


v0.17.0 adds standard CPU-backed GX operations and the original GX Transfers
console diagnostic while awaiting resources. PICA draw lists remain unsupported.
GitHub now has live milestone/issues tracking and reviewed CI/artifact/agent/docs
setup. Projects and repository administration changes are blocked by the GitHub
integration's permissions; the desired setup is recorded without claiming it live.
