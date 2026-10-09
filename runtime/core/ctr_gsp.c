/* Original single-process GSP direct/shared framebuffer bridge from wire facts. */
#include "ctr_gsp.h"
#include "ctr_kernel.h"
#include <string.h>
#define BAD_HANDLE UINT32_C(0xd8e007f7)
#define BAD_STATE UINT32_C(0xe0a01bf5)
#define NO_MEMORY UINT32_C(0xd86007f3)
static void unregister(ctr_guest *g) {
    ctr_guest_gsp *s=&g->gsp;
    if(!s->registered)return;
    s->registered=false;
    ctr_guest_object_release(g,CTR_HANDLE_EVENT,s->relay_event);
    ctr_guest_object_release(g,CTR_HANDLE_SHARED,s->relay_shared);
    s->relay_owner=s->relay_event=s->relay_shared=0;s->next_vblank_ns=0;
}
void ctr_gsp_session_closed(ctr_guest *g,unsigned session) {
    if(g->gsp.registered && g->gsp.relay_owner==session)unregister(g);
}
static unsigned pixel_bytes(unsigned format) { return format==0?4:format==1?3:2; }
static bool supported(unsigned screen,const uint32_t *info) {
    unsigned format=info[4]&7u;
    return screen<2 && info[0]<=1 && info[5]==info[0] && !info[6] && format<=4 &&
        !(info[4]&~0x147u) && (info[4]&0x100u) && ((info[4]&0x40u)!=0)==(screen==0) &&
        (!info[2] || info[2]==info[1]) && info[3]>=240*pixel_bytes(format) && info[3]<=4096;
}
static ctr_mem_result source_ok(const ctr_guest *g,unsigned screen,const uint32_t *info) {
    unsigned width=screen?320:400;
    uint32_t size=(width-1)*info[3]+240*pixel_bytes(info[4]&7u);
    return ctr_memory_check_range(&g->memory,info[1],size,CTR_MEM_READ);
}
bool ctr_gsp_request(ctr_guest *g,unsigned session,const uint32_t *words,uint32_t *out,unsigned *count) {
    *count=2;out[0]=(words[0]&0xffff0000u)|0x40u;out[1]=0;
    switch(words[0]) {
        case 0x00160042: /* AcquireRight, flags0, one copied current-process handle. */
            if(words[1] || words[2] || words[3]!=0xffff8001u)return false;
            g->gsp.right=true;return true;
        case 0x00170000:
            if(!g->gsp.right)return false;
            g->gsp.right=false;return true;
        case 0x00080082: /* FlushDataCache: coherent CPU framebuffer backing. */
        case 0x00090082: { /* InvalidateDataCache: same checked local backing. */
            if(!g->gsp.right || words[3] || words[4]!=0xffff8001u)return false;
            ctr_mem_result result=ctr_memory_check_range(&g->memory,words[1],words[2],CTR_MEM_READ);
            if(result!=CTR_MEM_OK) {
                g->cpu.memory_fault=result;g->fault=CTR_CPU_MEMORY_FAULT;g->fault_detail=words[1];
                g->state=CTR_GUEST_FAULTED;ctr_cpu_stop(&g->cpu,g->fault,g->fault_detail);return false;
            }
            /* No asynchronous GPU writes exist in this scoped CPU path. */
            ctr_cpu_barrier();return true;
        }
        case 0x000b0040:
            if(!g->gsp.right || words[1]>1)return false;
            g->gsp.black=words[1]!=0;return true;
        case 0x00130042: {
            if(!g->gsp.right || words[1]!=1 || words[2])return false;
            if(g->gsp.registered || UINT64_MAX-g->now_ns<CTR_GSP_FRAME_NS) { out[1]=BAD_STATE;return true; }
            ctr_guest_handle *event=ctr_guest_handle_for(g,words[3]);
            if(!event || event->type!=CTR_HANDLE_EVENT) { out[1]=BAD_HANDLE;return true; }
            unsigned event_object=event->object;uint32_t handle=ctr_shared_create(g,4096,3);
            if(!handle) { out[1]=NO_MEMORY;return true; }
            unsigned shared=ctr_guest_handle_for(g,handle)->object;
            ctr_guest_object_retain(g,CTR_HANDLE_EVENT,event_object);
            ctr_guest_object_retain(g,CTR_HANDLE_SHARED,shared);
            g->gsp.registered=true;g->gsp.relay_owner=session;
            g->gsp.relay_event=event_object;g->gsp.relay_shared=shared;
            g->gsp.next_vblank_ns=g->now_ns+CTR_GSP_FRAME_NS;
            out[0]=0x00130082;out[2]=0;out[3]=0;out[4]=handle;*count=5;return true;
        }
        case 0x00140000:
            if(!g->gsp.registered || g->gsp.relay_owner!=session) { out[1]=BAD_STATE;return true; }
            unregister(g);return true;
        case 0x00050200: {
            if(!g->gsp.right || !supported(words[1],words+2))return false;
            ctr_mem_result result=source_ok(g,words[1],words+2);
            if(result!=CTR_MEM_OK) {
                g->cpu.memory_fault=result;g->fault=CTR_CPU_MEMORY_FAULT;g->fault_detail=words[3];
                g->state=CTR_GUEST_FAULTED;ctr_cpu_stop(&g->cpu,g->fault,g->fault_detail);return false;
            }
            memcpy(g->gsp.info[words[1]],words+2,sizeof(g->gsp.info[0]));
            g->gsp.active[words[1]]=true;++g->gsp.swaps[words[1]];return true;
        }
        default:return false;
    }
}
static uint32_t word_at(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static bool device_fault(ctr_guest *g,ctr_mem_result result,uint32_t detail) {
    ++g->gsp.failures;g->fault=result==CTR_MEM_OK?CTR_CPU_UNSUPPORTED:CTR_CPU_MEMORY_FAULT;
    g->cpu.memory_fault=result;g->fault_detail=detail;g->state=CTR_GUEST_FAULTED;
    ctr_cpu_stop(&g->cpu,g->fault,detail);return false;
}
bool ctr_gsp_poll(ctr_guest *g,uint64_t now) {
    ctr_guest_gsp *s=&g->gsp;
    if(!s->registered || !s->right || now<s->next_vblank_ns)return true;
    uint64_t frames=1+(now-s->next_vblank_ns)/CTR_GSP_FRAME_NS;
    if(frames>(UINT64_MAX-s->next_vblank_ns)/CTR_GSP_FRAME_NS)return device_fault(g,CTR_MEM_OK,0x130042);
    uint8_t next[4096];memcpy(next,g->shared[s->relay_shared].storage,sizeof(next));
    if(next[0]>=52 || next[1]>52 || next[2]>1 || next[3])return device_fault(g,CTR_MEM_OK,0);
    if(word_at(next+0x800))return device_fault(g,CTR_MEM_OK,0x800); /* No erased GX work. */
    uint32_t info[2][7];bool dirty[2];
    for(unsigned screen=0;screen<2;++screen) {
        unsigned offset=0x200+screen*0x40;uint32_t header=word_at(next+offset);
        if((header&~0x101u) || (header&255u)>1)return device_fault(g,CTR_MEM_OK,offset);
        dirty[screen]=(header&0x100u)!=0;
        if(!dirty[screen])continue;
        unsigned entry=offset+4+(header&1u)*28;
        for(unsigned i=0;i<7;++i)info[screen][i]=word_at(next+entry+i*4);
        if(!supported(screen,info[screen]))return device_fault(g,CTR_MEM_OK,entry);
        ctr_mem_result result=source_ok(g,screen,info[screen]);
        if(result!=CTR_MEM_OK)return device_fault(g,result,info[screen][1]);
    }
    /* Both screen descriptors and the ring are checked before any publication. */
    for(unsigned screen=0;screen<2;++screen)if(dirty[screen]) {
        memcpy(s->info[screen],info[screen],sizeof(s->info[screen]));s->active[screen]=true;++s->swaps[screen];
        next[0x201+screen*0x40]=0;
    }
    for(unsigned irq=2;irq<=3;++irq) {
        if(next[1]==52) { next[2]=1;++s->dropped_interrupts; }
        else { next[0xc+(next[0]+next[1])%52]=(uint8_t)irq;++next[1]; }
    }
    ctr_shared_publish(g,s->relay_shared,next);
    ++s->vblanks;s->coalesced_vblanks+=frames-1;s->next_vblank_ns+=frames*CTR_GSP_FRAME_NS;
    g->events[s->relay_event].signaled=true;ctr_threads_poll(g);return true;
}
static unsigned expand(unsigned value,unsigned bits) {
    return bits==6?(value<<2)|(value>>4):bits==5?(value<<3)|(value>>2):(value<<4)|value;
}
ctr_mem_result ctr_gsp_pixels(const ctr_guest *g,unsigned screen,uint32_t *rgba,size_t count) {
    if(screen>1 || !rgba || count!=(screen?320u:400u)*240u || !g->gsp.active[screen])return CTR_MEM_UNMAPPED;
    if(g->gsp.black) { for(size_t i=0;i<count;++i)rgba[i]=0x000000ff;return CTR_MEM_OK; }
    const uint32_t *info=g->gsp.info[screen];ctr_mem_result result=source_ok(g,screen,info);
    if(result!=CTR_MEM_OK)return result;
    unsigned format=info[4]&7u,bpp=pixel_bytes(format),width=screen?320:400;
    for(unsigned x=0;x<width;++x)for(unsigned y=0;y<240;++y) {
        uint32_t address=info[1]+x*info[3]+(239-y)*bpp,raw=0,byte;
        for(unsigned i=0;i<bpp;++i) {
            result=ctr_memory_read(&g->memory,address+i,1,CTR_MEM_READ,&byte);
            if(result!=CTR_MEM_OK)return result;
            raw|=byte<<(8*i);
        }
        unsigned r,gc,b,a=255;
        if(format==0) { r=raw>>24;gc=(raw>>16)&255;b=(raw>>8)&255;a=raw&255; }
        else if(format==1) { r=(raw>>16)&255;gc=(raw>>8)&255;b=raw&255; }
        else if(format==2) { r=expand(raw>>11,5);gc=expand((raw>>5)&63,6);b=expand(raw&31,5); }
        else if(format==3) { r=expand(raw>>11,5);gc=expand((raw>>6)&31,5);b=expand((raw>>1)&31,5);a=(raw&1)?255:0; }
        else { r=expand(raw>>12,4);gc=expand((raw>>8)&15,4);b=expand((raw>>4)&15,4);a=expand(raw&15,4); }
        rgba[y*width+x]=(r<<24)|(gc<<16)|(b<<8)|a;
    }
    return CTR_MEM_OK;
}
