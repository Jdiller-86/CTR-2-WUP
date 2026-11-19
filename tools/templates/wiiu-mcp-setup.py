#!/usr/bin/env python3
"""Verify a Wii U MCP kit and prepare its companion computer; never contacts a console."""
from __future__ import annotations
import argparse
import hashlib
import ipaddress
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import subprocess
import sys
import venv
import zipfile


def safe_path(root, name):
    path = PurePosixPath(name)
    if not name or "\\" in name or ":" in name or path.is_absolute() or any(p in ("", ".", "..") for p in name.split("/")):
        raise ValueError("Unsafe package path")
    result = root
    for part in path.parts:
        result = result / part
        if result.is_symlink() or getattr(result, "is_junction", lambda: False)():
            raise ValueError("Package paths cannot traverse links")
    return result


def verify(root):
    manifest = json.loads(safe_path(root, "manifest.json").read_text(encoding="utf-8"))
    if manifest.get("kind") != "ctr2wup-wiiu-mcp-kit" or not manifest.get("files"):
        raise ValueError("Not a Wii U MCP kit")
    for name, record in manifest["files"].items():
        path = safe_path(root, name)
        if (not path.is_file() or path.stat().st_size != record["bytes"]
                or hashlib.sha256(path.read_bytes()).hexdigest() != record["sha256"]):
            raise ValueError("Package verification failed: " + name)
    return manifest


def extract_source(archive, destination):
    if destination.exists():
        raise ValueError("Source destination exists; use --project for an existing checkout")
    with zipfile.ZipFile(archive) as source:
        infos = source.infolist()
        names = [i.filename for i in infos]
        if len(names) != len(set(names)) or sum(i.file_size for i in infos) > 32 * 1024 * 1024:
            raise ValueError("Source archive inventory exceeds the contract")
        for info in infos:
            name = info.filename.rstrip("/")
            if (not (name.startswith("CTR2WUP/") or (name == "CTR2WUP" and info.is_dir()))
                    or stat.S_ISLNK(info.external_attr >> 16)):
                raise ValueError("Unexpected source entry")
            safe_path(destination, name)
        destination.mkdir(parents=True)
        for info in infos:
            target = safe_path(destination, info.filename.rstrip("/"))
            if info.is_dir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                with target.open("xb") as stream:
                    stream.write(source.read(info))
    return destination / "CTR2WUP"


def write_new_or_same(path, content):
    data = content.encode("utf-8") if isinstance(content, str) else content
    if path.exists():
        if path.read_bytes() != data:
            raise ValueError("Existing configuration preserved: " + str(path))
        return
    with path.open("xb") as stream:
        stream.write(data)


def prepare(root, console=None, project=None, *, install=True, server_executable=None, log=None):
    if console:
        ipaddress.IPv4Address(console)
    manifest = verify(root)
    local = safe_path(root, "local")
    local.mkdir(exist_ok=True)
    marker = safe_path(root, "local/.ctr2wup-kit")
    if any(local.iterdir()) and not marker.is_file():
        raise ValueError("Existing unowned local directory preserved")
    marker.touch(exist_ok=True)
    if project is None:
        source_directory = "local/source/" + manifest["source_commit"]
        snapshot = safe_path(root, source_directory + "/CTR2WUP")
        project = snapshot if snapshot.is_dir() else extract_source(root / "source.zip", safe_path(root, source_directory))
    else:
        project = Path(project).expanduser().resolve()
    if not (project / "runtime/wiiu/Makefile").is_file():
        raise ValueError("Select a CTR-2-WUP checkout or use the bundled source snapshot")
    # Keep a source-relative original payload available to the bridge tools.
    target = safe_path(project, "artifacts/wiiu-mcp-starter/ctr2wup-prototype.wuhb")
    payload = root / "sd-card/wiiu/apps/ctr2wup-prototype.wuhb"
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists() and target.read_bytes() != payload.read_bytes():
        raise ValueError("Existing starter payload preserved")
    if not target.exists():
        shutil.copyfile(payload, target)
    environment = safe_path(root, "local/venv")
    if environment.exists() and not (environment / "pyvenv.cfg").is_file():
        raise ValueError("Existing non-venv directory preserved")
    if install and server_executable is None:
        venv.create(environment, with_pip=True)
    python = environment / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
    if install and server_executable is None:
        subprocess.run([str(python), "-m", "pip", "install", "--only-binary=:all:", "--require-hashes",
                        "-r", str(root / "requirements.lock")], check=True, stdout=log,
                       stderr=subprocess.STDOUT if log else None)
        wheel, = (root / "wheels").glob("ctr2wup-*.whl")
        subprocess.run([str(python), "-m", "pip", "install", "--no-deps", str(wheel)], check=True, stdout=log,
                       stderr=subprocess.STDOUT if log else None)
        subprocess.run([str(python), "-m", "pip", "check"], check=True, stdout=log,
                       stderr=subprocess.STDOUT if log else None)
    if server_executable is not None:
        executable = Path(server_executable).resolve()
        if not executable.is_relative_to(root.resolve()) or not executable.is_file():
            raise ValueError("The packaged bridge executable is missing")
        command = [str(executable)]
    else:
        command = [str(python), "-I", "-m", "host.wiiu_mcp"]
    result = {"prepared": True, "console_contacted": False, "source_commit": manifest["source_commit"],
              "project": str(project), "command": command,
              "starter_payload": "artifacts/wiiu-mcp-starter/ctr2wup-prototype.wuhb"}
    if not console:
        return result
    configuration = safe_path(root, "local/wiiu.toml")
    write_new_or_same(configuration, "console_host = " + json.dumps(console) + "\nproject_root = "
                      + json.dumps(str(project), ensure_ascii=False)
                      + '\nwiiload = "builtin"\ncontrol_key = "private/wiiu-control.key"\n')
    public_key = None
    if install:
        subprocess.run(command + ["--config", str(configuration), "--action", "pair-control"],
                       check=True, stdout=subprocess.DEVNULL)
        public_key = safe_path(root, "local/ctr2wup-control.pub")
        data = safe_path(project, "private/ctr2wup-control.pub").read_bytes()
        if len(data) != 32:
            raise ValueError("Expected a 32-byte console public key")
        write_new_or_same(public_key, data)
    client = client_configuration(command, configuration, project)
    write_new_or_same(safe_path(root, "local/mcp-server.toml"), client)
    return {**result, "mcp_configuration": str(local / "mcp-server.toml"),
            "bridge_configuration": str(configuration),
            "console_public_key": str(public_key) if public_key else None,
            "starter_payload": "artifacts/wiiu-mcp-starter/ctr2wup-prototype.wuhb"}


def client_configuration(command, configuration, project):
    return ('[mcp_servers.wiiu]\ncommand = ' + json.dumps(command[0], ensure_ascii=False)
            + '\nargs = ' + json.dumps(command[1:] + ["--config", str(configuration)], ensure_ascii=False)
            + '\nenv = { CTR2WUP_PROJECT_ROOT = ' + json.dumps(str(project), ensure_ascii=False)
            + ' }\ntool_timeout_sec = 1800\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--console", help="Wii U's numeric local IPv4 address")
    parser.add_argument("--project", type=Path, help="Existing project checkout; default is bundled source snapshot")
    parser.add_argument("--verify-only", action="store_true")
    args = parser.parse_args()
    if sys.version_info < (3, 12):
        parser.error("Python 3.12 or newer is required")
    root = Path(__file__).resolve().parent
    try:
        if args.verify_only:
            manifest = verify(root)
            print(json.dumps({"verified": True, "files": len(manifest["files"]), "source_commit": manifest["source_commit"]}))
        elif args.console:
            print(json.dumps(prepare(root, args.console, args.project)))
        else:
            parser.error("Pass --console or --verify-only")
    except (ValueError, OSError, subprocess.SubprocessError, zipfile.BadZipFile) as error:
        print("Setup stopped: " + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
