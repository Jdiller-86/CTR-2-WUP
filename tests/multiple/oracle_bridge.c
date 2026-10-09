/* Original ctypes bridge for independent single-instruction reference checks. */
#include "ctr_cpu.h"
#include <string.h>
extern const ctr_block_fn ctr_reference_functions[];
int ctr_multiple_reference_step(unsigned translated,unsigned index,uint32_t opcode,
    uint32_t *registers,uint32_t *cpsr,uint8_t *data,uint32_t *fpscr,uint32_t *vfp) {
    uint8_t code[4];for(unsigned i=0;i<4;++i)code[i]=(uint8_t)(opcode>>(i*8));
    ctr_memory memory;ctr_memory_init(&memory);
    if(ctr_memory_map(&memory,0x100000,4,5,code,sizeof(code))!=CTR_MEM_OK ||
        ctr_memory_map(&memory,0x08000000,4096,3,data,4096)!=CTR_MEM_OK)return -1;
    ctr_cpu cpu;ctr_cpu_init(&cpu,&memory);memcpy(cpu.r,registers,sizeof(cpu.r));cpu.cpsr=*cpsr;cpu.fpscr=*fpscr;memcpy(cpu.vfp,vfp,sizeof(cpu.vfp));
    ctr_cpu_result result=translated?ctr_reference_functions[index](&cpu):ctr_cpu_step(&cpu);
    memcpy(registers,cpu.r,sizeof(cpu.r));*cpsr=cpu.cpsr;*fpscr=cpu.fpscr;memcpy(vfp,cpu.vfp,sizeof(cpu.vfp));return result;
}
