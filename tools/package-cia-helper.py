#!/usr/bin/env python3
"""Bundle the original intake utility with exact .NET runtime notices."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from host import __version__


def main():
    helper = ROOT / "artifacts/cia-helper/windows/CTR2WUP-CIA-Intake.exe"
    data = helper.read_bytes()
    offset = struct.unpack_from("<I", data, 60)[0]
    if data[:2] != b"MZ" or data[offset:offset + 4] != b"PE\0\0" or struct.unpack_from("<H", data, offset + 4)[0] != 0x8664:
        raise RuntimeError("Expected the self-contained Windows x64 PE")
    checks = json.loads((ROOT / "artifacts/reports/cia-helper-final/report.json").read_text())
    if (not checks["ok"] or checks["version"] != __version__ or not checks.get("checks")
            or not all(case["ok"] for case in checks["checks"])):
        raise RuntimeError("Refusing to bundle a helper without matching successful fixture checks")
    native_verified = checks.get("native_windows_execution") == "verified"
    if native_verified and (checks.get("helper_sha256") != hashlib.sha256(data).hexdigest()
            or checks.get("source_sha256") != hashlib.sha256((ROOT / "tools/CiaIntake/Program.cs").read_bytes()).hexdigest()
            or checks.get("platform") != "win32 .NET fixture execution"):
        raise RuntimeError("Native Windows evidence must match the exact executable and current source")
    report = {"schema_version": 1, "version": __version__, "artifact": helper.name,
              "sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data),
              "source_sha256": hashlib.sha256((ROOT / "tools/CiaIntake/Program.cs").read_bytes()).hexdigest(),
              "sdk": "10.0.403", "runtime": "10.0.14", "target": "win-x64 self-contained",
              "fixture_checks": len(checks["checks"]), "fixture_platform": checks.get("platform", "unknown"),
              "linux_dotnet_fixture_checks": (len(checks["checks"])
                  if checks.get("platform", "").lower().startswith("linux") else 0),
              "native_windows_execution": "verified" if native_verified else "unverified",
              "user_game_inspected": False, "product_gui": False, "commercial_injection": False}
    folder = ROOT / "artifacts/delivery"; folder.mkdir(parents=True, exist_ok=True)
    archive = folder / f"CTR2WUP-CIA-Intake-Windows-x64-{__version__}.zip"
    entries = [(helper, helper.name), (ROOT / "LICENSE", "LICENSE-CTR2WUP.txt"),
               (ROOT / "docs/licenses/dotnet-runtime-LICENSE.txt", "LICENSE-DOTNET.txt"),
               (ROOT / "docs/licenses/dotnet-runtime-THIRD-PARTY-NOTICES.txt", "THIRD-PARTY-NOTICES-DOTNET.txt"),
               (ROOT / "docs/cia-intake.md", "README.md")]
    with zipfile.ZipFile(archive, "x", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as bundle:
        for path, name in entries:
            bundle.write(path, name)
        bundle.writestr("build-report.json", json.dumps(report, indent=2) + "\n")
    checksum = hashlib.sha256(archive.read_bytes()).hexdigest()
    (folder / (archive.name + ".sha256")).write_text(checksum + "  " + archive.name + "\n")
    (folder / f"cia-helper-manifest-{__version__}.json").write_text(json.dumps({**report, "archive": archive.name, "archive_sha256": checksum}, indent=2) + "\n")
    print(json.dumps({"output": str(archive), "bytes": archive.stat().st_size, "sha256": checksum}, indent=2))
    return 0


if __name__ == "__main__":raise SystemExit(main())
