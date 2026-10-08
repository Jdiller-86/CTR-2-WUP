/* Original single-process shared-object and synchronization implementation. */
#include "ctr_kernel.h"
#include "ctr_heap.h"
#include <stdlib.h>
#include <string.h>
#define BAD_HANDLE UINT32_C(0xd8e007f7)
#define TIMEOUT UINT32_C(0x09401bfe)
#define NO_MEMORY UINT32_C(0xd86007f3)
#define BAD_COMBINATION UINT32_C(0xe0e01bee)
#define BAD_ADDRESS UINT32_C(0xe0e01bf5)
#define BAD_STATE UINT32_C(0xe0a01bf5)
#define DONTCARE UINT32_C(0x10000000)

static unsigned free_shared(ctr_guest *g) {
    for (unsigned i=0;i<8;++i) if (!g->shared[i].active) return i;
    return 8;
}
uint32_t ctr_shared_create(ctr_guest *g,uint32_t size,unsigned permissions) {
    unsigned slot=free_shared(g);uint64_t bytes=size;
    if (!size || size%CTR_GUEST_PAGE || permissions&~3u || slot==8) return 0;
    for (unsigned i=0;i<5;++i) bytes+=g->storage_size[i];
    for (unsigned i=0;i<CTR_GUEST_MAX_ALLOCATIONS;++i) if (g->allocations[i].storage) bytes+=g->allocations[i].size;
    for (unsigned i=0;i<8;++i) if (g->shared[i].owned) bytes+=g->shared[i].size;
    if (bytes>CTR_GUEST_MAX_BYTES) return 0;
    uint8_t *storage=calloc(1,size);if (!storage) return 0;
    ctr_guest_shared *s=&g->shared[slot];
    *s=(ctr_guest_shared){.active=true,.owned=true,.storage=storage,.size=size,
                         .own_permissions=permissions,.other_permissions=permissions};
    uint32_t handle=ctr_guest_handle_new(g,CTR_HANDLE_SHARED,slot);
    if (!handle) { free(storage);memset(s,0,sizeof(*s)); }
    return handle;
}
const ctr_guest_shared *ctr_shared_mapping(const ctr_guest *g,const ctr_mapping *m) {
    for (unsigned i=0;i<8;++i) {
        const ctr_guest_shared *s=&g->shared[i];
        if (!s->active || s->storage!=m->storage || s->size!=m->size) continue;
        for (unsigned j=0;j<4;++j) if (s->mappings[j]==m->base) return s;
    }
    return NULL;
}
void ctr_shared_svc(ctr_guest *g,uint32_t number) {
    uint32_t *r=g->cpu.r,result=0,handle=r[0],address=r[1];
    ++g->memory_requests;g->memory_operation=number;g->memory_address=address;
    if (number==0x1e) {
        uint32_t other=r[0],size=r[2],own=r[3];handle=0;
        if (address%CTR_GUEST_PAGE) result=0xe0e01bf1;
        else if (!size || size%CTR_GUEST_PAGE) result=0xe0e01bf2;
        else if ((own|other)&~3u) result=BAD_COMBINATION;
        else if (!address) {
            handle=ctr_shared_create(g,size,own);result=handle?0:NO_MEMORY;
            if (handle) g->shared[ctr_guest_handle_for(g,handle)->object].other_permissions=other;
        } else {
            unsigned slot=free_shared(g),allocation=CTR_GUEST_MAX_ALLOCATIONS;
            for (unsigned i=0;i<CTR_GUEST_MAX_ALLOCATIONS;++i) {
                const ctr_guest_allocation *a=&g->allocations[i];
                if (a->storage && address>=a->base && (uint64_t)address+size<=(uint64_t)a->base+a->size) { allocation=i;break; }
            }
            if (allocation==CTR_GUEST_MAX_ALLOCATIONS ||
                ctr_memory_check_range(&g->memory,address,size,3)!=CTR_MEM_OK) result=BAD_STATE;
            else if (slot==8) result=NO_MEMORY;
            else {
                ctr_guest_allocation *a=&g->allocations[allocation];
                ctr_guest_shared *s=&g->shared[slot];
                *s=(ctr_guest_shared){.active=true,.allocation=allocation,.storage=a->storage+(address-a->base),
                                     .size=size,.own_permissions=own,.other_permissions=other};
                handle=ctr_guest_handle_new(g,CTR_HANDLE_SHARED,slot);
                if (handle) ++a->pins;
                else { memset(s,0,sizeof(*s));result=NO_MEMORY; }
            }
        }
        r[1]=handle;
    } else {
        ctr_guest_handle *h=ctr_guest_handle_for(g,handle);
        if (!h || h->type!=CTR_HANDLE_SHARED) result=BAD_HANDLE;
        else if (address%CTR_GUEST_PAGE) result=0xe0e01bf1;
        else {
            ctr_guest_shared *s=&g->shared[h->object];
            if (number==0x1f) {
                unsigned own=r[2],other=r[3],slot=4;
                for (unsigned i=0;i<4;++i) if (!s->mappings[i]) { slot=i;break; }
                if (address<0x10000000 || (uint64_t)address+s->size>0x14000000) result=BAD_ADDRESS;
                else if ((own&~s->own_permissions) || (other!=DONTCARE && other!=s->other_permissions)) result=BAD_COMBINATION;
                else if (slot==4) result=NO_MEMORY;
                else {
                    ctr_memory next=g->memory;
                    ctr_mem_result mapped=ctr_memory_map(&next,address,s->size,own?own:CTR_MEM_READ,s->storage,s->size);
                    if (mapped==CTR_MEM_OK && !own) mapped=ctr_memory_change_range(&next,address,s->size,0,false);
                    result=mapped==CTR_MEM_OK?0:mapped==CTR_MEM_CAPACITY?NO_MEMORY:BAD_STATE;
                    if (!result) { g->memory=next;s->mappings[slot]=address;ctr_guest_object_retain(g,CTR_HANDLE_SHARED,h->object); }
                }
            } else {
                unsigned slot=4;
                for (unsigned i=0;i<4;++i) if (s->mappings[i]==address) { slot=i;break; }
                if (slot==4 || ctr_memory_change_range(&g->memory,address,s->size,0,true)!=CTR_MEM_OK) result=BAD_STATE;
                else { s->mappings[slot]=0;ctr_guest_object_release(g,CTR_HANDLE_SHARED,h->object); }
            }
        }
    }
    r[0]=result;g->memory_result=result;if (result) ++g->memory_failures;
}
static bool ready(const ctr_guest *g,ctr_guest_handle_type type,unsigned object) {
    if (type==CTR_HANDLE_EVENT) return g->events[object].signaled;
    if (type==CTR_HANDLE_MUTEX) return g->mutexes[object].depth<UINT32_MAX;
    if (type==CTR_HANDLE_SEMAPHORE) return g->semaphores[object].count!=0;
    return false;
}
static void acquire(ctr_guest *g,ctr_guest_handle_type type,unsigned object) {
    if (type==CTR_HANDLE_EVENT) { if (!g->events[object].reset) g->events[object].signaled=false; }
    else if (type==CTR_HANDLE_MUTEX) ++g->mutexes[object].depth; /* Same sole guest thread; recursive acquisition. */
    else if (type==CTR_HANDLE_SEMAPHORE) --g->semaphores[object].count;
}
static void finish(ctr_guest *g,uint32_t result,uint32_t index) {
    g->cpu.r[0]=result;if (g->wait_multiple) g->cpu.r[1]=index;
    g->state=CTR_GUEST_READY;
    for (unsigned i=0;i<g->wait_count;++i) ctr_guest_object_release(g,g->wait_types[i],g->wait_objects[i]);
    g->wait_count=0;
}
void ctr_sync_poll(ctr_guest *g) {
    if (g->state!=CTR_GUEST_WAITING) return;
    if (g->wait_all) {
        bool all=true;
        for (unsigned i=0;i<g->wait_count;++i) if (!ready(g,g->wait_types[i],g->wait_objects[i])) all=false;
        if (all) {
            for (unsigned i=0;i<g->wait_count;++i) acquire(g,g->wait_types[i],g->wait_objects[i]);
            finish(g,0,UINT32_MAX);return;
        }
    } else for (unsigned i=0;i<g->wait_count;++i) if (ready(g,g->wait_types[i],g->wait_objects[i])) {
        acquire(g,g->wait_types[i],g->wait_objects[i]);finish(g,0,i);return;
    }
    if (!g->infinite_wait && g->now_ns>=g->deadline_ns) finish(g,TIMEOUT,UINT32_MAX);
}
void ctr_sync_svc(ctr_guest *g,uint32_t number) {
    uint32_t *r=g->cpu.r;
    if (number==0x13 || number==0x15) {
        unsigned slot=16;uint32_t handle=0;
        bool mutex=number==0x13;
        if ((mutex && r[1]>1) || (!mutex && (!r[2] || r[2]>INT32_MAX || r[1]>r[2]))) {
            r[0]=BAD_COMBINATION;r[1]=0;return;
        }
        for (unsigned i=0;i<16;++i) if (mutex?!g->mutexes[i].active:!g->semaphores[i].active) { slot=i;break; }
        if (slot<16) {
            if (mutex) g->mutexes[slot]=(ctr_guest_mutex){true,0,r[1]};
            else g->semaphores[slot]=(ctr_guest_semaphore){true,0,r[1],r[2]};
            handle=ctr_guest_handle_new(g,mutex?CTR_HANDLE_MUTEX:CTR_HANDLE_SEMAPHORE,slot);
            if (!handle) {
                if (mutex) memset(&g->mutexes[slot],0,sizeof(g->mutexes[slot]));
                else memset(&g->semaphores[slot],0,sizeof(g->semaphores[slot]));
            }
        }
        r[0]=handle?0:NO_MEMORY;r[1]=handle;return;
    }
    if (number==0x14 || number==0x16) {
        uint32_t handle=number==0x14?r[0]:r[1],amount=r[2];
        ctr_guest_handle *h=ctr_guest_handle_for(g,handle);
        if (!h || h->type!=(number==0x14?CTR_HANDLE_MUTEX:CTR_HANDLE_SEMAPHORE)) { r[0]=BAD_HANDLE;return; }
        if (number==0x14) {
            ctr_guest_mutex *m=&g->mutexes[h->object];
            if (!m->depth) r[0]=BAD_STATE;
            else { --m->depth;r[0]=0; }
        } else {
            ctr_guest_semaphore *s=&g->semaphores[h->object];
            if (amount>INT32_MAX || (uint64_t)s->count+amount>s->maximum) { r[0]=BAD_COMBINATION;return; }
            r[1]=s->count;s->count+=amount;r[0]=0;
        }
        ctr_sync_poll(g);return;
    }
    bool multiple=number==0x25,all=multiple && r[3];unsigned count=multiple?r[2]:1;
    uint64_t nanos=multiple?(uint64_t)r[0]|((uint64_t)r[4]<<32):(uint64_t)r[2]|((uint64_t)r[3]<<32);
    uint32_t handles[16]={0};ctr_guest_handle_type types[16];unsigned objects[16];
    if (!count || count>16 || (multiple && r[3]>1)) { r[0]=BAD_COMBINATION;if (multiple) r[1]=UINT32_MAX;return; }
    if (multiple) {
        uint32_t address=r[1];
        ctr_mem_result checked=address%4?CTR_MEM_ALIGNMENT:ctr_memory_check_range(&g->memory,address,count*4,CTR_MEM_READ);
        if (checked!=CTR_MEM_OK) { r[0]=BAD_ADDRESS;r[1]=UINT32_MAX;return; }
        for (unsigned i=0;i<count;++i) {
            if (ctr_memory_read(&g->memory,address+i*4,4,CTR_MEM_READ,&handles[i])!=CTR_MEM_OK) { r[0]=BAD_ADDRESS;r[1]=UINT32_MAX;return; }
        }
    } else handles[0]=r[0];
    for (unsigned i=0;i<count;++i) {
        ctr_guest_handle *h=ctr_guest_handle_for(g,handles[i]);
        if (!h || (h->type!=CTR_HANDLE_EVENT && h->type!=CTR_HANDLE_MUTEX && h->type!=CTR_HANDLE_SEMAPHORE)) {
            r[0]=BAD_HANDLE;if (multiple) r[1]=UINT32_MAX;return;
        }
        types[i]=h->type;objects[i]=h->object;
        if (all) for (unsigned j=0;j<i;++j) if (types[j]==types[i] && objects[j]==objects[i]) {
            r[0]=BAD_COMBINATION;r[1]=UINT32_MAX;return;
        }
    }
    if (!(nanos>>63) && UINT64_MAX-g->now_ns<nanos) {
        r[0]=BAD_COMBINATION;if (multiple) r[1]=UINT32_MAX;return;
    }
    g->wait_count=count;g->wait_all=all;g->wait_multiple=multiple;g->wait_object=objects[0];
    g->infinite_wait=(nanos>>63)!=0;g->deadline_ns=g->now_ns+nanos;
    for (unsigned i=0;i<count;++i) {
        g->wait_types[i]=types[i];g->wait_objects[i]=objects[i];ctr_guest_object_retain(g,types[i],objects[i]);
    }
    g->state=CTR_GUEST_WAITING;ctr_sync_poll(g);
}
