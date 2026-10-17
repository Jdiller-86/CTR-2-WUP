# M1 completion check

**CTR2WUP Install Check** · title `0005000043543257` · build `@VERSION@` (`@SOURCE_COMMIT@`). FTP `sd-card/` contents to the SD root in binary mode, then close FTP. In signature-patched Aroma, open **CTR2WUP Install Capture**. Choose **A: internal memory** or **X: USB**; one destination is sufficient. It verifies the package and logs installation. Minus returns to Menu.

After installation succeeds, launch **CTR2WUP Install Check** twice from Wii U Menu. Check both screens and press Minus to return each time. Recovery should show **PREPARED**, then **RECOVERED**. Stop if installation fails.

FTP the whole `sd:/ctr2wup/install-check/` folder back, ZIP it and attach it with the installation outcome and screen/Menu-return observations. Include failures. Source, checksums and notices are in `support/`.
