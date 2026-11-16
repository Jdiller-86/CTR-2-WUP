#!/usr/bin/env python3
"""Freeze the bridge and guided installer on Windows; no cross-compilation."""
import argparse
import hashlib
import importlib.metadata
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tkinter
import urllib.request

ROOT = Path(__file__).resolve().parents[1]


def build(output):
    if sys.platform != "win32":
        raise RuntimeError("Build the Windows executables on a Windows runner")
    if output.exists():
        raise ValueError("Preserve the existing build; choose a new output directory")
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    if subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True).strip():
        raise ValueError("Freeze only clean committed source")
    output.mkdir(parents=True)
    common = [sys.executable, "-m", "PyInstaller", "--noconfirm", "--clean", "--noupx", "--onefile",
              "--paths", str(ROOT), "--distpath", str(output / "bin"),
              "--workpath", str(output / "build"), "--specpath", str(output / "spec")]
    server = common + ["--name", "WiiU-MCP-Bridge", "--recursive-copy-metadata", "mcp"]
    for package in ("mcp", "mcp_types", "pydantic", "jsonschema_specifications"):
        server += ["--collect-all", package]
    server += ["--hidden-import", "opentelemetry.context.contextvars_context",
               str(ROOT / "tools/windows/wiiu-mcp-server.py")]
    subprocess.run(server, cwd=ROOT, check=True)
    gui = common + ["--windowed", "--name", "Install Wii U MCP"]
    for module in ("hashlib", "shutil", "stat", "venv", "zipfile", "tempfile", "tomllib", "uuid", "copy", "base64"):
        gui += ["--hidden-import", module]
    gui += [str(ROOT / "tools/templates/wiiu-mcp-installer.py")]
    subprocess.run(gui, cwd=ROOT, check=True)
    notices = output / "licenses"
    notices.mkdir()
    distributions = []
    for distribution in importlib.metadata.distributions():
        name, version = distribution.metadata["Name"], distribution.version
        distributions.append({"name": name, "version": version, "source": f"https://pypi.org/project/{name}/{version}/"})
        for index, path in enumerate(distribution.files or []):
            text = str(path).lower()
            leaf = Path(path).name.lower()
            if ".dist-info/" in text and ("/licenses/" in text or leaf.startswith(("license", "copying", "notice"))):
                original = distribution.locate_file(path)
                if original.is_file() and original.stat().st_size <= 1024 * 1024:
                    shutil.copyfile(original, notices / (name.replace("/", "-") + f"-{index}.txt"))
    python_license = Path(sys.base_prefix) / "LICENSE.txt"
    if not python_license.is_file():
        raise ValueError("Python runtime license is missing")
    shutil.copyfile(python_license, notices / "Python-LICENSE.txt")
    patch = tkinter.Tcl().eval("info patchlevel")
    for library in ("tcl", "tk"):
        installed = list((Path(sys.base_prefix) / "tcl").glob(library + "*/license.terms"))
        if installed:
            data = installed[0].read_bytes()
        else:
            url = f"https://raw.githubusercontent.com/tcltk/{library}/core-{patch.replace('.', '-')}/license.terms"
            with urllib.request.urlopen(url, timeout=30) as response:
                data = response.read(1024 * 1024)
        (notices / (library + "-license.terms.txt")).write_bytes(data)
    (notices / "runtime-sources.json").write_text(json.dumps({
        "python": sys.version, "python_source": "https://github.com/python/cpython/tree/v" + sys.version.split()[0],
        "tcl_version": patch, "packages": sorted(distributions, key=lambda row: row["name"].lower()),
    }, indent=2) + "\n")
    if (subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip() != commit
            or subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True).strip()):
        raise ValueError("Source changed during Windows freezing")
    files = {}
    for name in ("WiiU-MCP-Bridge.exe", "Install Wii U MCP.exe"):
        path = output / "bin" / name
        if path.read_bytes()[:2] != b"MZ":
            raise ValueError("Expected a Windows executable")
        files[name] = {"bytes": path.stat().st_size, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
    report = {"source_commit": commit, "pyinstaller": importlib.metadata.version("pyinstaller"),
              "architecture": "windows-x64", "python_bundled": True, "files": files}
    (output / "windows-build.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    print(json.dumps(build(parser.parse_args().output)))
