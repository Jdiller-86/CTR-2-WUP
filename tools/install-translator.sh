#!/usr/bin/env bash
# Optional audited external host lifter; never vendored into project artifacts.
set -euo pipefail
ctr_repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
ctr_workspace=$(dirname -- "$ctr_repo_root")
ctr_tools_root="$ctr_workspace/ctr2wup-tools"
ctr_source="$ctr_workspace/ctr2wup-deps/3dsrecomp"
ctr_source_commit='f78cd3777a676a7c02538ab993ca574a6d843416'
export RUSTUP_HOME="$ctr_tools_root/rustup"
export CARGO_HOME="$ctr_tools_root/cargo"
export PATH="$CARGO_HOME/bin:$PATH"
export CARGO_NET_GIT_FETCH_WITH_CLI=true
export CARGO_TARGET_DIR="$ctr_tools_root/3dsrecomp-target"
if [[ ! -x "$CARGO_HOME/bin/rustup" ]]; then
    ctr_rustup_dir="$ctr_tools_root/rust-download"
    mkdir -p "$ctr_rustup_dir"
    curl --fail --show-error --location --proto '=https' --tlsv1.2 \
        https://static.rust-lang.org/rustup/archive/1.29.1/x86_64-unknown-linux-gnu/rustup-init \
        --output "$ctr_rustup_dir/rustup-init"
    printf '%s  %s\n' 'dda7234360b7f578ca8b0ddcb80145646fa61a67c1720a5abc7051b35c9fcb71' "$ctr_rustup_dir/rustup-init" | sha256sum --check
    chmod +x "$ctr_rustup_dir/rustup-init"
    "$ctr_rustup_dir/rustup-init" -y --no-modify-path --profile minimal --default-toolchain 1.95.0
fi
rustup toolchain install 1.95.0 --profile minimal
if [[ ! -e "$ctr_source" ]]; then
    mkdir -p "$(dirname -- "$ctr_source")"
    git clone https://github.com/fearkov/3dsrecomp.git "$ctr_source"
    git -C "$ctr_source" checkout --detach "$ctr_source_commit"
fi
if [[ $(git -C "$ctr_source" rev-parse HEAD) != "$ctr_source_commit" ]]; then
    echo 'Existing lifter source is not the audited commit; refusing to reset it.' >&2
    exit 1
fi
if [[ -n $(git -C "$ctr_source" status --porcelain --untracked-files=no) ]]; then
    echo 'Existing lifter source has tracked edits; refusing to claim a pinned build.' >&2
    exit 1
fi
cargo +1.95.0 build --manifest-path "$ctr_source/Cargo.toml" --locked --release
cargo +1.95.0 test --manifest-path "$ctr_source/Cargo.toml" --locked --release --workspace
rustc +1.95.0 --edition=2021 -O "$ctr_repo_root/translator/generate.rs" \
    --extern "recomp3ds=$CARGO_TARGET_DIR/release/librecomp3ds.rlib" \
    -L "dependency=$CARGO_TARGET_DIR/release/deps" \
    -o "$ctr_tools_root/ctr2wup-generate"
python3 - "$ctr_repo_root/translator/generate.rs" "$ctr_tools_root/ctr2wup-generate" "$ctr_source_commit" <<'PY'
import hashlib,json,sys
from pathlib import Path
driver,binary,commit=Path(sys.argv[1]),Path(sys.argv[2]),sys.argv[3]
binary.with_suffix('.json').write_text(json.dumps({'source_commit':commit,
    'driver_sha256':hashlib.sha256(driver.read_bytes()).hexdigest(),
    'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest()},indent=2)+'\n')
PY
