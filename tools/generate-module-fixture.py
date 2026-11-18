#!/usr/bin/env python3
"""Regenerate the original CRO/CRS end-to-end loader guest."""
import json
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from host.module_fixture import module_files,manifest,generated_sources
folder=ROOT/'tests/fixtures/modules';folder.mkdir(parents=True,exist_ok=True)
for name,data in module_files().items():(folder/name).write_bytes(data)
(folder/'package.json').write_text(json.dumps(manifest(),indent=2)+'\n')
sources,_=generated_sources(folder)
for name,source in sources.items():(folder/name).write_text(source)
