#include "ctr_heap.h"
#include "ctr_kernel.h"
#include <stdlib.h>
#include <string.h>

#define MISALIGNED_ADDRESS UINT32_C(0xe0e01bf1)
#define MISALIGNED_SIZE UINT32_C(0xe0e01bf2)
#define INVALID_ADDRESS UINT32_C(0xe0e01bf5)
#define INVALID_STATE UINT32_C(0xe0a01bf5)
#define INVALID_COMBINATION UINT32_C(0xe0e01bee)
#define OUT_OF_MEMORY UINT32_C(0xd86007f3)

static const ctr_mapping *mapping_at(const ctr_memory *memory,uint32_t address) {
    for (size_t i=0;i<memory->count;++i) {
        const ctr_mapping *m=&memory->maps[i];
        if (address>=m->base && (uint64_t)address<(uint64_t)m->base+m->size) return m;
    }
    return NULL;
}
static const ctr_guest_allocation *owner(const ctr_guest *g,const ctr_mapping *m) {
    for (unsigned i=0;i<CTR_GUEST_MAX_ALLOCATIONS;++i) {
        const ctr_guest_allocation *a=&g->allocations[i];
        if (a->storage && m->base>=a->base && (uint64_t)m->base+m->size<=(uint64_t)a->base+a->size &&
            m->storage==a->storage+(m->base-a->base)) return a;
    }
    return NULL;
}
static bool owned_range(const ctr_guest *g,uint32_t address,uint32_t size) {
    uint64_t at=address,end=at+size;
    while (at<end) {
        const ctr_mapping *m=mapping_at(&g->memory,(uint32_t)at);
        const ctr_guest_allocation *a=m?owner(g,m):NULL;
        if (!a || a->pins) return false;
        uint64_t next=(uint64_t)m->base+m->size;at=next<end?next:end;
    }
    return true;
}
static uint32_t available(const ctr_memory *memory,uint32_t begin,uint32_t limit,uint32_t size) {
    uint64_t at=begin;
    while (at+size<=limit) {
        uint64_t next=at;
        for (size_t i=0;i<memory->count;++i) {
            const ctr_mapping *m=&memory->maps[i];uint64_t end=(uint64_t)m->base+m->size;
            if (at<end && m->base<at+size && end>next) next=end;
        }
        if (next==at) return (uint32_t)at;
        at=(next+CTR_GUEST_PAGE-1)&~(uint64_t)(CTR_GUEST_PAGE-1);
    }
    return 0;
}
static uint32_t allocate(ctr_guest *g,uint32_t *address,uint32_t size,unsigned permissions,bool linear) {
    uint32_t begin=linear?CTR_LINEAR_BASE:CTR_HEAP_BASE,limit=linear?CTR_LINEAR_END:CTR_HEAP_END;
    if (*address && (*address<begin || (uint64_t)*address+size>limit)) return INVALID_ADDRESS;
    uint32_t base=*address?*address:available(&g->memory,begin,limit,size);
    if (!base) return OUT_OF_MEMORY;
    if (available(&g->memory,base,base+size,size)!=base) return INVALID_STATE;
    uint64_t bytes=size;unsigned slot=CTR_GUEST_MAX_ALLOCATIONS;
    for (unsigned i=0;i<CTR_GUEST_STORAGE_COUNT;++i) bytes+=g->storage_size[i];
    for (unsigned i=0;i<CTR_GUEST_MAX_ALLOCATIONS;++i) {
        if (g->allocations[i].storage) bytes+=g->allocations[i].size;
        else if (slot==CTR_GUEST_MAX_ALLOCATIONS) slot=i;
    }
    for (unsigned i=0;i<8;++i) if (g->shared[i].owned) bytes+=g->shared[i].size;
    if (bytes>CTR_GUEST_MAX_BYTES || slot==CTR_GUEST_MAX_ALLOCATIONS ||
        g->memory.count==CTR_MEMORY_MAX_MAPS) return OUT_OF_MEMORY;
    uint8_t *storage=calloc(1,size);if (!storage) return OUT_OF_MEMORY;
    ctr_memory next=g->memory;
    ctr_mem_result result=ctr_memory_map(&next,base,size,permissions?permissions:CTR_MEM_READ,storage,size);
    if (result==CTR_MEM_OK && !permissions) result=ctr_memory_change_range(&next,base,size,0,false);
    if (result!=CTR_MEM_OK) { free(storage);return result==CTR_MEM_CAPACITY?OUT_OF_MEMORY:INVALID_STATE; }
    g->allocations[slot]=(ctr_guest_allocation){base,size,storage,linear,0};
    g->memory=next;*address=base;return 0;
}
static void reap(ctr_guest *g) {
    for (unsigned i=0;i<CTR_GUEST_MAX_ALLOCATIONS;++i) {
        ctr_guest_allocation *a=&g->allocations[i];if (!a->storage) continue;
        bool used=false;uintptr_t begin=(uintptr_t)a->storage,end=begin+a->size;
        for (size_t j=0;j<g->memory.count;++j) {
            const ctr_mapping *m=&g->memory.maps[j];uintptr_t at=(uintptr_t)m->storage;
            if (at<end && begin<at+m->size) { used=true;break; }
        }
        if (!used) { free(a->storage);memset(a,0,sizeof(*a)); }
    }
}
static void unsupported(ctr_guest *g) {
    g->fault=CTR_CPU_UNSUPPORTED;g->fault_detail=1;g->state=CTR_GUEST_FAULTED;
    ctr_cpu_stop(&g->cpu,CTR_CPU_UNSUPPORTED,1);
}
void ctr_heap_control(ctr_guest *g) {
    uint32_t *r=g->cpu.r,op=r[0],address=r[1],source=r[2],size=r[3],permissions=r[4],result=0;
    ++g->memory_requests;g->memory_operation=op;g->memory_address=address;
    unsigned operation=op&255u;bool linear=(op&0x10000u)!=0;
    if ((address|source)&(CTR_GUEST_PAGE-1)) result=MISALIGNED_ADDRESS;
    else if (!size || (size&(CTR_GUEST_PAGE-1))) result=MISALIGNED_SIZE;
    else if (permissions&~3u) result=INVALID_COMBINATION;
    else if ((op&~UINT32_C(0x101ff)) || (linear && operation!=3) ||
             (operation!=1 && operation!=3 && operation!=6)) {
        result=INVALID_COMBINATION;unsupported(g);
    } else if (source) result=INVALID_COMBINATION; /* Aliases have a separate unsupported contract. */
    else if (operation==3) result=allocate(g,&address,size,permissions,linear);
    else if ((uint64_t)address+size>CTR_PROCESS_END) result=INVALID_ADDRESS;
    else if (!owned_range(g,address,size)) result=INVALID_STATE;
    else if (operation==1 && permissions) result=INVALID_COMBINATION;
    else {
        ctr_mem_result changed=ctr_memory_change_range(&g->memory,address,size,permissions,operation==1);
        result=changed==CTR_MEM_OK?0:changed==CTR_MEM_CAPACITY?OUT_OF_MEMORY:INVALID_STATE;
        if (!result && operation==1) reap(g);
    }
    r[0]=result;r[1]=result?0:address;
    g->memory_result=result;g->memory_address=address;
    if (result) ++g->memory_failures;
}
static unsigned state_of(const ctr_guest *g,const ctr_mapping *m) {
    const ctr_guest_allocation *a=owner(g,m);
    if (ctr_shared_mapping(g,m)) return 6u;
    if (a) return a->linear?7u:5u; /* Continuous / Private. */
    for (unsigned i=0;i<CTR_GUEST_STORAGE_COUNT;++i) if (g->storage[i] && m->storage==g->storage[i])
        return i<2?4u:i>=3?11u:5u; /* Code / Locked stack and TLS / Private data. */
    return 3u; /* Static host-provided mapping; no physical-page information. */
}
void ctr_heap_query(ctr_guest *g) {
    uint32_t address=g->cpu.r[2],begin=0,end=CTR_PROCESS_END,permissions=0,state=0;
    ++g->memory_requests;g->memory_operation=2;g->memory_address=address;
    memset(g->cpu.r,0,6*sizeof(g->cpu.r[0]));
    if (address>=CTR_PROCESS_END) {
        g->cpu.r[0]=INVALID_ADDRESS;g->memory_result=INVALID_ADDRESS;++g->memory_failures;return;
    }
    const ctr_mapping *m=mapping_at(&g->memory,address);
    if (m) {
        uint64_t stop=(uint64_t)m->base+m->size;
        begin=m->base;end=stop>CTR_PROCESS_END?CTR_PROCESS_END:(uint32_t)stop;
        permissions=m->permissions;state=state_of(g,m);
        /* Query a logical region, including fragments with identical state/permissions. */
        bool again=true;
        while (again) {
            again=false;
            for (size_t i=0;i<g->memory.count;++i) {
                const ctr_mapping *other=&g->memory.maps[i];
                if (other->base>=CTR_PROCESS_END) continue;
                if (other->permissions!=permissions || state_of(g,other)!=state) continue;
                if ((uint64_t)other->base+other->size==begin) { begin=other->base;again=true; }
                else if (other->base==end) {
                    uint64_t next=(uint64_t)other->base+other->size;
                    end=next>CTR_PROCESS_END?CTR_PROCESS_END:(uint32_t)next;again=true;
                }
            }
        }
    } else for (size_t i=0;i<g->memory.count;++i) {
        const ctr_mapping *other=&g->memory.maps[i];uint64_t stop=(uint64_t)other->base+other->size;
        if (stop<=address && stop>begin) begin=(uint32_t)stop;
        if (other->base>address && other->base<end) end=other->base;
    }
    g->cpu.r[1]=begin;g->cpu.r[2]=end-begin;g->cpu.r[3]=permissions;g->cpu.r[4]=state;
    g->memory_result=0;
}

/* Report the actual bounded runtime quotas used by libctru heap startup. */
void ctr_heap_resource_svc(ctr_guest *g,uint32_t number) {
    uint32_t *r=g->cpu.r;
    if(number==0x38) {
        uint32_t handle=r[1]==0xffff8001u?ctr_guest_handle_new(g,CTR_HANDLE_RESOURCE_LIMIT,0):0;
        r[0]=r[1]!=0xffff8001u?0xd8e007f7u:handle?0:OUT_OF_MEMORY;r[1]=handle;return;
    }
    ctr_guest_handle *h=ctr_guest_handle_for(g,r[1]);
    if(!h || h->type!=CTR_HANDLE_RESOURCE_LIMIT) { r[0]=0xd8e007f7u;return; }
    unsigned count=r[3];uint32_t types[16],output=r[0],input=r[2];
    if(!count || count>16) { r[0]=INVALID_COMBINATION;return; }
    if((output|input)&3u || ctr_memory_check_range(&g->memory,output,count*8u,CTR_MEM_WRITE)!=CTR_MEM_OK ||
        ctr_memory_check_range(&g->memory,input,count*4u,CTR_MEM_READ)!=CTR_MEM_OK) { r[0]=INVALID_ADDRESS;return; }
    for(unsigned i=0;i<count;++i) {
        ctr_memory_read(&g->memory,input+i*4u,4,CTR_MEM_READ,&types[i]);
        if(types[i]>8) { r[0]=INVALID_COMBINATION;return; }
    }
    uint64_t current[9]={0};const uint64_t limits[]={0x18,CTR_GUEST_MAX_BYTES,CTR_GUEST_MAX_THREADS,32,16,16,0,8,8};
    current[0]=0x18;
    for(unsigned i=0;i<CTR_GUEST_STORAGE_COUNT;++i)current[1]+=g->storage_size[i];
    for(unsigned i=0;i<CTR_GUEST_MAX_ALLOCATIONS;++i)if(g->allocations[i].storage)current[1]+=g->allocations[i].size;
    for(unsigned i=0;i<CTR_GUEST_MAX_THREADS;++i)current[2]+=g->threads[i].active && !g->threads[i].finished;
    for(unsigned i=0;i<32;++i)current[3]+=g->events[i].active;
    for(unsigned i=0;i<16;++i) { current[4]+=g->mutexes[i].active;current[5]+=g->semaphores[i].active; }
    for(unsigned i=0;i<8;++i) {
        current[7]+=g->shared[i].active;current[8]+=g->arbiters[i].active;
        if(g->shared[i].owned)current[1]+=g->shared[i].size;
    }
    for(unsigned i=0;i<count;++i) {
        uint64_t value=number==0x39?limits[types[i]]:current[types[i]];
        ctr_memory_write(&g->memory,output+i*8u,4,(uint32_t)value);
        ctr_memory_write(&g->memory,output+i*8u+4u,4,(uint32_t)(value>>32));
    }
    r[0]=0;
}
