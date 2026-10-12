# Remaining acceptance gates (0.14.6)

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
   executable and contains no code. Run the existing helper on a decrypted
   user-owned copy and privately provide the small executable export per
   [cia-intake.md](cia-intake.md). Its exact revision/service/module requirements
   remain unknown; the helper excludes assets, which will also need private access.
3. **Commercial renderer/runtime:** use that revision and private reference
   traces to implement required PICA-to-GX2 draws, services, modules, audio and
   retail saves, then prove boot/gameplay/save/reload/performance on Wii U.
   The new Level-3 RomFS transport does not implement NCCH/IVFC extraction,
   authentication, encrypted cartridge access or commercial asset integration.
4. **Installable WUP:** establish local packaging input provenance and record
   actual diagnostic install, Menu icon/launch/exit and removal without replacing
   an existing title. Synthetic offline packer checks do not prove installer acceptance.
5. **Windows and GUI:** validate the real conversion pipeline, caches/rebuild/
   cancellation and clean Windows execution, then implement the specified GUI.
   The pre-existing unapproved Windows workflow remains local and unpublished.

No console is attached. Further generic CPU/service/renderer work is possible;
it cannot replace these physical gates or reveal the selected game's actual
requirements. Current delivery is a five-app diagnostic RPX/WUHB kit and source,
not an installable commercial WUP. Approximately 20% implementation and 0/7 full
acceptance gates remain the current assessment.
