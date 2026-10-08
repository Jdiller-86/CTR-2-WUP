#include "ctr_heap_fixture.h"
#include <string.h>

bool ctr_heap_fixture_open(ctr_guest *g) {
    return ctr_guest_open(g,&ctr_heap_image,ctr_heap_fixture_blocks,ctr_heap_fixture_block_count);
}
int ctr_heap_fixture_check(const ctr_guest *g) {
    static const uint32_t expected[][2]={
        {0x00,0},{0x04,0x08001000},
        {0x10,0},{0x14,0x08000000},{0x18,0x4000},{0x1c,3},{0x20,5},{0x24,0},
        {0x30,0},{0x40,0},{0x44,0x08002000},{0x48,0x1000},{0x4c,1},{0x50,5},{0x54,0},
        {0x60,0x78563412},{0x64,0},{0x68,0},
        {0x70,0},{0x74,0x08002000},{0x78,0x1000},{0x7c,0},{0x80,0},{0x84,0},
        {0x90,0},{0x94,0x08002000},{0x98,0},{0x9c,0},{0xa0,0x14000000},{0xa4,0x44332211},
        {0xb0,0},{0xb4,0x14000000},{0xb8,0x2000},{0xbc,3},{0xc0,7},{0xc4,0},
        {0xd0,0},{0xd4,0},{0xd8,0},{0xdc,0}
    };
    if (g->state!=CTR_GUEST_EXITED || g->svc_count!=15 || g->memory_requests!=14 ||
        g->memory_failures || g->cpu.fallback_instructions) return 1;
    for (size_t i=0;i<sizeof(expected)/sizeof(expected[0]);++i) {
        uint32_t value;
        if (ctr_memory_read(&g->memory,0x08000000+expected[i][0],4,CTR_MEM_READ,&value)!=CTR_MEM_OK ||
            value!=expected[i][1]) return (int)i+2;
    }
    for (unsigned i=0;i<CTR_GUEST_MAX_ALLOCATIONS;++i) if (g->allocations[i].storage) return 50;
    return 0;
}
int ctr_heap_selftest(void) {
    ctr_guest a,b;int result=0;
    if (!ctr_heap_fixture_open(&a)) { ctr_guest_close(&a);return 60; }
    if (!ctr_heap_fixture_open(&b)) { ctr_guest_close(&a);ctr_guest_close(&b);return 61; }
    ctr_guest_run(&a,1000,0,false);ctr_guest_run(&b,1000,0,true);
    result=ctr_heap_fixture_check(&a);
    if (!result) result=ctr_heap_fixture_check(&b);
    if (!result && (a.cpu.instructions!=b.cpu.instructions || a.cpu.cpsr!=b.cpu.cpsr ||
        memcmp(a.cpu.r,b.cpu.r,sizeof(a.cpu.r)) || a.memory.code_generation!=b.memory.code_generation)) result=62;
    for (unsigned i=0;i<5 && !result;++i) if (a.storage_size[i]!=b.storage_size[i] ||
        (a.storage_size[i] && memcmp(a.storage[i],b.storage[i],a.storage_size[i]))) result=63;
    ctr_guest_close(&a);ctr_guest_close(&b);return result;
}
