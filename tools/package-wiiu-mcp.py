#!/usr/bin/env python3
"""Package a clean source-bound Wii U MCP starter kit and pinned Aroma components."""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import io
import json
from pathlib import Path, PurePosixPath
import re
import tomllib
import urllib.parse
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
VERSION = "0.1.0-alpha.3"
REQUIRED_CHECKS = {"host-tests", "release-tests", "native-build", "control-build", "docs-build", "wheel-smoke"}
CONTROL_BINARY = "ctr2wup-control.wps"
CONTROL_DESTINATION = "sd-card/wiiu/environments/aroma/plugins/" + CONTROL_BINARY
spec = importlib.util.spec_from_file_location("mcp_release_audio_helpers", ROOT / "tools/package-audio-round.py")
helpers = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helpers)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def fetch_verified(url, target, expected_size, expected_sha):
    if urllib.parse.urlsplit(url).scheme != "https" or not re.fullmatch("[0-9a-f]{64}", expected_sha):
        raise ValueError("Component requires HTTPS and an exact SHA-256 pin")
    if target.is_symlink():
        raise ValueError("Component cache cannot contain links")
    if not target.exists():
        with urllib.request.urlopen(url, timeout=60) as response:
            if urllib.parse.urlsplit(response.url).scheme != "https":
                raise ValueError("Component download downgraded from HTTPS")
            data = response.read(expected_size + 1)
        if len(data) != expected_size or sha(data) != expected_sha:
            raise ValueError("Downloaded component differs from its pin")
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open("xb") as stream:
            stream.write(data)
    data = helpers.release.regular(target, 32 * 1024 * 1024).read_bytes()
    if len(data) != expected_size or sha(data) != expected_sha:
        raise ValueError("Cached component differs from its pin")
    return data


def component_entries(lock, cache):
    entries = {}
    for row in lock["components"]:
        name = row["repository"].split("/")[-1]
        binary = fetch_verified(row["url"], cache / (name + ".zip"), row["bytes"], row["sha256"])
        source = fetch_verified(row["source_url"], cache / (name + "-source.zip"), row["source_bytes"], row["source_sha256"])
        with zipfile.ZipFile(io.BytesIO(binary)) as archive:
            if archive.namelist() != [row["archive_member"]] or archive.testzip():
                raise ValueError("Upstream binary archive inventory differs")
            data = archive.read(row["archive_member"])
            if len(data) > 8 * 1024 * 1024 or data[:4] != b"\x7fELF":
                raise ValueError("Expected a bounded official Wii U module/plugin")
            destination = "sd-card/" + row["sd_path"]
            if (destination in entries or not destination.startswith("sd-card/wiiu/environments/aroma/")
                    or ".." in PurePosixPath(destination).parts):
                raise ValueError("Invalid or duplicate component destination")
            entries[destination] = data
        with zipfile.ZipFile(io.BytesIO(source)) as archive:
            if archive.testzip() or not row["license_members"]:
                raise ValueError("Missing upstream source/license evidence")
            for index, member in enumerate(row["license_members"]):
                entries[f"licenses/{name}-{index}.txt"] = archive.read(member)
        entries["upstream/" + name + ".zip"] = binary
        entries["upstream/" + name + "-source.zip"] = source
    return entries


def control_entries(control, commit):
    def read(name, limit=8 * 1024 * 1024):
        return helpers.release.regular(control / name, limit).read_bytes()

    binary = read(CONTROL_BINARY)
    if (read("source-commit.txt", 128).decode("ascii").strip() != commit
            or read("sdk-image-digest.txt", 256).decode("ascii").strip() != helpers.release.SDK_IMAGE
            or read("SHA256SUMS", 256).decode("ascii").split() != [sha(binary), CONTROL_BINARY]
            or binary[:7] != b"\x7fELF\x01\x02\x01" or binary[9:11] != b"PL"
            or binary[18:20] != b"\x00\x14"):
        raise ValueError("Require a verified source-matched PowerPC control plugin")
    entries = {CONTROL_DESTINATION: binary}
    for name in ("GPL-3.0", "wups-LICENSE", "rpxloader-LICENSE", "monocypher-LICENSE"):
        entries["licenses/control/" + name + ".txt"] = read(name)
    for name in ("wups.tar.gz", "rpxloader.tar.gz", "monocypher-4.0.3.tar.gz"):
        entries["upstream/control/" + name] = read("source/upstream/" + name)
    return entries


def package(wheel, native, control, sdk, evidence, cache, output):
    commit = helpers.identity()
    if output.exists() or any(p.is_symlink() for p in (output, *output.parents)):
        raise ValueError("Use a fresh, real package output directory")
    checks = helpers.release.read_json(evidence)
    if (checks.get("source_commit") != commit or checks.get("ok") is not True
            or {r["name"] for r in checks.get("checks", []) if r.get("exit_code") == 0} != REQUIRED_CHECKS):
        raise ValueError("Require complete source-matched successful release checks")
    build = helpers.release.read_json(native / "build-report.json")
    if (build.get("source_commit") != commit or build.get("source_dirty") is not False
            or build.get("target") != "prototype" or build.get("ok") is not True):
        raise ValueError("Require the clean source-matched original prototype")
    helpers.release.check_records(build["artifacts"], helpers.release.NATIVE_NAMES, native)
    for name in helpers.release.NATIVE_NAMES:
        helpers.release.validate_binary(native / name)
    rpx = (native / "ctr2wup-prototype.rpx").read_bytes()
    wuhb = (native / "ctr2wup-prototype.wuhb").read_bytes()
    if wuhb.find(rpx) != 512 or wuhb.rfind(rpx) != 512:
        raise ValueError("WUHB does not contain the exact source-matched RPX")
    if (sdk / "ctr2wup-image-digest.txt").read_text().strip() != helpers.release.SDK_IMAGE:
        raise ValueError("Native SDK image differs from the verified pin")
    lock = json.loads((ROOT / "packaging/wiiu-mcp-components.json").read_text())
    entries = component_entries(lock, cache)
    entries.update(control_entries(control, commit))
    source = helpers.checked_source(commit)
    wheel_bytes = helpers.release.regular(wheel).read_bytes()
    helpers.release.validate_zip(wheel)
    with zipfile.ZipFile(io.BytesIO(source)) as src, zipfile.ZipFile(io.BytesIO(wheel_bytes)) as binary:
        modules = {n.removeprefix("CTR2WUP/") for n in src.namelist()
                   if n.startswith(("CTR2WUP/host/", "CTR2WUP/translator/", "CTR2WUP/renderer/")) and n.endswith(".py")}
        wheel_modules = {n for n in binary.namelist() if n.startswith(("host/", "translator/", "renderer/")) and n.endswith(".py")}
        if modules != wheel_modules or any(src.read("CTR2WUP/" + n) != binary.read(n) for n in modules):
            raise ValueError("Wheel modules differ from the committed source")
        version = tomllib.loads(src.read("CTR2WUP/pyproject.toml").decode())["project"]["version"]
        metadata = "ctr2wup-" + version + ".dist-info/"
        allowed = modules | {"host/assets/bootMovie.h264"} | {metadata + n for n in (
            "METADATA", "WHEEL", "entry_points.txt", "top_level.txt", "RECORD", "licenses/LICENSE")}
        if set(binary.namelist()) != allowed or binary.read("host/assets/bootMovie.h264") != src.read("CTR2WUP/host/assets/bootMovie.h264"):
            raise ValueError("Wheel contains an unexpected or changed non-module payload")
    entries.update(helpers.sdk_entries(sdk / "licenses"))
    entries.update({
        "source.zip": source, "wheels/" + wheel.name: wheel_bytes,
        "setup.py": (ROOT / "tools/templates/wiiu-mcp-setup.py").read_bytes(),
        "requirements.lock": (ROOT / "packaging/wiiu-mcp-requirements.lock").read_bytes(),
        "components.json": (ROOT / "packaging/wiiu-mcp-components.json").read_bytes(),
        "sd-card/wiiu/apps/ctr2wup-prototype.wuhb": wuhb,
        "support/native-build.json": (native / "build-report.json").read_bytes(),
        "support/checks.json": evidence.read_bytes(),
        "support/sdk-image-digest.txt": (sdk / "ctr2wup-image-digest.txt").read_bytes(),
        "LICENSE": (ROOT / "LICENSE").read_bytes(),
        "THIRD_PARTY_NOTICES": (ROOT / "THIRD_PARTY_NOTICES").read_bytes(),
        "START-HERE.txt": helpers.release.render_guide(ROOT / "tools/templates/wiiu-mcp-start.txt", VERSION, commit),
    })
    manifest = {"schema_version": 1, "kind": "ctr2wup-wiiu-mcp-kit", "version": VERSION,
                "source_commit": commit, "hardware_verified": False,
                "python_runtime_bundled": False, "python_dependencies_install_requires_internet": True,
                "console_components": len(lock["components"]) + 1, "sdk_bundled": False,
                "control_plugin": CONTROL_DESTINATION,
                "files": {n: {"bytes": len(d), "sha256": sha(d)} for n, d in sorted(entries.items())}}
    entries["manifest.json"] = (json.dumps(manifest, indent=2) + "\n").encode()
    entries["SHA256SUMS.txt"] = "".join(sha(d) + "  " + n + "\n" for n, d in sorted(entries.items())).encode()
    if sum(map(len, entries.values())) > 64 * 1024 * 1024 or helpers.identity() != commit:
        raise ValueError("Package size or source identity changed")
    output.mkdir(parents=True)
    archive_path = output / ("CTR2WUP-WiiU-MCP-" + VERSION + ".zip")
    with zipfile.ZipFile(archive_path, "x", compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(entries.items()):
            archive.writestr(name, data)
    with zipfile.ZipFile(archive_path) as archive:
        if archive.testzip() or set(archive.namelist()) != set(entries) or any(archive.read(n) != d for n, d in entries.items()):
            raise ValueError("Release ZIP failed read-back/CRC verification")
    (output / "SHA256SUMS.txt").write_text(helpers.release.sha256(archive_path) + "  " + archive_path.name + "\n")
    report = {"ok": True, "archive": str(archive_path), "sha256": helpers.release.sha256(archive_path),
              "bytes": archive_path.stat().st_size, "files": len(entries), "source_commit": commit,
              "hardware_verified": False}
    (output / "package-report.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("wheel", "native", "control", "sdk", "evidence", "cache", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(package(args.wheel, args.native, args.control, args.sdk, args.evidence, args.cache, args.output)))
