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
from host.service_fixture import fixture_bytes as service_fixture_bytes
from host.heap_fixture import fixture_bytes as heap_fixture_bytes
from host.kernel_fixture import fixture_bytes as kernel_fixture_bytes
from host.hid_fixture import fixture_bytes as hid_fixture_bytes
from host.gsp_fixture import fixture_bytes as gsp_fixture_bytes
from host.relay_fixture import fixture_bytes as relay_fixture_bytes
from host.stack_fixture import fixture_bytes as stack_fixture_bytes
from host.thread_fixture import fixture_bytes as thread_fixture_bytes
from tests.host_fixtures import ncch
from translator.upstream import generate_into
ROOT=Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc',default='gcc');parser.add_argument('--run-prefix',default='')
    parser.add_argument('--cc-prefix',default='',help='Argument-list launcher, e.g. bash PATH/dev-container.sh')
    parser.add_argument('--static',action='store_true');parser.add_argument('--sanitize',action='store_true')
    parser.add_argument('--service-api',action='store_true',help='Also execute malformed IPC/handle/resource tests with this compiler/target')
    parser.add_argument('--heap-api',action='store_true',help='Also execute allocation/protection/free/query/resource tests with this compiler/target')
    parser.add_argument('--kernel-api',action='store_true',help='Also execute shared-memory and synchronization API tests')
    parser.add_argument('--hid-api',action='store_true',help='Also execute HID wire-layout, input and lifetime API tests')
    parser.add_argument('--gsp-api',action='store_true',help='Also execute GSP display format/rights/bounds API tests')
    parser.add_argument('--relay-api',action='store_true',help='Also execute GSP relay/shared queue/timing/lifetime API tests')
    parser.add_argument('--fs-api',action='store_true',help='Also execute filesystem wire/lifetime/snapshot tests')
    parser.add_argument('--apt-api',action='store_true',help='Also execute standalone lifecycle/descriptor tests')
    parser.add_argument('--thread-api',action='store_true',help='Also execute thread ownership/context/rollback tests')
    parser.add_argument('--only',choices=('frame','abi','services','cp15','heap','kernel','hid','gsp','relay','stack','stack_reject','threads','thread_vfp','media'),help='Run one external integration probe')
    parser.add_argument('--multiple-api',action='store_true',help='Also execute stack/multiple-register transfer API tests')
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
    for name in ('frame','abi','services','cp15','heap','kernel','hid','gsp','relay','stack','stack_reject','threads','thread_vfp','media'):
        if args.only and args.only!=name:continue
        folder=out/name;folder.mkdir()
        code=(ROOT/'tests/recomp/abi.arm.bin').read_bytes() if name=='abi' else struct.pack('<6I',0x1e070f15,0xee070f15,0xf57ff05f,0xf57ff04f,0xf57ff06f,0xef000003) if name=='cp15' else None
        if name=='thread_vfp':code=(ROOT/'tests/recomp/thread-vfp.arm.bin').read_bytes()
        if name=='media':code=struct.pack('<10I',0xe6502f91,0xe6503ff1,0xe6804fb1,0xe1050281,0xe10602a1,0xe10702c1,0xe10802e1,0xee070fba,0xee070f9a,0xef000003)
        if name=='stack_reject':code=struct.pack('<4I8H',0xe59f0000,0xe12fff10,0x100011,0xe1a00000,0xb400,0xbc00,0xc000,0xc800,0xc103,0x47f8,0xdf03,0x46c0)
        rom=folder/'original.cxi'
        if code is not None:
            container=ncch([('.code',code.ljust(4096,b'\0'))])
            struct.pack_into('<I',container,0x218,len(code))
            container[0x160:0x180]=hashlib.sha256(container[0x200:0x600]).digest()
            rom.write_bytes(container)
        else: rom.write_bytes(service_fixture_bytes() if name=='services' else heap_fixture_bytes() if name=='heap' else kernel_fixture_bytes() if name=='kernel' else hid_fixture_bytes() if name=='hid' else gsp_fixture_bytes() if name=='gsp' else relay_fixture_bytes() if name=='relay' else stack_fixture_bytes() if name=='stack' else thread_fixture_bytes() if name=='threads' else fixture_bytes())
        generation=generate_into(rom,folder/'aot')
        sources=[core/n for n in ('ctr_cpu.c','ctr_memory.c','ctr_guest.c','ctr_ipc.c','ctr_heap.c','ctr_kernel.c','ctr_hid.c','ctr_gsp.c','ctr_fs.c','ctr_apt.c')]+[recomp/'ctr_recomp.c']
        flags=[]
        if name=='frame':
            flags=['-DCTR_EXTERNAL_AOT'];sources += [core/'ctr_guest_fixture.c',ROOT/'tests/fixtures/guest/image.generated.c']
        elif name=='services':
            sources += [core/'ctr_service_fixture.c',ROOT/'tests/fixtures/services/boot-image.generated.c',ROOT/'tests/fixtures/services/boot.generated.c']
        elif name=='heap':
            sources += [core/'ctr_heap_fixture.c',ROOT/'tests/fixtures/heap/heap-image.generated.c',ROOT/'tests/fixtures/heap/heap.generated.c']
        elif name=='kernel':
            sources += [core/'ctr_kernel_fixture.c',ROOT/'tests/fixtures/kernel/kernel-image.generated.c',ROOT/'tests/fixtures/kernel/kernel.generated.c']
        elif name=='hid':
            sources += [core/'ctr_hid_fixture.c',ROOT/'tests/fixtures/hid/hid-image.generated.c',ROOT/'tests/fixtures/hid/hid.generated.c']
        elif name=='gsp':
            sources += [core/'ctr_gsp_fixture.c',core/'ctr_hid_fixture.c',ROOT/'tests/fixtures/hid/hid-image.generated.c',ROOT/'tests/fixtures/hid/hid.generated.c',ROOT/'tests/fixtures/gsp/gsp-image.generated.c',ROOT/'tests/fixtures/gsp/gsp.generated.c']
        elif name=='relay':
            sources += [core/'ctr_relay_fixture.c',core/'ctr_hid_fixture.c',ROOT/'tests/fixtures/hid/hid-image.generated.c',ROOT/'tests/fixtures/hid/hid.generated.c',ROOT/'tests/fixtures/relay/relay-image.generated.c',ROOT/'tests/fixtures/relay/relay.generated.c']
        elif name=='stack':
            sources += [core/'ctr_stack_fixture.c',ROOT/'tests/fixtures/stack/stack-image.generated.c',ROOT/'tests/fixtures/stack/stack.generated.c']
        elif name=='threads':
            sources += [core/'ctr_thread_fixture.c',ROOT/'tests/fixtures/threads/thread-image.generated.c',ROOT/'tests/fixtures/threads/thread.generated.c']
        else:
            (folder/'abi_image.h').write_text('#define ABI_CODE_SIZE '+str(len(code))+'\nstatic const unsigned char abi_code[4096]={'+','.join(str(b) for b in code)+'};\n')
        binary=folder/(name+('.exe' if os.name=='nt' else ''))
        sources+=[ROOT/'tests/recomp'/('test_'+name+'.c')]+[folder/'aot'/n for n in generation['sources']]
        run(name+'-compile',common+flags+['-I',str(folder),'-I',str(folder/'aot'),*map(str,sources),'-lm','-o',str(binary)])
        run(name+'-execute',shlex.split(args.run_prefix)+[str(binary)]+([str(folder)] if name in ('gsp','relay') else []))
    if args.service_api:
        binary=out/('service-api'+('.exe' if os.name=='nt' else ''))
        sources=[core/n for n in ('ctr_cpu.c','ctr_memory.c','ctr_guest.c','ctr_ipc.c','ctr_heap.c','ctr_kernel.c','ctr_hid.c','ctr_gsp.c','ctr_fs.c','ctr_apt.c','ctr_service_fixture.c')]
        sources += [ROOT/'tests/services/test_services.c',ROOT/'tests/fixtures/services/boot-image.generated.c',ROOT/'tests/fixtures/services/boot.generated.c']
        run('service-api-compile',common+[*map(str,sources),'-o',str(binary)])
        run('service-api-execute',shlex.split(args.run_prefix)+[str(binary)])
    if args.heap_api:
        binary=out/('heap-api'+('.exe' if os.name=='nt' else ''))
        sources=[core/n for n in ('ctr_cpu.c','ctr_memory.c','ctr_guest.c','ctr_ipc.c','ctr_heap.c','ctr_kernel.c','ctr_hid.c','ctr_gsp.c','ctr_fs.c','ctr_apt.c','ctr_heap_fixture.c')]
        sources += [ROOT/'tests/heap/test_heap.c',ROOT/'tests/fixtures/heap/heap-image.generated.c',ROOT/'tests/fixtures/heap/heap.generated.c']
        run('heap-api-compile',common+[*map(str,sources),'-o',str(binary)])
        run('heap-api-execute',shlex.split(args.run_prefix)+[str(binary)])
    if args.kernel_api:
        binary=out/('kernel-api'+('.exe' if os.name=='nt' else ''))
        sources=[core/n for n in ('ctr_cpu.c','ctr_memory.c','ctr_guest.c','ctr_ipc.c','ctr_heap.c','ctr_kernel.c','ctr_hid.c','ctr_gsp.c','ctr_fs.c','ctr_apt.c','ctr_kernel_fixture.c')]
        sources += [ROOT/'tests/kernel/test_kernel.c',ROOT/'tests/fixtures/kernel/kernel-image.generated.c',ROOT/'tests/fixtures/kernel/kernel.generated.c']
        run('kernel-api-compile',common+[*map(str,sources),'-o',str(binary)])
        run('kernel-api-execute',shlex.split(args.run_prefix)+[str(binary)])
    if args.hid_api:
        binary=out/('hid-api'+('.exe' if os.name=='nt' else ''))
        sources=[core/n for n in ('ctr_cpu.c','ctr_memory.c','ctr_guest.c','ctr_ipc.c','ctr_heap.c','ctr_kernel.c','ctr_hid.c','ctr_gsp.c','ctr_fs.c','ctr_apt.c','ctr_hid_fixture.c')]
        sources += [ROOT/'tests/hid/test_hid.c',ROOT/'tests/fixtures/hid/hid-image.generated.c',ROOT/'tests/fixtures/hid/hid.generated.c']
        run('hid-api-compile',common+[*map(str,sources),'-o',str(binary)])
        run('hid-api-execute',shlex.split(args.run_prefix)+[str(binary)])
    if args.gsp_api:
        binary=out/('gsp-api'+('.exe' if os.name=='nt' else ''))
        sources=[core/n for n in ('ctr_cpu.c','ctr_memory.c','ctr_guest.c','ctr_ipc.c','ctr_heap.c','ctr_kernel.c','ctr_hid.c','ctr_gsp.c','ctr_fs.c','ctr_apt.c','ctr_hid_fixture.c','ctr_gsp_fixture.c')]
        sources += [ROOT/'tests/gsp/test_gsp.c',ROOT/'tests/fixtures/hid/hid-image.generated.c',ROOT/'tests/fixtures/hid/hid.generated.c',ROOT/'tests/fixtures/gsp/gsp-image.generated.c',ROOT/'tests/fixtures/gsp/gsp.generated.c']
        run('gsp-api-compile',common+[*map(str,sources),'-o',str(binary)])
        run('gsp-api-execute',shlex.split(args.run_prefix)+[str(binary)])
    if args.relay_api:
        binary=out/('relay-api'+('.exe' if os.name=='nt' else ''))
        sources=[core/n for n in ('ctr_cpu.c','ctr_memory.c','ctr_guest.c','ctr_ipc.c','ctr_heap.c','ctr_kernel.c','ctr_hid.c','ctr_gsp.c','ctr_fs.c','ctr_apt.c','ctr_hid_fixture.c','ctr_relay_fixture.c')]
        sources += [ROOT/'tests/relay/test_relay.c',ROOT/'tests/fixtures/hid/hid-image.generated.c',ROOT/'tests/fixtures/hid/hid.generated.c',ROOT/'tests/fixtures/relay/relay-image.generated.c',ROOT/'tests/fixtures/relay/relay.generated.c']
        run('relay-api-compile',common+[*map(str,sources),'-o',str(binary)])
        run('relay-api-execute',shlex.split(args.run_prefix)+[str(binary)])
    if args.multiple_api:
        binary=out/('multiple-api'+('.exe' if os.name=='nt' else ''))
        sources=[core/n for n in ('ctr_cpu.c','ctr_memory.c','ctr_guest.c','ctr_ipc.c','ctr_heap.c','ctr_kernel.c','ctr_hid.c','ctr_gsp.c','ctr_fs.c','ctr_apt.c','ctr_stack_fixture.c')]
        sources += [ROOT/'tests/multiple/test_multiple.c',ROOT/'tests/fixtures/stack/stack-image.generated.c',ROOT/'tests/fixtures/stack/stack.generated.c']
        run('multiple-api-compile',common+[*map(str,sources),'-o',str(binary)])
        run('multiple-api-execute',shlex.split(args.run_prefix)+[str(binary)])
    if args.thread_api:
        binary=out/('thread-api'+('.exe' if os.name=='nt' else ''))
        sources=[core/n for n in ('ctr_cpu.c','ctr_memory.c','ctr_guest.c','ctr_ipc.c','ctr_heap.c','ctr_kernel.c','ctr_hid.c','ctr_gsp.c','ctr_fs.c','ctr_apt.c','ctr_thread_fixture.c')]
        sources += [ROOT/'tests/threads/test_threads.c',ROOT/'tests/fixtures/threads/thread-image.generated.c',ROOT/'tests/fixtures/threads/thread.generated.c']
        run('thread-api-compile',common+[*map(str,sources),'-o',str(binary)])
        run('thread-api-execute',shlex.split(args.run_prefix)+[str(binary)])
    for enabled,name,source in ((args.fs_api,'filesystem','test_filesystem.c'),(args.apt_api,'apt','test_apt.c')):
        if not enabled:continue
        binary=out/(name+'-api')
        sources=[core/n for n in ('ctr_cpu.c','ctr_memory.c','ctr_guest.c','ctr_ipc.c','ctr_heap.c','ctr_kernel.c','ctr_hid.c','ctr_gsp.c','ctr_fs.c','ctr_fs_snapshot.c','ctr_apt.c','ctr_service_fixture.c')]
        sources += [ROOT/'tests'/name/source,ROOT/'tests/fixtures/services/boot-image.generated.c',ROOT/'tests/fixtures/services/boot.generated.c']
        run(name+'-api-compile',common+[*map(str,sources),'-o',str(binary)])
        run(name+'-api-execute',shlex.split(args.run_prefix)+[str(binary)])
    (out/'report.json').write_text(json.dumps({'ok':True,'checks':results,'console_verified':False,'powerpc_execution':'emulated Linux' if args.static else None},indent=2)+'\n')
    return 0
if __name__=='__main__':raise SystemExit(main())
