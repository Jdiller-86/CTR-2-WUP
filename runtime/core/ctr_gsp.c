/* Original single-process GSP direct/shared framebuffer bridge from wire facts. */
#include "ctr_gsp.h"
#include "ctr_apt.h"
#include "ctr_kernel.h"
#include "ctr_gx.h"
#include <stdlib.h>
#include <string.h>
#define BAD_HANDLE UINT32_C(0xd8e007f7)
#define BAD_STATE UINT32_C(0xe0a01bf5)
#define NO_MEMORY UINT32_C(0xd86007f3)
static bool execute_queue(ctr_guest *g);
bool ctr_gsp_open_vram(ctr_guest *g) {
    if(g->storage[CTR_VRAM_STORAGE_INDEX])return true;
    uint8_t *bytes=calloc(1,CTR_VRAM_BYTES);
    if(!bytes)return false;
    if(ctr_memory_map(&g->memory,CTR_VRAM_BASE,CTR_VRAM_BYTES,CTR_MEM_READ,bytes,CTR_VRAM_BYTES)!=CTR_MEM_OK) {
        free(bytes);return false;
    }
    g->storage[CTR_VRAM_STORAGE_INDEX]=bytes;
    g->storage_size[CTR_VRAM_STORAGE_INDEX]=CTR_VRAM_BYTES;
    return true;
}
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
    unsigned display_mode=info[4]&0x60u;
    bool stereo=screen==0u && display_mode==0x20u;
    return screen<2 && info[0]<=1 && info[5]==info[0] && !info[6] && format<=4 &&
        !(info[4]&~0x167u) && (info[4]&0x100u) &&
        (stereo || display_mode==(screen==0u?0x40u:0u)) &&
        (stereo?info[2]!=0u:(!info[2] || info[2]==info[1])) &&
        info[3]>=240*pixel_bytes(format) && info[3]<=4096;
}
static ctr_mem_result source_ok(const ctr_guest *g,unsigned screen,const uint32_t *info) {
    unsigned width=screen?320:400;
    uint32_t size=(width-1)*info[3]+240*pixel_bytes(info[4]&7u);
    ctr_mem_result result=ctr_memory_check_range(&g->memory,info[1],size,CTR_MEM_READ);
    if(result==CTR_MEM_OK && info[2] && info[2]!=info[1])
        result=ctr_memory_check_range(&g->memory,info[2],size,CTR_MEM_READ);
    return result;
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
        case 0x00180000: /* ImportDisplayCaptureInfo. */
            if(!g->gsp.right || !ctr_apt_mii_capture_ready(g))return false;
            for(unsigned screen=0;screen<2;++screen) {
                const uint32_t *info=g->gsp.info[screen];
                if(!g->gsp.active[screen] || source_ok(g,screen,info)!=CTR_MEM_OK) { out[1]=BAD_STATE;return true; }
                unsigned at=2u+4u*screen;
                out[at]=info[1];out[at+1]=info[2]?info[2]:info[1];
                out[at+2]=info[4];out[at+3]=info[3];
            }
            out[0]=0x00180240;*count=10;return true;
        case 0x00190000: /* SaveVramSysArea. */
            if(!g->gsp.right || !ctr_apt_mii_capture_ready(g) || g->apt.mii_vram_saved)return false;
            /* The headless selector leaves local VRAM intact while guest GPU rights are released. */
            g->apt.mii_vram_saved=true;return true;
        case 0x001a0000: /* RestoreVramSysArea. */
            if(!g->gsp.right || !g->apt.mii_vram_saved)return false;
            g->apt.mii_vram_saved=false;return true;
        case 0x001e0080: /* SetInternalPriorities; this GSP executes inline with the guest. */
            return words[1]>=0x10u && words[1]<=0x3fu && words[2]>=0x11u && words[2]<=0x3fu;
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
            unsigned event_object=event->object;uint32_t handle=ctr_shared_create_service_page(g,3);
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
        case 0x000c0000:
            if(!g->gsp.right || !g->gsp.registered || g->gsp.relay_owner!=session) { out[1]=BAD_STATE;return true; }
            return execute_queue(g);
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
static bool ring_valid(const uint8_t *page) { return page[0]<52 && page[1]<=52 && page[2]<=1 && !page[3]; }
static bool queue_valid(const uint8_t *page) {
    const uint8_t *h=page+0x800;
    if(h[0]>=15 || h[1]>15 || h[2]>1 || h[3]>1)return false;
    for(unsigned i=4;i<32;++i)if(h[i])return false;
    return true;
}
static bool interrupt(ctr_guest *g,uint8_t *page,unsigned irq) {
    if(page[1]==52) { page[2]=1;++g->gsp.dropped_interrupts;return false; }
    page[0xc+(page[0]+page[1])%52]=(uint8_t)irq;++page[1];return true;
}
static bool execute_queue(ctr_guest *g) {
    ctr_guest_gsp *s=&g->gsp;uint8_t page[4096];memcpy(page,g->shared[s->relay_shared].storage,sizeof(page));
    if(!ring_valid(page) || !queue_valid(page))return device_fault(g,CTR_MEM_OK,0x800);
    while(page[0x801]) {
        if(page[0x802])break;
        if(page[0x803]) { page[0x802]=1;ctr_shared_publish(g,s->relay_shared,page);break; }
        uint32_t command[8];unsigned at=0x820+page[0x800]*32;
        for(unsigned i=0;i<8;++i)command[i]=word_at(page+at+i*4);
        ctr_gx_completion completion;s->last_gx_header=command[0];
        unsigned id=command[0]&255u;
#ifdef CTR_GAME_CEMU_TEST
        uint64_t started=s->clock_us?s->clock_us(NULL):0u;
#endif
        ctr_gx_result r=ctr_gx_execute(g,command,&completion);s->last_gx_detail=completion.detail;
#ifdef CTR_GAME_CEMU_TEST
        if(s->clock_us) {
            uint64_t elapsed=s->clock_us(NULL)-started;
            s->gx_us+=elapsed;
            if(id==1u)s->pica_us+=elapsed;
        }
#endif
        if(r!=CTR_GX_OK)return device_fault(g,r==CTR_GX_MEMORY?completion.memory:r==CTR_GX_NO_MEMORY?CTR_MEM_CAPACITY:CTR_MEM_OK,completion.detail);
        ++s->gx_commands;
        if(id==0)++s->gx_dma;else if(id==2)++s->gx_fills;else if(id==3)++s->gx_transfers;
        else if(id==4)++s->gx_copies;else if(id==5)++s->gx_cache;
        else if(id==1)++s->gx_pica;
        page[0x800]=(uint8_t)((page[0x800]+1)%15);--page[0x801];
        if(command[0]&0x10000)page[0x802]=1;
        bool enqueued=completion.signal && interrupt(g,page,completion.irq);
        ctr_shared_publish(g,s->relay_shared,page);
        if(enqueued) { g->events[s->relay_event].signaled=true;ctr_threads_poll(g); }
    }
    return true;
}
bool ctr_gsp_poll(ctr_guest *g,uint64_t now) {
    ctr_guest_gsp *s=&g->gsp;
    if(!s->registered || !s->right || now<s->next_vblank_ns)return true;
    uint64_t frames=1+(now-s->next_vblank_ns)/CTR_GSP_FRAME_NS;
    if(frames>(UINT64_MAX-s->next_vblank_ns)/CTR_GSP_FRAME_NS)return device_fault(g,CTR_MEM_OK,0x130042);
    uint8_t next[4096];memcpy(next,g->shared[s->relay_shared].storage,sizeof(next));
    if(!ring_valid(next))return device_fault(g,CTR_MEM_OK,0);
    if(!queue_valid(next))return device_fault(g,CTR_MEM_OK,0x800);
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
    /* Frame descriptors are checked before queue or display publication. Queue
       commands commit in order; a failing command preserves its pending entry. */
    if(!execute_queue(g))return false;
    memcpy(next,g->shared[s->relay_shared].storage,sizeof(next));
    for(unsigned screen=0;screen<2;++screen)if(dirty[screen]) {
        memcpy(s->info[screen],info[screen],sizeof(s->info[screen]));s->active[screen]=true;++s->swaps[screen];
        next[0x201+screen*0x40]=0;
    }
    bool enqueued=false;for(unsigned irq=2;irq<=3;++irq)if(interrupt(g,next,irq))enqueued=true;
    ctr_shared_publish(g,s->relay_shared,next);
    ++s->vblanks;s->coalesced_vblanks+=frames-1;s->next_vblank_ns+=frames*CTR_GSP_FRAME_NS;
    if(enqueued) { g->events[s->relay_event].signaled=true;ctr_threads_poll(g); }return true;
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
    uint8_t column[240u*4u];
    for(unsigned x=0;x<width;++x) {
        result=ctr_memory_read_bytes(&g->memory,info[1]+x*info[3],column,240u*bpp);
        if(result!=CTR_MEM_OK)return result;
        for(unsigned y=0;y<240;++y) {
            const uint8_t *pixel=column+(239u-y)*bpp;uint32_t raw=0;
            for(unsigned i=0;i<bpp;++i)raw|=(uint32_t)pixel[i]<<(8*i);
            unsigned r,gc,b,a=255;
            if(format==0) { r=raw>>24;gc=(raw>>16)&255;b=(raw>>8)&255;a=raw&255; }
            else if(format==1) { r=(raw>>16)&255;gc=(raw>>8)&255;b=raw&255; }
            else if(format==2) { r=expand(raw>>11,5);gc=expand((raw>>5)&63,6);b=expand(raw&31,5); }
            else if(format==3) { r=expand(raw>>11,5);gc=expand((raw>>6)&31,5);b=expand((raw>>1)&31,5);a=(raw&1)?255:0; }
            else { r=expand(raw>>12,4);gc=expand((raw>>8)&15,4);b=expand((raw>>4)&15,4);a=expand(raw&15,4); }
            rgba[y*width+x]=(r<<24)|(gc<<16)|(b<<8)|a;
        }
    }
    return CTR_MEM_OK;
}
