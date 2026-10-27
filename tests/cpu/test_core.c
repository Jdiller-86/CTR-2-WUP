#include "ctr_cpu.h"
#include "ctr_fixture.h"
#include "viewport.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static unsigned checks;
#define CHECK(expr) do { ++checks; if (!(expr)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#expr); exit(1); } } while (0)
static uint8_t code[64], data[64];
static ctr_memory memory;
static ctr_cpu cpu;
static uint32_t logged_pc, logged_detail;
static unsigned logs;
static bool logged_thumb;
static const char *logged_hash;
static void record(void *context, const char *event, const char *hash, uint32_t pc, bool thumb, uint32_t detail) {
    (void)context; (void)event; ++logs; logged_pc=pc; logged_thumb=thumb; logged_detail=detail; logged_hash=hash;
}
static void setup(uint32_t instruction) {
    memset(code,0,sizeof(code)); memset(data,0,sizeof(data));
    for (unsigned i=0;i<4;++i) code[i]=(uint8_t)(instruction>>(i*8));
    ctr_memory_init(&memory);
    CHECK(ctr_memory_map(&memory,0x1000,sizeof(code),CTR_MEM_READ|CTR_MEM_EXEC,code,sizeof(code))==CTR_MEM_OK);
    CHECK(ctr_memory_map(&memory,0x2000,sizeof(data),CTR_MEM_READ|CTR_MEM_WRITE,data,sizeof(data))==CTR_MEM_OK);
    ctr_cpu_init(&cpu,&memory); cpu.r[15]=0x1000; cpu.log=record; logs=0;
}
static void memory_tests(void) {
    uint32_t value=0;
    setup(0);
    CHECK(ctr_memory_write(&memory,0x2000,4,0x78563412)==CTR_MEM_OK);
    CHECK(data[0]==0x12 && data[1]==0x34 && data[2]==0x56 && data[3]==0x78);
    CHECK(ctr_memory_read(&memory,0x2000,4,CTR_MEM_READ,&value)==CTR_MEM_OK && value==0x78563412);
    CHECK(ctr_memory_read(&memory,0x2002,2,CTR_MEM_READ,&value)==CTR_MEM_OK && value==0x7856);
    CHECK(ctr_memory_read(&memory,0x2001,4,CTR_MEM_READ,&value)==CTR_MEM_ALIGNMENT);
    CHECK(ctr_memory_read(&memory,0x3000,4,CTR_MEM_READ,&value)==CTR_MEM_UNMAPPED);
    CHECK(ctr_memory_write(&memory,0x1000,4,1)==CTR_MEM_PERMISSION);
    CHECK(ctr_memory_read(&memory,0x2000,4,CTR_MEM_EXEC,&value)==CTR_MEM_PERMISSION);
    CHECK(ctr_memory_read(&memory,UINT32_MAX,4,CTR_MEM_READ,&value)==CTR_MEM_OVERFLOW);
    CHECK(ctr_memory_check_range(&memory,0x2001,63,CTR_MEM_READ|CTR_MEM_WRITE)==CTR_MEM_OK);
    CHECK(ctr_memory_check_range(&memory,0x203f,2,CTR_MEM_READ)==CTR_MEM_UNMAPPED);
    CHECK(ctr_memory_check_range(&memory,0x1000,64,CTR_MEM_WRITE)==CTR_MEM_PERMISSION);
    CHECK(ctr_memory_check_range(&memory,UINT32_MAX-3,8,CTR_MEM_READ)==CTR_MEM_OVERFLOW);
    CHECK(ctr_memory_check_range(&memory,0x2000,4,0)==CTR_MEM_PERMISSION);
    CHECK(ctr_memory_map(&memory,0x2040,4,CTR_MEM_READ,data,4)==CTR_MEM_OK);
    CHECK(ctr_memory_check_range(&memory,0x203e,6,CTR_MEM_READ)==CTR_MEM_OK);
    CHECK(ctr_memory_check_range(&memory,0x203e,6,CTR_MEM_WRITE)==CTR_MEM_PERMISSION);
    CHECK(ctr_memory_map(&memory,0x2048,4,CTR_MEM_READ,data,4)==CTR_MEM_OK);
    CHECK(ctr_memory_check_range(&memory,0x2040,12,CTR_MEM_READ)==CTR_MEM_UNMAPPED);
    CHECK(ctr_memory_map(&memory,0x2008,8,CTR_MEM_READ,data,8)==CTR_MEM_OVERLAP);
    CHECK(ctr_memory_map(&memory,UINT32_MAX-3,8,CTR_MEM_READ,data,8)==CTR_MEM_OVERFLOW);
    CHECK(ctr_memory_map(&memory,0x4000,8,0,data,8)==CTR_MEM_PERMISSION);
    CHECK(ctr_memory_map(&memory,0x4000,8,CTR_MEM_READ,data,7)==CTR_MEM_OVERFLOW);
    CHECK(ctr_memory_map(&memory,UINT32_MAX-3,4,CTR_MEM_READ,data,4)==CTR_MEM_OK);
    CHECK(ctr_memory_read(&memory,UINT32_MAX-3,4,CTR_MEM_READ,&value)==CTR_MEM_OK && value==0x78563412);
    uint64_t generation=memory.code_generation;
    CHECK(ctr_memory_map(&memory,0x4000,sizeof(code),CTR_MEM_READ|CTR_MEM_WRITE,code,sizeof(code))==CTR_MEM_OK);
    generation=memory.code_generation;
    CHECK(ctr_memory_write(&memory,0x4000,4,0x11223344)==CTR_MEM_OK && memory.code_generation==generation+1);
    CHECK(ctr_memory_read(&memory,0x1000,4,CTR_MEM_EXEC,&value)==CTR_MEM_OK && value==0x11223344);
    generation=memory.code_generation;
    CHECK(ctr_memory_write(&memory,0x2000,1,9)==CTR_MEM_OK && memory.code_generation==generation);
    ctr_memory_init(&memory);
    for (unsigned i=0;i<CTR_MEMORY_MAX_MAPS;++i)
        CHECK(ctr_memory_map(&memory,i*16,4,CTR_MEM_READ,data,4)==CTR_MEM_OK);
    CHECK(ctr_memory_map(&memory,0x4000,4,CTR_MEM_READ,data,4)==CTR_MEM_CAPACITY);
}
static ctr_memory_resource_key resource_key(uint32_t address,uint32_t size,unsigned permissions) {
    ctr_memory_resource_key key;
    CHECK(ctr_memory_resource_key_capture(&memory,address,size,permissions,&key));return key;
}
static void resource_version_tests(void) {
    const uint32_t base=0x100000u,page=4096u,bytes=(CTR_MEMORY_RESOURCE_SLOTS*2u+1u)*4096u;
    uint8_t *allocation=malloc((size_t)bytes+8191u);CHECK(allocation);
    /* An even first host page makes its immediate neighbor a distinct slot. */
    uint8_t *backing=(uint8_t *)(((uintptr_t)allocation+8191u)&~(uintptr_t)8191u);
    memset(backing,0,bytes);ctr_memory_init(&memory);
    CHECK(ctr_memory_map(&memory,base,bytes,CTR_MEM_READ|CTR_MEM_WRITE,backing,bytes)==CTR_MEM_OK);
    CHECK(ctr_memory_map(&memory,0x2000000u,page*2u,CTR_MEM_READ|CTR_MEM_WRITE,backing+16u,page*2u)==CTR_MEM_OK);
    CHECK(ctr_memory_map(&memory,0x3000000u,page,CTR_MEM_READ|CTR_MEM_EXEC,backing,page)==CTR_MEM_OK);
    ctr_memory_resource_key first=resource_key(base,64u,CTR_MEM_READ),
        second=resource_key(base+page,64u,CTR_MEM_READ),same=resource_key(base,64u,CTR_MEM_READ);
    CHECK(first.owner==&memory && first.address==base && first.size==64u &&
          first.mapping_version==same.mapping_version && first.write_version==same.write_version);
    CHECK(ctr_memory_resource_key_current(&memory,&first));
    CHECK(ctr_memory_write(&memory,base+page,1u,7u)==CTR_MEM_OK);
    CHECK(ctr_memory_resource_key_current(&memory,&first) && !ctr_memory_resource_key_current(&memory,&second));
    uint64_t code_version=memory.code_generation;
    CHECK(ctr_memory_write(&memory,0x2000000u,4u,0x12345678u)==CTR_MEM_OK);
    CHECK(!ctr_memory_resource_key_current(&memory,&first) && memory.code_generation==code_version+1u);
    first=resource_key(base,64u,CTR_MEM_READ);
    CHECK(ctr_memory_write(&memory,0x3000000u,1u,9u)==CTR_MEM_PERMISSION);
    CHECK(ctr_memory_resource_key_current(&memory,&first));

    /* A host publication invalidates all guest views and executable aliases. */
    uint64_t generation=memory.generation;code_version=memory.code_generation;
    backing[20u]=19u;
    CHECK(ctr_memory_resource_changed(&memory,backing+20u,1u)==CTR_MEM_OK);
    CHECK(!ctr_memory_resource_key_current(&memory,&first) && memory.generation==generation &&
          memory.code_generation==code_version+1u);
    first=resource_key(base,64u,CTR_MEM_READ);code_version=memory.code_generation;
    backing[21u]=23u;
    CHECK(ctr_memory_backing_changed(&memory,backing+21u,1u)==CTR_MEM_OK);
    CHECK(!ctr_memory_resource_key_current(&memory,&first) && memory.generation==generation+1u &&
          memory.code_generation==code_version+1u);
    ctr_memory before=memory;
    CHECK(ctr_memory_resource_changed(&memory,NULL,1u)==CTR_MEM_OVERFLOW);
    CHECK(ctr_memory_backing_changed(&memory,(const void *)(UINTPTR_MAX-1u),4u)==CTR_MEM_OVERFLOW);
    CHECK(ctr_memory_resource_changed(&memory,NULL,0u)==CTR_MEM_OK);
    CHECK(ctr_memory_backing_changed(&memory,NULL,0u)==CTR_MEM_OK);
    CHECK(!memcmp(&before,&memory,sizeof(memory)));

    /* Unaligned stores and owned bulk output cover both backing pages. */
    first=resource_key(base+page-2u,8u,CTR_MEM_READ);generation=memory.generation;
    CHECK(ctr_memory_write_data(&memory,base+page-1u,4u,0x11223344u)==CTR_MEM_OK);
    CHECK(!ctr_memory_resource_key_current(&memory,&first) && memory.generation==generation+4u);
    uint8_t output[8]={1u,2u,3u,4u,5u,6u,7u,8u};
    first=resource_key(base+page-2u,8u,CTR_MEM_READ);generation=memory.generation;
    CHECK(ctr_memory_write_bytes(&memory,base+page-2u,output,sizeof(output))==CTR_MEM_OK);
    CHECK(!ctr_memory_resource_key_current(&memory,&first) && memory.generation==generation+1u);
    CHECK(!memcmp(backing+page-2u,output,sizeof(output)));

    /* Batch preflight must preserve every version and byte on late failure. */
    ctr_memory_write_range ranges[]={{base,output,4u},{0xf0000000u,output+4u,4u}};
    first=resource_key(base,64u,CTR_MEM_READ);before=memory;
    uint8_t saved[64];memcpy(saved,backing,sizeof(saved));
    CHECK(ctr_memory_write_ranges(&memory,ranges,2u)==CTR_MEM_UNMAPPED);
    CHECK(!memcmp(&before,&memory,sizeof(memory)) && !memcmp(saved,backing,sizeof(saved)) &&
          ctr_memory_resource_key_current(&memory,&first));
    ranges[1].address=base+page;generation=memory.generation;uint64_t clock=memory.resource_clock;
    second=resource_key(base+page,64u,CTR_MEM_READ);
    CHECK(ctr_memory_write_ranges(&memory,ranges,2u)==CTR_MEM_OK);
    CHECK(memory.generation==generation+1u && memory.resource_clock==clock+1u &&
          !ctr_memory_resource_key_current(&memory,&first) && !ctr_memory_resource_key_current(&memory,&second));
    ranges[0].address=base+16u;ranges[1].address=0x2000000u;
    first=resource_key(base,64u,CTR_MEM_READ);before=memory;
    CHECK(ctr_memory_write_ranges(&memory,ranges,2u)==CTR_MEM_OVERLAP);
    CHECK(!memcmp(&before,&memory,sizeof(memory)) && ctr_memory_resource_key_current(&memory,&first));

    /* Mapping epochs reject permission changes, splits, free/remap and rebind. */
    first=resource_key(base,64u,CTR_MEM_READ);before=memory;
    CHECK(ctr_memory_change_range(&memory,base,bytes,CTR_MEM_READ|CTR_MEM_WRITE,false)==CTR_MEM_OK);
    CHECK(!memcmp(&before,&memory,sizeof(memory)) && ctr_memory_resource_key_current(&memory,&first));
    CHECK(ctr_memory_change_range(&memory,base+page,page,CTR_MEM_READ,false)==CTR_MEM_OK);
    CHECK(!ctr_memory_resource_key_current(&memory,&first));
    first=resource_key(base+page-2u,8u,CTR_MEM_READ);
    CHECK(!ctr_memory_resource_key_capture(&memory,base+page,4u,CTR_MEM_WRITE,&same) && !same.owner);
    CHECK(ctr_memory_change_range(&memory,base+page,page,CTR_MEM_READ|CTR_MEM_WRITE,false)==CTR_MEM_OK);
    CHECK(!ctr_memory_resource_key_current(&memory,&first));
    first=resource_key(base+page-2u,8u,CTR_MEM_READ);
    CHECK(ctr_memory_change_range(&memory,base+page,page,0u,true)==CTR_MEM_OK);
    CHECK(!ctr_memory_resource_key_current(&memory,&first));
    CHECK(ctr_memory_map(&memory,base+page,page,CTR_MEM_READ|CTR_MEM_WRITE,backing+3u*page,page)==CTR_MEM_OK);
    CHECK(!ctr_memory_resource_key_current(&memory,&first));
    first=resource_key(base,64u,CTR_MEM_READ);before=memory;
    CHECK(ctr_memory_change_range(&memory,0xf0000000u,page,0u,true)==CTR_MEM_UNMAPPED);
    CHECK(ctr_memory_map(&memory,base,page,CTR_MEM_READ,backing,page)==CTR_MEM_OVERLAP);
    CHECK(!memcmp(&before,&memory,sizeof(memory)) && ctr_memory_resource_key_current(&memory,&first));

    /* A collision is a conservative miss with unchanged target bytes. */
    bool collided=false;
    for(uint32_t offset=4u*page;offset<bytes && !collided;offset+=page) {
        first=resource_key(base,64u,CTR_MEM_READ);memcpy(saved,backing,sizeof(saved));
        CHECK(ctr_memory_write(&memory,base+offset,1u,41u)==CTR_MEM_OK);
        collided=!ctr_memory_resource_key_current(&memory,&first);
        CHECK(!memcmp(saved,backing,sizeof(saved)));
    }
    CHECK(collided);
    first=resource_key(base,64u,CTR_MEM_READ);
    CHECK(ctr_memory_resource_changed(&memory,backing,(size_t)CTR_MEMORY_RESOURCE_SLOTS*page)==CTR_MEM_OK);
    CHECK(!ctr_memory_resource_key_current(&memory,&first));

    CHECK(!ctr_memory_resource_key_capture(&memory,base,0u,CTR_MEM_READ,&same) && !same.owner);
    CHECK(!ctr_memory_resource_key_capture(&memory,UINT32_MAX,2u,CTR_MEM_READ,&same) && !same.owner);
    CHECK(!ctr_memory_resource_key_capture(&memory,base,4u,0u,&same) && !same.owner);
    CHECK(!ctr_memory_resource_key_current(NULL,&first) && !ctr_memory_resource_key_current(&memory,NULL));
    before=memory;
    CHECK(!ctr_memory_resource_key_capture(&memory,base,4u,CTR_MEM_READ,(ctr_memory_resource_key *)&memory));
    CHECK(!memcmp(&before,&memory,sizeof(memory)));
    memcpy(saved,backing,sizeof(saved));
    CHECK(!ctr_memory_resource_key_capture(&memory,base,4u,CTR_MEM_READ,(ctr_memory_resource_key *)backing));
    CHECK(!memcmp(saved,backing,sizeof(saved)));
    uint8_t unaligned[sizeof(ctr_memory_resource_key)+_Alignof(ctr_memory_resource_key)];
    uint8_t unaligned_before[sizeof(unaligned)];memset(unaligned,0xa5,sizeof(unaligned));
    memcpy(unaligned_before,unaligned,sizeof(unaligned));
    uintptr_t aligned=((uintptr_t)unaligned+_Alignof(ctr_memory_resource_key)-1u)&
        ~(uintptr_t)(_Alignof(ctr_memory_resource_key)-1u);
    ctr_memory_resource_key *misaligned=(ctr_memory_resource_key *)(aligned+1u);
    CHECK(!ctr_memory_resource_key_capture(&memory,base,4u,CTR_MEM_READ,misaligned));
    CHECK(!ctr_memory_resource_key_current(&memory,misaligned));
    CHECK(!memcmp(unaligned,unaligned_before,sizeof(unaligned)));

    /* Exhaustion never wraps into an old identity, even for unchanged bytes. */
    first=resource_key(base,64u,CTR_MEM_READ);memory.resource_clock=UINT64_MAX-1u;
    CHECK(ctr_memory_write(&memory,base+4u*page,1u,99u)==CTR_MEM_OK && memory.resource_clock==UINT64_MAX);
    CHECK(!ctr_memory_resource_key_current(&memory,&first));
    CHECK(!ctr_memory_resource_key_capture(&memory,base,64u,CTR_MEM_READ,&same) && !same.owner);
    CHECK(ctr_memory_write(&memory,base,1u,17u)==CTR_MEM_OK && memory.resource_clock==UINT64_MAX);
    CHECK(!ctr_memory_resource_key_capture(&memory,base,64u,CTR_MEM_READ,&same) && !same.owner);
    ctr_memory_init(&memory);free(allocation);
}
static int64_t signed32(uint32_t value) { return value < 0x80000000u ? (int64_t)value : (int64_t)value-INT64_C(4294967296); }
static void flag_tests(void) {
    setup(0);
    uint32_t seed=1;
    for (unsigned i=0;i<10000;++i) {
        seed=seed*1664525u+1013904223u; uint32_t a=seed;
        seed=seed*1664525u+1013904223u; uint32_t b=seed;
        unsigned carry=i&1;
        uint64_t wide=(uint64_t)a+b+carry;
        int64_t mathematical=signed32(a)+signed32(b)+carry;
        uint32_t value=ctr_cpu_add(&cpu,a,b,carry,true);
        CHECK(value==(uint32_t)wide);
        CHECK(((cpu.cpsr&CTR_FLAG_C)!=0)==(wide>UINT32_MAX));
        CHECK(((cpu.cpsr&CTR_FLAG_V)!=0)==(mathematical>INT32_MAX || mathematical<INT32_MIN));
        CHECK(((cpu.cpsr&CTR_FLAG_Z)!=0)==(value==0));
        CHECK(((cpu.cpsr&CTR_FLAG_N)!=0)==((value&0x80000000u)!=0));
    }
    CHECK(ctr_cpu_add(&cpu,0x7fffffffu,1,0,true)==0x80000000u && (cpu.cpsr&CTR_FLAG_V));
    CHECK(ctr_cpu_add(&cpu,0xffffffffu,1,0,true)==0 && (cpu.cpsr&(CTR_FLAG_Z|CTR_FLAG_C))==(CTR_FLAG_Z|CTR_FLAG_C));
    cpu.cpsr=CTR_FLAG_V|CTR_FLAG_T;
    ctr_cpu_logic_flags(&cpu,0,1);
    CHECK(cpu.cpsr==(CTR_FLAG_V|CTR_FLAG_T|CTR_FLAG_Z|CTR_FLAG_C));
    uint32_t before=cpu.cpsr;
    (void)ctr_cpu_add(&cpu,1,2,0,false); CHECK(cpu.cpsr==before);
    unsigned carry;
    CHECK(ctr_cpu_shift(0x80000001,0,0,true,1,&carry)==0x80000001 && carry==1);
    CHECK(ctr_cpu_shift(0x80000001,0,1,true,0,&carry)==2 && carry==1);
    CHECK(ctr_cpu_shift(0x80000001,0,32,false,0,&carry)==0 && carry==1);
    CHECK(ctr_cpu_shift(0x80000001,0,33,false,1,&carry)==0 && carry==0);
    CHECK(ctr_cpu_shift(0x80000001,1,0,true,0,&carry)==0 && carry==1);
    CHECK(ctr_cpu_shift(0x80000001,1,0,false,1,&carry)==0x80000001 && carry==1);
    CHECK(ctr_cpu_shift(0x80000001,1,33,false,1,&carry)==0 && carry==0);
    CHECK(ctr_cpu_shift(0x80000001,2,1,false,0,&carry)==0xc0000000 && carry==1);
    CHECK(ctr_cpu_shift(0x80000001,2,32,false,0,&carry)==UINT32_MAX && carry==1);
    CHECK(ctr_cpu_shift(0x7fffffff,2,255,false,1,&carry)==0 && carry==0);
    CHECK(ctr_cpu_shift(0x80000001,3,0,true,1,&carry)==0xc0000000 && carry==1);
    CHECK(ctr_cpu_shift(0x80000001,3,32,false,0,&carry)==0x80000001 && carry==1);
    CHECK(ctr_cpu_shift(0x80000001,3,33,false,0,&carry)==0xc0000000 && carry==1);
    cpu.cpsr=CTR_FLAG_Z|CTR_FLAG_C;
    CHECK(ctr_cpu_condition(&cpu,0) && !ctr_cpu_condition(&cpu,1));
    CHECK(ctr_cpu_condition(&cpu,2) && !ctr_cpu_condition(&cpu,3));
    CHECK(!ctr_cpu_condition(&cpu,8) && ctr_cpu_condition(&cpu,9));
    CHECK(ctr_cpu_condition(&cpu,10) && !ctr_cpu_condition(&cpu,11));
    CHECK(!ctr_cpu_condition(&cpu,12) && ctr_cpu_condition(&cpu,13));
    CHECK(ctr_cpu_condition(&cpu,14) && !ctr_cpu_condition(&cpu,15));
}
static void instruction_tests(void) {
    setup(0xee1d0f70);cpu.tls_address=0x1ff82000;cpu.cpsr=0xa0000010;
    CHECK(ctr_cpu_step(&cpu)==CTR_CPU_OK && cpu.r[0]==0x1ff82000 && cpu.r[15]==0x1004 && cpu.cpsr==0xa0000010);
    setup(0x0e1d6f70);cpu.tls_address=0x1ff82200;cpu.cpsr=CTR_FLAG_Z;
    CHECK(ctr_cpu_step(&cpu)==CTR_CPU_OK && cpu.r[6]==0x1ff82200);
    setup(0x0e1d6f70);cpu.tls_address=0x1ff82200;
    CHECK(ctr_cpu_step(&cpu)==CTR_CPU_OK && cpu.r[6]==0 && cpu.r[15]==0x1004);
    setup(0xee1dff70);CHECK(ctr_cpu_step(&cpu)==CTR_CPU_UNSUPPORTED && cpu.r[15]==0x1000);
    setup(0xee0d0f70);CHECK(ctr_cpu_step(&cpu)==CTR_CPU_UNSUPPORTED);
    setup(0xee070f15);CHECK(ctr_cpu_step(&cpu)==CTR_CPU_UNSUPPORTED);
    setup(0xe3a0002a); CHECK(ctr_cpu_step(&cpu)==CTR_CPU_OK && cpu.r[0]==42 && cpu.r[15]==0x1004);
    setup(0x03a0002a); CHECK(ctr_cpu_step(&cpu)==CTR_CPU_OK && cpu.r[0]==0 && cpu.r[15]==0x1004);
    setup(0xe2800001); cpu.r[0]=41; CHECK(ctr_cpu_step(&cpu)==CTR_CPU_OK && cpu.r[0]==42);
    setup(0xe3a00480); CHECK(ctr_cpu_step(&cpu)==CTR_CPU_OK && cpu.r[0]==0x80000000u);
    setup(0xeb000001); CHECK(ctr_cpu_step(&cpu)==CTR_CPU_OK && cpu.r[15]==0x100c && cpu.r[14]==0x1004);
    setup(0xe12fff10); cpu.r[0]=0x2001; CHECK(ctr_cpu_step(&cpu)==CTR_CPU_OK && cpu.r[15]==0x2000 && (cpu.cpsr&CTR_FLAG_T));
    setup(0xe5810004); cpu.r[1]=0x2000; cpu.r[0]=0x78563412;
    CHECK(ctr_cpu_step(&cpu)==CTR_CPU_OK && data[4]==0x12 && data[7]==0x78);
    setup(0xe4910004); cpu.r[1]=0x2000; data[0]=99;
    CHECK(ctr_cpu_step(&cpu)==CTR_CPU_OK && cpu.r[0]==99 && cpu.r[1]==0x2004);
    setup(0xe5910001); cpu.r[1]=0x2000;data[1]=0x11;data[2]=0x22;data[3]=0x33;data[4]=0x44;
    CHECK(ctr_cpu_step(&cpu)==CTR_CPU_OK && cpu.r[0]==0x44332211 && cpu.r[15]==0x1004);
    setup(0xe5910000); cpu.r[1]=0x3000;
    CHECK(ctr_cpu_step(&cpu)==CTR_CPU_MEMORY_FAULT && cpu.memory_fault==CTR_MEM_UNMAPPED);
    setup(0xef000003); cpu.module_hash="test-module";
    CHECK(ctr_cpu_step(&cpu)==CTR_CPU_SVC && cpu.r[15]==0x1004 && logged_pc==0x1000 && logged_detail==3 && !logged_thumb && !strcmp(logged_hash,"test-module"));
    setup(0xee300a00); CHECK(ctr_cpu_step(&cpu)==CTR_CPU_UNSUPPORTED && cpu.r[15]==0x1000 && logged_detail==0xee300a00);
    setup(0xe1910f9f); cpu.r[1]=0x2000; data[0]=7;
    CHECK(ctr_cpu_step(&cpu)==CTR_CPU_OK && cpu.r[0]==7 && cpu.exclusive_valid && cpu.exclusive_address==0x2000);
    cpu.r[2]=9;
    code[4]=0x92; code[5]=0x3f; code[6]=0x81; code[7]=0xe1; /* STREX r3,r2,[r1] */
    CHECK(ctr_cpu_step(&cpu)==CTR_CPU_OK && cpu.r[3]==0 && data[0]==9 && !cpu.exclusive_valid);
    setup(0xe1911f9f); cpu.r[1]=0x2000; data[0]=7; /* LDREX base == destination */
    CHECK(ctr_cpu_step(&cpu)==CTR_CPU_OK && cpu.r[1]==7 && cpu.exclusive_address==0x2000);
    setup(0xe1910f9f); cpu.r[1]=0x2000;
    CHECK(ctr_cpu_step(&cpu)==CTR_CPU_OK);
    CHECK(ctr_memory_write(&memory,0x2004,4,1)==CTR_MEM_OK);
    cpu.r[2]=9; code[4]=0x92; code[5]=0x3f; code[6]=0x81; code[7]=0xe1;
    CHECK(ctr_cpu_step(&cpu)==CTR_CPU_OK && cpu.r[3]==1 && data[0]==0);
    setup(0xdf7f2507); cpu.cpsr=CTR_FLAG_T;
    CHECK(ctr_cpu_step(&cpu)==CTR_CPU_OK && cpu.r[5]==7);
    CHECK(ctr_cpu_step(&cpu)==CTR_CPU_SVC && logged_thumb && logged_detail==127 && cpu.r[15]==0x1004);
    setup(0xeafffffe); CHECK(ctr_cpu_run(&cpu,7)==CTR_CPU_LIMIT && cpu.instructions==7 && cpu.r[15]==0x1000);
    ctr_program empty={NULL,0,"dispatch-hash",memory.code_generation};
    CHECK(ctr_cpu_dispatch(&cpu,&empty,1,false)==CTR_CPU_MISSING_BLOCK && !strcmp(logged_hash,"dispatch-hash"));
    CHECK(ctr_cpu_dispatch(&cpu,&empty,2,true)==CTR_CPU_LIMIT && cpu.fallback_instructions==2);
    empty.code_generation=memory.code_generation-1;
    CHECK(ctr_cpu_dispatch(&cpu,&empty,1,false)==CTR_CPU_CODE_INVALIDATED);
    setup(0xe3a0002a); cpu.r[15]=0x2000;
    CHECK(ctr_cpu_step(&cpu)==CTR_CPU_MEMORY_FAULT && cpu.memory_fault==CTR_MEM_PERMISSION);
    CHECK(ctr_cpu_selftest()==0);
}
static void viewport_tests(void) {
    CtrViewport view; uint16_t x,y;
    CHECK(ctr_viewport_fit(&view,1280,720,400,240) && view.x==40 && view.width==1200 && view.height==720);
    CHECK(ctr_viewport_fit(&view,854,480,320,240) && view.x==107 && view.width==640);
    CHECK(!ctr_viewport_touch(&view,0,0,1280,720,&x,&y));
    CHECK(ctr_viewport_touch(&view,640,360,1280,720,&x,&y) && x==160 && y==120);
    CHECK(!ctr_viewport_touch(&view,1280,360,1280,720,&x,&y));
    CHECK(!ctr_viewport_fit(&view,0,480,320,240));
}
static void halfword_tests(void) {
    setup(0xe1c100b2);cpu.r[0]=0x1234abcd;cpu.r[1]=0x2000;
    CHECK(ctr_cpu_step(&cpu)==CTR_CPU_OK && cpu.r[15]==0x1004 && data[2]==0xcd && data[3]==0xab && !data[4]);
    setup(0xe15120b2);cpu.r[1]=0x2004;data[2]=0xff;data[3]=0xff;
    CHECK(ctr_cpu_step(&cpu)==CTR_CPU_OK && cpu.r[2]==0xffff && cpu.r[1]==0x2004);
    setup(0x01c100b2);cpu.r[1]=0xdead0000;
    CHECK(ctr_cpu_step(&cpu)==CTR_CPU_OK && cpu.r[15]==0x1004 && !logs);
    setup(0xe1c100b1);cpu.r[1]=0x2000;cpu.r[0]=0x1234;
    CHECK(ctr_cpu_step(&cpu)==CTR_CPU_OK && cpu.r[15]==0x1004 && data[1]==0x34 && data[2]==0x12);
    setup(0xe1c100b0);cpu.r[1]=0x1000;
    CHECK(ctr_cpu_step(&cpu)==CTR_CPU_MEMORY_FAULT && cpu.memory_fault==CTR_MEM_PERMISSION && cpu.r[15]==0x1000);
    const uint32_t unsupported[]={0xe0c100b0,0xe18100bf,0xe1e100b0,0xe1c1f0b0};
    for(unsigned i=0;i<4;++i){setup(unsupported[i]);CHECK(ctr_cpu_step(&cpu)==CTR_CPU_UNSUPPORTED && cpu.r[15]==0x1000);}
}
static void extend_tests(void) {
    const uint32_t forms[]={0xe6af4072,0xe6bf4072,0xe6ef4072,0xe6ff4072};
    const uint32_t values[]={0,0xff,0x80,0x7f,0xffff,0x8000,0x123480ab,0x800000ff,UINT32_MAX};
    for(unsigned form=0;form<4;++form)for(unsigned rotation=0;rotation<4;++rotation)
        for(unsigned i=0;i<9;++i) {
            setup(forms[form]|(rotation<<10));cpu.r[2]=values[i];cpu.cpsr=0x98050010;
            unsigned bits=rotation*8;
            uint32_t rotated=bits?(values[i]>>bits)|(values[i]<<(32-bits)):values[i];
            int64_t expected=rotated&((form&1)?65535u:255u);
            if(form<2 && expected>=((form&1)?32768:128))expected-=(form&1)?65536:256;
            CHECK(ctr_cpu_step(&cpu)==CTR_CPU_OK && cpu.r[4]==(uint32_t)expected);
            CHECK(cpu.r[2]==values[i] && cpu.r[15]==0x1004 && cpu.cpsr==0x98050010);
        }
    setup(0xe6ef2072);cpu.r[2]=0x123480ab;
    CHECK(ctr_cpu_step(&cpu)==CTR_CPU_OK && cpu.r[2]==0xab); /* Same source and destination. */
    setup(0x06ef4072);cpu.cpsr=0x10;cpu.r[4]=123;
    CHECK(ctr_cpu_step(&cpu)==CTR_CPU_OK && cpu.r[4]==123 && cpu.r[15]==0x1004); /* Failed EQ. */
    const uint32_t rejected[]={0xe6eff072,0xe6ef407f,0xe6e24072,0xe68f4072};
    for(unsigned i=0;i<4;++i) {
        setup(rejected[i]);cpu.r[4]=123;
        CHECK(ctr_cpu_step(&cpu)==CTR_CPU_UNSUPPORTED && cpu.r[4]==123 && cpu.r[15]==0x1000);
    }
}
int main(void) {
    memory_tests();resource_version_tests();flag_tests();instruction_tests();viewport_tests();halfword_tests();extend_tests();
    clock_t start=clock();
    for (unsigned i=0;i<1000;++i) CHECK(ctr_cpu_selftest()==0);
    double seconds=(double)(clock()-start)/CLOCKS_PER_SEC;
    printf("{\"ok\":true,\"assertions\":%u,\"fixture_runs\":1000,\"guest_instructions_per_path\":40000,\"combined_desktop_seconds\":%.9f,\"powerpc_execution\":\"awaiting_console\"}\n",checks,seconds);
    return 0;
}
