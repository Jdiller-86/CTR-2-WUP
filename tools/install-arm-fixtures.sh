#!/usr/bin/env bash
# Optional original compiled-homebrew tests; never replaces existing tooling.
set -euo pipefail
ctr_repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
ctr_sdk_root=${CTR2WUP_SDK_ROOT:-"$(dirname -- "$ctr_repo_root")/ctr2wup-sdk"}
ctr_arm_image='devkitpro/devkitppc@sha256:44cb1a920e1ec3ec7c06767493c3b85f8d643d6137cc4661f0201895ac6e4967'
ctr_arm_hash='2167ca62a80a1278ca06c98130de249e32eca8853af7e871ddce426222a3e57f'
ctr_library=${CTR2WUP_LIBCTRU_SOURCE:-"$(dirname -- "$ctr_repo_root")/ctr2wup-deps/libctru"}
ctr_library_pin='9b55eda44cf80b971503991e0c78f9bc8fe50425'
if [[ ! -d "$ctr_sdk_root/devkitARM" ]]; then
    mkdir -p -- "$ctr_sdk_root"
    docker pull "$ctr_arm_image"
    ctr_extract=$(mktemp -d "$ctr_sdk_root/.ctr2wup-arm-XXXXXX")
    ctr_container=''
    trap 'if [[ -n "$ctr_container" ]]; then docker rm "$ctr_container" >/dev/null; fi; if [[ -n "$ctr_extract" && -d "$ctr_extract" ]]; then rm -rf -- "$ctr_extract"; fi' EXIT
    ctr_container=$(docker create "$ctr_arm_image")
    docker cp "$ctr_container:/opt/devkitpro/devkitARM" "$ctr_extract/devkitARM"
    printf '%s  %s\n' "$ctr_arm_hash" "$ctr_extract/devkitARM/bin/arm-none-eabi-gcc" | sha256sum -c -
    "$ctr_extract/devkitARM/bin/arm-none-eabi-gcc" --version
    test -f "$ctr_extract/devkitARM/arm-none-eabi/lib/armv6k/fpu/3dsx_crt0.o"
    test ! -e "$ctr_sdk_root/devkitARM"
    mv -T -- "$ctr_extract/devkitARM" "$ctr_sdk_root/devkitARM"
    docker rm "$ctr_container" >/dev/null;ctr_container=''
    rmdir -- "$ctr_extract";ctr_extract=''
    trap - EXIT
fi
printf '%s  %s\n' "$ctr_arm_hash" "$ctr_sdk_root/devkitARM/bin/arm-none-eabi-gcc" | sha256sum -c -
if [[ ! -e "$ctr_library" ]]; then
    mkdir -p -- "$(dirname -- "$ctr_library")"
    git clone --no-checkout https://github.com/devkitPro/libctru.git "$ctr_library"
    git -C "$ctr_library" checkout --detach "$ctr_library_pin"
fi
test "$(git -C "$ctr_library" rev-parse HEAD)" = "$ctr_library_pin"
test -z "$(git -C "$ctr_library" status --porcelain)"
printf 'Original compiled-homebrew dependencies ready; DEVKITARM=%s\n' "$ctr_sdk_root/devkitARM"
