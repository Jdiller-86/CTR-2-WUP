#!/usr/bin/env bash
# Build the optional Aroma plugin from pinned, verified dependencies.
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
ctr_sdk=${DEVKITPRO:-${CTR2WUP_SDK_ROOT:-"$(dirname "$PWD")/ctr2wup-sdk"}}
ctr_deps=${CTR2WUP_CONTROL_DEPS:-"$(dirname "$ctr_sdk")/ctr2wup-control-deps"}
ctr_wups='ghcr.io/wiiu-env/wiiupluginsystem@sha256:91fe2c0b1fce10cb3744c670e57b161d4dd97f8c72cae7c5f9fb0ee2c69811ad'
ctr_rpx='ghcr.io/wiiu-env/librpxloader@sha256:b77cf29f0759ed9bca4a4f0ed54bee1af61b2cc292457c2fb65f09c9b2f7304b'
ctr_output=artifacts/wiiu-control

test -x "$ctr_sdk/devkitPPC/bin/powerpc-eabi-gcc"
mkdir -p "$ctr_deps/source"
ctr_deps=$(cd -- "$ctr_deps" && pwd)
ctr_scratch=$(mktemp -d "$ctr_deps/.build-XXXXXX")
ctr_container=''
trap 'if [[ -n "$ctr_container" ]]; then docker rm "$ctr_container" >/dev/null; fi; rm -rf -- "$ctr_scratch"' EXIT

fetch() {
    local url=$1 hash=$2 path=$3
    if [[ ! -f "$path" ]] || ! printf '%s  %s\n' "$hash" "$path" | sha256sum --check --status; then
        curl --fail --silent --show-error --location --retry 2 --max-time 120 "$url" -o "$ctr_scratch/download"
        printf '%s  %s\n' "$hash" "$ctr_scratch/download" | sha256sum --check
        mv -- "$ctr_scratch/download" "$path"
    fi
}

extract_library() {
    local image=$1 name=$2 component=$3
    if [[ -f "$ctr_deps/$name/.image" && $(<"$ctr_deps/$name/.image") == "$image" ]]; then
        return
    fi
    if [[ -e "$ctr_deps/$name" ]]; then
        echo "Select an empty CTR2WUP_CONTROL_DEPS or move aside the existing $name directory." >&2
        exit 1
    fi
    docker pull "$image"
    ctr_container=$(docker create "$image" true)
    docker cp "$ctr_container:/artifacts/$component" "$ctr_scratch/$name"
    printf '%s\n' "$image" > "$ctr_scratch/$name/.image"
    mv -- "$ctr_scratch/$name" "$ctr_deps/$name"
    docker rm "$ctr_container" >/dev/null
    ctr_container=''
}

extract_library "$ctr_wups" wups wups
extract_library "$ctr_rpx" rpxloader wums
fetch 'https://github.com/LoupVaillant/Monocypher/releases/download/4.0.3/monocypher-4.0.3.tar.gz' \
    8cc9bc341a66249016db9bd70e9142d8d0aef9945973744b1ac05dbc55d8ee66 "$ctr_deps/source/monocypher-4.0.3.tar.gz"
fetch 'https://codeload.github.com/wiiu-env/WiiUPluginSystem/tar.gz/6f1550fb41a3a8c53643a40cffd5405c59538d93' \
    2d7bed131c96b98f53a594917ef364ec75cc5d5897eb61cdec19b43f5b802072 "$ctr_deps/source/wups.tar.gz"
fetch 'https://codeload.github.com/wiiu-env/libRPXLoader/tar.gz/7b055f15f2c0e31af5b0a4ceda44ecb1ec39cc34' \
    0dbb528bb8a35e7844a85f5efcb0119d3c478f3516f37bb78d271e7c31bbb486 "$ctr_deps/source/rpxloader.tar.gz"
tar -xzf "$ctr_deps/source/monocypher-4.0.3.tar.gz" -C "$ctr_deps"
make -C runtime/wiiu/mcp-control -B -j 3 DEVKITPRO="$ctr_sdk" CONTROL_DEPS="$ctr_deps"

# Ship corresponding source and notices with the statically linked plugin.
ctr_bundle="$ctr_output/bundle"
mkdir -p "$ctr_bundle/source/upstream"
cp "$ctr_output/ctr2wup-control.wps" "$ctr_bundle/"
cp LICENSE THIRD_PARTY_NOTICES "$ctr_bundle/"
cp "$ctr_sdk/ctr2wup-image-digest.txt" "$ctr_bundle/sdk-image-digest.txt"
cp -R "$ctr_sdk/licenses/." "$ctr_bundle/sdk-licenses"
cp "$ctr_sdk/licenses/gamecube-tools/COPYING" "$ctr_bundle/GPL-3.0"
cp "$ctr_deps/source/"{monocypher-4.0.3,rpxloader,wups}.tar.gz "$ctr_bundle/source/upstream/"
for ctr_name in wups rpxloader; do
    tar -xOf "$ctr_deps/source/$ctr_name.tar.gz" --wildcards '*/LICENSE' > "$ctr_bundle/$ctr_name-LICENSE"
done
cp "$ctr_deps/monocypher-4.0.3/LICENCE.md" "$ctr_bundle/monocypher-LICENSE"
cp --parents tools/build-wiiu-control.sh runtime/wiiu/mcp-control/{Makefile,*.c,*.h} \
    runtime/wiiu/{install_capture,report_storage}.{c,h} runtime/core/ctr_sha256.{c,h} "$ctr_bundle/source"
git rev-parse HEAD > "$ctr_bundle/source-commit.txt"
(
    cd "$ctr_bundle"
    sha256sum ctr2wup-control.wps > SHA256SUMS
)
cat > "$ctr_bundle/START-HERE.txt" <<'GUIDE'
Copy ctr2wup-control.wps to wiiu/environments/aroma/plugins on the Wii U SD card.
Use the matching MCP bridge checkout to generate the console's public key.
See "Pair the optional Aroma plugin" in docs/wiiu-mcp.md at source-commit.txt.
The source directory contains the console code, its build script and libraries'
corresponding sources for rebuilding and relinking the plugin.
GUIDE
