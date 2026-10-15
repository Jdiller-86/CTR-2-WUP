# Native and GX diagnostics

Build `@VERSION@` · source `@SOURCE_COMMIT@`.
Use only the app and prompts requested for a changed capability. Accepted
controls, sound and ten-minute stability checks do not need repeating for a
new build. This package is not a new hardware test request.

1. FTP `sd-card/` contents to the SD root in binary mode. Preserve existing
   files and close FTP before launching an app in Aroma. No installation or
   game files are required.
2. **Native:** follow only the requested on-screen prompts. **R** confirms an
   observation; **L** records a problem. **ZL** skips a prompt. **Minus** exits
   without requiring the full tour or stability countdown.
3. **GX Transfers:** if this changed path needs verification, wait for TV
   green/red/green and a blue GamePad. **R** confirms the picture; **L** records
   a problem. The original guest checks its operations and exits automatically.
   Press **Minus** to return normally.
4. After closing the app, FTP the whole **`sd:/ctr2wup/results/`** folder to
   your computer, ZIP it and return that ZIP. Reports contain source identity;
   do not select or pair logs. Add a short note about the requested observation
   and whether Minus returned normally.

Keep partial failures and any displayed error; do not rerun to obtain a pass.
`support/` contains source, reference frames, notices and receipts with no extra
user steps. No PC command is required. Do not add games, saves or keys.
