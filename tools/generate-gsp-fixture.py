#!/usr/bin/env python3
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from host.gsp_fixture import generated_sources,gsp_code
folder=Path(__file__).resolve().parents[1]/'tests/fixtures/gsp';folder.mkdir(parents=True,exist_ok=True)
for name,text in generated_sources().items():(folder/name).write_text(text)
(folder/'gsp.arm.bin').write_bytes(gsp_code())
