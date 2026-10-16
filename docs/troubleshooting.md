# Troubleshooting

Use binaries and the guide from the same package. Preserve existing reports and
saves while investigating. Record the displayed build and WUHB/RPX/installed-title
launch route; a saved report and successful Menu return are separate observations.

| Symptom | Action |
|---|---|
| App missing from Aroma | Copy the contents of `sd-card/` to SD root; WUHB files belong under `sd:/wiiu/apps/`. |
| Installer rejects a candidate | Check the package's installer/environment prerequisites, including signature patches for self-signed packages. Return the displayed error. |
| Install check stops before RECOVERED | Preserve the entire install-check folder, including recovery files and partial logs. PREPARED on the first launch is expected; do not delete files or cut power to force recovery. |
| No new TXT result | Check the displayed `RESULT VERIFIED`/`RESULT STORAGE FAILED` status. Return available logs and the SD operation/error number; do not rerun only to obtain a log. |
| Save/reload failure | Preserve the snapshot plus `.pending`/`.previous` siblings. Do not delete them to force a pass. |
| Menu splash hangs or app freezes | Record the last prompt/action, approximate delay and whether music continued; return files already written after recovery. |
| Wrong graphics or audio | Mark the relevant on-screen observation as failed and describe the difference; automatic pixel/PCM checks cannot replace physical observations. |
| Game rejected | Check [compatibility](compatibility.md); commercial profiles and the final injector GUI remain unfinished. |
| devkitPPC/WUT missing | Follow [Contributing](../CONTRIBUTING.md) or [Windows setup](build-windows.md); Linux SDK executables need Linux. |

Copy the whole result folder named by the kit back over FTP and ZIP it. Ordinary
logs are under `sd:/ctr2wup/results/`; the install candidate has its own
`sd:/ctr2wup/install-check/` folder. The install reviewer selects the matching
source child automatically; preserve older children. Recovery fixtures are
original diagnostic data, separate from guest saves and asset blobs.
Include the observed exit behavior when reporting a failure.
Keep game dumps, keys, extracted commercial assets and saves out of public issues.
