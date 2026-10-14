#!/usr/bin/env python3
"""Package a checked source commit and explicitly selected original diagnostics.

This does not discover artifact directories, build games or make WUP packages.
The caller supplies successful check evidence; the packager verifies the source,
reported binary/frame hashes, ZIP contents and all published SHA-256 sums.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
from email.parser import BytesParser
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import struct
import subprocess
import tempfile
import tomllib
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SDK_IMAGE = "devkitpro/devkitppc@sha256:44cb1a920e1ec3ec7c06767493c3b85f8d643d6137cc4661f0201895ac6e4967"
LIBCTRU = "9b55eda44cf80b971503991e0c78f9bc8fe50425"
SOURCE_DIRS = {".github", ".githooks", "docs", "host", "translator", "runtime", "renderer", "packaging", "profiles", "tests", "tools"}
SOURCE_FILES = {".gitignore", "AGENTS.md", "CHANGELOG.md", "CONTRIBUTING.md", "LICENSE", "Makefile", "README.md", "SECURITY.md", "STATUS.md", "THIRD_PARTY_NOTICES", "pyproject.toml", "mkdocs.yml", "docs-requirements.txt"}
FORBIDDEN = {".3ds", ".cxi", ".cci", ".cia", ".3dsx", ".rom", ".sav", ".key", ".keys", ".cert", ".app", ".h3", ".tik", ".tmd", ".pyc", ".log", ".elf", ".o", ".rpx", ".wuhb", ".zip", ".exe", ".dll", ".so", ".pyd", ".whl", ".tar", ".gz", ".7z", ".wup", ".snapshot", ".trace"}
REQUIRED_CHECKS = {"make-check", "make-prototype", "native-build", "transfer-validation", "wheel-build", "wheel-smoke", "pica-validation"}
CHECK_NAMES = REQUIRED_CHECKS | {"sdk-install", "arm-fixtures-install", "translator-install", "docs-build", "docs-package", "draw-validation"}
NATIVE_NAMES = ("ctr2wup-prototype.rpx", "ctr2wup-prototype.wuhb")
TRANSFER_NAMES = ("ctr2wup-libctru-transfer.rpx", "ctr2wup-libctru-transfer.wuhb")
FRAME_NAMES = {f"frame-{i}-{screen}.ppm" for i in range(3) for screen in ("top", "bottom")}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def regular(path: Path, limit: int = 64 * 1024 * 1024) -> Path:
    if path.is_symlink() or not path.is_file() or path.stat().st_size > limit:
        raise ValueError("Expected a bounded regular input file")
    resolved = path.resolve()
    if any(part.lower() in {"private", ".aws", ".git", ".env"} for part in resolved.parts):
        raise ValueError("Private input paths cannot be published")
    return resolved


def read_json(path: Path) -> dict:
    data = json.loads(regular(path, 2 * 1024 * 1024).read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise ValueError("Expected a JSON object")
    return data


def git(root: Path, *args: str) -> str:
    return subprocess.check_output(["git", "-C", str(root), *args], text=True).strip()


def check_records(records: list[dict], names: tuple[str, ...], folder: Path) -> list[dict]:
    if not isinstance(records, list) or len(records) != len(names):
        raise ValueError("Artifact receipt must list exactly the selected original files")
    if any(not isinstance(record, dict) for record in records):
        raise ValueError("Malformed artifact receipt")
    by_name = {record["name"]: record for record in records}
    if set(by_name) != set(names):
        raise ValueError("Unexpected or missing artifact receipt name")
    checked = []
    for name in names:
        path = regular(folder / name)
        actual = {"name": name, "size": path.stat().st_size, "sha256": sha256(path)}
        if any(by_name[name].get(key) != value for key, value in actual.items()):
            raise ValueError(f"Artifact receipt does not match {name}")
        checked.append(actual)
    return checked


def validate_binary(path: Path) -> None:
    regular(path)
    with path.open("rb") as stream:
        header = stream.read(52)
        if path.suffix == ".wuhb":
            if header[:4] != b"WUHB":
                raise ValueError("Expected an original WUHB application")
            return
        if len(header) != 52 or header[:8] != b"\x7fELF\x01\x02\x01\xca" or header[16:20] != b"\xfe\x01\x00\x14":
            raise ValueError("Expected a big-endian PowerPC Wii U RPX")
        offset = struct.unpack_from(">I", header, 32)[0]
        size, width, count, names = (struct.unpack_from(">H", header, n)[0] for n in (40, 46, 48, 50))
        if size != 52 or width != 40 or not 0 < count <= 4096 or names >= count or offset < 52 or offset + width * count > path.stat().st_size:
            raise ValueError("Invalid RPX section table")
        stream.seek(offset)
        for _ in range(count):
            row = struct.unpack(">10I", stream.read(40))
            if row[1] not in (0, 8) and row[4] + row[5] > path.stat().st_size:
                raise ValueError("RPX section extends outside the file")


def validate_zip(path: Path, source: bool = False, version: str | None = None) -> int:
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        if len(names) > 20000 or sum(row.file_size for row in archive.infolist()) > 128 * 1024 * 1024:
            raise ValueError("ZIP exceeds release payload bounds")
        if len(names) != len(set(names)) or archive.testzip() is not None:
            raise ValueError("ZIP duplicate entries or CRC failure")
        for entry in archive.infolist():
            name = PurePosixPath(entry.filename)
            if name.is_absolute() or ".." in name.parts or "\\" in entry.filename or stat.S_ISLNK(entry.external_attr >> 16):
                raise ValueError("Unsafe ZIP entry")
            if source:
                parts = name.parts
                if not parts or parts[0] != "CTR2WUP":
                    raise ValueError("Unexpected source archive root")
                if len(parts) > 1:
                    if parts[1] not in SOURCE_DIRS and "/".join(parts[1:]) not in SOURCE_FILES:
                        raise ValueError("Unexpected tracked source path; review before publication")
                    if name.suffix.lower() in FORBIDDEN or any(p.lower() in {"private", "artifacts", "output", "__pycache__", ".env", "build", "bin", "obj"} for p in parts[1:]):
                        raise ValueError("Sensitive or generated source entry cannot be published")
                    if name.suffix.lower() == ".bin" and not entry.filename.startswith(("CTR2WUP/tests/fixtures/", "CTR2WUP/tests/recomp/")):
                        raise ValueError("Binary source inputs must be original tracked instruction fixtures")
            if version:
                dist = f"ctr2wup-{version}.dist-info"
                if not name.parts or name.parts[0] not in {"host", "translator", "renderer", dist} or name.suffix.lower() in FORBIDDEN | {".dll", ".so", ".pyd"}:
                    raise ValueError("Wheel contains an unexpected payload")
                if not entry.is_dir() and name.parts[0] != dist and name.suffix != ".py":
                    raise ValueError("Developer wheel packages may contain source modules only")
        if version:
            metadata = BytesParser().parsebytes(archive.read(f"ctr2wup-{version}.dist-info/METADATA"))
            if metadata["Name"] != "ctr2wup" or metadata["Version"] != version:
                raise ValueError("Wheel version does not match the release")
        return len(names)


def check_documentation(folder: Path, version: str, commit: str, checks: list[dict]) -> tuple[dict, list[dict]]:
    report = read_json(folder / "publication-report.json")
    if (report.get("ok") is not True or report.get("version") != version
            or report.get("source_commit") != commit
            or report.get("all_maintained_article_pages_retained") is not True
            or report.get("external_runtime_dependencies") is not False
            or report.get("archive_contains_documentation_only") is not True
            or report.get("application_source_bundled") is not False
            or report.get("private_payloads_bundled") is not False
            or report.get("zip_crc_verified") is not True
            or not isinstance(report.get("internal_anchors_verified"), int)
            or report["internal_anchors_verified"] <= 0
            or not {"docs-build", "docs-package"} <= {row["name"] for row in checks}):
        raise ValueError("Documentation must have passing build/package evidence for this source commit")
    names = (f"CTR2WUP-Documentation-{version}.html", f"CTR2WUP-Public-Docs-{version}.zip")
    records = check_records(report.get("artifacts"), names, folder)
    html = regular(folder / names[0], 16 * 1024 * 1024).read_text(encoding="utf-8")
    if "<!doctype html>" not in html[:100].lower() or commit not in html or "Windows GUI" not in html:
        raise ValueError("Offline documentation lacks source identity or the product goal")
    validate_zip(regular(folder / names[1]))
    return report, records


def package(options: argparse.Namespace) -> dict:
    root = options.source_root.resolve()
    commit = git(root, "rev-parse", "HEAD")
    if not re.fullmatch(r"[0-9a-f]{40}", commit) or git(root, "status", "--porcelain"):
        raise ValueError("Release source must be a clean checked Git commit")
    version = tomllib.loads((root / "pyproject.toml").read_text(encoding="utf-8"))["project"]["version"]
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", options.version) or version != options.version:
        raise ValueError("Release version must match pyproject.toml exactly")
    evidence = read_json(options.evidence)
    checks = evidence.get("checks")
    if evidence.get("ok") is not True or evidence.get("version") != version or evidence.get("source_commit") != commit or evidence.get("original_fixtures_only") is not True or not isinstance(checks, list):
        raise ValueError("Release evidence must identify this clean source commit and original fixtures")
    if any(not isinstance(row, dict) for row in checks):
        raise ValueError("Malformed check evidence")
    names = {row.get("name") for row in checks}
    if not REQUIRED_CHECKS <= names or not names <= CHECK_NAMES or len(names) != len(checks) or any(type(row.get("exit_code")) is not int or row["exit_code"] != 0 for row in checks):
        raise ValueError("Required release checks did not all pass")
    if regular(options.sdk_receipt, 512).read_text(encoding="utf-8").strip() != SDK_IMAGE:
        raise ValueError("Unexpected SDK image receipt")
    native = read_json(options.native_dir / "build-report.json")
    if native.get("ok") is not True or native.get("target") != "prototype" or native.get("profile") != "native-diagnostics" or native.get("host_version") != version or native.get("runtime_version") != version or native.get("input_identity") is not None:
        raise ValueError("Native report must describe the current original diagnostic")
    native_records = check_records(native["artifacts"], NATIVE_NAMES, options.native_dir)
    transfer = read_json(options.transfer_report)
    apps = transfer.get("applications", [])
    transfer_checks = transfer.get("checks", [])
    required_transfer = {"dependency-pin", "dependency-clean", "arm-compiler", "libctru-build", "transfer-arm-compile", "transfer-host-compile", "transfer-execute", "transfer-wiiu-build"}
    if not isinstance(transfer_checks, list) or any(not isinstance(row, dict) for row in transfer_checks) or not required_transfer <= {row.get("name") for row in transfer_checks}:
        raise ValueError("Transfer validation check evidence is incomplete")
    if transfer.get("ok") is not True or transfer.get("version") != version or transfer.get("libctru_pin") != LIBCTRU or len(apps) != 1 or apps[0].get("application") != "transfer" or any(row.get("exit_code") != row.get("expected", 0) for row in transfer_checks):
        raise ValueError("Expected the passing pinned original transfer application report")
    app = apps[0]
    identity = app.get("generation", {}).get("input_identity", {})
    guest = app.get("guest", {})
    if identity.get("program_id") != "000400000ff00f0c" or identity.get("product_code") != "CTR-P-TEST" or guest.get("state") != "exited" or guest.get("fallback") != 0 or "CTR2WUP compiled GX transfers PASS" not in guest.get("messages", "") or guest.get("frames") != [0x00FF00FF, 0xFF0000FF, 0x00FF00FF] or guest.get("gx") != {"commands": 17, "dma": 3, "fills": 8, "transfers": 3, "copies": 1, "cache": 1, "pica": 1} or guest.get("pica_state") != {"lists": 1, "writes": 11, "finalizers": 1, "viewport_width": 0x12bb56dd}:
        raise ValueError("Original transfer execution identity or behavior is unverified")
    transfer_records = check_records(app["wiiu_artifacts"], TRANSFER_NAMES, options.transfer_dir)
    frames = app.get("frames", [])
    if len(frames) != 6 or {row.get("name") for row in frames} != FRAME_NAMES:
        raise ValueError("Require all six exact original transfer reference frames")
    for row in frames:
        if sha256(regular(options.reference_dir / row["name"], 1024 * 1024)) != row["sha256"]:
            raise ValueError("Reference frame differs from the checked original application")
    for folder, names in ((options.native_dir, NATIVE_NAMES), (options.transfer_dir, TRANSFER_NAMES)):
        for name in names:
            validate_binary(folder / name)
    wheel = regular(options.wheel, 32 * 1024 * 1024)
    if wheel.name != f"ctr2wup-{version}-py3-none-any.whl":
        raise ValueError("Expected the matching pure-Python wheel")
    wheel_entries = validate_zip(wheel, version=version)
    documentation = None
    documentation_records = []
    if options.documentation_dir is not None:
        documentation, documentation_records = check_documentation(options.documentation_dir, version, commit, checks)
    output = options.output.resolve()
    if output.exists():
        raise ValueError("Release output must not exist")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".ctr2wup-release-", dir=output.parent) as temporary:
        stage = Path(temporary)
        source = stage / f"CTR2WUP-source-{version}.zip"
        subprocess.run(["git", "-C", str(root), "archive", "--format=zip", "--prefix=CTR2WUP/", "--output", str(source), commit], check=True)
        source_entries = validate_zip(source, source=True)
        with zipfile.ZipFile(source) as src, zipfile.ZipFile(wheel) as binary:
            source_modules = {name.removeprefix("CTR2WUP/") for name in src.namelist()
                              if name.startswith(tuple("CTR2WUP/" + folder + "/" for folder in ("host", "translator", "renderer"))) and name.endswith(".py")}
            wheel_modules = {name for name in binary.namelist() if name.endswith(".py")}
            if source_modules != wheel_modules or any(src.read("CTR2WUP/" + name) != binary.read(name) for name in source_modules):
                raise ValueError("Developer wheel modules differ from the exact source commit")
        timestamp = datetime.fromtimestamp(int(git(root, "show", "-s", "--format=%ct", commit)), timezone.utc)
        date = (max(1980, timestamp.year), timestamp.month, timestamp.day, timestamp.hour, timestamp.minute, timestamp.second)
        diagnostic = stage / f"CTR2WUP-original-diagnostics-{version}.zip"
        with zipfile.ZipFile(source) as src, zipfile.ZipFile(diagnostic, "x", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            entries = {}
            for folder, names in ((options.native_dir, NATIVE_NAMES), (options.transfer_dir, TRANSFER_NAMES)):
                for name in names:
                    destination = f"sd-card/wiiu/apps/{name}" if name.endswith(".wuhb") else f"rpx/{name}"
                    entries[destination] = regular(folder / name).read_bytes()
            for name in sorted(FRAME_NAMES):
                entries[f"reference/gx-transfer/{name}"] = regular(options.reference_dir / name).read_bytes()
            for name in src.namelist():
                relative = name.removeprefix("CTR2WUP/")
                root_document = "/" not in relative and relative.endswith(".md")
                repository_document = relative.startswith(".github/") and relative.endswith(".md")
                if not name.endswith("/") and (relative in {"LICENSE", "THIRD_PARTY_NOTICES", ".github/repository-settings.json"}
                        or root_document or repository_document or relative.startswith("docs/")):
                    entries[relative] = src.read(name)
            for required in ("docs/gx-transfers.md", "docs/licenses/libctru.txt", "docs/licenses/COPYING.NEWLIB.ARM", "docs/licenses/devkitarm-crtls/3dsx_crt0.s", "docs/licenses/MPL-2.0.txt"):
                if required not in entries:
                    raise ValueError("Required diagnostic guide or corresponding license/CRT source is missing")
            entries["source-commit.txt"] = (commit + "\n").encode()
            entries["sdk-image-digest.txt"] = (SDK_IMAGE + "\n").encode()
            entries["README.txt"] = (f"CTR2WUP original diagnostics {version}\nSource commit: {commit}\n\nCopy sd-card contents to the SD root; launch the WUHB in Aroma.\nRPX files need an RPX-capable homebrew launcher. These are not WUP packages.\nGX Transfers: green/red/green TV, blue GamePad; R confirms the observed picture,\nL reports a problem, Minus exits. Results: sd:/ctr2wup/result-compiled-gx-transfer-{version}-*.txt.\nSee docs/gx-transfers.md. Preserve existing reports/saves.\nDeveloper builds require extracting the separate matching source ZIP.\nNo ROM/key is required. Console execution, commercial gameplay, PICA draws,\nguest audio and WUP installation are pending separate acceptance.\n").encode()
            for name, data in sorted(entries.items()):
                entry = zipfile.ZipInfo(name, date_time=date)
                entry.external_attr = 0o100644 << 16
                entry.compress_type = zipfile.ZIP_DEFLATED
                archive.writestr(entry, data)
        diagnostic_entries = validate_zip(diagnostic)
        shutil.copyfile(wheel, stage / wheel.name)
        if documentation is not None:
            for record in documentation_records:
                shutil.copyfile(options.documentation_dir / record["name"], stage / record["name"])
            (stage / "documentation-report.json").write_text(json.dumps(documentation, indent=2) + "\n", encoding="utf-8")
        public_evidence = {"ok": True, "version": version, "source_commit": commit, "original_fixtures_only": True, "checks": checks, "sdk_image": SDK_IMAGE, "libctru_pin": LIBCTRU, "native_artifacts": native_records, "transfer_artifacts": transfer_records, "transfer_guest": {key: guest[key] for key in ("state", "fault", "instructions", "fallback", "frames", "gx", "pica_state")}, "reference_frames": frames, "zip_crc_verified": True, "wheel_source_modules_verified": True, "source_archive_entries": source_entries, "diagnostic_archive_entries": diagnostic_entries, "wheel_entries": wheel_entries, "console_verified": False, "wup_installation_verified": False, "commercial_gameplay_verified": False}
        public_evidence["documentation_artifacts"] = documentation_records
        (stage / "validation-report.json").write_text(json.dumps(public_evidence, indent=2) + "\n", encoding="utf-8")
        published = [{"name": path.name, "size": path.stat().st_size, "sha256": sha256(path)} for path in sorted(stage.iterdir())]
        manifest = {"schema_version": 1, "version": version, "source_commit": commit, "prerelease": True, "original_fixtures_only": True, "artifacts": published}
        (stage / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        rows = published + [{"name": "manifest.json", "sha256": sha256(stage / "manifest.json")}]
        notes = f"# CTR2WUP {version} development diagnostics\n\nSource commit: `{commit}`.\n\nOriginal Native and GX Transfer WUHB/RPX diagnostics, exact reference frames,\nsource and a pure-Python developer wheel. Required host/C/sanitizer checks,\nWUT builds and original transfer execution passed for this source commit.\nZIP CRCs and SHA-256 checksums were verified before publication.\n\nCopy the diagnostic ZIP's `sd-card/` contents to the SD root and launch in Aroma.\nSee `docs/gx-transfers.md` inside the ZIP; preserve older SD reports and saves.\nThe wheel is a developer CLI, not the planned Windows injector GUI.\n\nPhysical-console acceptance, playable commercial games, PICA draw rendering,\nguest audio, installed WUP output and the Windows GUI remain pending.\nNo game dump, personal key, commercial asset or external reference implementation\nis included. SDK/runtime/CRT notices and corresponding startup source accompany\nthe original diagnostic.\n\n| Asset | SHA-256 |\n|---|---|\n"
        notes += "".join(f"| `{row['name']}` | `{row['sha256']}` |\n" for row in rows)
        if documentation is not None:
            notes += f"\nDownload `CTR2WUP-Documentation-{version}.html` and open it directly in a browser for offline navigation, search and printing. The checked export also supports offline review; the public source repository hosts the canonical Pages site.\n"
        (stage / "release-notes.md").write_text(notes, encoding="utf-8")
        rows.append({"name": "release-notes.md", "sha256": sha256(stage / "release-notes.md")})
        (stage / "SHA256SUMS.txt").write_text("".join(f"{row['sha256']}  {row['name']}\n" for row in rows), encoding="utf-8")
        for row in rows:
            if sha256(stage / row["name"]) != row["sha256"]:
                raise ValueError("Final release checksum mismatch")
        if git(root, "rev-parse", "HEAD") != commit or git(root, "status", "--porcelain"):
            raise ValueError("Source changed during release packaging")
        output.mkdir()  # Exclusive publication; never replace a prior directory.
        try:
            for path in stage.iterdir():
                os.replace(path, output / path.name)
        except BaseException:
            shutil.rmtree(output)
            raise
    return {"ok": True, "version": version, "source_commit": commit, "output": str(output), "manifest": str(output / "manifest.json")}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--version", required=True)
    parser.add_argument("--documentation-dir", type=Path, help="Verified offline HTML and public docs-only export")
    for name in ("native-dir", "transfer-dir", "transfer-report", "reference-dir", "wheel", "evidence", "sdk-receipt", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    try:
        result = package(parser.parse_args())
    except (ValueError, OSError, KeyError, TypeError, subprocess.CalledProcessError, zipfile.BadZipFile) as error:
        parser.exit(1, f"Release packaging failed: {error}\n")
    print(json.dumps(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
