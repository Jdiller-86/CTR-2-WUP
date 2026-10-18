# M1 completion check

**CTR2WUP Install Check** · title `0005000043543257` · build `@VERSION@` (`@SOURCE_COMMIT@`). FTP `sd-card/` contents to the SD root in binary mode, replacing files, then close FTP. Open **CTR2WUP Install Capture** in signature-patched Aroma. Press **X** for USB or **A** for internal; one destination is sufficient. This complete package supports fresh installation or a guarded update of the verified previous installation. Stop if installation fails; return the capture.

After **Installed files and Menu metadata: PASS**, press Minus to leave Capture. Launch **CTR2WUP Install Check** twice from Wii U Menu: **PREPARED** → Minus; **RECOVERED** → HOME → Wii U Menu. Check both screens and confirm both exits return normally. These two launches check the repaired exit and recovery paths.

FTP the whole `sd:/ctr2wup/install-check/` folder back, ZIP it and attach it with the installation outcome and both Menu-return observations. Include failures. Source and notices: `support/`.
