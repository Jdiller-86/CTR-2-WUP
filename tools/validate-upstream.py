#!/usr/bin/env python3
"""Real lifter integration checks. Requires the optional pinned installation.

Use --cc powerpc-linux-gnu-gcc --run-prefix 'qemu-ppc -cpu 750' --static
inside the development container for emulated big-endian execution.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import struct
import sys
import tempfile
import time
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from host.guest_fixture import fixture_bytes
from tests.host_fixtures import ncch
from translator.upstream import generate_into
ROOT=Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc',default='gcc');parser.add_argument('--run-prefix',default='')
    parser.add_argument('--cc-prefix',default='',help='Argument-list launcher, e.g. bash PATH/dev-container.sh')
    parser.add_argument('--static',action='store_true');parser.add_argument('--sanitize',action='store_true')
    parser.add_argument('--output',required=True)
    args=parser.parse_args();out=Path(args.output).absolute();out.mkdir(parents=True,exist_ok=False)
    results=[]
    def run(name,command,cwd=ROOT):
        start=time.perf_counter();p=subprocess.run(command,cwd=cwd,capture_output=True,text=True,check=False)
        (out/(name+'.log')).write_text(p.stdout+p.stderr)
        if p.returncode: raise RuntimeError(f'{name} failed ({p.returncode}); see {out/(name+".log")}')
        result={'name':name,'exit_code':p.returncode,'elapsed_seconds':round(time.perf_counter()-start,6)}
        if p.stdout.startswith('{'): result['result']=json.loads(p.stdout)
        results.append(result);print(name+': PASS',flush=True)
    core=ROOT/'runtime/core';recomp=ROOT/'runtime/recomp'
    common=shlex.split(args.cc_prefix)+[args.cc,'-std=c11','-O2','-g','-Wall','-Wextra','-Werror','-ffp-contract=off','-fno-fast-math','-I',str(core),'-I',str(recomp)]
    if args.static:common+=['-static','-mcpu=750']
    if args.sanitize:common+=['-O1','-fno-omit-frame-pointer','-fsanitize=address,undefined']
    for name in ('frame','abi'):
        folder=out/name;folder.mkdir()
        code=(ROOT/'tests/recomp/abi.arm.bin').read_bytes() if name=='abi' else None
        rom=folder/'original.cxi'
        if code is not None:
            container=ncch([('.code',code.ljust(4096,b'\0'))])
            struct.pack_into('<I',container,0x218,len(code))
            container[0x160:0x180]=hashlib.sha256(container[0x200:0x600]).digest()
            rom.write_bytes(container)
        else: rom.write_bytes(fixture_bytes())
        generation=generate_into(rom,folder/'aot')
        sources=[core/n for n in ('ctr_cpu.c','ctr_memory.c','ctr_guest.c')]+[recomp/'ctr_recomp.c']
        flags=[]
        if name=='frame':
            flags=['-DCTR_EXTERNAL_AOT'];sources += [core/'ctr_guest_fixture.c',ROOT/'tests/fixtures/guest/image.generated.c']
        else:
            (folder/'abi_image.h').write_text('#define ABI_CODE_SIZE '+str(len(code))+'\nstatic const unsigned char abi_code[4096]={'+','.join(str(b) for b in code)+'};\n')
        binary=folder/(name+('.exe' if os.name=='nt' else ''))
        sources+=[ROOT/'tests/recomp'/('test_'+name+'.c')]+[folder/'aot'/n for n in generation['sources']]
        run(name+'-compile',common+flags+['-I',str(folder),'-I',str(folder/'aot'),*map(str,sources),'-lm','-o',str(binary)])
        run(name+'-execute',shlex.split(args.run_prefix)+[str(binary)])
    (out/'report.json').write_text(json.dumps({'ok':True,'checks':results,'console_verified':False,'powerpc_execution':'emulated Linux' if args.static else None},indent=2)+'\n')
    return 0
if __name__=='__main__':raise SystemExit(main())
