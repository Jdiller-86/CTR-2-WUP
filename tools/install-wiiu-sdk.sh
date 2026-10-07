#!/usr/bin/env bash
# Official immutable SDK image; extraction is local and does not run the image.
set -euo pipefail
ctr_repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
ctr_sdk_root=${CTR2WUP_SDK_ROOT:-"$(dirname -- "$ctr_repo_root")/ctr2wup-sdk"}
ctr_sdk_image='devkitpro/devkitppc@sha256:44cb1a920e1ec3ec7c06767493c3b85f8d643d6137cc4661f0201895ac6e4967'
if [[ ! -x "$ctr_sdk_root/devkitPPC/bin/powerpc-eabi-gcc" || ! -f "$ctr_sdk_root/wut/lib/libwut.a" ]]; then
    if [[ -e "$ctr_sdk_root" ]]; then
        echo "Refusing to replace an existing incomplete SDK: $ctr_sdk_root" >&2
        exit 1
    fi
    command -v docker >/dev/null
    docker pull "$ctr_sdk_image"
    ctr_extract_dir=$(mktemp -d "$(dirname -- "$ctr_sdk_root")/.ctr2wup-sdk-XXXXXX")
    ctr_container_id=''
    trap 'if [[ -n "$ctr_container_id" ]]; then docker rm "$ctr_container_id" >/dev/null; fi; if [[ -n "$ctr_extract_dir" && -d "$ctr_extract_dir" ]]; then rm -rf -- "$ctr_extract_dir"; fi' EXIT
    ctr_container_id=$(docker create "$ctr_sdk_image")
    # Pacman's container-only keyring links escape /opt/devkitpro. Copy only
    # runtime/build components, not container package-manager state or keys.
    for ctr_component in devkitPPC wut tools licenses; do
        docker cp "$ctr_container_id:/opt/devkitpro/$ctr_component" "$ctr_extract_dir/$ctr_component"
    done
    printf '%s\n' "$ctr_sdk_image" > "$ctr_extract_dir/ctr2wup-image-digest.txt"
    "$ctr_extract_dir/devkitPPC/bin/powerpc-eabi-gcc" --version
    test -f "$ctr_extract_dir/wut/lib/libwut.a"
    mv -- "$ctr_extract_dir" "$ctr_sdk_root"
    ctr_extract_dir=''
    docker rm "$ctr_container_id" >/dev/null
    ctr_container_id=''
    trap - EXIT
fi
export DEVKITPRO="$ctr_sdk_root"
export DEVKITPPC="$ctr_sdk_root/devkitPPC"
export PATH="$DEVKITPPC/bin:$DEVKITPRO/tools/bin:$PATH"
cd -- "$ctr_repo_root"
python3 -m host doctor
make -C runtime/wiiu -j 3
