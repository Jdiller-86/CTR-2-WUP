#include "ctr_relay_fixture.h"
#include "ctr_kernel.h"
#include <stdlib.h>
#include <string.h>
bool ctr_relay_fixture_open(ctr_guest *g) {
    return ctr_guest_open(g,&ctr_relay_image,ctr_relay_fixture_blocks,ctr_relay_fixture_block_count);
}
bool ctr_relay_fixture_exit(ctr_guest *g) {
    if(!ctr_hid_fixture_exit(g))return false;
    if(g->gsp.registered) { g->events[g->gsp.relay_event].signaled=true;ctr_sync_poll(g); }
    return true;
}
int ctr_relay_selftest(void) {
    ctr_guest a,b;int result=0;uint32_t value,*pixels=malloc(400*240*sizeof(*pixels));
    if(!pixels)return 1;
    if(!ctr_relay_fixture_open(&a)){free(pixels);ctr_guest_close(&a);return 2;}
    if(!ctr_relay_fixture_open(&b)){free(pixels);ctr_guest_close(&a);ctr_guest_close(&b);return 3;}
    ctr_guest *paths[]={&a,&b};
    for(unsigned i=0;i<2 && !result;++i) {
        ctr_guest *g=paths[i];
        if(!ctr_hid_update(g,1,0,0,true,100,80,1000000) || ctr_guest_run(g,1000000,1000000,i!=0)!=CTR_GUEST_WAITING ||
            !g->gsp.registered || g->gsp.swaps[0] || g->gsp.swaps[1]) {result=4;break;}
        uint64_t due=g->gsp.next_vblank_ns;
        if(ctr_guest_run(g,0,due-1,i!=0)!=CTR_GUEST_WAITING || g->gsp.swaps[0] ||
            ctr_guest_run(g,10000,due,i!=0)!=CTR_GUEST_WAITING || g->gsp.swaps[0]!=1 || g->gsp.swaps[1]!=1 ||
            g->gsp.vblanks!=1 || g->shared[g->gsp.relay_shared].storage[1]) {result=5;break;}
        if(ctr_memory_read(&g->memory,0x08000018,4,1,&value)!=CTR_MEM_OK || value!=2 ||
            ctr_memory_read(&g->memory,0x08000024,4,1,&value)!=CTR_MEM_OK || value!=12) {result=6;break;}
        if(ctr_gsp_pixels(g,0,pixels,400*240)!=CTR_MEM_OK || pixels[0]!=0xffffffff || pixels[399]!=0xff0000ff ||
            pixels[239*400]!=0x00ff00ff || pixels[240*400-1]!=0x0000ffff || pixels[401]!=0x00ff00ff) {result=7;break;}
        if(ctr_gsp_pixels(g,1,pixels,320*240)!=CTR_MEM_OK || pixels[0]!=0xff00ffff || pixels[80*320+100]!=0xffffffff) {result=8;break;}
        if(!ctr_relay_fixture_exit(g) || ctr_guest_run(g,10000,due,i!=0)!=CTR_GUEST_EXITED ||
            g->gsp.right || g->gsp.registered || g->cpu.fallback_instructions) {result=9;break;}
    }
    if(!result && (a.cpu.instructions!=b.cpu.instructions || a.cpu.cpsr!=b.cpu.cpsr || memcmp(a.cpu.r,b.cpu.r,sizeof(a.cpu.r)) ||
        memcmp(&a.gsp,&b.gsp,sizeof(a.gsp)) || memcmp(a.handles,b.handles,sizeof(a.handles))))result=10;
    for(unsigned i=0;i<5 && !result;++i)if(a.storage_size[i]!=b.storage_size[i] ||
        (a.storage_size[i] && memcmp(a.storage[i],b.storage[i],a.storage_size[i])))result=11;
    free(pixels);ctr_guest_close(&a);ctr_guest_close(&b);return result;
}
