# M1 completion check

**CTR2WUP Install Check** · title `0005000043543257` · build `@VERSION@` (`@SOURCE_COMMIT@`). Use WUP Installer GX2 under Aroma with `01_sigpatches.rpx`. FTP `sd-card/` contents to the SD root in binary mode, replacing the supplied files, then close FTP. Install `CTR2WUP-M1`; stop if that title ID belongs to another app.

Launch the title twice from Wii U Menu. Each time, check both screens and press Minus to return to Menu. Recovery should show **PREPARED** on the first launch and **RECOVERED** on the second. No power cut or other diagnostics are needed.

FTP the whole `sd:/ctr2wup/install-check/` folder to the computer, ZIP it and attach it with a brief installer/screen/Menu-return observation. Include partial failures and any installer error. Source, checksums and notices are in `support/`.
