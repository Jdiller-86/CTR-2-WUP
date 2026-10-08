#!/usr/bin/env python3
"""Regenerate only the original ARM heap fixture and portable AOT sources."""
from pathlib import Path
import sys
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from host.heap_fixture import generated_sources, heap_code

if __name__ == "__main__":
    destination = ROOT / "tests/fixtures/heap"
    destination.mkdir(parents=True, exist_ok=True)
    (destination / "heap.arm.bin").write_bytes(heap_code())
    for name, contents in generated_sources().items():
        (destination / name).write_text(contents)
