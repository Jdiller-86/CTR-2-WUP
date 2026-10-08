#!/usr/bin/env python3
"""Run readiness checks and write a versioned report with actual exit statuses."""
from datetime import datetime, timezone
import hashlib
import importlib.metadata
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from host import __version__

ROOT = Path(__file__).resolve().parents[1]


def main():
    os.chdir(ROOT)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    report_root = ROOT / "artifacts" / "reports" / ("validation-v1-" + stamp)
    report_root.mkdir(parents=True, exist_ok=False)
    results = []

    def run(name, command, *, expected=0, env=None, cwd=ROOT):
        start = time.perf_counter()
        process = subprocess.run(command, cwd=cwd, env=env, stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, text=True, errors="replace", check=False)
        log = report_root / (name + ".log")
        log.write_text(process.stdout, encoding="utf-8")
        result = {"name": name, "command": list(map(str, command)), "exit_code": process.returncode,
                  "expected_exit_code": expected, "passed": process.returncode == expected,
                  "elapsed_seconds": round(time.perf_counter() - start, 6),
                  "log": str(log.relative_to(ROOT))}
        results.append(result)
        print(name + ": " + ("PASS" if result["passed"] else "FAIL"), flush=True)
        return process

    python_tests = run("python-tests", [sys.executable, "-m", "unittest", "discover", "-s", "tests", "-v"])
    match = re.search(r"Ran (\d+) tests", python_tests.stdout)
    test_count = int(match[1]) if match else 0
    if not test_count:
        results[-1]["passed"] = False
    c_tests = run("cpu-tests", ["make", "-C", "tests/cpu", "check", "sanitize"])
    c_results = [json.loads(line) for line in c_tests.stdout.splitlines() if line.startswith('{"ok":')]
    if len(c_results) != 2 or any(not value.get("ok") or not value.get("assertions") for value in c_results):
        results[-1]["passed"] = False
    guest_tests=run("guest-tests",["make","-C","tests/guest","check","sanitize"])
    guest_results=[json.loads(line) for line in guest_tests.stdout.splitlines() if line.startswith('{"ok":')]
    if len(guest_results)!=2 or any(not item.get("ok") or not item.get("aot_interpreter_memory_match") for item in guest_results):
        results[-1]["passed"]=False
    service_tests=run("service-tests",["make","-C","tests/services","check","sanitize"])
    service_results=[json.loads(line) for line in service_tests.stdout.splitlines() if line.startswith('{"ok":')]
    if len(service_results)!=2 or any(not item.get("ok") or not item.get("arm_tls_srv_cfg_startup") for item in service_results):
        results[-1]["passed"]=False
    heap_tests=run("heap-tests",["make","-C","tests/heap","check","sanitize"])
    heap_results=[json.loads(line) for line in heap_tests.stdout.splitlines() if line.startswith('{"ok":')]
    if len(heap_results)!=2 or any(not item.get("ok") or not item.get("arm_heap_allocation_lifecycle") for item in heap_results):
        results[-1]["passed"]=False
    run("doctor", [sys.executable, "-m", "host", "doctor"])
    run("fixtures", [sys.executable, "-m", "tests.host_fixtures", "artifacts/fixtures"])
    for kind in ("cxi", "3ds"):
        run("inspect-" + kind, [sys.executable, "-m", "host", "inspect", "artifacts/fixtures/synthetic." + kind])
    gate = run("commercial-unsupported-gate", [sys.executable, "-m", "host", "build", "artifacts/fixtures/synthetic.cxi", "--output", "artifacts/unsupported-never-published"], expected=2)
    if '"unsupported_profile"' not in gate.stdout or (ROOT / "artifacts/unsupported-never-published").exists():
        results[-1]["passed"] = False
    run("external-analysis", [sys.executable, "-m", "translator.upstream", "artifacts/fixtures/synthetic.cxi"])
    output = ROOT / "artifacts" / ("diagnostic-v1-" + stamp)
    run("prototype-build", [sys.executable, "-m", "host", "build", "--target", "prototype", "--output", str(output)])
    fixture=ROOT/"artifacts"/("guest-fixture-"+stamp)
    run("guest-fixture-create",[sys.executable,"-m","host","make-fixture","--output",str(fixture)])
    guest_output=ROOT/"artifacts"/("guest-console-"+stamp)
    run("guest-rpx-build",[sys.executable,"-m","host","build",str(fixture/"ctr2wup-guest.cxi"),"--target","guest-fixture","--output",str(guest_output)])
    guest_desktop=ROOT/"artifacts"/("guest-desktop-"+stamp)
    run("guest-desktop-execute",[sys.executable,"-m","host","run-fixture",str(fixture/"ctr2wup-guest.cxi"),"--output",str(guest_desktop),"--touch","--touch-x","100","--touch-y","80","--buttons","1"])
    upstream_console=ROOT/"artifacts"/("upstream-console-"+stamp)
    upstream_desktop=ROOT/"artifacts"/("upstream-desktop-"+stamp)
    run("external-abi-tests",[sys.executable,"tools/validate-upstream.py","--output",str(report_root/"upstream-native")])
    run("external-abi-sanitizers",[sys.executable,"tools/validate-upstream.py","--sanitize","--output",str(report_root/"upstream-sanitized")])
    run("external-desktop-execute",[sys.executable,"-m","host","run-fixture",str(fixture/"ctr2wup-guest.cxi"),"--output",str(upstream_desktop),"--backend","upstream","--touch","--touch-x","100","--touch-y","80","--buttons","1"])
    if (upstream_desktop/"top.ppm").is_file():
        same=all((upstream_desktop/name).read_bytes()==(guest_desktop/name).read_bytes() for name in ("top.ppm","bottom.ppm"))
        results.append({"name":"external-original-frame-parity","passed":same})
    run("external-rpx-build",[sys.executable,"-m","host","build",str(fixture/"ctr2wup-guest.cxi"),"--target","guest-fixture","--backend","upstream","--output",str(upstream_console)])
    title_stage=ROOT/"artifacts"/("title-staging-"+stamp)
    run("original-title-staging",[sys.executable,"-m","host","stage-title",str(upstream_console/"ctr2wup-upstream-guest.rpx"),"--output",str(title_stage),"--title-id","00050000cf520001","--name","CTR2WUP AOT Prototype"])
    run("offline-encrypted-packaging",[sys.executable,"tools/validate-packaging.py","--rpx",str(upstream_console/"ctr2wup-upstream-guest.rpx"),"--output",str(report_root/"offline-packaging")])
    run("private-title-intake",[sys.executable,"-m","host","analyze-title",str(fixture/"ctr2wup-guest.cxi"),"--output",str(report_root/"intake-smoke"),"--lifter"])
    run("wheel-build", [sys.executable, "-m", "pip", "wheel", "--no-deps", "--no-build-isolation", "--wheel-dir", "artifacts/dist", "."], env=dict(os.environ, PIP_CACHE_DIR=str(ROOT / "artifacts/pip-cache")))
    wheel = ROOT / ("artifacts/dist/ctr2wup-"+__version__+"-py3-none-any.whl")
    if wheel.exists():
        with tempfile.TemporaryDirectory(prefix="ctr2wup-wheel-", dir="/tmp") as temporary:
            run("wheel-install", [sys.executable, "-m", "pip", "install", "--no-deps", "--no-index", "--target", temporary, str(wheel)])
            task_env = dict(os.environ, PYTHONPATH=temporary)
            run("installed-wheel-inspect", [sys.executable, "-m", "host", "inspect", str(ROOT / "artifacts/fixtures/synthetic.cxi")], env=task_env, cwd=Path(temporary))
            task_env["CTR2WUP_PROJECT_ROOT"]=str(ROOT)
            run("installed-wheel-guest-execute",[sys.executable,"-m","host","run-fixture",str(fixture/"ctr2wup-guest.cxi"),"--output",str(ROOT/"artifacts"/("installed-guest-"+stamp)),"--frames","1"],env=task_env,cwd=Path(temporary))
            from tests.test_package_writer import original_inputs
            _, local_config, _ = original_inputs(Path(temporary))
            run("installed-wheel-encrypted-packaging",[sys.executable,"-m","host","pack-title",str(title_stage),"--config",str(local_config),"--output",str(Path(temporary)/"installed package 日本語")],env=task_env,cwd=Path(temporary))
    source_digest = hashlib.sha256()
    for folder in ("host", "runtime", "translator", "renderer", "packaging"):
        for path in sorted((ROOT / folder).rglob("*")):
            if path.is_file() and "__pycache__" not in path.parts:
                source_digest.update(str(path.relative_to(ROOT)).encode("utf-8") + b"\0" + path.read_bytes())
    report = {"schema_version": 1, "generated_at_utc": datetime.now(timezone.utc).isoformat(),
              "workflow": "native/original/external-AOT RPX builds, checked ARM/Thumb/scalar-VFP/TLS, SRV/CFG startup and heap-memory lifecycle, title staging/intake and offline encrypted packaging",
              "version": __version__,
              "ok": all(result["passed"] for result in results), "python_tests": test_count,
              "cpu_runs": c_results, "guest_runs":guest_results,"service_runs":service_results,"heap_runs":heap_results,"checks": results,
              "source_sha256": source_digest.hexdigest(),
              "build_tools": {package: importlib.metadata.version(package) for package in ("setuptools", "wheel", "pip")},
              "prototype_output": str(output.relative_to(ROOT)),
              "guest_console_output":str(guest_output.relative_to(ROOT)),
              "guest_desktop_output":str(guest_desktop.relative_to(ROOT)),
              "guest_fixture_output":str(fixture.relative_to(ROOT)),
              "upstream_console_output":str(upstream_console.relative_to(ROOT)),
              "upstream_desktop_output":str(upstream_desktop.relative_to(ROOT)),
              "title_staging_output":str(title_stage.relative_to(ROOT)),
              "unverified": ["physical Wii U execution, input/audio/exit", "PowerPC correctness/timings", "clean Windows execution", "commercial ROM execution", "WUP installation"],
              "unimplemented": ["complete ARM/VFP", "multi-thread kernel and general IPC/HID/GSP/other services", "PICA-to-GX2 rendering", "game saves/audio/CRO", "commercial conversion pipeline", "Windows GUI"],
              "packaging": {"writer": "original scoped encrypted profile", "offline_report": str((report_root/"offline-packaging/report.json").relative_to(ROOT)), "synthetic_credentials_only": True, "installer_acceptance": "unverified"},
              "configuration": {"draft_persistence": "see STATUS.md", "publication": "not_checked_by_runner", "fresh_task_restore": "not_checked_by_runner"}}
    destination = report_root / "report.json"
    destination.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(str(destination))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
