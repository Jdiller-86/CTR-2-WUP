"""Installer operations shared by the wizard and original temporary-directory tests."""
import copy
import base64
import hashlib
import ipaddress
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import tomllib
import uuid


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def atomic_write(target, data):
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=target.parent, prefix=".ctr2wup-", delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        if sha(temporary) != hashlib.sha256(data).hexdigest():
            raise OSError("Written file failed read-back verification")
        temporary.replace(target)
        if sha(target) != hashlib.sha256(data).hexdigest():
            raise OSError("Published file failed read-back verification")
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()


def backup_write(target, data):
    if target.is_symlink():
        raise ValueError("Settings cannot be symbolic links")
    if target.exists():
        previous = target.read_bytes()
        if previous == data:
            return None
        backup = target.with_name(target.name + ".ctr2wup-backup-" + uuid.uuid4().hex[:12])
        atomic_write(backup, previous)
    else:
        backup = None
    atomic_write(target, data)
    return str(backup) if backup else None


def install(backend, source, destination, *, install_dependencies=True, progress=lambda message: None):
    source, destination = source.resolve(), destination.expanduser().resolve()
    progress("Verifying the installation package…")
    manifest = backend.verify(source)
    state_path = backend.safe_path(destination, "local/install-state.json")
    if source != destination and destination.exists() and any(destination.iterdir()):
        if not state_path.is_file() or json.loads(state_path.read_text()).get("kind") != "ctr2wup-wiiu-mcp-install":
            raise ValueError("Choose an empty folder. Existing unrelated files have been preserved.")
    destination.mkdir(parents=True, exist_ok=True)
    local = backend.safe_path(destination, "local")
    local.mkdir(exist_ok=True)
    backend.safe_path(destination, "local/.ctr2wup-kit").touch(exist_ok=True)
    state = {"kind": "ctr2wup-wiiu-mcp-install", "source_commit": manifest["source_commit"], "complete": False}
    atomic_write(state_path, (json.dumps(state) + "\n").encode())
    if source != destination:
        progress("Installing the bridge and setup files…")
        backup_root = "local/backups/" + uuid.uuid4().hex
        for name in [*manifest["files"], "manifest.json", "SHA256SUMS.txt"]:
            incoming = backend.safe_path(source, name)
            if not incoming.is_file():
                raise ValueError("Installation package file is missing: " + name)
            target = backend.safe_path(destination, name)
            data = incoming.read_bytes()
            if target.exists():
                previous = target.read_bytes()
                if previous == data:
                    continue
                atomic_write(backend.safe_path(destination, backup_root + "/" + name), previous)
            atomic_write(target, data)
    progress("Preparing your project and connection settings…")
    executable = backend.safe_path(destination, "runtime/WiiU-MCP-Bridge.exe")
    with backend.safe_path(destination, "local/setup.log").open("a", encoding="utf-8") as log:
        result = backend.prepare(destination, install=install_dependencies,
                                 server_executable=executable if executable.is_file() else None, log=log)
    state.update(complete=True, project=result["project"], command=result["command"])
    atomic_write(state_path, (json.dumps(state, indent=2) + "\n").encode())
    progress("Bridge installed. No console connection was made.")
    return {**result, "installation": str(destination)}


def sd_plan(backend, kit, sd_root):
    backend.verify(kit)
    sd_root = sd_root.expanduser().resolve()
    if not backend.safe_path(sd_root, "wiiu/environments/aroma").is_dir():
        raise ValueError("Select the SD card root containing wiiu/environments/aroma.")
    manifest = json.loads((kit / "manifest.json").read_text())
    rows = []
    for name, record in manifest["files"].items():
        if not name.startswith("sd-card/"):
            continue
        relative = name.removeprefix("sd-card/")
        target = backend.safe_path(sd_root, relative)
        if target.exists() and not target.is_file():
            raise ValueError("An SD destination is not a regular file: " + relative)
        old_hash = sha(target) if target.exists() else None
        state = "same" if old_hash == record["sha256"] else "replace" if old_hash else "new"
        rows.append({"path": relative, "state": state, "old_sha256": old_hash,
                     "sha256": record["sha256"], "bytes": record["bytes"]})
    if not rows:
        raise ValueError("The kit contains no SD files")
    return {"sd_root": str(sd_root), "files": rows,
            "new": sum(r["state"] == "new" for r in rows),
            "replace": sum(r["state"] == "replace" for r in rows),
            "same": sum(r["state"] == "same" for r in rows)}


def copy_sd(backend, kit, plan):
    current = sd_plan(backend, kit, Path(plan["sd_root"]))
    if current != plan:
        raise ValueError("The SD files changed. Check the SD card again before copying.")
    sd_root = Path(plan["sd_root"])
    changed = [r for r in plan["files"] if r["state"] != "same"]
    required = sum(r["bytes"] for r in changed) + sum(
        backend.safe_path(sd_root, r["path"]).stat().st_size for r in changed if r["state"] == "replace")
    if shutil.disk_usage(sd_root).free < required + 2 * 1024 * 1024:
        raise ValueError("The SD card needs more free space before copying")
    backup_root = "ctr2wup/installer-backups/" + uuid.uuid4().hex
    # Back up every changed original before publishing any replacement.
    for row in changed:
        target = backend.safe_path(sd_root, row["path"])
        if row["state"] == "replace":
            original = target.read_bytes()
            if hashlib.sha256(original).hexdigest() != row["old_sha256"]:
                raise ValueError("The SD card changed during backup")
            atomic_write(backend.safe_path(sd_root, backup_root + "/" + row["path"]), original)
    for row in changed:
        target = backend.safe_path(sd_root, row["path"])
        if (sha(target) if target.exists() else None) != row["old_sha256"]:
            raise ValueError("The SD card changed before copying")
        data = backend.safe_path(kit, "sd-card/" + row["path"]).read_bytes()
        if hashlib.sha256(data).hexdigest() != row["sha256"]:
            raise ValueError("The source file changed before copying")
        atomic_write(target, data)
    return {"copied": len(changed), "backup": str(sd_root / backup_root) if plan["replace"] else None}


def configure(backend, installation, console):
    ipaddress.IPv4Address(console)
    state = json.loads(backend.safe_path(installation, "local/install-state.json").read_text())
    if not state.get("complete"):
        raise ValueError("Install the bridge first")
    config = backend.safe_path(installation, "local/wiiu.toml")
    values = tomllib.loads(config.read_text()) if config.exists() else {}
    values.update(console_host=console, project_root=state["project"], wiiload="builtin")
    content = "".join(name + " = " + json.dumps(value) + "\n" for name, value in values.items())
    tomllib.loads(content)
    backup_write(config, content.encode())
    client = backend.client_configuration(state["command"], config, Path(state["project"]))
    backup_write(backend.safe_path(installation, "local/mcp-server.toml"), client.encode())
    return {"configuration": str(config), "client": client, "command": state["command"]}


BEGIN = "# BEGIN CTR2WUP WII U MCP\n"
END = "# END CTR2WUP WII U MCP\n"


def register_codex(client, target):
    target = target.expanduser()
    original = target.read_text(encoding="utf-8") if target.exists() else ""
    old = tomllib.loads(original)
    server = tomllib.loads(client)["mcp_servers"]["wiiu"]
    if BEGIN in original or END in original:
        if original.count(BEGIN) != 1 or original.count(END) != 1:
            raise ValueError("Existing Codex configuration markers are inconsistent; settings preserved")
        start, finish = original.index(BEGIN), original.index(END) + len(END)
        if finish <= start:
            raise ValueError("Existing Codex configuration is inconsistent; settings preserved")
        candidate = original[:start] + BEGIN + client + END + original[finish:]
    elif "wiiu" in old.get("mcp_servers", {}):
        if old["mcp_servers"]["wiiu"] == server:
            return {"registered": True, "configuration": str(target), "backup": None}
        raise ValueError("An existing Wii U MCP entry was preserved. Use Copy configuration to review it in Codex settings.")
    else:
        candidate = original.rstrip() + "\n\n" + BEGIN + client + END
    new = tomllib.loads(candidate)
    before, after = copy.deepcopy(old), copy.deepcopy(new)
    for document in (before, after):
        document.get("mcp_servers", {}).pop("wiiu", None)
        if not document.get("mcp_servers"):
            document.pop("mcp_servers", None)
    if before != after or new["mcp_servers"]["wiiu"] != server:
        raise ValueError("Codex settings outside this bridge would change; existing settings preserved")
    backup = backup_write(target, candidate.encode())
    return {"registered": True, "configuration": str(target), "backup": backup}


def test_connection(command, configuration):
    result = subprocess.run(command + ["--config", str(configuration), "--action", "status"],
                            capture_output=True, text=True, timeout=20, check=False,
                            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    if result.returncode:
        raise ValueError("Could not reach FTPiiU. Check the IP, enable FTPiiU in Aroma, and use the same home network.")
    status = json.loads(result.stdout)
    if not status.get("ftp_reachable") or not status.get("sd_readable"):
        raise ValueError("FTPiiU responded, but the SD card is not readable")
    return status


def make_shortcut(installation, target=None):
    if os.name != "nt":
        return None
    target = target or (Path(os.environ["APPDATA"]) / "Microsoft/Windows/Start Menu/Programs/CTR2WUP Wii U MCP.lnk")
    target.parent.mkdir(parents=True, exist_ok=True)
    executable = installation / "Install Wii U MCP.exe"
    if not executable.is_file():
        return None
    if target.exists():
        atomic_write(target.with_name(target.name + ".ctr2wup-backup-" + uuid.uuid4().hex[:12]), target.read_bytes())
    def quoted(value):
        return "'" + str(value).replace("'", "''") + "'"
    script = ("$ErrorActionPreference='Stop'; $s=(New-Object -ComObject WScript.Shell).CreateShortcut(" + quoted(target)
              + "); $s.TargetPath=" + quoted(executable) + "; $s.WorkingDirectory=" + quoted(installation)
              + "; $s.Description='Set up Wii U MCP'; $s.Save()")
    powershell = Path(os.environ["SYSTEMROOT"]) / "System32/WindowsPowerShell/v1.0/powershell.exe"
    subprocess.run([str(powershell), "-NoProfile", "-NonInteractive", "-EncodedCommand",
                    base64.b64encode(script.encode("utf-16-le")).decode("ascii")],
                   check=True, capture_output=True, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    return str(target)
