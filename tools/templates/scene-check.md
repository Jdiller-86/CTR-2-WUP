# Scene Check

Build `@VERSION@` · source `@SOURCE_COMMIT@`.
Use this only when a changed display or rendering path needs console
verification. A new build alone does not require another run.

1. FTP `sd-card/` contents to the SD root in binary mode. Preserve existing
   files, close FTP and launch **CTR2WUP Scene Check** in Aroma.
2. Both screens should show a square with red lower left, green upper right
   and a dark border. Compare with `images/software-scene.png`.
   Follow both picture prompts: **R** confirms correct output; **L** records a
   problem. Automatic pixel checks must also pass.
3. Press **Minus** when complete and note whether it returns normally.
4. After closing the app, FTP the whole `sd:/ctr2wup/results/` folder to your computer,
   ZIP it and return that ZIP with a short note about both pictures and Minus
   return. Reports contain source identity; do not select or pair logs.

Return partial failure logs and any displayed error; do not rerun to obtain a
pass. No PC command is required. Do not add games or keys. This original
software-rendered scene does not establish native GPU translation or gameplay.
