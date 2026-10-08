#include "ctr_gsp_fixture.h"
#include <stdlib.h>
#include <string.h>
bool ctr_gsp_fixture_open(ctr_guest *g) {
    return ctr_guest_open(g,&ctr_gsp_image,ctr_gsp_fixture_blocks,ctr_gsp_fixture_block_count);
}
int ctr_gsp_selftest(void) {
    ctr_guest a,b;int result=0;uint32_t *pixels=malloc(400*240*sizeof(*pixels));
    if(!pixels)return 1;
    if(!ctr_gsp_fixture_open(&a)){free(pixels);ctr_guest_close(&a);return 2;}
    if(!ctr_gsp_fixture_open(&b)){free(pixels);ctr_guest_close(&a);ctr_guest_close(&b);return 3;}
    ctr_guest *paths[]={&a,&b};
    for(unsigned i=0;i<2 && !result;++i) {
        ctr_guest *g=paths[i];
        if(!ctr_hid_update(g,1,0,0,true,100,80,1000000) || ctr_guest_run(g,1000000,1000000,i!=0)!=CTR_GUEST_WAITING ||
            g->gsp.swaps[0]!=1 || g->gsp.swaps[1]!=1) {result=4;break;}
        if(ctr_gsp_pixels(g,0,pixels,400*240)!=CTR_MEM_OK || pixels[0]!=0xffffffff || pixels[399]!=0xff0000ff ||
            pixels[239*400]!=0x00ff00ff || pixels[240*400-1]!=0x0000ffff || pixels[401]!=0x00ff00ff) {result=5;break;}
        if(ctr_gsp_pixels(g,1,pixels,320*240)!=CTR_MEM_OK || pixels[0]!=0xff00ffff || pixels[80*320+100]!=0xffffffff) {result=6;break;}
        if(!ctr_hid_fixture_exit(g) || ctr_guest_run(g,1000,1000000,i!=0)!=CTR_GUEST_EXITED || g->gsp.right || g->cpu.fallback_instructions) {result=7;break;}
    }
    if(!result && (a.cpu.instructions!=b.cpu.instructions || a.cpu.cpsr!=b.cpu.cpsr || memcmp(a.cpu.r,b.cpu.r,sizeof(a.cpu.r)) || memcmp(&a.gsp,&b.gsp,sizeof(a.gsp))))result=8;
    for(unsigned i=0;i<5 && !result;++i)if(a.storage_size[i]!=b.storage_size[i] ||
        (a.storage_size[i] && memcmp(a.storage[i],b.storage[i],a.storage_size[i])))result=9;
    free(pixels);ctr_guest_close(&a);ctr_guest_close(&b);return result;
}
