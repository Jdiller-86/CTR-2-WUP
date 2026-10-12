# Save and result-file check (0.14.6)

The user reports that Native, Guarded AOT and Compiled AOT completed without
errors on **0.14.5**. Repeating their long guides is unnecessary for this round.
No new TXT reports were found. The uploaded text logs identify 0.14.0–0.14.3;
the snapshot is valid and contains both expected records at visit 1. It proves
an initial write, but cannot establish the failed save update/reload stage.
See [the evidence review](console-evidence.md).

## First: Compiled Save

1. Extract the **0.14.6 console testing kit** and copy `sd-card/` to the SD root,
   replacing the previous apps. Keep `sd:/ctr2wup/` and all its existing files.
   Preserve `filesystem-test.snapshot`, `.pending` and `.previous` if present.
2. Launch **CTR2WUP Compiled Save 0.14.6**. Check that the in-app footer says
   **BUILD 0.14.6** and **RESULT VERIFIED**, followed by its result filename.
   An RPX launcher can use `rpx/ctr2wup-libctru-filesystem.rpx` instead.
3. Wait for the first save/reload check to complete, then press **Plus** when
   prompted. The app checks the new guest's PASS and exit automatically.
4. Press **Minus** after the guide completes. If it requests a fresh launch,
   launch this app once more and follow its two prompts. An existing visit-1
   snapshot should already permit fresh-launch reload confirmation.
5. If **RESULT STORAGE FAILED** or a guest/save failure appears, stop that test
   and exit with Minus. Return the available files and short error text.
   Do not delete a snapshot or its siblings to make a failure disappear.

## Then: Compiled RomFS

If the save app exits normally, launch **CTR2WUP Compiled RomFS 0.14.6**.
This one-step automatic test reads original root, nested and Unicode-named assets,
checks seeking and directory listing, and rejects writes. Wait for its PASS/exit
and **GUIDED TEST COMPLETE**, then press Minus. No game file is needed.

## Upload here

Copy the new files from **`sd:/ctr2wup/`** and upload them in this chat,
individually or in a ZIP below 32 MiB:

- `result-compiled-save-0.14.6-*.txt` from each save launch;
- `result-compiled-romfs-0.14.6-*.txt` from the asset test;
- any matching `compiled-save-*.txt` / `compiled-romfs-*.txt` full traces;
- the updated `filesystem-test.snapshot` and any `.pending` / `.previous` siblings.

Add a brief note about launcher return or any freeze. If no new result exists,
report that and any displayed `SD ERROR` operation/number; do not repeat a failed
run just to generate a file. Screenshots are optional.

## Why these changes

Each launch now creates a **separate 4 KiB plain-text result file** independently
of the full trace handler. Every update closes the file and reads back all bytes
before displaying **RESULT VERIFIED**. Named guide results, self-tests, guest
state/fault, save errors and full-trace status are retained there. A full-trace
error can be reported through this independent result; result-storage failure
pauses guest execution. VERIFIED means byte read-back, not power-loss durability.

Save updates no longer require rename to overwrite an existing file. The old
committed snapshot moves to `.previous`, the staged file moves into the vacant
primary name, and failures attempt rollback. Recovery can use a validated backup
when the primary is absent; corrupt or ambiguous data remains a visible failure.
This fixes a tested platform assumption, but the exact console failure and new
result-file behavior still need the tests above. These RPX/WUHB diagnostics do
not establish commercial-game or WUP installation support.
