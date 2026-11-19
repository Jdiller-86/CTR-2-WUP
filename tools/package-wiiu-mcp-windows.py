#!/usr/bin/env python3
"""Add source-matched self-contained Windows programs to a verified starter kit."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import zipfile


def digest(data):
    return hashlib.sha256(data).hexdigest()


def package(base, frozen, output):
    if output.exists():
        raise ValueError("Use a fresh Windows package output directory")
    report = json.loads((frozen / "windows-build.json").read_text())
    with zipfile.ZipFile(base) as archive:
        if archive.testzip():
            raise ValueError("Base kit ZIP failed CRC verification")
        entries = {name: archive.read(name) for name in archive.namelist()}
    manifest = json.loads(entries.pop("manifest.json"))
    entries.pop("SHA256SUMS.txt")
    if set(entries) != set(manifest["files"]):
        raise ValueError("Base kit contains unexpected files")
    if manifest["source_commit"] != report["source_commit"] or report["architecture"] != "windows-x64":
        raise ValueError("Windows programs and kit source identity differ")
    for name, record in manifest["files"].items():
        if len(entries[name]) != record["bytes"] or digest(entries[name]) != record["sha256"]:
            raise ValueError("Base kit differs from its inventory")
    for name, destination in (("WiiU-MCP-Bridge.exe", "runtime/WiiU-MCP-Bridge.exe"),
                              ("Install Wii U MCP.exe", "Install Wii U MCP.exe")):
        data = (frozen / "bin" / name).read_bytes()
        if data[:2] != b"MZ" or digest(data) != report["files"][name]["sha256"]:
            raise ValueError("Windows executable differs from its build receipt")
        entries[destination] = data
    for path in sorted((frozen / "licenses").iterdir()):
        if path.is_symlink() or not path.is_file() or path.stat().st_size > 1024 * 1024:
            raise ValueError("Expected bounded runtime license files")
        entries["licenses/windows/" + path.name] = path.read_bytes()
    entries["support/windows-build.json"] = (frozen / "windows-build.json").read_bytes()
    manifest.update(variant="windows-x64", python_runtime_bundled=True,
                    python_dependencies_install_requires_internet=False)
    manifest["files"] = {name: {"bytes": len(data), "sha256": digest(data)} for name, data in sorted(entries.items())}
    entries["manifest.json"] = (json.dumps(manifest, indent=2) + "\n").encode()
    entries["SHA256SUMS.txt"] = "".join(digest(data) + "  " + name + "\n" for name, data in sorted(entries.items())).encode()
    if sum(map(len, entries.values())) > 256 * 1024 * 1024:
        raise ValueError("Windows kit exceeds the archive size limit")
    output.mkdir(parents=True)
    target = output / ("CTR2WUP-WiiU-MCP-" + manifest["version"] + "-Windows-x64.zip")
    with zipfile.ZipFile(target, "x", compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(entries.items()):
            path = PurePosixPath(name)
            if path.is_absolute() or ".." in path.parts or "\\" in name:
                raise ValueError("Unsafe package path")
            archive.writestr(name, data)
    with zipfile.ZipFile(target) as archive:
        if archive.testzip() or any(archive.read(name) != data for name, data in entries.items()):
            raise ValueError("Windows ZIP failed read-back verification")
    (output / "SHA256SUMS-Windows-x64.txt").write_text(digest(target.read_bytes()) + "  " + target.name + "\n")
    report = {"ok": True, "archive": str(target), "source_commit": manifest["source_commit"],
              "bytes": target.stat().st_size, "sha256": digest(target.read_bytes()), "python_bundled": True}
    (output / "package-report-Windows-x64.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("base", "frozen", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(package(args.base, args.frozen, args.output)))
