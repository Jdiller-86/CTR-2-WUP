#!/usr/bin/env python3
"""Independent Unicorn execution of the original ARM/Thumb/scalar VFP probe.
Install the optional development dependency unicorn==2.1.4 to run this check.
"""
import json
from pathlib import Path
import unicorn as u
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_C1_C0_2,UC_ARM_REG_FPEXC,UC_ARM_REG_R0,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_S0

ROOT=Path(__file__).resolve().parents[1]
cpu=u.Uc(u.UC_ARCH_ARM,u.UC_MODE_ARM)
cpu.mem_map(0x100000,4096);cpu.mem_write(0x100000,(ROOT/'tests/recomp/abi.arm.bin').read_bytes())
cpu.mem_map(0x0ff00000,0x100000);cpu.reg_write(UC_ARM_REG_SP,0x10000000)
cpu.reg_write(UC_ARM_REG_C1_C0_2,0x00f00000);cpu.reg_write(UC_ARM_REG_FPEXC,0x40000000)
trace=[]
cpu.hook_add(u.UC_HOOK_CODE,lambda cpu,address,size,context:trace.append(address))
cpu.hook_add(u.UC_HOOK_INTR,lambda cpu,*args:cpu.emu_stop())
cpu.emu_start(0x100000,0x101000,count=100)
actual=[cpu.reg_read(UC_ARM_REG_S0+i) for i in range(12)]
expected=[0x3fc00000,0x3f000000,0x40000000,0x3f800000,0x3f400000,0x40400000,0,0,0,0x40000000,0,0x40080000]
assert actual==expected,actual
assert [cpu.reg_read(r) for r in (UC_ARM_REG_R0,UC_ARM_REG_R2,UC_ARM_REG_R3)]==[1,0x40000000,17]
assert len(trace)==30,trace
print(json.dumps({'ok':True,'unicorn':u.__version__,'instructions':len(trace),'arm_thumb_scalar_vfp_match':True,'console_verified':False}))
