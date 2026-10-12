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
                        for cond,flags in ((14,0xa80f0010),(0,0x68050010),(0,0x08050010)):
                            opcode=cond<<28|0x080c0000|up<<23|pre<<24|wb<<21|load<<20|mask
                            rows.append((opcode,False,flags))
    # ARM base in list without writeback, and interworking PC loads to ARM/Thumb.
    for load in (False,True):rows.append((0xe88c1003|load<<20,False,0x28050010))
    # ARMv6 defined base-in-list stores: base is the lowest listed register.
    # Includes the compiler's STMDB SP!,{SP,LR} division-routine prologue.
    for base,mask in ((12,0x1000),(12,0x5000),(13,0x6000)):
        for up in (False,True):
            for pre in (False,True):
                for cond,flags in ((14,0xa80f0010),(0,0x68050010),(0,0x08050010)):
                    rows.append((cond<<28|0x08200000|base<<16|up<<23|pre<<24|mask,False,flags))
    for width in (2,4):
        for load in (False,True):
            for offset in (1,2,3):
                for cond,flags in ((14,0xa80f0010),(0,0x68050010),(0,0x08050010)):
                    op=(0x01cc40b0 if width==2 else 0x058c4000)|load<<20|offset
                    rows.append((cond<<28|op,False,flags))
    rows.extend(((0xe18230b7,False,0xa80f0010),(0xe19230b7,False,0xa80f0010)))
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
    # Compiled newlib uses unsigned byte arithmetic and GE-based selection.
    for op in (0xe6524f93,0xe6524ff3,0xe6824fb3):
        for flags in (0x98000010,0x080f0010,0x28050010,0x880a0010):
            for _ in range(16):rows.append((op,False,flags))
    for op in (0xeee10a10,0xeef10a10,0xeef1fa10):
        for _ in range(8):rows.append((op,False,0x98050010))
    for wide in (False,True):
        for load in (False,True):
            for up in (False,True):
                for reg in (0,7,15):
                    rows.append((0xed0c0a08|wide<<8|load<<20|up<<23|reg<<12,False,0xa8050010))
    rows.extend(((0xee070fba,False,0x98050010),(0xee070f9a,False,0x98050010)))
    for x in (0,1):
        for y in (0,1):
            for flags in (0x98050010,0x90050010,0x10050010):
                for _ in range(32):rows.append((0xe1042381|x<<5|y<<6,False,flags))
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
    fpwords=ctypes.c_uint32*32
    words=ctypes.c_uint32*16;page=ctypes.c_uint8*4096
    bridge.argtypes=[ctypes.c_uint,ctypes.c_uint,ctypes.c_uint32,ctypes.POINTER(ctypes.c_uint32),ctypes.POINTER(ctypes.c_uint32),ctypes.POINTER(ctypes.c_uint8),ctypes.POINTER(ctypes.c_uint32),ctypes.POINTER(ctypes.c_uint32)]
    bridge.restype=ctypes.c_int
    registers=[getattr(ac,f'UC_ARM_REG_R{i}') for i in range(13)]+[ac.UC_ARM_REG_SP,ac.UC_ARM_REG_LR,ac.UC_ARM_REG_PC]
    seed=random.Random(0xc712);results=[]
    for index,(opcode,thumb,flags) in enumerate(rows):
        initial=[seed.getrandbits(32) for _ in range(16)];initial[12]=DATA+2048;initial[13]=DATA+2048;initial[15]=TEXT
        if opcode in (0xe18230b7,0xe19230b7):initial[2]=DATA+2048;initial[7]=1
        initial_fp=0xa3000000 if opcode&0x0fff0fff in (0x0ee10a10,0x0ef10a10) or opcode&0x0f200f00 in (0x0d000a00,0x0d000b00) else 0
        initial_vfp=[seed.getrandbits(32) for _ in range(32)]
        if opcode&0x0fff0fff==0x0ee10a10:initial[(opcode>>12)&15]=seed.getrandbits(32)&0xf3000000
        memory=struct.pack('<1024I',*[seed.getrandbits(32) for _ in range(1024)])
        pc_load=(not thumb and opcode&0x0e100000==0x08100000 and opcode&0x8000) or (thumb and opcode&0xff00==0xbd00)
        if pc_load:memory=struct.pack('<I',TEXT+0x100+(index&1))*1024
        if thumb and opcode&0xf000==0xc000:initial[(opcode>>8)&7]=DATA+2048
        link=(thumb and opcode&0xff07==0x4700) or (not thumb and opcode&0x0ffffff0==0x012fff30)
        if link:initial[((opcode>>3)&15) if thumb else (opcode&15)]=TEXT+0x100+(index&1)
        reference=u.Uc(u.UC_ARCH_ARM,u.UC_MODE_ARM);reference.ctl_set_cpu_model(ac.UC_CPU_ARM_1176)
        control=(15,0,0,1,0,0,0)
        reference.reg_write(ac.UC_ARM_REG_CP_REG,(*control,(reference.reg_read(ac.UC_ARM_REG_CP_REG,control)|(1<<22))&~2))
        reference.reg_write(ac.UC_ARM_REG_FPEXC,0x40000000);reference.reg_write(ac.UC_ARM_REG_FPSCR,initial_fp)
        reference.mem_map(TEXT,4096);reference.mem_write(TEXT,struct.pack('<H' if thumb else '<I',opcode));reference.mem_map(DATA,4096);reference.mem_write(DATA,memory)
        for i,value in enumerate(initial_vfp):reference.reg_write(getattr(ac,f'UC_ARM_REG_S{i}'),value)
        reference.reg_write(ac.UC_ARM_REG_CPSR,flags)
        for reg,value in zip(registers,initial):reference.reg_write(reg,value)
        reference.emu_start(TEXT|int(thumb),TEXT+4096,count=1)
        expected=[reference.reg_read(reg) for reg in registers];expected_flags=reference.reg_read(ac.UC_ARM_REG_CPSR);expected_memory=bytes(reference.mem_read(DATA,4096));expected_fp=reference.reg_read(ac.UC_ARM_REG_FPSCR);expected_vfp=[reference.reg_read(getattr(ac,f'UC_ARM_REG_S{i}')) for i in range(32)]
        for translated in (False,True):
            actual=words(*initial);actual_flags=ctypes.c_uint32(flags);actual_memory=page.from_buffer_copy(memory);actual_fp=ctypes.c_uint32(initial_fp);actual_vfp=fpwords(*initial_vfp)
            result=bridge(translated,index,opcode,actual,ctypes.byref(actual_flags),actual_memory,ctypes.byref(actual_fp),actual_vfp)
            assert result==0,(index,hex(opcode),result)
            assert list(actual)==expected,(index,hex(opcode),translated,list(actual),expected)
            assert actual_flags.value==expected_flags,(index,hex(opcode),hex(actual_flags.value),hex(expected_flags))
            assert actual_fp.value==expected_fp,(index,hex(opcode),hex(actual_fp.value),hex(expected_fp))
            assert list(actual_vfp)==expected_vfp,(index,hex(opcode),translated,'vfp')
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
    report={'ok':True,'reference':'Unicorn '+u.__version__,'cpu_model':'ARM1176','data_alignment_policy':'SCTLR U=1 A=0','cases':len(rows),'interpreter_and_original_aot_match':True,'nested_arm_thumb_program_instructions':len(trace),'console_verified':False,'results':results}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='results'}));return 0
if __name__=='__main__':raise SystemExit(main())
