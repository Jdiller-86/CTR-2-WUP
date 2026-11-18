#!/usr/bin/env python3
"""Smoke the real ROM-to-WUP build chain with an original executable/RomFS.

Requires the installed Wii U SDK, pinned lifter and packaging extras. No owned
game or keys are needed; this does not establish console or Mario compatibility.
"""
import argparse
import json
from pathlib import Path
import sys
import tempfile

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from host.errors import HostError
from host.mario_build import build_test_title
from tests.test_mario_build import original_rom


def original_profile(inspection):
    assert inspection['identity']['product_code']=='CTR-P-TEST'
    return {'id':'original-intake-smoke','name':'CTR2WUP Intake Smoke',
        'program_id':inspection['identity']['program_id'],'gameplay_verified':False}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='ctr2wup-original-') as temporary:
        rom=Path(temporary)/'original.cxi';rom.write_bytes(original_rom())
        try:
            result=build_test_title(rom,args.output,select_profile=original_profile,
                progress=lambda stage:print(stage+'...',file=sys.stderr,flush=True))
        except HostError as exc:
            print(json.dumps(exc.as_dict(),indent=2),file=sys.stderr);return 1
    print(json.dumps({'ok':result['ok'],'output':result['output'],'stages':result['stages'],
        'original_input_only':True,'console_verified':False,'gameplay_verified':False},indent=2))
    return 0


if __name__=='__main__':raise SystemExit(main())
