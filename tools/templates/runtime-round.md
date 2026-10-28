# Runtime check

FTP **sd-card/** contents to the SD root in binary mode. In Aroma, launch **WiiUCrashLogDumper**, select **Dump Logs** with **A**, then press **A** again. Wait for **Dumping done** and exit. This collects retained logs; no installation or repeat of the hang is needed.

Launch **CTR2WUP Guest Audio**. It automatically checks the new guest DSP path and plays a low left tone, a higher right tone, then both, with one automatic pause. When prompted, press **R** if correct on TV and GamePad, or **L** if wrong/missing; it saves and exits. Use **Minus** if a failure is displayed.

FTP **sd:/ctr2wup/dsp-check/** to the computer, ZIP that one folder and send it back privately. Results and any dumped system logs are included automatically. No command or edited file is needed. Build @VERSION@; source @SOURCE_COMMIT@.
