# Diagnostic troubleshooting

Use the application and guide from the same release. Record the displayed build
version, WUHB or RPX launch route, and firmware/Aroma/launcher versions when known.
Keep existing reports and snapshots while investigating a failure.

| Symptom | What to check and return |
|---|---|
| App missing from Aroma | Copy the kit's `sd-card/` contents to SD root. WUHB files belong in `sd:/wiiu/apps/`; a source ZIP or RPX is a different download. |
| No new TXT result | Check the displayed build and `RESULT VERIFIED` / `RESULT STORAGE FAILED` footer. Return any displayed SD operation/error number, plus available matching files. A missing report is useful evidence. |
| Save/reload failure | Preserve `filesystem-test.snapshot`, `.pending` and `.previous`. Follow the versioned [Save/result check](console-retest.md); do not delete the files to force a pass. |
| Wii U Menu splash never finishes | Record which app/version and launch route were used, and whether music continued. Report the observed return behavior separately from saved test results. |
| System freezes during a guide | Record the last prompt/action and approximate delay. After recovery, return any files already written. Do not repeat a known failing run solely to obtain a screenshot or log. |
| GX picture differs or never appears | Mark the picture step with **L** and return the [GX result](gx-transfers.md). A desktop reference image cannot substitute for what the console displayed. |
| Console guide passes but injector rejects a game | Commercial execution and the final GUI remain unfinished. Read [compatibility](compatibility.md) and [current status](../STATUS.md) for the supported boundary. |
| Desktop build cannot find devkitPPC/WUT | Follow [Contributing](../CONTRIBUTING.md) or [Windows setup](build-windows.md). Linux SDK executables require Linux; select the native Windows toolchain on Windows. |

Diagnostic results are under **`sd:/ctr2wup/`**. ZIP the new `result-*.txt` files
and requested original diagnostic traces, then attach them to the console-result
issue form or the current development conversation. Include a brief observation
of the picture and exit behavior. Check files for private paths/data before
posting. Share commercial dumps, keys or saves only through the separately agreed
private intake process; they do not belong in GitHub issues.

The older Native/Guarded/Compiled successes are already recorded. The pending
0.14.6 Save/RomFS round and optional 0.16 Asset Files / 0.17 GX round have different
purposes; [testing](testing.md) explains which evidence each provides.
