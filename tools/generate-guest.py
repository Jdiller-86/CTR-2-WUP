#!/usr/bin/env python3
"""Explicitly regenerate original tracked guest fixture C sources."""
from pathlib import Path
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from host.guest_fixture import fixture_bytes
from host.guest_pipeline import generated_sources, profiled_image

root=Path(__file__).resolve().parents[1]
destination=root/"tests/fixtures/guest"
with tempfile.TemporaryDirectory() as temporary:
    fixture=Path(temporary)/"original.cxi"
    fixture.write_bytes(fixture_bytes())
    sources=generated_sources(profiled_image(fixture))
    destination.mkdir(parents=True,exist_ok=True)
    for name,source in sources.items():
        (destination/name).write_text(source,encoding="utf-8")
