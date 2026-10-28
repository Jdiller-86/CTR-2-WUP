# Runtime check

FTP the contents of **sd-card/** to the SD root in binary mode. In Aroma, run **WiiUCrashLogDumper** once to collect the already-retained system logs, then launch **CTR2WUP Guest Audio**. No installation or repeat of the installed-app hang is needed.

The audio check runs automatically: listen for a low tone on the left, a higher tone on the right, then both, with one automatic pause. When prompted, press **R** if correct on TV and GamePad, or **L** if wrong/missing; the app saves and exits. Use **Minus** if a failure is displayed. This checks the new guest DSP path; existing controls/save/tone tests are not repeated.

FTP **sd:/ctr2wup/dsp-check/** back to the computer, ZIP that one folder and send it back. It contains the automatic results and any collected system logs; keep it private. No command, questionnaire or edited file is required. Build @VERSION@, source @SOURCE_COMMIT@.
