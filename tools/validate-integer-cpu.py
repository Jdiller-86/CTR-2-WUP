#!/usr/bin/env python3
"""Focused original ARM11 integer interpreter/AOT checks against Unicorn 2.1.4.

No production assembler/emulator dependency. Encodings are original test code.
"""
import argparse
import ctypes
from pathlib import Path
import random
import shlex
import struct
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from translator.aot import translate

CASES=[
    ('clz r0,r1',0xe16f0f11,False),
    ('mul r0,r1,r2',0xe0000291,False),
    ('mlas r0,r1,r2,r3',0xe0303291,False),
    ('umull r0,r1,r2,r3',0xe0810392,False),
    ('smulls r0,r1,r2,r3',0xe0d10392,False),
    ('umlal r0,r1,r2,r3',0xe0a10392,False),
    ('smlals r0,r1,r2,r3',0xe0f10392,False),
    ('umaal r0,r1,r2,r3',0xe0410392,False),
    ('mrs r0,cpsr',0xe10f0000,False),
    ('msr cpsr_f,r1',0xe128f001,False),
    ('msr cpsr_f,#0x80000000',0xe328f102,False),
    ('add r0,r1,r2,lsl r3',0xe0810312,False),
    ('movs r0,r1,ror r2',0xe1b00271,False),
    ('rev r0,r1',0xe6bf0f31,False),
    ('rev16 r0,r1',0xe6bf0fb1,False),
    ('revsh r0,r1',0xe6ff0fb1,False),
    ('qadd r0,r1,r2',0xe1020051,False),
    ('qsub r0,r1,r2',0xe1220051,False),
    ('qdadd r0,r1,r2',0xe1420051,False),
    ('qdsub r0,r1,r2',0xe1620051,False),
    ('ssat r0,#8,r1',0xe6a70011,False),
    ('usat r0,#16,r1,asr #8',0xe6f00451,False),
    ('ssat16 r0,#8,r1',0xe6a70f31,False),
    ('usat16 r0,#8,r1',0xe6e80f31,False),
    ('pkhbt r0,r1,r2,lsl #8',0xe6810412,False),
    ('pkhtb r0,r1,r2,asr #8',0xe6810452,False),
    ('sxth r0,r1',0xe6bf0071,False),
    ('sxtab r0,r1,r2,ror #8',0xe6a10472,False),
    ('sxtb16 r0,r1',0xe68f0071,False),
    ('uxtab16 r0,r1,r2',0xe6c10072,False),
    ('ldr r0,[r8,r2,lsl #2]',0xe7980102,False),
    ('ldr r0,[r8,#8]!',0xe5b80008,False),
    ('ldr r0,[r8],#8',0xe4980008,False),
    ('str r0,[r8],#8',0xe4880008,False),
    ('str pc,[r8]',0xe588f000,False),
    ('ldrsb r0,[r8,#1]',0xe1d800d1,False),
    ('ldrsh r0,[r8,#2]!',0xe1f800f2,False),
    ('ldrd r0,r1,[r8,#8]!',0xe1e800d8,False),
    ('strd r0,r1,[r8],#8',0xe0c800f8,False),
    ('blx #0x100040',0xfa00000e,False),
    ('sadd16 r0,r1,r2',0xe6110f12,False),
    ('sasx r0,r1,r2',0xe6110f32,False),
    ('ssax r0,r1,r2',0xe6110f52,False),
    ('ssub16 r0,r1,r2',0xe6110f72,False),
    ('sadd8 r0,r1,r2',0xe6110f92,False),
    ('ssub8 r0,r1,r2',0xe6110ff2,False),
    ('qadd16 r0,r1,r2',0xe6210f12,False),
    ('qasx r0,r1,r2',0xe6210f32,False),
    ('qsax r0,r1,r2',0xe6210f52,False),
    ('qsub16 r0,r1,r2',0xe6210f72,False),
    ('qadd8 r0,r1,r2',0xe6210f92,False),
    ('qsub8 r0,r1,r2',0xe6210ff2,False),
    ('shadd16 r0,r1,r2',0xe6310f12,False),
    ('shasx r0,r1,r2',0xe6310f32,False),
    ('shsax r0,r1,r2',0xe6310f52,False),
    ('shsub16 r0,r1,r2',0xe6310f72,False),
    ('shadd8 r0,r1,r2',0xe6310f92,False),
    ('shsub8 r0,r1,r2',0xe6310ff2,False),
    ('uadd16 r0,r1,r2',0xe6510f12,False),
    ('uasx r0,r1,r2',0xe6510f32,False),
    ('usax r0,r1,r2',0xe6510f52,False),
    ('usub16 r0,r1,r2',0xe6510f72,False),
    ('uadd8 r0,r1,r2',0xe6510f92,False),
    ('usub8 r0,r1,r2',0xe6510ff2,False),
    ('uqadd16 r0,r1,r2',0xe6610f12,False),
    ('uqasx r0,r1,r2',0xe6610f32,False),
    ('uqsax r0,r1,r2',0xe6610f52,False),
    ('uqsub16 r0,r1,r2',0xe6610f72,False),
    ('uqadd8 r0,r1,r2',0xe6610f92,False),
    ('uqsub8 r0,r1,r2',0xe6610ff2,False),
    ('uhadd16 r0,r1,r2',0xe6710f12,False),
    ('uhasx r0,r1,r2',0xe6710f32,False),
    ('uhsax r0,r1,r2',0xe6710f52,False),
    ('uhsub16 r0,r1,r2',0xe6710f72,False),
    ('uhadd8 r0,r1,r2',0xe6710f92,False),
    ('uhsub8 r0,r1,r2',0xe6710ff2,False),
    ('smulbb r0,r1,r2',0xe1600281,False),
    ('smultt r0,r1,r2',0xe16002e1,False),
    ('smulwb r0,r1,r2',0xe12002a1,False),
    ('smulwt r0,r1,r2',0xe12002e1,False),
    ('smlawb r0,r1,r2,r3',0xe1203281,False),
    ('smlalbb r0,r1,r2,r3',0xe1410382,False),
    ('smlaltt r0,r1,r2,r3',0xe14103e2,False),
    ('smlad r0,r1,r2,r3',0xe7003211,False),
    ('smladx r0,r1,r2,r3',0xe7003231,False),
    ('smlsd r0,r1,r2,r3',0xe7003251,False),
    ('smuad r0,r1,r2',0xe700f211,False),
    ('smusd r0,r1,r2',0xe700f251,False),
    ('smlald r0,r1,r2,r3',0xe7410312,False),
    ('smlsldx r0,r1,r2,r3',0xe7410372,False),
    ('smmla r0,r1,r2,r3',0xe7503211,False),
    ('smmlar r0,r1,r2,r3',0xe7503231,False),
    ('smmul r0,r1,r2',0xe750f211,False),
    ('smmls r0,r1,r2,r3',0xe75032d1,False),
    ('usad8 r0,r1,r2',0xe780f211,False),
    ('usada8 r0,r1,r2,r3',0xe7803211,False),
    ('swp r0,r1,[r8]',0xe1080091,False),
    ('swpb r0,r1,[r8]',0xe1480091,False),
    ('ldrexb r0,[r8]',0xe1d80f9f,False),
    ('ldrexh r0,[r8]',0xe1f80f9f,False),
    ('ldrexd r0,r1,[r8]',0xe1b80f9f,False),
    ('lsls r0,r1,#7',0x000001c8,True),
    ('lsrs r0,r1,#32',0x00000808,True),
    ('asrs r0,r1,#5',0x00001148,True),
    ('adds r0,r1,r2',0x00001888,True),
    ('subs r0,r1,#7',0x00001fc8,True),
    ('movs r0,#17',0x00002011,True),
    ('cmp r0,#3',0x00002803,True),
    ('adds r0,#7',0x00003007,True),
    ('subs r0,#7',0x00003807,True),
    ('ands r0,r1',0x00004008,True),
    ('eors r0,r1',0x00004048,True),
    ('lsls r0,r1',0x00004088,True),
    ('lsrs r0,r1',0x000040c8,True),
    ('asrs r0,r1',0x00004108,True),
    ('adcs r0,r1',0x00004148,True),
    ('sbcs r0,r1',0x00004188,True),
    ('rors r0,r1',0x000041c8,True),
    ('tst r0,r1',0x00004208,True),
    ('rsbs r0,r1,#0',0x00004248,True),
    ('cmp r0,r1',0x00004288,True),
    ('cmn r0,r1',0x000042c8,True),
    ('orrs r0,r1',0x00004308,True),
    ('muls r0,r1,r0',0x00004348,True),
    ('bics r0,r1',0x00004388,True),
    ('mvns r0,r1',0x000043c8,True),
    ('add r8,r1',0x00004488,True),
    ('mov r8,r1',0x00004688,True),
    ('cmp r8,r1',0x00004588,True),
    ('ldr r0,[r4,r2]',0x000058a0,True),
    ('str r0,[r4,r2]',0x000050a0,True),
    ('ldrsb r0,[r4,r2]',0x000056a0,True),
    ('ldrsh r0,[r4,r2]',0x00005ea0,True),
    ('ldrh r0,[r4,#2]',0x00008860,True),
    ('ldr r0,[sp,#4]',0x00009801,True),
    ('adr r0,#16',0x0000a004,True),
    ('add r0,sp,#16',0x0000a804,True),
    ('sub sp,#16',0x0000b084,True),
    ('sxth r0,r1',0x0000b208,True),
    ('sxtb r0,r1',0x0000b248,True),
    ('uxth r0,r1',0x0000b288,True),
    ('uxtb r0,r1',0x0000b2c8,True),
    ('rev r0,r1',0x0000ba08,True),
    ('rev16 r0,r1',0x0000ba48,True),
    ('revsh r0,r1',0x0000bac8,True),
    ('beq #0x100040',0x0000d01e,True),
    ('b #0x100040',0x0000e01e,True),
    ('bx r2',0x00004710,True),
    ('blx r2',0x00004790,True),
]

def validate(output,cc):
    import unicorn as u
    from unicorn import arm_const as ac
    if u.__version__!='2.1.4':raise RuntimeError('Require pinned Unicorn 2.1.4')
    output=output.resolve();output.mkdir(parents=True,exist_ok=False)
    sources=[];names=[]
    for index,(_,opcode,thumb) in enumerate(CASES):
        symbol=f'case_{index}'
        sources.append(translate(struct.pack('<H' if thumb else '<I',opcode),0x100000,symbol,thumb))
        names.append(symbol+'_at_00100000')
    generated=output/'functions.c'
    generated.write_text('\n'.join(sources)+'\nconst ctr_block_fn ctr_reference_functions[]={'+','.join(names)+'};\n')
    library=output/'reference.so'
    command=shlex.split(cc)+['-std=c11','-O1','-Wall','-Wextra','-Werror','-shared','-fPIC',
        '-I'+str(ROOT/'runtime/core'),str(ROOT/'runtime/core/ctr_cpu.c'),
        str(ROOT/'runtime/core/ctr_memory.c'),str(ROOT/'tests/multiple/oracle_bridge.c'),
        str(generated),'-o',str(library)]
    subprocess.run(command,check=True)
    step=ctypes.CDLL(str(library)).ctr_multiple_reference_step
    ptr=ctypes.POINTER(ctypes.c_uint32)
    step.argtypes=[ctypes.c_uint,ctypes.c_uint,ctypes.c_uint32,ptr,ptr,ctypes.POINTER(ctypes.c_uint8),ptr,ptr]
    step.restype=ctypes.c_int
    registers=[getattr(ac,f'UC_ARM_REG_R{i}') for i in range(13)]+[ac.UC_ARM_REG_SP,ac.UC_ARM_REG_LR,ac.UC_ARM_REG_PC]
    randoms=random.Random(32);count=0
    for index,(name,opcode,thumb) in enumerate(CASES):
        for variant in range(4):
            initial=[randoms.getrandbits(32) for _ in range(16)]
            if variant==1:initial[1]=initial[2]=0x80008000;initial[3]=0x7fffffff
            if '[' in name:initial[2]&=28
            initial[4]=initial[8]=initial[13]=0x08000040;initial[15]=0x100000
            if name in ('bx r2','blx r2'):initial[2]=0x100040
            flags=0x10|(0x20 if thumb else 0)|(0xf80f0000 if variant==0 else 0)
            reference=u.Uc(u.UC_ARCH_ARM,u.UC_MODE_THUMB if thumb else u.UC_MODE_ARM)
            reference.ctl_set_cpu_model(ac.UC_CPU_ARM_1176)
            reference.mem_map(0x100000,4096);reference.mem_map(0x08000000,4096)
            reference.mem_write(0x100000,struct.pack('<H' if thumb else '<I',opcode))
            memory=bytes(randoms.randrange(256) for _ in range(4096));reference.mem_write(0x08000000,memory)
            reference.reg_write(ac.UC_ARM_REG_CPSR,flags)
            for reg,value in zip(registers,initial):reference.reg_write(reg,value)
            reference.emu_start(0x100000|thumb,0x100100,count=1)
            expected=[reference.reg_read(reg) for reg in registers]
            expected_flags=reference.reg_read(ac.UC_ARM_REG_CPSR)
            expected_memory=bytes(reference.mem_read(0x08000000,4096))
            for translated in (0,1):
                actual=(ctypes.c_uint32*16)(*initial);status=ctypes.c_uint32(flags)
                data=(ctypes.c_uint8*4096).from_buffer_copy(memory);fpscr=ctypes.c_uint32();vfp=(ctypes.c_uint32*32)()
                result=step(translated,index,opcode,actual,ctypes.byref(status),data,ctypes.byref(fpscr),vfp)
                if result or list(actual)!=expected or status.value!=expected_flags or bytes(data)!=expected_memory:
                    raise RuntimeError(f'{name} opcode={opcode:08x} backend={"AOT" if translated else "interpreter"} variant={variant}: state mismatch')
                count+=1
    report={'ok':True,'reference':'Unicorn 2.1.4 ARM1176','instruction_forms':len(CASES),
        'cases':count,'interpreter_and_aot':True,'console_verified':False}
    import json
    (output/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    return report

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True);parser.add_argument('--cc',default='gcc')
    args=parser.parse_args()
    print(validate(args.output,args.cc))
