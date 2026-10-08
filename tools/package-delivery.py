#!/usr/bin/env python3
"""Bundle only original source and native diagnostic artifacts, never user data."""
import hashlib
import json
from pathlib import Path
import zipfile
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from host import __version__
from host.guest_fixture import fixture_bytes
OUT = ROOT / "artifacts" / "delivery"
SOURCE_FOLDERS = (".github", "docs", "host", "translator", "runtime", "renderer", "packaging", "profiles", "tests", "tools")
SOURCE_FILES = (".gitignore", "AGENTS.md", "LICENSE", "Makefile", "README.md", "STATUS.md", "THIRD_PARTY_NOTICES", "pyproject.toml")
FORBIDDEN = {".3ds", ".cxi", ".cia", ".3dsx", ".sav", ".keys", ".key", ".cert", ".app", ".h3", ".tik", ".tmd", ".pyc", ".log"}


def archive(path, entries):
    with zipfile.ZipFile(path, "x", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as bundle:
        for source, destination in sorted(entries, key=lambda pair: pair[1]):
            info = zipfile.ZipInfo(destination, date_time=(2026, 10, 7, 0, 0, 0))
            info.external_attr = (0o40755 if source.is_dir() else 0o100755 if source.name.endswith(".sh") else 0o100644) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            bundle.writestr(info, b"" if source.is_dir() else source.read_bytes())


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    entries = [(ROOT / name, "CTR2WUP/" + name) for name in SOURCE_FILES]
    for folder in SOURCE_FOLDERS:
        for source in (ROOT / folder).rglob("*"):
            if not source.is_file() or any(part in source.parts for part in ("__pycache__", "obj", "bin")):
                continue
            if source.is_symlink() or source.suffix.lower() in FORBIDDEN:
                raise RuntimeError(f"Refusing sensitive/generated input: {source}")
            entries.append((source, "CTR2WUP/" + str(source.relative_to(ROOT))))
    source_archive = OUT / ("CTR2WUP-source-"+__version__+".zip")
    archive(source_archive, entries)
    latest = sorted((ROOT / "artifacts/reports").glob("validation-v1-*/report.json"))[-1]
    report = json.loads(latest.read_text())
    if not report["ok"] or report.get("version")!=__version__:
        raise RuntimeError("Refusing to ship diagnostics from a failed validation run")
    prototype = ROOT / report["prototype_output"]
    binary_entries = [(prototype / name, name) for name in ("ctr2wup-prototype.rpx", "ctr2wup-prototype.wuhb", "build-report.json", "build.log")]
    binary_entries += [(ROOT / name, name) for name in ("LICENSE", "THIRD_PARTY_NOTICES", "docs/console-testing.md", "docs/compatibility.md")]
    binary_entries += [(path, str(path.relative_to(ROOT))) for path in (ROOT / "docs/licenses").iterdir() if path.is_file()]
    binary_entries.append((latest, "validation-report.json"))
    binary_entries.append((ROOT/report["packaging"]["offline_report"], "offline-packaging-report.json"))
    diagnostic_archive = OUT / ("CTR2WUP-diagnostic-"+__version__+".zip")
    archive(diagnostic_archive, binary_entries)
    guest_console=ROOT/report["guest_console_output"]
    guest_desktop=ROOT/report["guest_desktop_output"]
    fixture=ROOT/report["guest_fixture_output"]
    if (fixture/"ctr2wup-guest.cxi").read_bytes()!=fixture_bytes():
        raise RuntimeError("Refusing to include any ROM other than the original generated fixture")
    guest_entries=[(guest_console/name,name) for name in ("ctr2wup-guest-fixture.rpx","ctr2wup-guest-fixture.wuhb","build-report.json")]
    guest_entries += [(guest_desktop/name,"desktop-reference/"+name) for name in ("top.ppm","bottom.ppm","build-report.json")]
    guest_entries += [(fixture/"ctr2wup-guest.cxi","original-fixture/ctr2wup-guest.cxi"),(fixture/"fixture-report.json","original-fixture/fixture-report.json")]
    guest_entries += [(ROOT/name,name) for name in ("LICENSE","THIRD_PARTY_NOTICES","docs/guest-fixture.md","docs/console-testing.md")]
    guest_entries += [(path,str(path.relative_to(ROOT))) for path in (ROOT/"docs/licenses").iterdir() if path.is_file()]
    guest_archive=OUT/("CTR2WUP-guest-fixture-"+__version__+".zip")
    archive(guest_archive,guest_entries)
    upstream=ROOT/report["upstream_console_output"]
    upstream_entries=[(upstream/name,name) for name in ("ctr2wup-upstream-guest.rpx","ctr2wup-upstream-guest.wuhb","build-report.json")]
    upstream_entries += [(ROOT/name,name) for name in ("LICENSE","THIRD_PARTY_NOTICES","docs/upstream-integration.md","docs/console-testing.md")]
    upstream_entries += [(p,str(p.relative_to(ROOT))) for p in (ROOT/"docs/licenses").iterdir() if p.is_file()]
    upstream_entries += [(ROOT/report["upstream_desktop_output"]/name,"desktop-reference/"+name) for name in ("top.ppm","bottom.ppm","build-report.json")]
    upstream_archive=OUT/("CTR2WUP-upstream-guest-"+__version__+".zip")
    archive(upstream_archive,upstream_entries)
    staged=ROOT/report["title_staging_output"]
    staged_entries=[(p,str(p.relative_to(staged))+("/" if p.is_dir() else "")) for p in staged.rglob('*')]
    staged_entries += [(ROOT/name,name) for name in ("LICENSE","THIRD_PARTY_NOTICES","docs/packaging.md")]
    staged_entries += [(p,str(p.relative_to(ROOT))) for p in (ROOT/"docs/licenses").iterdir() if p.is_file()]
    staging_archive=OUT/("CTR2WUP-title-staging-"+__version__+".zip")
    archive(staging_archive,staged_entries)
    wheel = ROOT / ("artifacts/dist/ctr2wup-"+__version__+"-py3-none-any.whl")
    results = [{"name": path.name, "size": path.stat().st_size, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()} for path in (source_archive, diagnostic_archive,guest_archive,upstream_archive,staging_archive,wheel)]
    (OUT / ("SHA256SUMS-"+__version__+".txt")).write_text("".join(f"{item['sha256']}  {item['name']}\n" for item in results))
    (OUT / ("delivery-manifest-"+__version__+".json")).write_text(json.dumps({"schema_version": 1, "files": results, "console_verified": False, "commercial_conversion": "unimplemented", "wup_generated": False, "encrypted_writer": "implemented; offline verified with synthetic inputs", "installer_acceptance": "unverified"}, indent=2) + "\n")
    print(json.dumps(results, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
