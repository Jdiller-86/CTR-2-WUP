#include "ctr_hid_fixture.h"
#include <string.h>
bool ctr_hid_fixture_open(ctr_guest *g) {
    return ctr_guest_open(g,&ctr_hid_image,ctr_hid_fixture_blocks,ctr_hid_fixture_block_count);
}
bool ctr_hid_fixture_snapshot(const ctr_guest *g,uint32_t words[5]) {
    for (unsigned i=0;i<5;++i) if (ctr_memory_read(&g->memory,0x08000000+i*4,4,1,&words[i])!=CTR_MEM_OK) return false;
    return true;
}
bool ctr_hid_fixture_exit(ctr_guest *g) {
    return ctr_memory_write(&g->memory,0x08000100,4,1)==CTR_MEM_OK && ctr_guest_signal(g,g->hid.handles[1]);
}
int ctr_hid_selftest(void) {
    ctr_guest a,b;int result=0;uint32_t words[5];
    if (!ctr_hid_fixture_open(&a)) { ctr_guest_close(&a);return 1; }
    if (!ctr_hid_fixture_open(&b)) { ctr_guest_close(&a);ctr_guest_close(&b);return 2; }
    ctr_guest *paths[]={&a,&b};
    for (unsigned i=0;i<2;++i) {
        ctr_guest *g=paths[i];
        if (!ctr_hid_update(g,0x401,12,-34,true,100,80,1000000) ||
            ctr_guest_run(g,1000,1000000,i!=0)!=CTR_GUEST_WAITING ||
            !ctr_hid_fixture_snapshot(g,words) || words[0]!=0x401 || words[1]!=0xffde000c ||
            words[2]!=0x00500064 || words[3]!=1 || words[4]!=1) { result=3;break; }
        if (!ctr_hid_update(g,2,0,0,false,0,0,2000000) ||
            ctr_guest_run(g,1000,2000000,i!=0)!=CTR_GUEST_WAITING ||
            !ctr_hid_fixture_snapshot(g,words) || words[0]!=2 || words[1] || words[2] || words[3] || words[4]!=2) { result=4;break; }
        if (!ctr_hid_fixture_exit(g) || ctr_guest_run(g,1000,2000000,i!=0)!=CTR_GUEST_EXITED || g->cpu.fallback_instructions) { result=5;break; }
    }
    if (!result && (a.cpu.instructions!=b.cpu.instructions || a.cpu.cpsr!=b.cpu.cpsr || memcmp(a.cpu.r,b.cpu.r,sizeof(a.cpu.r)))) result=6;
    for (unsigned i=0;i<5 && !result;++i) if (a.storage_size[i]!=b.storage_size[i] ||
        (a.storage_size[i] && memcmp(a.storage[i],b.storage[i],a.storage_size[i]))) result=7;
    ctr_guest_close(&a);ctr_guest_close(&b);return result;
}
