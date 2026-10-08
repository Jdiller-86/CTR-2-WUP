"""Analysis and checked C generation using the separately installed lifter.

Desktop shared-library output and its native-endian ABI are deliberately not
accepted as a PowerPC executable backend. License notices need completion
before distributing the external tool or its generated ABI support header.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import shutil
import tempfile
import time

from host.containers import inspect
from host.errors import HostError

SOURCE_COMMIT = "f78cd3777a676a7c02538ab993ca574a6d843416"


def checked_source(source=None):
    from host.doctor import repository_root
    workspace = repository_root().parent
    source = Path(source or os.environ.get("CTR2WUP_LIFTER_SOURCE",workspace / "ctr2wup-deps/3dsrecomp"))
    commit = subprocess.run(["git", "-C", str(source), "rev-parse", "HEAD"], capture_output=True, text=True, check=False)
    status = subprocess.run(["git", "-C", str(source), "status", "--porcelain", "--untracked-files=no"], capture_output=True, text=True, check=False)
    if commit.returncode or status.returncode or commit.stdout.strip() != SOURCE_COMMIT or status.stdout.strip():
        raise HostError("unverified_lifter_source", "External lifter requires a clean checkout of the audited source pin", stage="translate")
    return source


def generate_into(path, output, *, source=None, generator=None):
    """Internal caller-owned staging; output must not exist."""
    from host.build import _run_build, _sha256
    from host.doctor import repository_root, tool_environment
    from .portable import adapt_source
    checked_source(source)
    repo = repository_root()
    workspace = repo.parent
    executable="ctr2wup-generate.exe" if os.name=="nt" else "ctr2wup-generate"
    generator = Path(generator or os.environ.get("CTR2WUP_GENERATOR", workspace / "ctr2wup-tools" / executable))
    receipt = generator.with_suffix(".json")
    try:
        witness = json.loads(receipt.read_text())
    except (OSError, ValueError):
        witness = {}
    if (not generator.is_file() or witness.get("source_commit") != SOURCE_COMMIT
            or witness.get("driver_sha256") != _sha256(repo/"translator/generate.rs")
            or witness.get("binary_sha256") != _sha256(generator)):
        raise HostError("unverified_generator", "Run tools/install-translator.sh to build the current generation driver against the audited source", stage="translate")
    inspection = inspect(path)
    output = Path(output)
    if output.exists():
        raise HostError("output_exists", "Generation output already exists", stage="translate")
    output.parent.mkdir(parents=True, exist_ok=True)
    log = output.parent / (output.name + "-generation.log")
    start = time.perf_counter()
    _run_build([str(generator.resolve()),str(Path(path).resolve()),str(output.resolve())],
               tool_environment(), log, repo, stage="translate", timeout=300)
    try:
        metadata = json.loads((output/"generation.json").read_text())
        if (metadata["abi"] != 4 or metadata["source_commit"] != SOURCE_COMMIT
                or metadata["program_id"] != inspection["identity"]["program_id"]):
            raise ValueError("Generation identity/ABI mismatch")
        instructions = 0
        for name in metadata["sources"]:
            if not name.endswith(".c") or Path(name).name != name:
                raise ValueError("Invalid generated source name")
            if name.startswith("code"):
                p = output / name
                adapted, count = adapt_source(p.read_text())
                p.write_text(adapted, encoding="utf-8")
                instructions += count
        if not instructions: raise ValueError("No executable instructions generated")
    except (OSError, ValueError, KeyError) as exc:
        raise HostError("invalid_generated_code", str(exc), stage="translate") from exc
    if inspect(path)["executable_revision"] != inspection["executable_revision"]:
        raise HostError("input_changed", "Input changed during translation", stage="translate")
    shutil.copy2(repo/"runtime/recomp/recomp.h", output/"recomp.h")
    report = {**metadata,"ok":True,"stage":"translate","input_identity":inspection["identity"],
              "executable_revision":inspection["executable_revision"],
              "generator_sha256":_sha256(generator),"generated_instructions":instructions,
              "generated_c_bytes":sum((output/name).stat().st_size for name in metadata["sources"]),
              "elapsed_seconds":round(time.perf_counter()-start,6),
              "abi_implementation":"original checked CTR2WUP ABI; instruction-state synchronization; little-endian callbacks",
              "execution_support":"profile gated; no commercial compatibility claim",
              "upstream_tool_bundled":False,"upstream_header_bundled":False,"console_verified":False}
    (output/"generation.json").write_text(json.dumps(report,indent=2)+"\n")
    return report


def generate(path, output, **options):
    from host.atomic import publish_directory
    output = Path(output).absolute()
    if output.exists(): raise HostError("output_exists", "Generation output already exists", stage="prepare")
    output.parent.mkdir(parents=True,exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix=".ctr2wup-lifter-",dir=output.parent))
    try:
        result = generate_into(path,stage/"code",**options)
        publish_directory(stage,output)
        return {**result,"output":str(output)}
    finally:
        if stage.exists(): shutil.rmtree(stage)


def analyze(path, *, binary=None, source=None):
    inspection = inspect(path)
    from host.doctor import repository_root
    workspace = repository_root().parent
    executable="3dsrecomp.exe" if os.name=="nt" else "3dsrecomp"
    binary = Path(binary or os.environ.get("CTR2WUP_LIFTER", workspace / "ctr2wup-tools/3dsrecomp-target/release" / executable))
    source = checked_source(source)
    if not binary.is_file():
        raise HostError("missing_lifter", "Run tools/install-translator.sh or configure an audited external lifter", stage="analyze")
    result = subprocess.run([str(binary.resolve()), "analyze", str(Path(path).resolve())], capture_output=True, text=True, errors="replace", check=False)
    if result.returncode:
        raise HostError("lifter_failed", "External analysis failed", stage="analyze", details={"exit_code": result.returncode, "diagnostic": result.stderr[-4096:]})
    return {"ok": True, "schema_version": 1, "stage": "analyze", "source_commit": SOURCE_COMMIT,
            "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
            "input_identity": inspection["identity"], "executable_revision": inspection["executable_revision"],
            "analysis_text": result.stdout, "powerpc_backend": "checked-static-C-profile-gated",
            "game_compatibility": "unverified"}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input")
    parser.add_argument("--binary")
    parser.add_argument("--source")
    parser.add_argument("--generate-output")
    args = parser.parse_args(argv)
    try:
        result = generate(args.input,args.generate_output,source=args.source) if args.generate_output else analyze(args.input,binary=args.binary,source=args.source)
        print(json.dumps(result, indent=2))
        return 0
    except (HostError, OSError) as exc:
        result = exc.as_dict() if isinstance(exc, HostError) else {"ok": False, "error": str(exc)}
        print(json.dumps(result, indent=2), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
