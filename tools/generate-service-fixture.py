#!/usr/bin/env python3
"""Regenerate only the original redistributable SRV/CFG startup probe."""
from pathlib import Path
import sys
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from host.service_fixture import generated_sources, service_code

folder = ROOT / "tests/fixtures/services"
folder.mkdir(parents=True, exist_ok=True)
for name, data in generated_sources().items():
    (folder / name).write_text(data, encoding="utf-8")
(folder / "boot.arm.bin").write_bytes(service_code())
