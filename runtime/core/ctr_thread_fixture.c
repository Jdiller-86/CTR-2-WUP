#include "ctr_thread_fixture.h"
#include <string.h>
bool ctr_thread_fixture_open(ctr_guest *g) {
    return ctr_guest_open(g,&ctr_thread_image,ctr_thread_fixture_blocks,ctr_thread_fixture_block_count);
}
int ctr_thread_fixture_check(const ctr_guest *g) {
    if(g->state!=CTR_GUEST_EXITED || !g->process_exited || g->cpu.fallback_instructions ||
        g->cpu.tls_address!=0x1ff82000 || g->context_switches<6)return 1;
    const uint32_t offsets[]={16,20,24,28,32,36,40,44,48,52,56,60,64};
    const uint32_t expected[]={0,0,0,0,0x1111,0x1ff82000,0x1ff82200,0xe0a01bf5,0,0,2,0x1ff82400,0};
    uint32_t n;
    for(unsigned i=0;i<sizeof(offsets)/sizeof(*offsets);++i)
        if(ctr_memory_read(&g->memory,0x08000000+offsets[i],4,1,&n)!=CTR_MEM_OK || n!=expected[i])return (int)i+2;
    for(unsigned i=0;i<64;++i)if(g->handles[i].active)return 30;
    for(unsigned i=0;i<CTR_GUEST_MAX_THREADS;++i)if(g->threads[i].active)return 31;
    return 0;
}
int ctr_thread_selftest(void) {
    ctr_guest a,b;int result=0;
    if(!ctr_thread_fixture_open(&a))return 40;
    if(!ctr_thread_fixture_open(&b)) { ctr_guest_close(&a);return 41; }
    for(unsigned n=0;n<500 && a.state!=CTR_GUEST_EXITED && !result;++n) {
        unsigned budget=n%3==0?1:n%3==1?7:23;uint64_t time=n*10000u;
        ctr_guest_run(&a,budget,time,false);ctr_guest_run(&b,budget,time,true);
        if(a.state==CTR_GUEST_FAULTED || a.state!=b.state || a.cpu.instructions!=b.cpu.instructions ||
            a.current_thread!=b.current_thread || a.cpu.cpsr!=b.cpu.cpsr || memcmp(a.cpu.r,b.cpu.r,sizeof(a.cpu.r)) ||
            memcmp(a.threads,b.threads,sizeof(a.threads)))result=42;
        for(unsigned i=0;i<5 && !result;++i)if(a.storage_size[i] && memcmp(a.storage[i],b.storage[i],a.storage_size[i]))result=43;
    }
    if(!result)result=ctr_thread_fixture_check(&a);
    if(!result)result=ctr_thread_fixture_check(&b);
    ctr_guest_close(&a);ctr_guest_close(&b);return result;
}
