/* Original standalone APT state machine; wire facts from pinned libctru. */
#include "ctr_apt.h"
#include "ctr_fs.h"
#include "ctr_kernel.h"
#include <string.h>
#define STATE UINT32_C(0xc8a06c00)
#define EMPTY UINT32_C(0xc8806c02)
#define FULL UINT32_C(0xd86007f3)
bool ctr_apt_configure(ctr_guest *g) {
    if(!g || g->state!=CTR_GUEST_READY || g->cpu.instructions || g->svc_count || g->apt.available)return false;
    g->apt.available=true;return true;
}
void ctr_apt_close(ctr_guest *g) {
    if(g->apt.initialized)for(unsigned i=0;i<2;++i)ctr_guest_object_release(g,CTR_HANDLE_EVENT,g->apt.events[i]);
    if(g->apt.lock)ctr_guest_object_release(g,CTR_HANDLE_MUTEX,g->apt.mutex);
    memset(&g->apt,0,sizeof(g->apt));
}
static bool memory(ctr_guest *g,ctr_mem_result result,uint32_t at) {
    if(result==CTR_MEM_OK)return true;
    g->state=CTR_GUEST_FAULTED;g->fault=CTR_CPU_MEMORY_FAULT;g->fault_detail=at;g->cpu.memory_fault=result;
    ctr_cpu_stop(&g->cpu,CTR_CPU_MEMORY_FAULT,at);return false;
}
static bool static_output(ctr_guest *g,uint32_t size,uint32_t base,uint32_t *address) {
    uint32_t descriptor,at=g->cpu.tls_address+0x180;
    if(size>4096 || !memory(g,ctr_memory_read(&g->memory,at,4,CTR_MEM_READ,&descriptor),at) ||
       !memory(g,ctr_memory_read(&g->memory,at+4,4,CTR_MEM_READ,address),at+4))return false;
    if(descriptor!=((size<<14)|2u))return false;
    return ctr_fs_buffer(g,*address,size,CTR_MEM_WRITE,base);
}
bool ctr_apt_request(ctr_guest *g,const uint32_t *w,uint32_t base,uint32_t *o,unsigned *count) {
    o[0]=(w[0]&0xffff0000u)|0x40u;*count=2;
    if(!g->apt.available) { o[1]=STATE;return true; }
    if(w[0]==0x00010040) {
        if(w[1])return false;
        bool created=false;
        if(!g->apt.lock) {
            unsigned i;for(i=0;i<16 && g->mutexes[i].active;++i){}
            if(i==16) { o[1]=FULL;return true; }
            g->mutexes[i]=(ctr_guest_mutex){.active=true,.references=1};g->apt.mutex=i;g->apt.lock=true;created=true;
        }
        uint32_t h=ctr_guest_handle_new(g,CTR_HANDLE_MUTEX,g->apt.mutex);
        if(!h) { if(created) { ctr_guest_object_release(g,CTR_HANDLE_MUTEX,g->apt.mutex);g->apt.lock=false; }o[1]=FULL;return true; }
        o[0]=0x000100c2;o[4]=0;o[5]=h;*count=6;return true;
    }
    if(w[0]==0x00020080) {
        if(w[1]!=0x300 || w[2])return false;
        if(!g->apt.lock || g->apt.initialized) { o[1]=STATE;return true; }
        uint32_t h[2]={ctr_guest_event_create(g,0),0};if(h[0])h[1]=ctr_guest_event_create(g,0);
        if(!h[0] || !h[1]) { if(h[0])ctr_guest_handle_close(g,h[0]);o[1]=FULL;return true; }
        for(unsigned i=0;i<2;++i) { g->apt.events[i]=ctr_guest_handle_for(g,h[i])->object;ctr_guest_object_retain(g,CTR_HANDLE_EVENT,g->apt.events[i]); }
        g->apt.initialized=true;o[0]=0x00020043;o[2]=1u<<26;o[3]=h[0];o[4]=h[1];*count=5;return true;
    }
    if(!g->apt.initialized) { o[1]=STATE;return true; }
    switch(w[0]) {
        case 0x00030040:
            if(w[1])return false;
            if(g->apt.enabled) { o[1]=STATE;return true; }
            g->apt.enabled=true;g->apt.transitions=0x10;g->apt.pending=true;g->events[g->apt.events[1]].signaled=true;ctr_threads_poll(g);return true;
        case 0x00430040:
            if(w[1]!=0x300)return false;
            if(!g->apt.enabled || !g->apt.pending) { o[1]=STATE;return true; }
            g->apt.notified=true;return true;
        case 0x000e0080: case 0x000d0080: {
            uint32_t at;if(w[1]!=0x300 || !static_output(g,w[2],base,&at))return false;
            if(!g->apt.pending || !g->apt.notified) { o[1]=EMPTY;return true; }
            /* Empty wakeup parameter from the local manager, no transferred handle. */
            o[0]=(w[0]&0xffff0000u)|0x102u;o[2]=0;o[3]=1;o[4]=0;o[5]=0;o[6]=0;*count=7;
            if(w[0]==0x000d0080) { g->apt.pending=false;g->events[g->apt.events[1]].signaled=false; }
            return true;
        }
        case 0x004b00c2: {
            uint32_t at;if(!static_output(g,w[3],base,&at) || w[3]!=1 || w[4]!=((w[2]<<14)|0x402u))return false;
            uint32_t required=w[1]==4?1:w[1]==7?4:0;
            if(!required || w[2]!=required || !ctr_fs_buffer(g,w[5],required,CTR_MEM_READ,base))return false;
            uint32_t input;if(!memory(g,ctr_memory_read(&g->memory,w[5],required==1?1:4,CTR_MEM_READ,&input),w[5]))return false;
            if((w[1]==4 && input) || (w[1]==7 && input!=0x10))return false;
            /* The standalone runtime has no shell-closed condition. Unlocking
               the enable transition changes the explicit local state. */
            if(!memory(g,ctr_memory_write(&g->memory,at,1,0),at))return false;
            if(w[1]==7)g->apt.transitions&=~input;
            o[0]=0x004b0080;o[2]=0;*count=3;return true;
        }
        case 0x00220040:
            if(w[1]>1)return false;
            if(!g->apt.enabled || g->apt.pending || g->apt.closing) { o[1]=STATE;return true; }
            g->apt.closing=true;return true;
        case 0x00270044:
            if(w[1] || w[2] || w[3] || w[4]!=2)return false;
            if(!g->apt.closing) { o[1]=STATE;return true; }
            ctr_threads_exit_process(g);return true;
        case 0x00040040:
            if(w[1]!=0x300)return false;
            /* Finalize unregisters this application; existing copied handles
               retain their objects independently of the manager. */
            for(unsigned i=0;i<2;++i)ctr_guest_object_release(g,CTR_HANDLE_EVENT,g->apt.events[i]);
            g->apt.initialized=false;g->apt.enabled=false;g->apt.pending=false;g->apt.notified=false;g->apt.closing=false;return true;
        case 0x01020000:o[0]=0x01020080;o[2]=0;*count=3;return true; /* Old-family policy. */
        default:return false;
    }
}
