# Covered 3DSX startup source

Unmodified files from https://github.com/devkitPro/devkitarm-crtls at
`1c0c10257c44bbb5a433453bb6bba91582825492`, supplied under MPL 2.0 for the
startup code embedded in the optional compiled libctru interactive artifact.
The full license is `../MPL-2.0.txt`. These files and their MPL notices remain
available as source without CTR2WUP's project license replacing their terms.
No modification to these original source files was made.

On 2026-10-09, normal devkitARM `arm-none-eabi-as -mcpu=mpcore -mfpu=vfp
-mfloat-abi=hard 3dsx_crt0.s` and objcopy of `.crt0` produced the same section
bytes as the installed `armv6k/fpu/3dsx_crt0.o`:
`7d6c815f71e12fa284d444fc7d63924de75e63d4b9461b4f7eb2ed15f14e1afe`.
Their complete `readelf -r` output also matches. Whole-object SHA256 differs:
source-built `997a20aad1f26a45d0c2b9ded8af0d6fe62ebc4c05e94279a51e3996fd25d3f9`,
SDK `6f1188b6f8b64d36818e95a4cc54a95c857e1a0bd0ca41906081efe6babda895`.
This establishes the linked section/relocations, not identical SDK package builds.
The preserved `3dsx.ld` and `3dsx.specs` also match the installed SDK files byte
for byte.

Preserved file SHA256:

- `3dsx_crt0.s`: 0cd0169dd5b711a2adf5efefe57134afff22a70b54e51cc4a87e608fd11071a8
- `3dsx.ld`: 930b274d551cb4b61718c09d8bfbc76212f5f574152f43937687984b916508aa
- `3dsx.specs`: 2663fca222c4a97bd51fae61a19f95bdabc068000685997ecbe7b11a6675f84b

The original application C, build flags, pinned library, checked translation
driver/adapter/runtime and reproducible generation instructions are supplied
in the CTR2WUP source bundle. No compiler/toolchain binary is redistributed.
