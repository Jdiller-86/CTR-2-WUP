# Audio Check and GX Transfers

Build `@VERSION@` · source `@SOURCE_COMMIT@`.
Use this kit only for a changed launch-audio or lifecycle path. Completed
unchanged checks do not need repeating.

1. FTP `sd-card/` contents to the SD root in binary mode. Preserve existing
   files, close FTP and launch the apps in Aroma.
2. Open **Audio Check** with TV and GamePad volume audible. **R** confirms launch
   silence; **L** records a problem. **A** plays the short tone; confirm with R
   only if both outputs play it correctly. Press **Minus** to exit.
3. Open **GX Transfers**. Follow the two picture prompts: TV green/red/green,
   GamePad blue. Use R for correct or L for a problem, then Minus to exit.
   No test tone should play in this app.
4. After closing the apps, FTP the whole `sd:/ctr2wup/results/` folder to your computer,
   ZIP it and return that ZIP. Reports contain source identity; do not select
   or pair individual logs. Include partial failures.

State whether launch music stopped and Minus returned normally in both apps.
If an app hangs or reports an error, return any partial files and the error;
do not rerun to obtain a pass. No PC command is required. Do not add games or keys. These original diagnostics do not
establish 3DS guest audio.
