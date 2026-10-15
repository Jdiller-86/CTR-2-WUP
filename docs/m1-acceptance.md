# Install check

This checks the **new WUP installation and Wii U Menu launch path**, about five
minutes. The earlier screen, controls, touch, tone, ten-minute stability and
homebrew exit results remain accepted. **Do not repeat those guides.**

Only a kit with an actual install package is distributed. Its `START-HERE.md`
shows the exact title name and ID and whether installation is ready. Keep using
WUP Installer GX2 under **Aroma with signature patches** enabled (the
`wiiu/environments/aroma/modules/setup/01_sigpatches.rpx` module). No keys or
certificates need to be supplied or returned.

1. **Copy one folder.** FTP the contents of the kit's **`sd-card/`** folder to
   your SD root in binary mode. Keep your existing files. Close FTP before
   opening the installer.
2. **Install with WUP Installer GX2.** Select **CTR2WUP-M1** from its installation
   list and your normal system-memory or USB target. Check the supplied title
   name and ID. If that ID belongs to another title, stop and report the
   collision. Record the install result, or the exact error; a failed install
   is useful evidence and does not need repeated attempts.
3. **Launch twice from Wii U Menu.** Open the new installed title, check that
   both screens appear, then press **Minus** and confirm return to Wii U Menu.
   Repeat that short launch once. No button tour, tone test or countdown is
   needed. Two brief launches check the newly installed route can start again after cleanup.
4. **Return one folder.** After exiting, FTP **`sd:/ctr2wup/install-check/`**
   to your computer, ZIP the whole folder and attach it here. Do not select or
   pair individual logs: the kit identity is already inside that folder.
   Answer the short card below in your message. No PC command is needed.

If the app hangs or no folder is created, say where it stopped and send any
partial folder. Keep the failure; do not rerun just to produce a pass. Leave the
SD inserted and do not deliberately cut power. Do not include the install
package, game files, saves, keys or certificates in your return.

```text
Firmware / Aroma: unchanged from 5.5.6 U / Beta 27, or:
WUP Installer GX2 version (if shown) / system memory or USB:
Install: passed, or exact error:
First Menu launch: both screens? Minus returned to Menu?
Second Menu launch: both screens? Minus returned to Menu?
Any other problem or recovery action:
```

The kit's **`support/`** folder contains identity, checksums, source and required
notices for review; it has no additional user steps. Installer acceptance and
actual Menu launches remain unverified until this focused new return is
reviewed. Earlier accepted results are carried forward for unchanged behavior;
a source/version change alone is never a reason to repeat them. Any later test
request must identify the changed capability, why existing evidence cannot
cover it and the smallest useful action.
