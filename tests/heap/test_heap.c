#include "ctr_heap_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned assertions;
#define CHECK(x) do { ++assertions;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
#define BAD_ADDRESS UINT32_C(0xe0e01bf5)
#define BAD_STATE UINT32_C(0xe0a01bf5)
#define BAD_COMBINATION UINT32_C(0xe0e01bee)
#define NO_MEMORY UINT32_C(0xd86007f3)
static uint32_t control(ctr_guest *g,uint32_t op,uint32_t address,uint32_t source,uint32_t size,
                        uint32_t permissions,uint32_t expected) {
    g->cpu.r[0]=op;g->cpu.r[1]=address;g->cpu.r[2]=source;g->cpu.r[3]=size;g->cpu.r[4]=permissions;
    ctr_guest_svc(g,1);CHECK(g->cpu.r[0]==expected && g->memory_result==expected);
    if (expected) CHECK(!g->cpu.r[1]);
    return g->cpu.r[1];
}
static void query(ctr_guest *g,uint32_t address,uint32_t base,uint32_t size,unsigned permissions,unsigned state) {
    g->cpu.r[2]=address;CHECK(ctr_guest_svc(g,2)==CTR_GUEST_READY);
    if (g->cpu.r[0] || g->cpu.r[1]!=base || g->cpu.r[2]!=size)
        fprintf(stderr,"query %08x: got %08x/%08x/%08x expected %08x/%08x\n",address,g->cpu.r[0],g->cpu.r[1],g->cpu.r[2],base,size);
    CHECK(!g->cpu.r[0] && g->cpu.r[1]==base && g->cpu.r[2]==size);
    CHECK(g->cpu.r[3]==permissions && g->cpu.r[4]==state && !g->cpu.r[5]);
}
static void allocation_tests(void) {
    ctr_guest g;uint32_t v=0;CHECK(ctr_heap_fixture_open(&g));
    uint64_t code=g.memory.code_generation;g.cpu.r[7]=0x12345678;g.cpu.exclusive_valid=true;
    uint32_t base=control(&g,3,0,0,0x3000,3,0);CHECK(base==0x08001000 && !g.cpu.exclusive_valid);
    CHECK(g.cpu.r[7]==0x12345678 && code==g.memory.code_generation);
    CHECK(ctr_memory_read(&g.memory,base,4,CTR_MEM_READ,&v)==CTR_MEM_OK && !v);
    CHECK(ctr_memory_read(&g.memory,base+0x2ffc,4,CTR_MEM_READ,&v)==CTR_MEM_OK && !v);
    query(&g,base+2,0x08000000,0x4000,3,5);
    CHECK(ctr_memory_write(&g.memory,base+0x1000,4,0x76543210)==CTR_MEM_OK);
    control(&g,6,base+0x1000,0,0x1000,1,0);query(&g,base+0x1800,base+0x1000,0x1000,1,5);
    CHECK(ctr_memory_write(&g.memory,base+0x1000,4,4)==CTR_MEM_PERMISSION);
    CHECK(ctr_memory_read(&g.memory,base+0x1000,4,CTR_MEM_READ,&v)==CTR_MEM_OK && v==0x76543210);
    CHECK(ctr_memory_write(&g.memory,base,4,0x87654321)==CTR_MEM_OK);
    control(&g,6,base+0x1000,0,0x1000,0,0);query(&g,base+0x1000,base+0x1000,0x1000,0,5);
    CHECK(ctr_memory_read(&g.memory,base+0x1000,4,CTR_MEM_READ,&v)==CTR_MEM_PERMISSION);
    CHECK(ctr_memory_write(&g.memory,base+0x1000,4,0)==CTR_MEM_PERMISSION);
    control(&g,6,base,0,0x3000,3,0);query(&g,base+0x1001,0x08000000,0x4000,3,5);
    control(&g,1,base+0x1000,0,0x1000,0,0);query(&g,base+0x1000,base+0x1000,0x1000,0,0);
    CHECK(ctr_memory_read(&g.memory,base+0x1000,4,CTR_MEM_READ,&v)==CTR_MEM_UNMAPPED);
    ctr_memory before=g.memory;
    control(&g,6,base,0,0x3000,1,BAD_STATE);CHECK(!memcmp(&before,&g.memory,sizeof(before)));
    control(&g,1,base,0,0x3000,0,BAD_STATE);CHECK(!memcmp(&before,&g.memory,sizeof(before)));
    uint32_t again=control(&g,0x103,0,0,0x1000,3,0);CHECK(again==base+0x1000);
    CHECK(ctr_memory_read(&g.memory,again,4,CTR_MEM_READ,&v)==CTR_MEM_OK && !v);
    CHECK(ctr_memory_read(&g.memory,base,4,CTR_MEM_READ,&v)==CTR_MEM_OK && v==0x87654321);
    control(&g,1,base,0,0x3000,0,0);
    for(unsigned i=0;i<CTR_GUEST_MAX_ALLOCATIONS;++i) CHECK(!g.allocations[i].storage);
    CHECK(g.memory.code_generation==code && g.memory.count==5);
    uint32_t linear=control(&g,0x10103,0,0,0x2000,3,0);CHECK(linear==CTR_LINEAR_BASE);
    query(&g,linear+0x1fff,linear,0x2000,3,7);
    control(&g,3,0x08100000,0,0x1000,2,0);query(&g,0x08100080,0x08100000,0x1000,2,5);
    CHECK(ctr_memory_read(&g.memory,0x08100000,4,CTR_MEM_READ,&v)==CTR_MEM_PERMISSION);
    CHECK(ctr_memory_write(&g.memory,0x08100000,4,0x1234)==CTR_MEM_OK);
    uint32_t guard=control(&g,3,0,0,0x1000,0,0);query(&g,guard,guard,0x1000,0,5);
    CHECK(ctr_memory_read(&g.memory,guard,1,CTR_MEM_READ,&v)==CTR_MEM_PERMISSION);
    CHECK(ctr_memory_write(&g.memory,guard,1,0)==CTR_MEM_PERMISSION);
    ctr_guest_close(&g); /* Active heap allocations are released on close too. */
}
static void query_tests(void) {
    ctr_guest g;CHECK(ctr_heap_fixture_open(&g));g.cpu.r[7]=0x12345678;
    query(&g,0,0,0x100000,0,0);
    query(&g,0x100080,0x100000,0x1000,5,4);
    query(&g,0x08000000,0x08000000,0x1000,3,5);
    query(&g,0x0fffffff,0x0fffc000,0x4000,3,11);
    query(&g,0x1ff820a0,0x1ff82000,0x1000,3,11);
    query(&g,0x3fffffff,0x1ff83000,0x2007d000,0,0);
    uint8_t backing[8192]={0};
    CHECK(ctr_memory_map(&g.memory,0xfffff000,0x1000,1,backing,sizeof(backing))==CTR_MEM_OK);
    query(&g,0x3fffffff,0x1ff83000,0x2007d000,0,0);
    CHECK(ctr_memory_map(&g.memory,0x3ffff000,0x2000,1,backing,sizeof(backing))==CTR_MEM_OK);
    query(&g,0x3fffffff,0x3ffff000,0x1000,1,3);
    g.cpu.r[2]=CTR_PROCESS_END;ctr_guest_svc(&g,2);CHECK(g.cpu.r[0]==BAD_ADDRESS);
    for(unsigned i=1;i<6;++i)CHECK(!g.cpu.r[i]);
    CHECK(g.cpu.r[7]==0x12345678 && g.memory_failures==1);ctr_guest_close(&g);
}
static void failure_tests(void) {
    ctr_guest g;CHECK(ctr_heap_fixture_open(&g));ctr_memory before=g.memory;
    control(&g,3,0x08001001,0,0x1000,3,0xe0e01bf1);
    control(&g,3,0,1,0x1000,3,0xe0e01bf1);
    control(&g,3,0,0,0x1001,3,0xe0e01bf2);
    control(&g,3,0,0,0,3,0xe0e01bf2);
    control(&g,3,0,0,0x1000,4,BAD_COMBINATION);
    control(&g,3,0,0,0x1000,0x10000000,BAD_COMBINATION);
    control(&g,3,0,0x08000000,0x1000,3,BAD_COMBINATION);
    control(&g,3,0x100000,0,0x1000,3,BAD_ADDRESS);
    control(&g,3,0xfffff000,0,0x2000,3,BAD_ADDRESS);
    control(&g,0x10003,CTR_HEAP_BASE,0,0x1000,3,BAD_ADDRESS);
    control(&g,3,0x08000000,0,0x1000,3,BAD_STATE);
    control(&g,3,0x0ffff000,0,0x2000,3,BAD_ADDRESS);
    control(&g,3,0,0,CTR_GUEST_MAX_BYTES,3,NO_MEMORY);
    control(&g,1,0x08000000,0,0x1000,0,BAD_STATE);
    control(&g,6,0x0fffc000,0,0x1000,1,BAD_STATE);
    control(&g,6,0x100000,0,0x1000,1,BAD_STATE);
    control(&g,1,0x1ff82000,0,0x1000,0,BAD_STATE);
    control(&g,1,0xfffff000,0,0x2000,0,BAD_ADDRESS);
    CHECK(!memcmp(&before,&g.memory,sizeof(before)) && g.state==CTR_GUEST_READY);
    uint32_t base=control(&g,3,0,0,0x1000,3,0);before=g.memory;
    control(&g,1,base,0,0x1000,3,BAD_COMBINATION);CHECK(!memcmp(&before,&g.memory,sizeof(before)));
    ctr_guest_close(&g);
    const unsigned unsupported[]={2,4,5,0,7,0x203,0x303,0x10001,0x10006,0x80000003};
    for(size_t i=0;i<sizeof(unsupported)/sizeof(unsupported[0]);++i) {
        CHECK(ctr_heap_fixture_open(&g));before=g.memory;
        control(&g,unsupported[i],0,0,0x1000,3,BAD_COMBINATION);
        CHECK(g.state==CTR_GUEST_FAULTED && g.fault==CTR_CPU_UNSUPPORTED && g.fault_detail==1);
        CHECK(!memcmp(&before,&g.memory,sizeof(before)));ctr_guest_close(&g);
    }
}
static void capacity_tests(void) {
    ctr_guest g;uint32_t addresses[CTR_GUEST_MAX_ALLOCATIONS];CHECK(ctr_heap_fixture_open(&g));
    for(unsigned i=0;i<CTR_GUEST_MAX_ALLOCATIONS;++i) {
        addresses[i]=control(&g,3,0,0,0x1000,3,0);CHECK(addresses[i]==0x08001000+i*0x1000);
    }
    ctr_memory before=g.memory;control(&g,3,0,0,0x1000,3,NO_MEMORY);
    CHECK(!memcmp(&before,&g.memory,sizeof(before)));
    control(&g,1,addresses[3],0,0x1000,0,0);
    CHECK(control(&g,3,0,0,0x1000,3,0)==addresses[3]);ctr_guest_close(&g);
    CHECK(ctr_heap_fixture_open(&g));uint32_t base=control(&g,3,0,0,0x3000,3,0);
    uint8_t scratch[4096]={0};
    while(g.memory.count<CTR_MEMORY_MAX_MAPS)
        CHECK(ctr_memory_map(&g.memory,0x20000000+(uint32_t)g.memory.count*0x1000,0x1000,1,scratch,sizeof(scratch))==CTR_MEM_OK);
    before=g.memory;
    control(&g,6,base+0x1000,0,0x1000,1,NO_MEMORY);CHECK(!memcmp(&before,&g.memory,sizeof(before)));
    control(&g,1,base+0x1000,0,0x1000,0,NO_MEMORY);CHECK(!memcmp(&before,&g.memory,sizeof(before)));
    control(&g,3,0,0,0x1000,3,NO_MEMORY);CHECK(!memcmp(&before,&g.memory,sizeof(before)));
    control(&g,1,base,0,0x3000,0,0);CHECK(g.memory.count==CTR_MEMORY_MAX_MAPS-1);ctr_guest_close(&g);
}
static void backing_budget_tests(void) {
    ctr_guest g;CHECK(ctr_heap_fixture_open(&g));
    uint32_t base=control(&g,3,0,0,0x02000000,3,0);
    control(&g,1,base+0x1000,0,0x01fff000,0,0);
    /* A freed subrange retains its original backing allocation until the last
       page is removed. Capacity is bounded by retained host memory, not just VA. */
    ctr_memory before=g.memory;
    control(&g,3,0,0,0x02000000,3,NO_MEMORY);CHECK(!memcmp(&before,&g.memory,sizeof(before)));
    control(&g,1,base,0,0x1000,0,0);
    CHECK(control(&g,3,0,0,0x02000000,3,0)==base);ctr_guest_close(&g);
}
static void mapping_tests(void) {
    ctr_memory m;uint8_t bytes[64]={0};uint32_t v;ctr_memory_init(&m);
    CHECK(ctr_memory_map(&m,0x1000,16,5,bytes,16)==CTR_MEM_OK);uint64_t code=m.code_generation;
    CHECK(ctr_memory_map(&m,0x2000,64,3,bytes,64)==CTR_MEM_OK && m.code_generation==code);
    CHECK(ctr_memory_change_range(&m,0x2010,16,1,false)==CTR_MEM_OK && m.code_generation==code);
    CHECK(ctr_memory_write(&m,0x2010,4,1)==CTR_MEM_PERMISSION);
    CHECK(ctr_memory_write(&m,0x2020,4,1)==CTR_MEM_OK && m.code_generation==code);
    CHECK(ctr_memory_write(&m,0x2000,4,0x76543210)==CTR_MEM_OK && m.code_generation==code+1);
    CHECK(ctr_memory_read(&m,0x1000,4,CTR_MEM_EXEC,&v)==CTR_MEM_OK && v==0x76543210);
    CHECK(ctr_memory_change_range(&m,0x1004,4,1,false)==CTR_MEM_OK && m.code_generation==code+2);
    CHECK(ctr_memory_read(&m,0x1004,4,CTR_MEM_EXEC,&v)==CTR_MEM_PERMISSION);
    CHECK(ctr_memory_change_range(&m,0x1008,8,0,true)==CTR_MEM_OK && m.code_generation==code+3);
    ctr_memory before=m;
    CHECK(ctr_memory_change_range(&m,0x1000,16,1,false)==CTR_MEM_UNMAPPED);
    CHECK(!memcmp(&before,&m,sizeof(m)));
    CHECK(ctr_memory_change_range(&m,0xffffffff,2,1,false)==CTR_MEM_OVERFLOW);
    CHECK(ctr_memory_change_range(&m,0x2000,0,1,false)==CTR_MEM_OVERFLOW);
    CHECK(ctr_memory_change_range(&m,0x2000,1,8,false)==CTR_MEM_PERMISSION);
}
int main(void) {
    allocation_tests();query_tests();failure_tests();capacity_tests();backing_budget_tests();mapping_tests();
    CHECK(ctr_heap_selftest()==0);
    printf("{\"ok\":true,\"assertions\":%u,\"arm_heap_allocation_lifecycle\":true,\"atomic_mapping_changes\":true,\"aot_interpreter_memory_match\":true,\"console_verified\":false}\n",assertions);
    return 0;
}
