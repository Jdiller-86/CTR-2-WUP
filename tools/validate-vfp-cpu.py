#!/usr/bin/env python3
"""Focused original scalar VFP interpreter/AOT checks against Unicorn 2.1.4."""
import argparse
import ctypes
import json
from pathlib import Path
import shlex
import struct
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from translator.aot import translate

CASES=[
    ('vadd.f32 s0,s2,s4',0xee310a02),
    ('vsub.f32 s0,s2,s4',0xee310a42),
    ('vmul.f32 s0,s2,s4',0xee210a02),
    ('vnmul.f32 s0,s2,s4',0xee210a42),
    ('vmla.f32 s0,s2,s4',0xee010a02),
    ('vmls.f32 s0,s2,s4',0xee010a42),
    ('vnmla.f32 s0,s2,s4',0xee110a42),
    ('vnmls.f32 s0,s2,s4',0xee110a02),
    ('vdiv.f32 s0,s2,s4',0xee810a02),
    ('vmov.f32 s0,s4',0xeeb00a42),
    ('vabs.f32 s0,s4',0xeeb00ac2),
    ('vneg.f32 s0,s4',0xeeb10a42),
    ('vsqrt.f32 s0,s4',0xeeb10ac2),
    ('vcmp.f32 s0,s4',0xeeb40a42),
    ('vcmp.f32 s0,#0',0xeeb50a40),
    ('vcmpe.f32 s0,s4',0xeeb40ac2),
    ('vcmpe.f32 s0,#0',0xeeb50ac0),
    ('vcvt.f32.s32 s0,s4',0xeeb80ac2),
    ('vcvt.s32.f32 s0,s4',0xeebd0ac2),
    ('vcvtr.s32.f32 s0,s4',0xeebd0a42),
    ('vcvt.f32.u32 s0,s4',0xeeb80a42),
    ('vcvt.u32.f32 s0,s4',0xeebc0ac2),
    ('vcvtr.u32.f32 s0,s4',0xeebc0a42),
    ('vadd.f64 d0,d2,d4',0xee320b04),
    ('vsub.f64 d0,d2,d4',0xee320b44),
    ('vmul.f64 d0,d2,d4',0xee220b04),
    ('vnmul.f64 d0,d2,d4',0xee220b44),
    ('vmla.f64 d0,d2,d4',0xee020b04),
    ('vmls.f64 d0,d2,d4',0xee020b44),
    ('vnmla.f64 d0,d2,d4',0xee120b44),
    ('vnmls.f64 d0,d2,d4',0xee120b04),
    ('vdiv.f64 d0,d2,d4',0xee820b04),
    ('vmov.f64 d0,d4',0xeeb00b44),
    ('vabs.f64 d0,d4',0xeeb00bc4),
    ('vneg.f64 d0,d4',0xeeb10b44),
    ('vsqrt.f64 d0,d4',0xeeb10bc4),
    ('vcmp.f64 d0,d4',0xeeb40b44),
    ('vcmp.f64 d0,#0',0xeeb50b40),
    ('vcmpe.f64 d0,d4',0xeeb40bc4),
    ('vcmpe.f64 d0,#0',0xeeb50bc0),
    ('vcvt.f64.s32 d0,s4',0xeeb80bc2),
    ('vcvt.s32.f64 s0,d4',0xeebd0bc4),
    ('vcvtr.s32.f64 s0,d4',0xeebd0b44),
    ('vcvt.f64.u32 d0,s4',0xeeb80b42),
    ('vcvt.u32.f64 s0,d4',0xeebc0bc4),
    ('vcvtr.u32.f64 s0,d4',0xeebc0b44),
    ('vcvt.f64.f32 d0,s4',0xeeb70ac2),
    ('vcvt.f32.f64 s0,d4',0xeeb70bc4),
    ('vadd.f32 s31,s29,s27',0xee7efaad),
    ('vadd.f64 d15,d14,d13',0xee3efb0d),
    ('vadd.f32 s4,s2,s4',0xee312a02),
    ('vmov s3,r2',0xee012a90),
    ('vmov r2,s3',0xee112a90),
    ('vmov d3,r2,r4',0xec442b13),
    ('vmov r2,r4,d3',0xec542b13),
    ('vmov s2,s3,r2,r4',0xec442a11),
    ('vmov r2,r4,s2,s3',0xec542a11),
    ('vldr d3,[r8,#8]',0xed983b02),
    ('vstr s3,[r8,#4]',0xedc81a01),
    ('vpush {d0-d3}',0xed2d0b08),
    ('vpop {s0-s7}',0xecbd0a08),
    ('vldmia r8!,{s2-s5}',0xecb81a04),
    ('vstmdb r8!,{d2-d4}',0xed282b06),
]

def validate(output,cc):
    import unicorn as u
    from unicorn import arm_const as ac
    if u.__version__!='2.1.4':raise RuntimeError('Require pinned Unicorn 2.1.4')
    output=output.resolve();output.mkdir(parents=True,exist_ok=False)
    sources=[];names=[]
    for index,(_,opcode) in enumerate(CASES):
        symbol=f'case_{index}'
        sources.append(translate(struct.pack('<I',opcode),0x100000,symbol,False))
        names.append(symbol+'_at_00100000')
    generated=output/'functions.c'
    generated.write_text('\n'.join(sources)+'\nconst ctr_block_fn ctr_reference_functions[]={'+','.join(names)+'};\n')
    library=output/'reference.so'
    command=shlex.split(cc)+['-std=c11','-O2','-Wall','-Wextra','-Werror','-shared','-fPIC',
        '-I'+str(ROOT/'runtime/core')]+[str(ROOT/'runtime/core'/n) for n in ('ctr_cpu.c','ctr_vfp.c','ctr_memory.c')]+[
        str(ROOT/'tests/multiple/oracle_bridge.c'),str(generated),'-lm','-o',str(library)]
    subprocess.run(command,check=True)
    step=ctypes.CDLL(str(library)).ctr_multiple_reference_step
    ptr=ctypes.POINTER(ctypes.c_uint32)
    step.argtypes=[ctypes.c_uint,ctypes.c_uint,ctypes.c_uint32,ptr,ptr,ctypes.POINTER(ctypes.c_uint8),ptr,ptr]
    step.restype=ctypes.c_int
    registers=[getattr(ac,f'UC_ARM_REG_R{i}') for i in range(13)]+[ac.UC_ARM_REG_SP,ac.UC_ARM_REG_LR,ac.UC_ARM_REG_PC]
    fp_registers=[getattr(ac,f'UC_ARM_REG_S{i}') for i in range(32)]
    samples=[(0x3fa00000,0x3ff4000000000000),(0xc0200000,0xc004000000000000),
        (0,0),(0x80000000,0x8000000000000000),(0x7f800000,0x7ff0000000000000),
        (0x7fc12345,0x7ff8123456789abc),(0x7f812345,0x7ff0123456789abc),
        (1,1),(0x7f7fffff,0x7fefffffffffffff)]
    count=0
    for index,(name,opcode) in enumerate(CASES):
        for variant in range(len(samples)+5):
            single,double=samples[variant%len(samples)]
            words=[single]*32
            if '.f64' in name or ' d' in name:
                words=[(double>>(32*(i%2)))&0xffffffff for i in range(32)]
            if variant==0 or variant>=9:
                if opcode&0x100:words[8:10]=[0,0x40080000]
                else:words[4]=0x40400000
            control=((variant-9)<<22) if variant>=9 else (0x03000000 if variant==7 else 0)
            if variant==13:
                control=0x01000000
                words=[0x00800000]*32 if not opcode&0x100 else [0,0x00100000]*16
            initial=list(range(16));initial[8]=initial[13]=0x08000040;initial[15]=0x100000
            flags=0xa0000010;memory=bytes(i&255 for i in range(4096))
            ref=u.Uc(u.UC_ARCH_ARM,u.UC_MODE_ARM);ref.ctl_set_cpu_model(ac.UC_CPU_ARM_1176)
            ref.mem_map(0x100000,4096);ref.mem_map(0x08000000,4096)
            ref.mem_write(0x100000,struct.pack('<I',opcode));ref.mem_write(0x08000000,memory)
            ref.reg_write(ac.UC_ARM_REG_CPSR,flags);ref.reg_write(ac.UC_ARM_REG_FPEXC,0x40000000)
            ref.reg_write(ac.UC_ARM_REG_FPSCR,control)
            for reg,value in zip(registers,initial):ref.reg_write(reg,value)
            for reg,value in zip(fp_registers,words):ref.reg_write(reg,value)
            ref.emu_start(0x100000,0x100100,count=1)
            expected=[ref.reg_read(reg) for reg in registers]
            expected_words=[ref.reg_read(reg) for reg in fp_registers]
            expected_status=ref.reg_read(ac.UC_ARM_REG_FPSCR)
            expected_memory=bytes(ref.mem_read(0x08000000,4096))
            for translated in (0,1):
                actual=(ctypes.c_uint32*16)(*initial);cpsr=ctypes.c_uint32(flags)
                data=(ctypes.c_uint8*4096).from_buffer_copy(memory)
                fpscr=ctypes.c_uint32(control);vfp=(ctypes.c_uint32*32)(*words)
                result=step(translated,index,opcode,actual,ctypes.byref(cpsr),data,ctypes.byref(fpscr),vfp)
                if result or list(actual)!=expected or list(vfp)!=expected_words or fpscr.value!=expected_status or cpsr.value!=ref.reg_read(ac.UC_ARM_REG_CPSR) or bytes(data)!=expected_memory:
                    differences=[(i,hex(a),hex(b)) for i,(a,b) in enumerate(zip(vfp,expected_words)) if a!=b]
                    raise RuntimeError(f'{name} backend={translated} variant={variant} result={result} FPSCR={fpscr.value:08x}/{expected_status:08x} FP={differences}')
                count+=1
    report={'ok':True,'reference':'Unicorn 2.1.4 ARM1176','instruction_forms':len(CASES),'cases':count,'interpreter_and_aot':True,'console_verified':False}
    (output/'report.json').write_text(json.dumps(report,indent=2)+'\n');return report

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--cc',default='gcc');args=parser.parse_args()
    print(validate(args.output,args.cc))
