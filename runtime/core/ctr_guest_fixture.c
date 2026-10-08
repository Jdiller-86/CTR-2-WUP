#include "ctr_guest_fixture.h"
#ifdef CTR_EXTERNAL_AOT
#include "ctr_recomp.h"
#endif
bool ctr_frame_open(ctr_guest *g) {
#ifdef CTR_EXTERNAL_AOT
    if (!ctr_guest_open(g,&ctr_frame_image,NULL,0)) return false;
    if (!ctr_recomp_attach(g,false)) { ctr_guest_close(g);return false; }
    return true;
#else
    return ctr_guest_open(g,&ctr_frame_image,ctr_frame_fixture_blocks,ctr_frame_fixture_block_count);
#endif
}
bool ctr_frame_input(ctr_guest *g,uint32_t buttons,bool touched,uint32_t x,uint32_t y) {
    if (touched && (x>=320 || y>=240)) return false;
    uint32_t values[]={buttons,touched?x:0,touched?y:0,touched?1u:0u};
    for (unsigned i=0;i<4;++i)
        if (ctr_memory_write(&g->memory,CTR_FRAME_DATA+i*4,4,values[i])!=CTR_MEM_OK) return false;
    return true;
}
bool ctr_frame_count(const ctr_guest *g,uint32_t *count) {
    return ctr_memory_read(&g->memory,CTR_FRAME_DATA+16,4,CTR_MEM_READ,count)==CTR_MEM_OK;
}
bool ctr_frame_exit(ctr_guest *g) {
    return ctr_memory_write(&g->memory,CTR_FRAME_DATA+20,4,1)==CTR_MEM_OK;
}
bool ctr_frame_pixels(const ctr_guest *g,bool top,uint32_t *rgba,size_t count) {
    if (!rgba || count!=(top?400u:320u)*240u) return false;
    uint32_t address=top?CTR_FRAME_TOP:CTR_FRAME_BOTTOM;
    for (size_t i=0;i<count;++i) {
        uint32_t value;
        if (ctr_memory_read(&g->memory,address+(uint32_t)i*4,4,CTR_MEM_READ,&value)!=CTR_MEM_OK) return false;
        rgba[i]=((value&255u)<<24)|((value&0xff00u)<<8)|((value&0xff0000u)>>8)|(value>>24);
    }
    return true;
}
