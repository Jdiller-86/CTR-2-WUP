# Build the external lifter and the original generation driver, without bundling
# upstream implementation. Run from PowerShell after installing Git and rustup.
param([string]$Workspace = (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent))
$ErrorActionPreference = 'Stop'
$repository = Split-Path $PSScriptRoot -Parent
$pin = 'f78cd3777a676a7c02538ab993ca574a6d843416'
$source = Join-Path $Workspace 'ctr2wup-deps/3dsrecomp'
$toolDirectory = Join-Path $Workspace 'ctr2wup-tools'
function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
if (-not (Get-Command rustup -ErrorAction SilentlyContinue)) { throw 'Install official Rust/rustup before running this script.' }
if (-not (Test-Path $source)) {
    New-Item -ItemType Directory -Force (Split-Path $source -Parent) | Out-Null
    Invoke-Checked git @('clone','https://github.com/fearkov/3dsrecomp.git',$source)
    Invoke-Checked git @('-C',$source,'checkout','--detach',$pin)
}
$head = & git -C $source rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $head -ne $pin) { throw 'Lifter is not at the audited source pin; refusing to reset it.' }
$changes = & git -C $source status --porcelain --untracked-files=no
if ($LASTEXITCODE -ne 0 -or $changes) { throw 'Lifter has tracked changes; refusing to claim a pinned build.' }
Invoke-Checked rustup @('toolchain','install','1.95.0','--profile','minimal')
New-Item -ItemType Directory -Force $toolDirectory | Out-Null
$env:CARGO_TARGET_DIR = Join-Path $toolDirectory '3dsrecomp-target'
$env:CARGO_NET_GIT_FETCH_WITH_CLI = 'true'
Invoke-Checked cargo @('+1.95.0','build','--manifest-path',(Join-Path $source 'Cargo.toml'),'--locked','--release')
Invoke-Checked cargo @('+1.95.0','test','--manifest-path',(Join-Path $source 'Cargo.toml'),'--locked','--release','--workspace')
$driver = Join-Path $repository 'translator/generate.rs'
$binary = Join-Path $toolDirectory 'ctr2wup-generate.exe'
Invoke-Checked rustc @('+1.95.0','--edition=2021','-O',$driver,
    '--extern',('recomp3ds=' + (Join-Path $env:CARGO_TARGET_DIR 'release/librecomp3ds.rlib')),
    '-L',('dependency=' + (Join-Path $env:CARGO_TARGET_DIR 'release/deps')),'-o',$binary)
$receipt = @{source_commit=$pin;driver_sha256=(Get-FileHash $driver -Algorithm SHA256).Hash.ToLowerInvariant();
    binary_sha256=(Get-FileHash $binary -Algorithm SHA256).Hash.ToLowerInvariant()} | ConvertTo-Json
[System.IO.File]::WriteAllText((Join-Path $toolDirectory 'ctr2wup-generate.json'), $receipt,
    (New-Object System.Text.UTF8Encoding($false)))
