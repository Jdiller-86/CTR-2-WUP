#!/usr/bin/env python3
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from host.thread_fixture import generated_sources,image_code
folder=Path(__file__).resolve().parents[1]/'tests/fixtures/threads';folder.mkdir(parents=True,exist_ok=True)
for name,text in generated_sources().items():(folder/name).write_text(text)
(folder/'thread.code.bin').write_bytes(image_code())
