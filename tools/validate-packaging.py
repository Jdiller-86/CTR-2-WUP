#!/usr/bin/env python3
"""Offline original staged-RPX packaging proof. Temporary synthetic inputs only."""
from datetime import datetime, timezone
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from host import __version__
from host.package_integrity import verify_package
from host.package_writer import pack_title
from host.title_stage import stage_title
from tests.test_package_writer import original_inputs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rpx", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    output = Path(args.output).absolute()
    output.mkdir(parents=True, exist_ok=False)
    start = time.perf_counter()
    with tempfile.TemporaryDirectory(prefix="ctr2wup-offline-packaging-") as temporary:
        root = Path(temporary)
        _, config, key_file = original_inputs(root)
        staging = root / "real RPX staging 日本語"
        stage_title(args.rpx, staging, title_id="00050000cf520001", name="CTR2WUP Offline Package Test", version=7)
        package = root / "synthetic WUP layout 日本語"
        packed = pack_title(staging, package, config)
        checked = verify_package(package, key_file, expected_staging=staging)
        if packed["validation"] != "decrypted_integrity" or checked["staging_comparison"] != "all files and directories match":
            raise RuntimeError("Offline package proof failed")
        # CLI pathways must also operate with spaces/UTF-8 paths. Logs contain
        # no credential values; the temporary input paths are never persisted.
        for command in (("pack-title", str(staging), "--output", str(root / "CLI package 日本語"), "--config", str(config)),
                        ("verify-package", str(package), "--common-key-file", str(key_file), "--expected-staging", str(staging))):
            result = subprocess.run([sys.executable, "-m", "host", *command], cwd=ROOT,
                                    capture_output=True, text=True, check=True)
            document = json.loads(result.stdout)
            if document.get("validation") != "decrypted_integrity":
                raise RuntimeError("Packaging CLI did not verify decrypted integrity")
        report = {"schema_version": 1, "ok": True, "version": __version__,
                  "generated_at_utc": datetime.now(timezone.utc).isoformat(),
                  "workflow": "real WUT RPX -> original title staging -> encrypted package -> independent decrypt/FST/file comparison",
                  "rpx_sha256": hashlib.sha256(Path(args.rpx).read_bytes()).hexdigest(),
                  "content_count": 4, "encrypted_bytes": sum((package / f"{n:08x}.app").stat().st_size for n in range(4)),
                  "staged_file_count": len(packed["files"]), "validation": "decrypted_integrity",
                  "cli_unicode_paths": True, "synthetic_key_and_certificates": True,
                  "signatures_authenticated": False, "console_verified": False, "installable_package": False,
                  "package_retained": False, "elapsed_seconds": round(time.perf_counter() - start, 6),
                  "unverified": ["user-local production certificate provenance/trust", "installer acceptance of nonhashed contents",
                                 "FST logical cluster/ownership and COS metadata behavior", "physical installation/Menu launch"]}
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
