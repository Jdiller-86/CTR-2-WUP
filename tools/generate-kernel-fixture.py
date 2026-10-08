#!/usr/bin/env python3
from pathlib import Path
import sys
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from host.kernel_fixture import generated_sources, kernel_program
if __name__ == "__main__":
    destination = ROOT / "tests/fixtures/kernel"
    destination.mkdir(parents=True, exist_ok=True)
    (destination / "kernel.arm.bin").write_bytes(kernel_program()[0])
    for name, contents in generated_sources().items():
        (destination / name).write_text(contents)
