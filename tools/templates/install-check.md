# Installed-title check

**CTR2WUP Install Check** · title `0005000043543257` · build `@VERSION@` (`@SOURCE_COMMIT@`). FTP `sd-card/` contents to the SD root in binary mode, replacing files, then close FTP. Open **CTR2WUP Install Capture** in signature-patched Aroma. Press **X** for USB or **A** for internal; one destination is sufficient. This complete package supports fresh installation or a guarded update of the verified previous installation. Stop if installation fails; return the capture.

This check covers native startup, Menu return and automatic SD recovery. After **Installed files and Menu metadata: PASS**, leave Capture. Launch **CTR2WUP Install Check** twice: **PREPARED** → Minus; **RECOVERED** → HOME → Wii U Menu. Observe each Menu return. Recovery runs automatically.

FTP the whole `sd:/ctr2wup/install-check/` folder back, ZIP it and attach it with the installation outcome and both Menu-return observations. If an exit hangs, stop and copy that folder before uninstalling. Source and notices: `support/`.
