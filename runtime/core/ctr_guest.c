#include "ctr_guest.h"
#include "ctr_ipc.h"
#include "ctr_heap.h"
#include "ctr_kernel.h"
#include <stdlib.h>
#include <string.h>

#define RESULT_INVALID_HANDLE UINT32_C(0xd8e007f7)
#define RESULT_TIMEOUT UINT32_C(0x09401bfe)
#define RESULT_NO_MEMORY UINT32_C(0xd86007f3)
#define STACK_TOP UINT32_C(0x10000000)
#define TLS_ADDRESS UINT32_C(0x1ff82000)

const char *ctr_guest_state_name(ctr_guest_state state) {
    static const char *const names[]={"ready","yielded","sleeping","waiting","exited","faulted"};
    return (unsigned)state<sizeof(names)/sizeof(names[0])?names[state]:"unknown";
}
void ctr_guest_close(ctr_guest *guest) {
    if (guest->executor_close) guest->executor_close(guest->executor_context);
    for (unsigned i=0;i<5;++i) free(guest->storage[i]);
    for (unsigned i=0;i<CTR_GUEST_MAX_ALLOCATIONS;++i) free(guest->allocations[i].storage);
    for (unsigned i=0;i<8;++i) if (guest->shared[i].owned) free(guest->shared[i].storage);
    memset(guest,0,sizeof(*guest)); guest->state=CTR_GUEST_EXITED;
}
static bool region(ctr_guest *g,unsigned index,uint32_t address,uint32_t size,unsigned permissions,
                   const uint8_t *source,size_t source_size) {
    if (!size) return source_size==0;
    if (source_size>size) return false;
    g->storage[index]=calloc(1,size); g->storage_size[index]=size;
    if (!g->storage[index]) return false;
    if (source_size) memcpy(g->storage[index],source,source_size);
    return ctr_memory_map(&g->memory,address,size,permissions,g->storage[index],size)==CTR_MEM_OK;
}
bool ctr_guest_open(ctr_guest *g,const ctr_guest_image *image,const ctr_block *blocks,size_t block_count) {
    memset(g,0,sizeof(*g)); g->state=CTR_GUEST_FAULTED; g->fault=CTR_CPU_MEMORY_FAULT;
    if (!image || !image->code || !image->module_hash || !image->text.size || image->text.size%4 || !image->stack_size ||
        image->stack_size%CTR_GUEST_PAGE || image->stack_size>STACK_TOP || (!blocks && block_count)) return false;
    ctr_guest_segment segments[]={image->text,image->rodata,image->data};
    uint64_t total=image->stack_size+CTR_GUEST_PAGE, code_pages=0;
    for (unsigned i=0;i<3;++i) {
        const ctr_guest_segment *s=&segments[i];
        uint64_t bytes=(uint64_t)s->pages*CTR_GUEST_PAGE;
        if (s->address%CTR_GUEST_PAGE || bytes>CTR_GUEST_MAX_BYTES || s->size>bytes ||
            s->pages!=(uint64_t)(s->size+UINT64_C(4095))/CTR_GUEST_PAGE) return false;
        total+=bytes; code_pages+=bytes;
    }
    /* Until non-page-aligned BSS layouts are proven, reject their ambiguity. */
    if (image->bss_size && image->data.size!=(uint64_t)image->data.pages*CTR_GUEST_PAGE) return false;
    uint64_t bss=((uint64_t)image->bss_size+4095)&~UINT64_C(4095);
    total+=bss;
    uint64_t preceding=((uint64_t)image->text.pages+image->rodata.pages)*CTR_GUEST_PAGE;
    if (total>CTR_GUEST_MAX_BYTES || image->code_size<preceding+image->data.size || image->code_size>code_pages) return false;
    ctr_memory_init(&g->memory);
    size_t offset=0;
    for (unsigned i=0;i<3;++i) {
        size_t size=(size_t)segments[i].pages*CTR_GUEST_PAGE;
        size_t available=image->code_size>offset?image->code_size-offset:0;
        if (available>size) available=size;
        uint64_t allocation=size+(i==2?bss:0);
        if (allocation>UINT32_MAX || !region(g,i,segments[i].address,(uint32_t)allocation,
            i==0?CTR_MEM_READ|CTR_MEM_EXEC:i==1?CTR_MEM_READ:CTR_MEM_READ|CTR_MEM_WRITE,
            image->code+offset,available)) goto failure;
        offset+=size;
    }
    if (!region(g,3,STACK_TOP-image->stack_size,image->stack_size,CTR_MEM_READ|CTR_MEM_WRITE,NULL,0) ||
        !region(g,4,TLS_ADDRESS,CTR_GUEST_PAGE,CTR_MEM_READ|CTR_MEM_WRITE,NULL,0)) goto failure;
    ctr_cpu_init(&g->cpu,&g->memory); g->cpu.module_hash=image->module_hash;
    g->cpu.r[13]=STACK_TOP; g->cpu.r[15]=image->text.address; g->cpu.cpsr=0x10; /* User ARM mode. */
    g->program=(ctr_program){blocks,block_count,image->module_hash,g->memory.code_generation};
    g->tls_address=TLS_ADDRESS; g->state=CTR_GUEST_READY; g->fault=CTR_CPU_OK;
    g->cpu.tls_address=TLS_ADDRESS;
    g->configuration=(ctr_guest_configuration){1,1,0}; /* Explicit diagnostic defaults: USA, English, Old 3DS. */
    return true;
failure:
    ctr_guest_close(g); g->state=CTR_GUEST_FAULTED; g->fault=CTR_CPU_MEMORY_FAULT; return false;
}
ctr_guest_handle *ctr_guest_handle_for(ctr_guest *g,uint32_t handle) {
    unsigned slot=(handle&255u);
    if (!slot || slot>64) return NULL;
    ctr_guest_handle *h=&g->handles[slot-1];
    return h->active && h->generation==(handle>>8)?h:NULL;
}
static int object_for(ctr_guest *g,uint32_t handle) {
    ctr_guest_handle *h=ctr_guest_handle_for(g,handle);
    return h && h->type==CTR_HANDLE_EVENT?(int)h->object:-1;
}
void ctr_guest_object_retain(ctr_guest *g,ctr_guest_handle_type type,unsigned object) {
    if (type==CTR_HANDLE_EVENT) ++g->events[object].references;
    else if (type==CTR_HANDLE_SESSION) ++g->sessions[object].references;
    else if (type==CTR_HANDLE_SHARED) ++g->shared[object].references;
    else if (type==CTR_HANDLE_MUTEX) ++g->mutexes[object].references;
    else if (type==CTR_HANDLE_SEMAPHORE) ++g->semaphores[object].references;
}
uint32_t ctr_guest_handle_new(ctr_guest *g,ctr_guest_handle_type type,unsigned object) {
    for (unsigned i=0;i<64;++i) {
        ctr_guest_handle *h=&g->handles[i];
        if (!h->active && h->generation<0x00ffffffu) {
            ++h->generation; h->active=true; h->object=object;h->type=type;
            ctr_guest_object_retain(g,type,object);
            return (h->generation<<8)|(i+1);
        }
    }
    return 0;
}
void ctr_guest_object_release(ctr_guest *g,ctr_guest_handle_type type,unsigned object) {
    if (type==CTR_HANDLE_SHARED) {
        ctr_guest_shared *s=&g->shared[object];
        if (s->references && --s->references==0) {
            if (s->owned) free(s->storage);
            else if (g->allocations[s->allocation].pins) --g->allocations[s->allocation].pins;
            memset(s,0,sizeof(*s));
        }
        return;
    }
    if (type==CTR_HANDLE_MUTEX) {
        ctr_guest_mutex *m=&g->mutexes[object];
        if (m->references && --m->references==0) memset(m,0,sizeof(*m));
        return;
    }
    if (type==CTR_HANDLE_SEMAPHORE) {
        ctr_guest_semaphore *s=&g->semaphores[object];
        if (s->references && --s->references==0) memset(s,0,sizeof(*s));
        return;
    }
    if (type==CTR_HANDLE_SESSION) {
        ctr_guest_session *s=&g->sessions[object];
        if (s->references && --s->references==0) {
            bool gsp=s->service==CTR_SERVICE_GSP;memset(s,0,sizeof(*s));
            if(gsp) {
                bool live=false;for(unsigned i=0;i<16;++i)if(g->sessions[i].active && g->sessions[i].service==CTR_SERVICE_GSP)live=true;
                if(!live)g->gsp.right=false;
            }
        }
        return;
    }
    ctr_guest_event *e=&g->events[object];
    if (e->references && --e->references==0) memset(e,0,sizeof(*e));
}
uint32_t ctr_guest_session_open(ctr_guest *g,ctr_guest_service service) {
    if (service!=CTR_SERVICE_SRV && service!=CTR_SERVICE_CFG && service!=CTR_SERVICE_HID && service!=CTR_SERVICE_GSP) return 0;
    for (unsigned i=0;i<16;++i) if (!g->sessions[i].active) {
        g->sessions[i]=(ctr_guest_session){true,false,0,service};
        uint32_t handle=ctr_guest_handle_new(g,CTR_HANDLE_SESSION,i);
        if (!handle) memset(&g->sessions[i],0,sizeof(g->sessions[i]));
        return handle;
    }
    return 0;
}
uint32_t ctr_guest_event_create(ctr_guest *g,unsigned reset) {
    if (reset>1) return 0;
    for (unsigned i=0;i<32;++i) if (!g->events[i].active) {
        g->events[i]=(ctr_guest_event){true,false,reset,0};
        uint32_t handle=ctr_guest_handle_new(g,CTR_HANDLE_EVENT,i);
        if (!handle) memset(&g->events[i],0,sizeof(g->events[i]));
        return handle;
    }
    return 0;
}
bool ctr_guest_handle_close(ctr_guest *g,uint32_t handle) {
    ctr_guest_handle *h=ctr_guest_handle_for(g,handle);
    if (!h) return false;
    h->active=false;ctr_guest_object_release(g,h->type,h->object);return true;
}
ctr_guest_session *ctr_guest_session_for(ctr_guest *g,uint32_t handle) {
    ctr_guest_handle *h=ctr_guest_handle_for(g,handle);
    return h && h->type==CTR_HANDLE_SESSION?&g->sessions[h->object]:NULL;
}
bool ctr_guest_configure(ctr_guest *g,ctr_guest_configuration configuration) {
    if (g->state!=CTR_GUEST_READY || g->svc_count || g->cpu.instructions ||
        configuration.region>6 || configuration.language>11 ||
        (configuration.model!=CTR_GUEST_MODEL_3DS && configuration.model!=CTR_GUEST_MODEL_3DSXL &&
         configuration.model!=CTR_GUEST_MODEL_2DS)) return false;
    for (unsigned i=0;i<16;++i) if (g->sessions[i].active) return false;
    g->configuration=configuration;return true;
}
bool ctr_guest_signal(ctr_guest *g,uint32_t handle) {
    int object=object_for(g,handle);
    if (object<0) return false;
    ctr_guest_event *e=&g->events[object];
    e->signaled=true;
    ctr_sync_poll(g);
    return true;
}
static ctr_guest_state unsupported(ctr_guest *g,uint32_t number) {
    g->fault=CTR_CPU_UNSUPPORTED; g->fault_detail=number; g->state=CTR_GUEST_FAULTED;
    ctr_cpu_stop(&g->cpu,CTR_CPU_UNSUPPORTED,number);
    return g->state;
}
ctr_guest_state ctr_guest_svc(ctr_guest *g,uint32_t number) {
    uint32_t *r=g->cpu.r; ++g->svc_count;
    switch (number) {
        case 0x01: ctr_heap_control(g);break;
        case 0x02: ctr_heap_query(g);break;
        case 0x13: case 0x14: case 0x15: case 0x16: case 0x24: case 0x25:
            ctr_sync_svc(g,number);break;
        case 0x1e: case 0x1f: case 0x20: ctr_shared_svc(g,number);break;
        case 0x03: g->state=CTR_GUEST_EXITED; break;
        case 0x0a: {
            uint64_t nanos=(uint64_t)r[0]|((uint64_t)r[1]<<32);
            if (nanos>>63 || UINT64_MAX-g->now_ns<nanos) return unsupported(g,number);
            r[0]=0; g->deadline_ns=g->now_ns+nanos;
            g->state=nanos?CTR_GUEST_SLEEPING:CTR_GUEST_YIELDED; break;
        }
        case 0x17: {
            if (r[1]>1) return unsupported(g,number); /* Pulse/multiple waiters not yet supported. */
            uint32_t handle=ctr_guest_event_create(g,r[1]);
            r[0]=handle?0:RESULT_NO_MEMORY; r[1]=handle; break;
        }
        case 0x18: r[0]=ctr_guest_signal(g,r[0])?0:RESULT_INVALID_HANDLE; break;
        case 0x19: {
            int object=object_for(g,r[0]);
            if (object>=0) g->events[object].signaled=false;
            r[0]=object>=0?0:RESULT_INVALID_HANDLE; break;
        }
        case 0x23: {
            r[0]=ctr_guest_handle_close(g,r[0])?0:RESULT_INVALID_HANDLE;break;
        }
        case 0x27: {
            ctr_guest_handle *h=ctr_guest_handle_for(g,r[1]);
            uint32_t handle=h?ctr_guest_handle_new(g,h->type,h->object):0;
            r[0]=!h?RESULT_INVALID_HANDLE:handle?0:RESULT_NO_MEMORY; r[1]=handle; break;
        }
        case 0x28: {
            uint64_t ticks=(g->now_ns/1000000000u)*UINT64_C(268111856)+
                           ((g->now_ns%1000000000u)*UINT64_C(268111856))/1000000000u;
            r[0]=(uint32_t)ticks; r[1]=(uint32_t)(ticks>>32); break;
        }
        case 0x2d: ctr_ipc_connect(g);break;
        case 0x32: ctr_ipc_request(g);break;
        case 0x3d: {
            char message[257];
            if (r[1]>256 || (uint64_t)r[0]+r[1]>UINT64_C(0x100000000)) return unsupported(g,number);
            for (uint32_t i=0;i<r[1];++i) {
                uint32_t value;
                ctr_mem_result result=ctr_memory_read(&g->memory,r[0]+i,1,CTR_MEM_READ,&value);
                if (result!=CTR_MEM_OK) { g->cpu.memory_fault=result; g->fault=CTR_CPU_MEMORY_FAULT; g->fault_detail=r[0]+i; g->state=CTR_GUEST_FAULTED; return g->state; }
                message[i]=(char)value;
            }
            message[r[1]]=0;
            if (g->debug) g->debug(g->debug_context,message);
            r[0]=0;
            break;
        }
        default: return unsupported(g,number);
    }
    g->cpu.exclusive_valid=false;
    return g->state;
}
ctr_guest_state ctr_guest_run(ctr_guest *g,uint64_t budget,uint64_t now_ns,bool translated) {
    if (g->state==CTR_GUEST_EXITED || g->state==CTR_GUEST_FAULTED) return g->state;
    if (now_ns<g->now_ns) return unsupported(g,0xffffffffu);
    g->now_ns=now_ns;
    if (g->state==CTR_GUEST_SLEEPING) {
        if (now_ns<g->deadline_ns) return g->state;
        g->state=CTR_GUEST_READY;
    }
    if (g->state==CTR_GUEST_WAITING) {
        ctr_sync_poll(g);if (g->state==CTR_GUEST_WAITING) return g->state;
    }
    if (g->state==CTR_GUEST_YIELDED) g->state=CTR_GUEST_READY;
    uint64_t start=g->cpu.instructions;
    while (g->cpu.instructions-start<budget) {
        uint64_t remaining=budget-(g->cpu.instructions-start);
        ctr_cpu_result result=translated?(g->executor?g->executor(&g->cpu,g->executor_context,remaining):
            ctr_cpu_dispatch(&g->cpu,&g->program,remaining,false)):ctr_cpu_run(&g->cpu,remaining);
        if (result==CTR_CPU_LIMIT) return g->state;
        if (result!=CTR_CPU_SVC) {
            g->fault=result; g->fault_detail=g->cpu.r[15]; g->state=CTR_GUEST_FAULTED; return g->state;
        }
        ctr_guest_svc(g,g->cpu.svc_number);
        if (g->state!=CTR_GUEST_READY) return g->state;
    }
    return g->state;
}
