# Wii U MCP bridge

The optional host bridge builds CTR2WUP diagnostics, transfers files, submits
homebrew launches and collects results from an owned Aroma Wii U. It reuses
FTPiiU, the wiiload protocol and the existing build/report helpers. No new resident
plugin or Wii U application is required. Physical-console acceptance is pending.

## Setup

The [Wii U MCP starter kit](https://github.com/Jdiller-86/CTR-2-WUP/releases)
includes the bridge, guided installer, all ten pinned Aroma components and their
source/licenses, an original diagnostic WUHB and a source snapshot. On Windows,
download the Windows-x64 ZIP, choose **Extract All**, then double-click
**Install Wii U MCP.exe**. Python and runtime dependencies are included.

The wizard installs into a per-user folder, previews SD changes with automatic
backups, and adds the bridge to local Codex. Enter the console IP and save the
connection, then restart Codex. SD preparation and the IP can be completed later.
The portable kit uses the same wizard through `python installer.py` and needs
Python 3.12+ and Internet for its initial dependency installation. The SDK is
needed only for native rebuilds. Hardware acceptance is pending.

For source installation, on a computer on the console's trusted LAN, install Python 3.12+ and run
`python -m pip install -e '.[mcp]'` in the checkout. Native builds also need the
[verified SDK](../CONTRIBUTING.md#build-and-validate).
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
wiiload = "builtin"
```

Register this stdio command in the MCP client. For a TOML client configuration:

```toml
[mcp_servers.wiiu]
command = "/absolute/path/to/CTR-2-WUP/.venv/bin/python"
args = ["-m", "host.wiiu_mcp", "--config", "/absolute/path/to/CTR-2-WUP/private/wiiu.toml"]
env = { CTR2WUP_PROJECT_ROOT = "/absolute/path/to/CTR-2-WUP" }
```

Use `.venv/Scripts/python.exe` on Windows; forward slashes work in TOML paths.
Install the package in that interpreter. The built-in Python wiiload sender
requires no desktop executable. An official devkitPro executable can still be
selected by setting `wiiload` to its absolute path. FTPiiU normally accepts empty credentials; optional credentials
come from `CTR2WUP_FTP_USER`/`CTR2WUP_FTP_PASSWORD`, never tracked files. A numeric
IPv4 address binds all three transports to one peer. Cloud use needs a separately
configured route to this host and stdio registration, for example through SSH.
This package does not add tools to an already running cloud session.

## Operations

| Tool | Behavior |
| --- | --- |
| `wiiu_capabilities` | Offline operations, access roots, limits and unavailable controls |
| `wiiu_status` | FTP/SD connectivity and launch sender availability |
| `wiiu_build` | Existing diagnostic pipeline with source identity and artifact hashes |
| `wiiu_list_files`, `wiiu_read_text` | Directory inventory or up to 64 KiB of untrusted SD text |
| `wiiu_upload` | Stage, read back, publish and verify a new SD file |
| `wiiu_download` | Two matching reads into a fresh file under `private/wiiu/` |
| `wiiu_collect_results` | Bounded recursive collection with a private hash receipt |
| `wiiu_launch` | Submit a local RPX/WUHB once using the wiiload protocol |
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

The starter kit is built by `tools/package-wiiu-mcp.py` from clean source, a
source-matched wheel/native build, the verified SDK license directory and
successful check evidence. `packaging/wiiu-mcp-components.json` pins upstream
release/source hashes and exact SD destinations; no floating latest download is
used during packaging. The packager checks ZIP CRCs, source/wheel agreement,
component inventories and read-back hashes. `setup.py --verify-only` validates
the extracted kit before installation. The command-line helper never writes to
an SD card or contacts a console. The wizard copies only the staged SD inventory
after an explicit preview/copy action. It validates an existing Aroma layout,
backs up all changed originals before publishing replacements and verifies each
copied file. A changed card invalidates its preview. The staged modules require
an existing Aroma installation; no CFW, formatting or title installation is performed.

`packaging/wiiu-mcp-requirements.lock` pins the Python dependency closure and
published wheel hashes, including Windows-specific packages. Setup creates a
private virtual environment and absolute MCP/configuration paths. The source
snapshot supports file/launch operations without Git. Native builds need a real
Git checkout, selected with `setup.py --project`, and the verified SDK; the full
compiler toolchain is not redistributed. The Windows kit bundles Python and
the MCP dependencies in source-matched executables built on a Windows runner.
The portable kit does not bundle Python. Windows freezing uses the pinned tools
in `packaging/wiiu-mcp-windows-build.txt`; Python, Tcl/Tk and package notices are
included with the executables. The Windows check runs the real wizard and a
frozen MCP handshake/FTP call with Python removed from the child search path.

Codex registration adds one managed `mcp_servers.wiiu` block and backs up
changed settings. Unrelated configuration is preserved and parsed before/after
the change. An existing unmanaged Wii U entry is preserved for manual review;
the wizard also offers Copy configuration for other MCP clients. Only Test
connection contacts the configured console, checking FTPiiU/SD without a launch.

Install `.[mcp-test]` and run
`python -m unittest discover -s tests -p test_wiiu_bridge.py -v` for actual
loopback FTP/MCP stdio, corruption/size checks, partial evidence preservation,
UDP buffering and launch timeouts. These tests contact no Wii U. Run `make check`
and the strict documentation build for repository changes.
`test_wiiu_mcp_release.py` also checks the built-in sender over an actual TCP
connection, installer configuration, tamper rejection and archive traversal.

Physical acceptance waits until the console is available: check status, submit
one original diagnostic and collect its actual results, recording plugin
versions and source/artifact identity. Extend disconnect/relaunch tests for
specific changed or unresolved paths. The known installed-title exit conflict
remains unresolved; this host adapter does not justify replaying that failure.
