#!/usr/bin/env python3
"""Compare original ARMv6 extension cases with pinned Unicorn ARM1176.

Developer-only interpreter check; production has no Unicorn dependency.
"""
import argparse
import ctypes
import json
from pathlib import Path
import random
import shlex
import struct
import subprocess

ROOT=Path(__file__).resolve().parents[1]
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--cc',default='gcc')
    a=parser.parse_args()
    import unicorn as u
    from unicorn import arm_const as ac
    if u.__version__!='2.1.4':raise RuntimeError('Require pinned Unicorn 2.1.4')
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    source=out/'functions.c'
    source.write_text('#include "ctr_cpu.h"\nconst ctr_block_fn ctr_reference_functions[]={0};\n')
    library=out/'oracle.so'
    command=shlex.split(a.cc)+['-std=c11','-O2','-Wall','-Wextra','-Werror','-shared','-fPIC',
        '-I'+str(ROOT/'runtime/core'),str(ROOT/'runtime/core/ctr_cpu.c'),
        str(ROOT/'runtime/core/ctr_memory.c'),str(ROOT/'tests/multiple/oracle_bridge.c'),str(source),'-o',str(library)]
    p=subprocess.run(command,capture_output=True,text=True)
    (out/'compile.log').write_text(p.stdout+p.stderr)
    if p.returncode:raise RuntimeError('Reference bridge compile failed')
    bridge=ctypes.CDLL(str(library)).ctr_multiple_reference_step
    ptr=ctypes.POINTER(ctypes.c_uint32)
    bridge.argtypes=[ctypes.c_uint,ctypes.c_uint,ctypes.c_uint32,ptr,ptr,
                     ctypes.POINTER(ctypes.c_uint8),ptr,ptr];bridge.restype=ctypes.c_int
    regs=[getattr(ac,f'UC_ARM_REG_R{i}') for i in range(13)]+[ac.UC_ARM_REG_SP,ac.UC_ARM_REG_LR,ac.UC_ARM_REG_PC]
    randoms=random.Random(0xc743);count=0
    values=[0,0x7f,0x80,0xff,0x7fff,0x8000,0xffff,0x800000ff,0x123480ab,0xffffffff]
    values+=[randoms.getrandbits(32) for _ in range(6)]
    for form in (0x06af4072,0x06bf4072,0x06ef4072,0x06ff4072):
        for rotation in range(4):
            for value in values:
                for cond,flags in ((14,0x98050010),(0,0xd8050010),(0,0x98050010),(1,0x98050010)):
                    for alias in (False,True):
                        opcode=cond<<28|form|rotation<<10
                        if alias:opcode=(opcode&~0xf000)|0x2000
                        initial=[randoms.getrandbits(32) for _ in range(16)]
                        initial[2]=value;initial[15]=0x100000
                        reference=u.Uc(u.UC_ARCH_ARM,u.UC_MODE_ARM)
                        reference.ctl_set_cpu_model(ac.UC_CPU_ARM_1176)
                        reference.mem_map(0x100000,4096);reference.mem_write(0x100000,struct.pack('<I',opcode))
                        reference.reg_write(ac.UC_ARM_REG_CPSR,flags)
                        for reg,n in zip(regs,initial):reference.reg_write(reg,n)
                        reference.emu_start(0x100000,0x101000,count=1)
                        actual=(ctypes.c_uint32*16)(*initial);status=ctypes.c_uint32(flags)
                        data=(ctypes.c_uint8*4096)();fpscr=ctypes.c_uint32();vfp=(ctypes.c_uint32*32)()
                        result=bridge(0,0,opcode,actual,ctypes.byref(status),data,ctypes.byref(fpscr),vfp)
                        expected=[reference.reg_read(reg) for reg in regs]
                        if result or list(actual)!=expected or status.value!=reference.reg_read(ac.UC_ARM_REG_CPSR):
                            raise RuntimeError(f'ARM reference mismatch opcode={opcode:08x} value={value:08x}')
                        count+=1
    report={'ok':True,'reference':'Unicorn '+u.__version__,'cpu_model':'ARM1176','cases':count,
            'interpreter_matches':True,'forms':['SXTB','SXTH','UXTB','UXTH'],
            'conditions_rotations_aliases_flags_checked':True,'console_verified':False}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
