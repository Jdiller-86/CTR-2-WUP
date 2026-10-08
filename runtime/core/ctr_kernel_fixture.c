#include "ctr_kernel_fixture.h"
#include <string.h>
bool ctr_kernel_fixture_open(ctr_guest *g) {
    return ctr_guest_open(g,&ctr_kernel_image,ctr_kernel_fixture_blocks,ctr_kernel_fixture_block_count);
}
int ctr_kernel_fixture_check(const ctr_guest *g) {
    if (g->state!=CTR_GUEST_EXITED || g->cpu.fallback_instructions || g->memory_failures ||
        g->cpu.instructions!=ctr_kernel_image.text.size/4) return 1;
    for (unsigned i=0;i<ctr_kernel_expected_count;++i) {
        uint32_t n;
        if (ctr_memory_read(&g->memory,0x08000000+i*4,4,CTR_MEM_READ,&n)!=CTR_MEM_OK || n!=ctr_kernel_expected[i]) return (int)i+2;
    }
    for (unsigned i=0;i<64;++i) if (g->handles[i].active) return 60;
    for (unsigned i=0;i<8;++i) if (g->shared[i].active) return 61;
    for (unsigned i=0;i<16;++i) if (g->allocations[i].storage) return 62;
    return 0;
}
int ctr_kernel_selftest(void) {
    ctr_guest a,b;int result;
    if (!ctr_kernel_fixture_open(&a)) { ctr_guest_close(&a);return 70; }
    if (!ctr_kernel_fixture_open(&b)) { ctr_guest_close(&a);ctr_guest_close(&b);return 71; }
    ctr_guest_run(&a,2000,0,false);ctr_guest_run(&b,2000,0,true);
    result=ctr_kernel_fixture_check(&a);if (!result) result=ctr_kernel_fixture_check(&b);
    if (!result && (a.cpu.instructions!=b.cpu.instructions || a.cpu.cpsr!=b.cpu.cpsr || memcmp(a.cpu.r,b.cpu.r,sizeof(a.cpu.r)))) result=72;
    for (unsigned i=0;i<5 && !result;++i) if (a.storage_size[i]!=b.storage_size[i] ||
        (a.storage_size[i] && memcmp(a.storage[i],b.storage[i],a.storage_size[i]))) result=73;
    ctr_guest_close(&a);ctr_guest_close(&b);return result;
}
