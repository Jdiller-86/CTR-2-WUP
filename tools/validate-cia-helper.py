#!/usr/bin/env python3
"""Execute the .NET helper on original CIA fixtures and compare Python intake."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from host import __version__
from host.containers import inspect
from host.intake_export import export_intake
from tests.host_fixtures import cia, ncch, blz_repeat


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--helper", required=True, help="Built .NET helper DLL or native Windows executable")
    parser.add_argument("--dotnet", default="dotnet")
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    output = Path(args.output).absolute(); output.mkdir(parents=True, exist_ok=False)
    helper = Path(args.helper).absolute()
    native_executable = helper.suffix.lower() == ".exe"
    if native_executable and sys.platform != "win32":
        parser.error("A Windows executable requires native Windows execution")
    launcher = [str(helper)] if native_executable else [args.dotnet, str(helper)]
    cases = []
    def execute(name, data, *, code=None, executable=None):
        with tempfile.TemporaryDirectory(prefix="ctr2wup-cia-reference-") as temporary:
            root = Path(temporary); path = root / "original game 日本語.cia"; path.write_bytes(data)
            target = root / "intake 日本語"
            process = subprocess.run([*launcher, str(path), str(target)], capture_output=True,
                                     text=True, encoding="utf-8")
            good = process.returncode == (2 if code else 0)
            if code:
                good &= code in process.stderr and not target.exists()
            else:
                with zipfile.ZipFile(target / "CTR2WUP-intake.zip") as archive:
                    document = json.loads(archive.read("intake-report.json"))
                    good &= document["executable_available"] == executable
                    good &= document["source_sha256"] == hashlib.sha256(data).hexdigest()
                    good &= not document["credentials_exported"] and not document["romfs_exported"]
                    good &= set(archive.namelist()) == ({"intake-report.json", "executable-view.cxi"} if executable else {"intake-report.json"})
                    if executable:
                        cxi = root / "view.cxi"; cxi.write_bytes(archive.read("executable-view.cxi"))
                        good &= inspect(cxi)["executable_revision"] == inspect(path)["executable_revision"]
                        python = export_intake(path, root / "python export")
                        good &= cxi.read_bytes() == (root / "python export/executable-view.cxi").read_bytes()
                        good &= python["executable_available"]
                good &= path.read_bytes() == data
            cases.append({"name": name, "ok": bool(good), "exit_code": process.returncode})
            if not good:
                (output / (name + ".log")).write_text(process.stdout + process.stderr)
            print(name + (": PASS" if good else ": FAIL"), flush=True)
    execute("decrypted-executable", cia(), executable=True)
    image = ncch(); start = len(image); payload = b"Original RomFS test".ljust(33 * 1024 * 1024, b"\0"); image += payload
    image[0x18F] &= ~2; struct.pack_into("<I", image, 0x104, len(image)//512)
    struct.pack_into("<III", image, 0x1B0, start//512, len(payload)//512, 1)
    image[0x1E0:0x200] = hashlib.sha256(payload[:512]).digest()
    execute("romfs-omitted", cia([(0, image)]), executable=True)
    execute("compressed-executable-view", cia([(0, bytes(ncch([(".code", blz_repeat())], compressed=True)))]), executable=True)
    execute("sparse-content-indices", cia([(0, bytes(ncch())), (8, b"original auxiliary bytes")]), executable=True)
    execute("outer-encrypted-report", cia(encrypted=True), executable=False)
    inner = ncch(); inner[0x18F] &= ~4
    execute("inner-encrypted-report", cia([(0, inner)]), executable=False)
    execute("update-report", cia(title_id=0x0004000E00054000), executable=False)
    execute("missing-main-report", cia([(8, bytes(ncch()))]), executable=False)
    corrupt = cia(); corrupt[-1] ^= 1
    execute("bad-content-hash", corrupt, code="cia_content_hash_mismatch")
    bitmap = cia(); bitmap[0x20] |= 0x40
    execute("bad-bitmap", bitmap, code="invalid_cia_bitmap")
    execute("trailing-data", cia() + b"extra", code="trailing_cia_data")
    huge = cia(); struct.pack_into("<Q", huge, 24, 2**63)
    execute("invalid-size", huge, code="invalid_bounds")
    execute("truncated", b"original", code="invalid_bounds")
    report = {"schema_version": 1, "ok": all(x["ok"] for x in cases), "version": __version__,
              "helper_sha256": hashlib.sha256(helper.read_bytes()).hexdigest(), "checks": cases,
              "source_sha256": hashlib.sha256((ROOT / "tools/CiaIntake/Program.cs").read_bytes()).hexdigest(),
              "platform": sys.platform + " .NET fixture execution",
              "actual_user_cia_inspected": False,
              "native_windows_execution": "verified" if native_executable and all(x["ok"] for x in cases) else "unverified"}
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    return 0 if report["ok"] else 1


if __name__ == "__main__":raise SystemExit(main())
