#include "ctr_stack_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned assertions;
#define CHECK(x) do { ++assertions;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static ctr_memory memory;static ctr_cpu cpu;static uint8_t code[4],data[64];
static void setup(uint32_t opcode,bool thumb) {
    memset(data,0,sizeof(data));for(unsigned i=0;i<4;++i)code[i]=(uint8_t)(opcode>>(i*8));
    ctr_memory_init(&memory);CHECK(ctr_memory_map(&memory,0x100000,4,5,code,4)==CTR_MEM_OK);
    CHECK(ctr_memory_map(&memory,0x08000000,64,3,data,64)==CTR_MEM_OK);ctr_cpu_init(&cpu,&memory);
    cpu.r[15]=0x100000;cpu.cpsr=0xa80f0010|(thumb?CTR_FLAG_T:0);
}
static uint32_t word(unsigned offset) {
    uint32_t value;CHECK(ctr_memory_read(&memory,0x08000000+offset,4,1,&value)==CTR_MEM_OK);return value;
}
static void modes(void) {
    const unsigned pre[]={0,1,0,1},up[]={1,1,0,0},first[]={16,20,8,4},final[]={28,28,4,4};
    for(unsigned mode=0;mode<4;++mode)for(unsigned load=0;load<2;++load) {
        setup(0xe82c008a|pre[mode]<<24|up[mode]<<23|load<<20,false);
        cpu.r[12]=0x08000010;cpu.r[1]=0x11223344;cpu.r[3]=0x55667788;cpu.r[7]=0xaabbccdd;
        if(load)for(unsigned i=0;i<3;++i)CHECK(ctr_memory_write(&memory,0x08000000+first[mode]+i*4,4,0x100+i)==CTR_MEM_OK);
        uint64_t generation=memory.generation;cpu.exclusive_valid=true;
        CHECK(ctr_cpu_step(&cpu)==CTR_CPU_OK && cpu.r[15]==0x100004 && cpu.r[12]==0x08000000+final[mode]);
        CHECK(cpu.cpsr==0xa80f0010 && cpu.instructions==1);
        if(load)CHECK(cpu.r[1]==0x100 && cpu.r[3]==0x101 && cpu.r[7]==0x102 && memory.generation==generation);
        else CHECK(word(first[mode])==0x11223344 && word(first[mode]+4)==0x55667788 && word(first[mode]+8)==0xaabbccdd &&
            memory.generation==generation+3 && !cpu.exclusive_valid);
    }
}
static void faults(void) {
    for(unsigned load=0;load<2;++load) {
        setup(0xe8ac0003|load<<20,false);cpu.r[12]=0x0800003c;cpu.r[0]=0x11223344;cpu.r[1]=0x55667788;
        CHECK(ctr_memory_write(&memory,0x0800003c,4,0xaabbccdd)==CTR_MEM_OK);
        CHECK(ctr_cpu_step(&cpu)==CTR_CPU_MEMORY_FAULT && cpu.memory_fault==CTR_MEM_UNMAPPED && cpu.r[15]==0x100000 && cpu.r[12]==0x0800003c);
        CHECK(cpu.r[1]==0x55667788 && (load?cpu.r[0]==0xaabbccdd:word(60)==0x11223344));
    }
    setup(0xbd01,true);cpu.r[13]=0x0800003c;CHECK(ctr_memory_write(&memory,0x0800003c,4,42)==CTR_MEM_OK);
    CHECK(ctr_cpu_step(&cpu)==CTR_CPU_MEMORY_FAULT && cpu.r[0]==42 && cpu.r[13]==0x0800003c && cpu.r[15]==0x100000 && (cpu.cpsr&CTR_FLAG_T));
    setup(0xe8ac0003,false);cpu.r[12]=0x08000000;cpu.r[0]=42;cpu.r[1]=99;
    CHECK(ctr_memory_change_range(&memory,0x08000004,4,1,false)==CTR_MEM_OK);
    CHECK(ctr_cpu_step(&cpu)==CTR_CPU_MEMORY_FAULT && cpu.memory_fault==CTR_MEM_PERMISSION && word(0)==42 && !word(4) && cpu.r[12]==0x08000000);
    setup(0xb401,true);cpu.r[13]=0x08000007;
    CHECK(ctr_cpu_step(&cpu)==CTR_CPU_MEMORY_FAULT && cpu.memory_fault==CTR_MEM_ALIGNMENT && cpu.r[13]==0x08000007 && cpu.r[15]==0x100000);
}
static void unsupported(void) {
    const uint32_t arm[]={0xe8cc0001,0xe8ac1001,0xe8bc1001,0xe89f0001,0xe88c8000,0xe8ac0000,0xe12fff3f};
    const uint32_t thumb[]={0xb400,0xbc00,0xc000,0xc800,0xc103,0x47f8};
    for(unsigned t=0;t<2;++t)for(unsigned i=0;i<(t?6u:7u);++i) {
        setup(t?thumb[i]:arm[i],t!=0);cpu.r[12]=cpu.r[13]=0x08000020;
        uint32_t before[16];memcpy(before,cpu.r,sizeof(before));uint64_t generation=memory.generation;
        CHECK(ctr_cpu_step(&cpu)==CTR_CPU_UNSUPPORTED && !memcmp(before,cpu.r,sizeof(before)) && generation==memory.generation);
    }
    setup(0x08bc0003,false);cpu.r[12]=0xdead0000; /* EQ false, valid form. */
    CHECK(ctr_cpu_step(&cpu)==CTR_CPU_OK && cpu.r[15]==0x100004 && cpu.r[12]==0xdead0000);
}
static void interworking_and_wrap(void) {
    for(unsigned thumb=0;thumb<2;++thumb)for(unsigned target_thumb=0;target_thumb<2;++target_thumb) {
        setup(thumb?0xbd01:0xe8bd8001,thumb!=0);cpu.r[13]=0x08000000;
        CHECK(ctr_memory_write(&memory,0x08000000,4,99)==CTR_MEM_OK);
        CHECK(ctr_memory_write(&memory,0x08000004,4,0x100020+target_thumb)==CTR_MEM_OK);
        CHECK(ctr_cpu_step(&cpu)==CTR_CPU_OK && cpu.r[0]==99 && cpu.r[13]==0x08000008 && cpu.r[15]==0x100020);
        CHECK(((cpu.cpsr&CTR_FLAG_T)!=0)==(target_thumb!=0));
    }
    setup(0xe82c0007,false);uint8_t wrapped[12]={0};
    CHECK(ctr_memory_map(&memory,0xfffffff8u,8,3,wrapped,8)==CTR_MEM_OK);
    CHECK(ctr_memory_map(&memory,0,4,3,wrapped+8,4)==CTR_MEM_OK);
    cpu.r[12]=0;cpu.r[0]=1;cpu.r[1]=2;cpu.r[2]=3;
    CHECK(ctr_cpu_step(&cpu)==CTR_CPU_OK && cpu.r[12]==0xfffffff4u && wrapped[0]==1 && wrapped[4]==2 && wrapped[8]==3);
}
int main(void) {
    modes();faults();unsupported();interworking_and_wrap();int result=ctr_stack_selftest();
    if(result)fprintf(stderr,"stack selftest=%d\n",result);
    CHECK(!result);
    printf("{\"ok\":true,\"assertions\":%u,\"original_arm_thumb_stack_calls\":true,\"console_verified\":false}\n",assertions);return 0;
}
