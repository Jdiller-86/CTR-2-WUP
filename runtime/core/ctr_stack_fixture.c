#include "ctr_stack_fixture.h"
#include <string.h>
bool ctr_stack_fixture_open(ctr_guest *g) {
    return ctr_guest_open(g,&ctr_stack_image,ctr_stack_fixture_blocks,ctr_stack_fixture_block_count);
}
int ctr_stack_selftest(void) {
    ctr_guest a,b;int result=0;
    if(!ctr_stack_fixture_open(&a)){ctr_guest_close(&a);return 1;}
    if(!ctr_stack_fixture_open(&b)){ctr_guest_close(&a);ctr_guest_close(&b);return 2;}
    uint32_t initial_sp=a.cpu.r[13];ctr_guest *paths[]={&a,&b};
    for(unsigned slice=0;slice<100 && a.state!=CTR_GUEST_EXITED && !result;++slice) {
        unsigned budget=slice%3==0?1:slice%3==1?7:23;
        for(unsigned i=0;i<2;++i)ctr_guest_run(paths[i],budget,slice,i!=0);
        if(a.state==CTR_GUEST_FAULTED || b.state==CTR_GUEST_FAULTED || a.state!=b.state ||
            a.cpu.instructions!=b.cpu.instructions || a.cpu.cpsr!=b.cpu.cpsr || memcmp(a.cpu.r,b.cpu.r,sizeof(a.cpu.r)))result=3;
        for(unsigned i=0;i<5 && !result;++i)if(a.storage_size[i]!=b.storage_size[i] ||
            (a.storage_size[i] && memcmp(a.storage[i],b.storage[i],a.storage_size[i])))result=4;
    }
    if(!result && (a.state!=CTR_GUEST_EXITED || a.cpu.r[13]!=initial_sp || a.cpu.fallback_instructions || b.cpu.fallback_instructions ||
        (a.cpu.cpsr&CTR_FLAG_T) || a.svc_count!=1 || a.cpu.instructions!=47))result=5;
    const uint32_t expected[]={10,10,3,7,0x4444,0x5555,0x08000000,0x7777};uint32_t value;
    for(unsigned i=0;i<8 && !result;++i)if(ctr_memory_read(&a.memory,0x08000000+i*4,4,1,&value)!=CTR_MEM_OK || value!=expected[i])result=6;
    ctr_guest_close(&a);ctr_guest_close(&b);return result;
}
