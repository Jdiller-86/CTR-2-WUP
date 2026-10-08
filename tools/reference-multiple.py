#!/usr/bin/env python3
"""Original ARMv6/Thumb transfer cases compared with Unicorn ARM1176 (2.1.4).

Optional developer check; the production runtime has no Unicorn dependency.
The compiler must produce a native shared library for ctypes.
"""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import random
import shlex
import struct
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from translator.aot import translate

ROOT=Path(__file__).resolve().parents[1]
TEXT,DATA=0x100000,0x08000000

def cases():
    rows=[]
    masks=(1,0x95,0x6eff,0x8001)
    for load in (False,True):
        for up in (False,True):
            for pre in (False,True):
                for wb in (False,True):
                    for mask in masks:
                        if not load and mask&0x8000:continue
                        for cond,flags in ((14,0xa80f0010),(0,0x68050010),(0,0x08050010)):
                            opcode=cond<<28|0x080c0000|up<<23|pre<<24|wb<<21|load<<20|mask
                            rows.append((opcode,False,flags))
    # ARM base in list without writeback, and interworking PC loads to ARM/Thumb.
    for load in (False,True):rows.append((0xe88c1003|load<<20,False,0x28050010))
    rows.extend(((0xe8bd8001,False,0x180a0010),(0xe8bd8001,False,0x180a0010)))
    for load in (False,True):
        for extra in (False,True):
            for mask in (0,1,0x55,0xff):
                if not mask and not extra:continue
                rows.append((0xb400|load<<11|extra<<8|mask,True,0xa80f0030))
        for rn in (0,3,7):
            for mask in (1,0x55,0xff,1<<rn):
                if not load and mask&(1<<rn) and mask&((1<<rn)-1):continue
                rows.append((0xc000|load<<11|rn<<8|mask,True,0x18050030))
    # Link targets include LR itself, to check that it is captured before linking.
    for thumb in (False,True):
        for rn in (0,8,14):
            for target_thumb in (False,True):
                rows.append(((0x4780|rn<<3) if thumb else (0xe12fff30|rn),thumb,0x98050010|(0x20 if thumb else 0)))
    return rows

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--cc',default='gcc');args=parser.parse_args()
    import unicorn as u
    from unicorn import arm_const as ac
    assert u.__version__=='2.1.4','Use the pinned reference version'
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=False);rows=cases();generated=[]
    for i,(opcode,thumb,_) in enumerate(rows):
        generated.append(translate(struct.pack('<H' if thumb else '<I',opcode),TEXT,f'ctr_case_{i}',thumb))
    generated.append('const ctr_block_fn ctr_reference_functions[]={'+','.join(f'ctr_case_{i}_at_{TEXT:08x}' for i in range(len(rows)))+'};\n')
    source=out/'oracle-aot.c';source.write_text('\n'.join(generated));library=out/'oracle.so'
    command=shlex.split(args.cc)+['-std=c11','-O2','-Wall','-Wextra','-Werror','-shared','-fPIC','-I',str(ROOT/'runtime/core'),
        str(ROOT/'runtime/core/ctr_cpu.c'),str(ROOT/'runtime/core/ctr_memory.c'),str(ROOT/'tests/multiple/oracle_bridge.c'),str(source),'-o',str(library)]
    p=subprocess.run(command,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(out/'compile.log').write_text(p.stdout)
    if p.returncode:raise RuntimeError('Reference bridge compilation failed')
    bridge=ctypes.CDLL(str(library)).ctr_multiple_reference_step
    words=ctypes.c_uint32*16;page=ctypes.c_uint8*4096
    bridge.argtypes=[ctypes.c_uint,ctypes.c_uint,ctypes.c_uint32,ctypes.POINTER(ctypes.c_uint32),ctypes.POINTER(ctypes.c_uint32),ctypes.POINTER(ctypes.c_uint8)]
    bridge.restype=ctypes.c_int
    registers=[getattr(ac,f'UC_ARM_REG_R{i}') for i in range(13)]+[ac.UC_ARM_REG_SP,ac.UC_ARM_REG_LR,ac.UC_ARM_REG_PC]
    seed=random.Random(0xc712);results=[]
    for index,(opcode,thumb,flags) in enumerate(rows):
        initial=[seed.getrandbits(32) for _ in range(16)];initial[12]=DATA+2048;initial[13]=DATA+2048;initial[15]=TEXT
        memory=struct.pack('<1024I',*[seed.getrandbits(32) for _ in range(1024)])
        pc_load=(not thumb and opcode&0x0e100000==0x08100000 and opcode&0x8000) or (thumb and opcode&0xff00==0xbd00)
        if pc_load:memory=struct.pack('<I',TEXT+0x100+(index&1))*1024
        if thumb and opcode&0xf000==0xc000:initial[(opcode>>8)&7]=DATA+2048
        link=(thumb and opcode&0xff07==0x4700) or (not thumb and opcode&0x0ffffff0==0x012fff30)
        if link:initial[((opcode>>3)&15) if thumb else (opcode&15)]=TEXT+0x100+(index&1)
        reference=u.Uc(u.UC_ARCH_ARM,u.UC_MODE_ARM);reference.ctl_set_cpu_model(ac.UC_CPU_ARM_1176)
        reference.mem_map(TEXT,4096);reference.mem_write(TEXT,struct.pack('<H' if thumb else '<I',opcode));reference.mem_map(DATA,4096);reference.mem_write(DATA,memory)
        reference.reg_write(ac.UC_ARM_REG_CPSR,flags)
        for reg,value in zip(registers,initial):reference.reg_write(reg,value)
        reference.emu_start(TEXT|int(thumb),TEXT+4096,count=1)
        expected=[reference.reg_read(reg) for reg in registers];expected_flags=reference.reg_read(ac.UC_ARM_REG_CPSR);expected_memory=bytes(reference.mem_read(DATA,4096))
        for translated in (False,True):
            actual=words(*initial);actual_flags=ctypes.c_uint32(flags);actual_memory=page.from_buffer_copy(memory)
            result=bridge(translated,index,opcode,actual,ctypes.byref(actual_flags),actual_memory)
            assert result==0,(index,hex(opcode),result)
            assert list(actual)==expected,(index,hex(opcode),translated,list(actual),expected)
            assert actual_flags.value==expected_flags,(index,hex(opcode),hex(actual_flags.value),hex(expected_flags))
            assert bytes(actual_memory)==expected_memory,(index,hex(opcode),translated,'memory')
        results.append({'opcode':hex(opcode),'thumb':thumb,'cpsr':hex(expected_flags),'registers':expected,'memory_sha256':hashlib.sha256(expected_memory).hexdigest()})
    from host.stack_fixture import image_code,EXPECTED
    reference=u.Uc(u.UC_ARCH_ARM,u.UC_MODE_ARM);reference.ctl_set_cpu_model(ac.UC_CPU_ARM_1176)
    reference.mem_map(TEXT,4096);reference.mem_write(TEXT,image_code()[:4096]);reference.mem_map(DATA,4096)
    reference.mem_map(0x0fffc000,16384);reference.reg_write(ac.UC_ARM_REG_CPSR,0x10)
    reference.reg_write(ac.UC_ARM_REG_SP,0x10000000);trace=[]
    reference.hook_add(u.UC_HOOK_CODE,lambda cpu,address,size,context:trace.append(address))
    reference.hook_add(u.UC_HOOK_INTR,lambda cpu,*args:cpu.emu_stop())
    reference.emu_start(TEXT,TEXT+4096,count=100)
    assert len(trace)==47 and reference.reg_read(ac.UC_ARM_REG_SP)==0x10000000
    assert struct.unpack('<8I',reference.mem_read(DATA,32))==EXPECTED
    assert not reference.reg_read(ac.UC_ARM_REG_CPSR)&0x20
    report={'ok':True,'reference':'Unicorn '+u.__version__,'cpu_model':'ARM1176','cases':len(rows),'interpreter_and_original_aot_match':True,'nested_arm_thumb_program_instructions':len(trace),'console_verified':False,'results':results}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='results'}));return 0
if __name__=='__main__':raise SystemExit(main())
