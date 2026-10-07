"""Read-only adapter for the audited external lifter's analysis command.

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

from host.containers import inspect
from host.errors import HostError

SOURCE_COMMIT = "f78cd3777a676a7c02538ab993ca574a6d843416"


def analyze(path, *, binary=None, source=None):
    inspection = inspect(path)
    workspace = Path(__file__).resolve().parents[2]
    binary = Path(binary or os.environ.get("CTR2WUP_LIFTER", workspace / "ctr2wup-tools/3dsrecomp-target/release/3dsrecomp"))
    source = Path(source or workspace / "ctr2wup-deps/3dsrecomp")
    if not binary.is_file():
        raise HostError("missing_lifter", "Run tools/install-translator.sh or configure an audited external lifter", stage="analyze")
    commit = subprocess.run(["git", "-C", str(source), "rev-parse", "HEAD"], capture_output=True, text=True, check=False)
    status = subprocess.run(["git", "-C", str(source), "status", "--porcelain", "--untracked-files=no"], capture_output=True, text=True, check=False)
    if commit.returncode or status.returncode or commit.stdout.strip() != SOURCE_COMMIT or status.stdout.strip():
        raise HostError("unverified_lifter_source", "External lifter requires a clean checkout of the audited source pin", stage="analyze")
    result = subprocess.run([str(binary.resolve()), "analyze", str(Path(path).resolve())], capture_output=True, text=True, errors="replace", check=False)
    if result.returncode:
        raise HostError("lifter_failed", "External analysis failed", stage="analyze", details={"exit_code": result.returncode, "diagnostic": result.stderr[-4096:]})
    return {"ok": True, "schema_version": 1, "stage": "analyze", "source_commit": SOURCE_COMMIT,
            "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
            "input_identity": inspection["identity"], "executable_revision": inspection["executable_revision"],
            "analysis_text": result.stdout, "powerpc_backend": "unimplemented",
            "game_compatibility": "unverified"}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input")
    parser.add_argument("--binary")
    parser.add_argument("--source")
    args = parser.parse_args(argv)
    try:
        print(json.dumps(analyze(args.input, binary=args.binary, source=args.source), indent=2))
        return 0
    except (HostError, OSError) as exc:
        result = exc.as_dict() if isinstance(exc, HostError) else {"ok": False, "error": str(exc)}
        print(json.dumps(result, indent=2), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
