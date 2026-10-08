#!/usr/bin/env python3
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from host.hid_fixture import hid_code,generated_sources
if __name__=='__main__':
    out=ROOT/'tests/fixtures/hid';out.mkdir(parents=True,exist_ok=True)
    (out/'hid.arm.bin').write_bytes(hid_code())
    for name,value in generated_sources().items():(out/name).write_text(value)
