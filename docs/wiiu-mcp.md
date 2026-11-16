# Wii U MCP bridge

The optional host bridge builds CTR2WUP diagnostics, transfers files, submits
homebrew launches and collects results from an owned Aroma Wii U. It reuses
FTPiiU, devkitPro wiiload and the existing build/report helpers. No new resident
plugin or Wii U application is required. Physical-console acceptance is pending.

## Setup

On a computer on the console's trusted LAN, install Python 3.12+ and run
`python -m pip install -e '.[mcp]'` in the checkout. Native builds also need the
[verified SDK](../CONTRIBUTING.md#build-and-validate), which includes wiiload.
Enable the official [FTPiiU](https://github.com/wiiu-env/ftpiiu_plugin) and
[wiiload](https://github.com/wiiu-env/wiiload_plugin) Aroma plugins and their
documented dependencies. Leave FTPiiU system-file access disabled. These
services lack an authenticated encrypted control channel; keep their ports on
the trusted LAN. FTPiiU uses `/fs/vol/external01` for SD; wiiload uses TCP 4299.

Create ignored `private/wiiu.toml`, replacing the IP and absolute project path:

```toml
console_host = "192.168.1.50"
project_root = "/absolute/path/to/CTR-2-WUP"
ftp_port = 21
timeout = 10
wiiload = "wiiload"
```

Register this stdio command in the MCP client. For a TOML client configuration:

```toml
[mcp_servers.wiiu]
command = "/absolute/path/to/CTR-2-WUP/.venv/bin/python"
args = ["-m", "host.wiiu_mcp", "--config", "/absolute/path/to/CTR-2-WUP/private/wiiu.toml"]
env = { CTR2WUP_PROJECT_ROOT = "/absolute/path/to/CTR-2-WUP" }
```

Use `.venv/Scripts/python.exe` on Windows; forward slashes work in TOML paths.
Install the package in that interpreter. If needed, set `wiiload` to its absolute
executable path. FTPiiU normally accepts empty credentials; optional credentials
come from `CTR2WUP_FTP_USER`/`CTR2WUP_FTP_PASSWORD`, never tracked files. A numeric
IPv4 address binds all three transports to one peer. Cloud use needs a separately
configured route to this host and stdio registration, for example through SSH.
This package does not add tools to an already running cloud session.

## Operations

| Tool | Behavior |
| --- | --- |
| `wiiu_capabilities` | Offline operations, access roots, limits and unavailable controls |
| `wiiu_status` | FTP/SD connectivity and host wiiload availability |
| `wiiu_build` | Existing diagnostic pipeline with source identity and artifact hashes |
| `wiiu_list_files`, `wiiu_read_text` | Directory inventory or up to 64 KiB of untrusted SD text |
| `wiiu_upload` | Stage, read back, publish and verify a new SD file |
| `wiiu_download` | Two matching reads into a fresh file under `private/wiiu/` |
| `wiiu_collect_results` | Bounded recursive collection with a private hash receipt |
| `wiiu_launch` | Submit a local RPX/WUHB once through official wiiload |
| `wiiu_start_logs`, `wiiu_read_logs`, `wiiu_stop_logs` | Bounded console-source UDP capture with cursors |

Call capabilities and status, build a diagnostic, then launch its returned local
RPX/WUHB. Upload required original assets using the package instructions; the
executable itself needs no separate upload. Supported build targets are
`prototype`, `audio-check`, `audio-stream-check`, `guest-audio`, `scene-check`,
`m1-check` and `install-check`. Each FTP connection closes before launch. One
bridge instance serializes operations; use one controller and avoid competing
external FTP writes. Launch submission does not confirm execution.

Direct CLI actions work without installing the MCP SDK:

```sh
python -m host.wiiu_mcp --config private/wiiu.toml --action capabilities
python -m host.wiiu_mcp --config private/wiiu.toml --action status
python -m host.wiiu_mcp --config private/wiiu.toml --action build --target guest-audio
python -m host.wiiu_mcp --config private/wiiu.toml --action collect --sd-path ctr2wup/results
```

## Transport and evidence contract

Read roots default to `ctr2wup`, `wiiu/apps`, `wiiu/crash_logs`,
`wiiu/screenshots` and `install`; write roots to `ctr2wup`, `wiiu/apps` and
`install`. Nonempty `read_roots`/`write_roots` arrays can change SD subdirectories.
Remote paths are relative to SD, local sources to the checkout. Traversal,
control characters and local symbolic links are rejected. Existing files are
preserved; use distinct build names/directories. Uploads are individually
verified, not a multi-file transaction or protection against external writers.
Limits are 64 MiB/file, 256 MiB/collection, 1024 entries and 12 directory levels.

Start UDP capture before launch if the app/plugin actually emits network logs.
Port 4405 is the default; standard builds do not enable UDP automatically.
Capture retains 128 datagrams of up to 4096 bytes and reports buffer overruns.
Source IP filtering is not authentication; UDP delivery remains best effort.

Collect after reports close. Each return gets a unique ignored
`private/wiiu/collection-…/` with the SD tree under `sd-card/` and a hash receipt
in `collection.json`. Failed collections preserve partial files and an incomplete
receipt. Two matching reads detect ordinary concurrent writes, not an atomic
directory snapshot. Use the existing matching collector/reviewer on `sd-card/`
to evaluate source/session identity and criteria. A generic FTP receipt is not a
hardware-round review archive. Commercial assets, credentials and console logs
stay private. Retained crash logs must first be copied to SD by the official
dumper; this adapter does not read SLC.

Transfer success, launch submission, log text and cleanup markers never become
test passes. Active title identity, Menu return and execution remain unconfirmed.
After a failed/timed-out launch, inspect console state before deciding to retry.
Controller/touch injection, triggered screenshots, live memory debugging,
installed-title launch/installation and power recovery are explicitly unavailable.
An already saved screenshot can be downloaded. Add runtime input/state hooks
only for a concrete test; system-wide control needs a separately verified plugin.
WUP automation must preserve the platform protocol's ownership/predecessor checks.

## Validation and next acceptance

Install `.[mcp-test]` and run
`python -m unittest discover -s tests -p test_wiiu_bridge.py -v` for actual
loopback FTP/MCP stdio, corruption/size checks, partial evidence preservation,
UDP buffering and launch timeouts. These tests contact no Wii U. Run `make check`
and the strict documentation build for repository changes.

Physical acceptance waits until the console is available: check status, submit
one original diagnostic and collect its actual results, recording plugin
versions and source/artifact identity. Extend disconnect/relaunch tests for
specific changed or unresolved paths. The known installed-title exit conflict
remains unresolved; this host adapter does not justify replaying that failure.
