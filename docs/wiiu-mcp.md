# Wii U MCP bridge

The optional host bridge builds CTR2WUP diagnostics, transfers files, submits
homebrew launches and collects results from an owned Aroma Wii U. It reuses
FTPiiU, the wiiload protocol and the existing build/report helpers. An optional
paired Aroma plugin adds console state, observed launches, reboot and WUP
installation. Physical-console acceptance of the new plugin is pending.

## Setup

The [Wii U MCP starter kit](https://github.com/Jdiller-86/CTR-2-WUP/releases)
includes the bridge, guided installer, paired control plugin, ten pinned Aroma
components with their sources and licenses, an original diagnostic WUHB and
a source snapshot. On Windows,
download the Windows-x64 ZIP, choose **Extract All**, then double-click
**Install Wii U MCP.exe**. Python and runtime dependencies are included.

The wizard installs into a per-user folder, previews SD changes with automatic
backups, and adds the bridge to local Codex. Enter the console IP and save the
connection, then restart Codex. SD preparation and the IP can be completed later.
The portable kit uses the same wizard through `python installer.py` and needs
Python 3.12+ and Internet for initial dependency installation. Follow
`START-HERE.txt` to export and copy the public key to the SD card. The SDK is
needed only for native rebuilds.

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
| `wiiu_launch` | Launch a local RPX/WUHB via the paired plugin or wiiload |
| `wiiu_console_state`, `wiiu_console_result` | Read the paired plugin's current title and operation receipts |
| `wiiu_reboot` | Request a full restart of the responsive Wii U |
| `wiiu_install_wup` | Stage, install and verify a new homebrew WUP on MLC or USB |
| `wiiu_launch_title` | Launch an installed Wii U application by exact title ID |
| `wiiu_start_logs`, `wiiu_read_logs`, `wiiu_stop_logs` | Bounded console-source UDP capture with cursors |

Call capabilities and status, build a diagnostic, then pass its `launch_file`
to `wiiu_launch`. Absolute artifact paths inside the checkout also work. Upload
required original assets using the package instructions. Supported build targets are
`prototype`, `audio-check`, `audio-stream-check`, `guest-audio`, `scene-check`,
`m1-check` and `install-check`. Each FTP connection closes before launch. One
bridge instance serializes operations. Use one controller and avoid competing
external FTP writes. The legacy wiiload sender reports submission. The paired
plugin can observe the requested executable or installed title in a new session.

Direct CLI actions work without installing the MCP SDK:

```sh
python -m host.wiiu_mcp --config private/wiiu.toml --action capabilities
python -m host.wiiu_mcp --config private/wiiu.toml --action status
python -m host.wiiu_mcp --config private/wiiu.toml --action build --target guest-audio
python -m host.wiiu_mcp --config private/wiiu.toml --action collect --sd-path ctr2wup/results
```

## Pair the optional Aroma plugin

Starter-kit setup generates the pairing files described in `START-HERE.txt`.
For a source checkout, install the updated host with
`python -m pip install -e '.[packaging,mcp]'`. Get the `wiiu-control-…` artifact
from the matching GitHub Actions Wii U build. With the verified devkitPro SDK
and Docker, you can also run `bash tools/build-wiiu-control.sh` on Linux.
The plugin uses Aroma's Plugin Loader Backend 0.4.1 and RPX Loading Module 0.3.5
from the starter kit or compatible newer versions.

1. In `private/wiiu.toml`, set `control_key = "private/wiiu-control.key"`.
   Run `python -m host.wiiu_mcp --config private/wiiu.toml --action pair-control`.
2. Copy `ctr2wup-control.wps` to `wiiu/environments/aroma/plugins/` and the
   exported `ctr2wup-control.pub` to `wiiu/` on the SD card. The private
   `.key` stays on the host. Boot Aroma with FTPiiU and the control plugin
   enabled, then restart the MCP client to discover the tools.
3. Call `wiiu_console_state` to check the pairing. If the console opens an
   account selector, select your user before launching homebrew.

For large WUP transfers, allow time for the upload and read-back. In Codex set
`tool_timeout_sec = 1800` in the existing `[mcp_servers.wiiu]` table. From the
Wii U Menu, call `wiiu_install_wup` with the local install directory, its
`title_id` and `device` (`mlc` or `usb`). It accepts a fresh CTR2WUP
plaintext homebrew WUP with the `wup-menu-v3` startup profile, up to 1 GiB and
64 package files. The console needs the signature patches required by that
package. Existing titles are refused. Keep Aroma in the Menu until the
installation finishes. Use `wiiu_console_result` with the returned
`operation_id`. At `installed_verified`, call `wiiu_launch_title` with the
same title ID. Staged files and receipts stay on SD for inspection.

`wiiu_reboot` requests a normal full restart. When FTPiiU reconnects, check
`wiiu_console_state` for the new session. After any interrupted operation, read
its receipt and console state before submitting another request. A new session
showing the requested title or homebrew operation confirms the observed launch.
The app's own results remain the evidence for its behavior.

## Transport and evidence contract

Read roots default to `ctr2wup`, `wiiu/apps`, `wiiu/crash_logs`,
`wiiu/screenshots` and `install`; write roots to `ctr2wup`, `wiiu/apps` and
`install`. Nonempty `read_roots`/`write_roots` arrays can change SD subdirectories.
Remote paths are relative to SD. Local artifacts stay inside the checkout.
Traversal, control characters and local symbolic links are rejected. Existing
files are preserved. General transfers are bounded at 64 MiB per file,
256 MiB per collection, 1024 entries and 12 directory levels.

The paired plugin uses the FTPiiU SD connection for its signed request mailbox.
Requests are bound to one console session and sequence. Keep FTPiiU on a
trusted LAN and keep SD writes to one controller. The pairing key authorizes
the fixed commands. Anyone who can replace SD files or the plugin itself must
already be trusted.

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

Transfer success, launch receipts, log text and cleanup markers do not establish
a diagnostic verdict. The soft reboot requires a running controller on the Wii U.
An already saved screenshot can be downloaded.

## Validation and next acceptance

The starter kit is built by `tools/package-wiiu-mcp.py` from clean source,
source-matched wheel, diagnostic and control plugin builds, the verified SDK
license directory and successful check evidence. `packaging/wiiu-mcp-components.json` pins upstream
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

Physical acceptance uses one original diagnostic WUHB and a fresh original
WUP. Record the paired launch observation, install receipt, installed-title
launch and the new session after reboot. Use the original diagnostic's report
to assess execution. The known installed-title exit conflict remains tracked
separately.
