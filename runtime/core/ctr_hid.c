/* Original HID shared-ring producer from public interface facts. */
#include "ctr_hid.h"
#include <string.h>
static bool initialize(ctr_guest *g) {
    if (g->hid.initialized) return true;
    uint32_t handles[6]={0};handles[0]=ctr_shared_create(g,4096,CTR_MEM_READ);
    for (unsigned i=1;i<6 && handles[i-1];++i) handles[i]=ctr_guest_event_create(g,0);
    for (unsigned i=0;i<6;++i) if (!handles[i]) {
        for (unsigned j=0;j<6;++j) if (handles[j]) ctr_guest_handle_close(g,handles[j]);
        return false;
    }
    memcpy(g->hid.handles,handles,sizeof(handles));g->hid.initialized=true;return true;
}
bool ctr_hid_handles(ctr_guest *g,uint32_t handles[6]) {
    if (!initialize(g)) return false;
    memset(handles,0,6*sizeof(handles[0]));
    for (unsigned i=0;i<6;++i) {
        ctr_guest_handle *h=ctr_guest_handle_for(g,g->hid.handles[i]);
        if (!h || !(handles[i]=ctr_guest_handle_new(g,h->type,h->object))) {
            for (unsigned j=0;j<i;++j) ctr_guest_handle_close(g,handles[j]);
            memset(handles,0,6*sizeof(handles[0]));return false;
        }
    }
    return true;
}
static void put(uint8_t *bytes,unsigned at,unsigned width,uint64_t value) {
    for (unsigned i=0;i<width;++i) bytes[at+i]=(uint8_t)(value>>(8*i));
}
bool ctr_hid_update(ctr_guest *g,uint32_t buttons,int cx,int cy,bool touched,uint16_t x,uint16_t y,uint64_t now) {
    if ((buttons&~0xfffu) || cx < -156 || cx>156 || cy < -156 || cy>156 ||
        (touched && (x>=320 || y>=240)) || (g->hid.published && now<=g->hid.last_ns)) return false;
    if (!initialize(g)) return false;
    int xx=cx*cx,yy=cy*cy;
    if (xx+yy>1600) {
        if (cx && yy<3*xx) buttons|=cx>0?1u<<28:1u<<29;
        if (!cx || 3*yy>xx) buttons|=cy>0?1u<<30:1u<<31;
    }
    ctr_guest_handle *h=ctr_guest_handle_for(g,g->hid.handles[0]);if (!h) return false;
    ctr_guest_shared *s=&g->shared[h->object];uint8_t next[4096];memcpy(next,s->storage,sizeof(next));
    unsigned index=(g->hid.index+1)%8;uint64_t ticks=(now/1000000000u)*UINT64_C(268111856)+
                                                  ((now%1000000000u)*UINT64_C(268111856))/1000000000u;
    if (!index) {
        memcpy(next+8,next,8);memcpy(next+0xb0,next+0xa8,8);
        put(next,0,8,ticks);put(next,0xa8,8,ticks);
    }
    put(next,0x10,4,index);put(next,0xb8,4,index);put(next,0x1c,4,buttons);
    unsigned pad=0x28+index*16,touch=0xc8+index*8;
    put(next,pad,4,buttons);put(next,pad+4,4,buttons&~g->hid.held);put(next,pad+8,4,g->hid.held&~buttons);
    put(next,pad+12,2,(uint16_t)cx);put(next,pad+14,2,(uint16_t)cy);
    put(next,touch,2,touched?x:0);put(next,touch+2,2,touched?y:0);put(next,touch+4,4,touched?1:0);
    ctr_shared_publish(g,h->object,next);
    g->hid.index=index;g->hid.held=buttons;g->hid.published=true;g->hid.last_ns=now;++g->hid.samples;
    ctr_guest_signal(g,g->hid.handles[1]);ctr_guest_signal(g,g->hid.handles[2]);return true;
}
bool ctr_hid_read(const ctr_guest *g,uint32_t base,uint32_t *buttons,int *cx,int *cy,bool *touched,uint16_t *x,uint16_t *y) {
    uint32_t pad_index,touch_index,held,circle,position,valid;
    if (!buttons || !cx || !cy || !touched || !x || !y) return false;
    if (ctr_memory_check_range(&g->memory,base,0x2b0,CTR_MEM_READ)!=CTR_MEM_OK ||
        ctr_memory_read(&g->memory,base+0x10,4,1,&pad_index)!=CTR_MEM_OK ||
        ctr_memory_read(&g->memory,base+0xb8,4,1,&touch_index)!=CTR_MEM_OK || pad_index>7 || touch_index>7) return false;
    if (ctr_memory_read(&g->memory,base+0x28+pad_index*16,4,1,&held)!=CTR_MEM_OK ||
        ctr_memory_read(&g->memory,base+0x34+pad_index*16,4,1,&circle)!=CTR_MEM_OK ||
        ctr_memory_read(&g->memory,base+0xc8+touch_index*8,4,1,&position)!=CTR_MEM_OK ||
        ctr_memory_read(&g->memory,base+0xcc+touch_index*8,4,1,&valid)!=CTR_MEM_OK) return false;
    *buttons=held;*cx=(circle&0x8000u)?(int)(circle&0xffffu)-65536:(int)(circle&0xffffu);
    *cy=(circle&0x80000000u)?(int)(circle>>16)-65536:(int)(circle>>16);
    *touched=valid!=0;*x=(uint16_t)position;*y=(uint16_t)(position>>16);return true;
}
