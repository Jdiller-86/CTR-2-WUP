# Install check

This checks the new installation and Wii U Menu launch path, about five minutes.
Use **WUP Installer GX2** under **Aroma with signature patches**
(`wiiu/environments/aroma/modules/setup/01_sigpatches.rpx`). No keys are needed.
Earlier screen, controls, tone and stability checks do not need repeating.

1. **Copy one folder.** FTP the contents of the kit's **`sd-card/`** folder to
   your SD root in binary mode. Keep your existing files. Close FTP before
   opening the installer.
2. **Install with WUP Installer GX2.** Select **CTR2WUP-M1** from its installation
   list and your normal system-memory or USB target. Check the supplied title
   name and ID. If that ID belongs to another title, stop and report it.
   Record success or the exact installer error; no repeated attempts are needed.
3. **Launch twice from Wii U Menu.** Open the new installed title, check that
   both screens appear, then press **Minus** and confirm return to Wii U Menu.
   Repeat once to check startup after cleanup. No button tour or countdown.
4. **Return one folder.** After exiting, FTP **`sd:/ctr2wup/install-check/`**
   to your computer, ZIP the whole folder and attach it here. Do not select or
   pair logs: kit identity is already inside. No PC command is needed.

If the app hangs or no folder is created, say where it stopped and send any
partial folder. Do not rerun to produce a pass or deliberately cut power.
Exclude the install package, games, saves and keys from your return.

```text
Firmware / Aroma (only if changed):
WUP Installer GX2 version (if shown) / system memory or USB:
Install: passed, or exact error:
First Menu launch: both screens? Minus returned to Menu?
Second Menu launch: both screens? Minus returned to Menu?
Any other problem or recovery action:
```

`support/` contains checksums, source and notices; it has no additional steps.
