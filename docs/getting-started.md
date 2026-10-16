# Getting started

CTR2WUP currently supplies original Wii U diagnostics and host development tools.
It does not yet convert commercial 3DS games into playable WUPs or provide the
finished Windows injector GUI. See [compatibility](compatibility.md) for scope.

## Download

Use [Releases](https://github.com/Jdiller-86/CTR-2-WUP/releases) for published
packages or a successful [Actions run](https://github.com/Jdiller-86/CTR-2-WUP/actions)
for development artifacts. Keep the binaries, source identity and instructions
from the same package. Check its checksum manifest when supplied.

Begin with the kit's single `START-HERE.md` and the `sd-card/` tree to copy.
Source, notices and build receipts are supporting material. The short guide
identifies the app, required environment and result folder. Building a binary
does not establish a physical-console pass.

## Copy and launch

1. Extract the package on your computer.
2. Copy the **contents** of `sd-card/` to the SD root, including when using FTP.
3. For an Aroma diagnostic, launch its WUHB under `sd:/wiiu/apps/`.
4. For an installed-title candidate, use the package's named WUP installer and
   prerequisites, then launch the new title from the Wii U Menu.

WUHB/RPX homebrew applications and installable WUP directories use different
launch routes. WUP Installer is unnecessary for WUHB diagnostics. Original
self-signed WUP candidates require signature patches such as `01_sigpatches`;
Aroma alone does not supply that prerequisite. CTR2WUP does not install or update
the console environment.

Diagnostics show their controls on screen. Minus saves the current result and
requests exit. Preserve existing saves and reports. If a requested test fails,
return the files already written and describe the last action; a repeated run
is unnecessary solely to obtain another log. See [troubleshooting](troubleshooting.md).

## Return a requested result

Copy the **whole result folder named by the package guide** back over FTP and ZIP
it. Attach that ZIP to the development conversation or a console-result issue.
The folder includes the build identity and full diagnostic traces; no PC intake
command is needed. A written result and successful Menu return are separate
observations.

## Build or prepare input locally

[Contributing](../CONTRIBUTING.md) explains source builds; [Windows builds](build-windows.md)
covers the native Windows toolchain. The Python wheel provides engineering CLI
commands. [Encrypted input preparation](encrypted-input.md) and [CIA intake](cia-intake.md)
inspect private owned files without enabling unsupported games. Keep keys, dumps,
saves and commercial assets out of public issues and attachments.
