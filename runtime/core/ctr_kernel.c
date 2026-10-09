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
    for (unsigned i=0;i<CTR_GUEST_STORAGE_COUNT;++i) bytes+=g->storage_size[i];
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
void ctr_shared_publish(ctr_guest *g,unsigned object,const uint8_t *bytes) {
    ctr_guest_shared *s=&g->shared[object];memcpy(s->storage,bytes,s->size);
    ++g->memory.generation;g->cpu.exclusive_valid=false;
    for(size_t i=0;i<g->memory.count;++i) {
        const ctr_mapping *m=&g->memory.maps[i];
        if((m->permissions&CTR_MEM_EXEC) && (uintptr_t)m->storage<(uintptr_t)s->storage+s->size &&
            (uintptr_t)s->storage<(uintptr_t)m->storage+m->size) { ++g->memory.code_generation;break; }
    }
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
    if (type==CTR_HANDLE_MUTEX) return !g->mutexes[object].depth ||
        (g->mutexes[object].owner==g->threads[g->current_thread].id && g->mutexes[object].depth<UINT32_MAX);
    if (type==CTR_HANDLE_SEMAPHORE) return g->semaphores[object].count!=0;
    if (type==CTR_HANDLE_THREAD) return g->threads[object].finished;
    return false;
}
static void acquire(ctr_guest *g,ctr_guest_handle_type type,unsigned object) {
    if (type==CTR_HANDLE_EVENT) { if (!g->events[object].reset) g->events[object].signaled=false; }
    else if (type==CTR_HANDLE_MUTEX) {
        ctr_guest_mutex *m=&g->mutexes[object];
        if(!m->depth)ctr_guest_object_retain(g,CTR_HANDLE_MUTEX,object);
        ++m->depth;m->owner=g->threads[g->current_thread].id;
    }
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
            if (mutex) g->mutexes[slot]=(ctr_guest_mutex){.active=true};
            else g->semaphores[slot]=(ctr_guest_semaphore){true,0,r[1],r[2]};
            handle=ctr_guest_handle_new(g,mutex?CTR_HANDLE_MUTEX:CTR_HANDLE_SEMAPHORE,slot);
            if(handle && mutex && r[1])acquire(g,CTR_HANDLE_MUTEX,slot);
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
            if (!m->depth || m->owner!=g->threads[g->current_thread].id) r[0]=BAD_STATE;
            else {
                --m->depth;r[0]=0;
                if(!m->depth) { m->owner=0;ctr_guest_object_release(g,CTR_HANDLE_MUTEX,h->object); }
            }
        } else {
            ctr_guest_semaphore *s=&g->semaphores[h->object];
            if (amount>INT32_MAX || (uint64_t)s->count+amount>s->maximum) { r[0]=BAD_COMBINATION;return; }
            r[1]=s->count;s->count+=amount;r[0]=0;
        }
        ctr_threads_poll(g);return;
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
        if (!h || (h->type!=CTR_HANDLE_EVENT && h->type!=CTR_HANDLE_MUTEX &&
                   h->type!=CTR_HANDLE_SEMAPHORE && h->type!=CTR_HANDLE_THREAD)) {
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

/* Context ownership is separate from handles: closing a running thread handle
   cannot kill it, and join waits retain terminated objects until completion. */
static void thread_capture(ctr_guest *g) {
    ctr_guest_thread *t=&g->threads[g->current_thread];
    if(!t->active)return;
    memcpy(t->r,g->cpu.r,sizeof(t->r));t->cpsr=g->cpu.cpsr;
    memcpy(t->vfp,g->cpu.vfp,sizeof(t->vfp));t->fpscr=g->cpu.fpscr;t->tls_address=g->cpu.tls_address;
    t->state=g->state;t->deadline_ns=g->deadline_ns;t->infinite_wait=g->infinite_wait;
    t->wait_all=g->wait_all;t->wait_multiple=g->wait_multiple;t->wait_count=g->wait_count;t->wait_object=g->wait_object;
    t->wait_address=g->wait_address;
    memcpy(t->wait_types,g->wait_types,sizeof(t->wait_types));memcpy(t->wait_objects,g->wait_objects,sizeof(t->wait_objects));
}
static void thread_restore(ctr_guest *g,unsigned slot) {
    const ctr_guest_thread *t=&g->threads[slot];g->current_thread=slot;
    memcpy(g->cpu.r,t->r,sizeof(t->r));g->cpu.cpsr=t->cpsr;
    memcpy(g->cpu.vfp,t->vfp,sizeof(t->vfp));g->cpu.fpscr=t->fpscr;
    g->cpu.tls_address=g->tls_address=t->tls_address;
    g->state=t->state;g->deadline_ns=t->deadline_ns;g->infinite_wait=t->infinite_wait;
    g->wait_all=t->wait_all;g->wait_multiple=t->wait_multiple;g->wait_count=t->wait_count;g->wait_object=t->wait_object;
    g->wait_address=t->wait_address;
    memcpy(g->wait_types,t->wait_types,sizeof(t->wait_types));memcpy(g->wait_objects,t->wait_objects,sizeof(t->wait_objects));
}
void ctr_threads_poll(ctr_guest *g) {
    if(!g->threads_enabled) { ctr_sync_poll(g);return; }
    unsigned original=g->current_thread;
    thread_capture(g);
    /* Preserve the active CPU even if it just exited and has no retained handle. */
    ctr_cpu saved_cpu=g->cpu;ctr_guest_thread saved_control={0};
    saved_control.state=g->state;saved_control.deadline_ns=g->deadline_ns;
    saved_control.infinite_wait=g->infinite_wait;saved_control.wait_all=g->wait_all;
    saved_control.wait_multiple=g->wait_multiple;saved_control.wait_count=g->wait_count;saved_control.wait_object=g->wait_object;
    saved_control.wait_address=g->wait_address;
    memcpy(saved_control.wait_types,g->wait_types,sizeof(g->wait_types));
    memcpy(saved_control.wait_objects,g->wait_objects,sizeof(g->wait_objects));uint32_t saved_tls=g->tls_address;
    for(unsigned n=0;n<CTR_GUEST_MAX_THREADS;++n) {
        unsigned slot=(original+n)%CTR_GUEST_MAX_THREADS;ctr_guest_thread *t=&g->threads[slot];
        if(!t->active || t->finished)continue;
        thread_restore(g,slot);
        if(g->state==CTR_GUEST_SLEEPING && g->now_ns>=g->deadline_ns)g->state=CTR_GUEST_READY;
        if(g->state==CTR_GUEST_YIELDED)g->state=CTR_GUEST_READY;
        ctr_sync_poll(g);thread_capture(g);
    }
    if(g->threads[original].active)thread_restore(g,original);
    else {
        g->current_thread=original;g->cpu=saved_cpu;g->tls_address=saved_tls;
        g->state=saved_control.state;g->deadline_ns=saved_control.deadline_ns;
        g->infinite_wait=saved_control.infinite_wait;g->wait_all=saved_control.wait_all;
        g->wait_multiple=saved_control.wait_multiple;g->wait_count=saved_control.wait_count;g->wait_object=saved_control.wait_object;
        g->wait_address=saved_control.wait_address;
        memcpy(g->wait_types,saved_control.wait_types,sizeof(g->wait_types));memcpy(g->wait_objects,saved_control.wait_objects,sizeof(g->wait_objects));
    }
}
bool ctr_thread_schedule(ctr_guest *g,bool rotate) {
    unsigned old=g->current_thread,best=CTR_GUEST_MAX_THREADS;int priority=INT32_MAX;
    ctr_threads_poll(g);
    for(unsigned n=0;n<CTR_GUEST_MAX_THREADS;++n) {
        unsigned slot=(old+n+(rotate?1u:0u))%CTR_GUEST_MAX_THREADS;
        const ctr_guest_thread *t=&g->threads[slot];
        if(t->active && !t->finished && t->state==CTR_GUEST_READY && t->priority<priority) { best=slot;priority=t->priority; }
    }
    bool runnable=best<CTR_GUEST_MAX_THREADS;
    if(!runnable) {
        for(unsigned n=0;n<CTR_GUEST_MAX_THREADS;++n) {
            unsigned slot=(old+n)%CTR_GUEST_MAX_THREADS;const ctr_guest_thread *t=&g->threads[slot];
            if(t->active && !t->finished) { best=slot;break; }
        }
    }
    if(best==CTR_GUEST_MAX_THREADS) { g->state=CTR_GUEST_EXITED;g->process_exited=true;return false; }
    if(best!=old) {
        thread_restore(g,best);g->cpu.exclusive_valid=false;
        ++g->context_switches;g->quantum_left=CTR_GUEST_THREAD_QUANTUM;
    }
    return runnable;
}
static void thread_finish(ctr_guest *g) {
    unsigned slot=g->current_thread;ctr_guest_thread *t=&g->threads[slot];uint32_t id=t->id;
    if(t->finished)return;
    for(unsigned i=0;i<g->wait_count;++i)ctr_guest_object_release(g,g->wait_types[i],g->wait_objects[i]);
    g->wait_count=0;g->state=CTR_GUEST_EXITED;t->finished=true;t->state=CTR_GUEST_EXITED;
    if(t->stack_allocation<CTR_GUEST_MAX_ALLOCATIONS && g->allocations[t->stack_allocation].pins)
        --g->allocations[t->stack_allocation].pins;
    t->stack_allocation=CTR_GUEST_MAX_ALLOCATIONS;
    for(unsigned i=0;i<16;++i) {
        ctr_guest_mutex *m=&g->mutexes[i];
        if(m->active && m->depth && m->owner==id) {
            m->depth=0;m->owner=0;ctr_guest_object_release(g,CTR_HANDLE_MUTEX,i);
        }
    }
    thread_capture(g);ctr_guest_object_release(g,CTR_HANDLE_THREAD,slot);
}
void ctr_threads_exit_process(ctr_guest *g) {
    if(!g->threads_enabled) { g->state=CTR_GUEST_EXITED;g->process_exited=true;return; }
    thread_capture(g);unsigned original=g->current_thread;ctr_cpu saved_cpu=g->cpu;uint32_t saved_tls=g->tls_address;
    for(unsigned i=0;i<CTR_GUEST_MAX_THREADS;++i)if(g->threads[i].active && !g->threads[i].finished) {
        thread_restore(g,i);thread_finish(g);
    }
    g->current_thread=original;g->cpu=saved_cpu;g->tls_address=saved_tls;g->wait_count=0;
    g->state=CTR_GUEST_EXITED;g->process_exited=true;
}
static ctr_guest_thread *thread_for(ctr_guest *g,uint32_t handle) {
    if(handle==CTR_GUEST_CURRENT_THREAD)return &g->threads[g->current_thread];
    ctr_guest_handle *h=ctr_guest_handle_for(g,handle);
    return h && h->type==CTR_HANDLE_THREAD?&g->threads[h->object]:NULL;
}
static void arbitrate(ctr_guest *g) {
    uint32_t *r=g->cpu.r,address=r[1],type=r[2],value=r[3];
    ctr_guest_handle *h=ctr_guest_handle_for(g,r[0]);
    if(!h || h->type!=CTR_HANDLE_ARBITER) { r[0]=BAD_HANDLE;return; }
    if(type>4) { r[0]=BAD_COMBINATION;return; }
    bool decrement=type==2 || type==4,timed=type>=3;
    if(address%4 || ctr_memory_check_range(&g->memory,address,4,decrement?3u:1u)!=CTR_MEM_OK) { r[0]=BAD_ADDRESS;return; }
    unsigned object=h->object;
    if(type==0) {
        if(value==0) { r[0]=0;return; }
        uint32_t left=(value>>31)?UINT32_MAX:value;
        thread_capture(g);
        while(left) {
            unsigned best=CTR_GUEST_MAX_THREADS;int priority=INT32_MAX;
            for(unsigned i=0;i<CTR_GUEST_MAX_THREADS;++i) {
                const ctr_guest_thread *t=&g->threads[i];
                if(t->active && !t->finished && t->state==CTR_GUEST_WAITING && t->wait_count==1 &&
                    t->wait_types[0]==CTR_HANDLE_ARBITER && t->wait_objects[0]==object && t->wait_address==address &&
                    t->priority<priority) { best=i;priority=t->priority; }
            }
            if(best==CTR_GUEST_MAX_THREADS)break;
            ctr_guest_thread *t=&g->threads[best];t->r[0]=0;t->state=CTR_GUEST_READY;t->wait_count=0;
            ctr_guest_object_release(g,CTR_HANDLE_ARBITER,object);--left;
        }
        if(g->threads[g->current_thread].active)thread_restore(g,g->current_thread);
        r[0]=0;return;
    }
    uint64_t nanos=(uint64_t)r[4]|((uint64_t)r[5]<<32);
    if(timed && !(nanos>>63) && UINT64_MAX-g->now_ns<nanos) { r[0]=BAD_COMBINATION;return; }
    uint32_t word=0;ctr_memory_read(&g->memory,address,4,1,&word);
    /* Bias both unsigned bit patterns to compare signed 32-bit values without
       implementation-defined casts or signed overflow at INT32_MIN. */
    if((word^0x80000000u)>=(value^0x80000000u)) { r[0]=0;return; }
    if(decrement)ctr_memory_write(&g->memory,address,4,word-1u);
    g->wait_count=1;g->wait_types[0]=CTR_HANDLE_ARBITER;g->wait_objects[0]=g->wait_object=object;
    g->wait_address=address;g->wait_all=g->wait_multiple=false;
    g->infinite_wait=!timed || (nanos>>63);g->deadline_ns=g->now_ns+nanos;
    ctr_guest_object_retain(g,CTR_HANDLE_ARBITER,object);g->state=CTR_GUEST_WAITING;ctr_sync_poll(g);
}
void ctr_thread_svc(ctr_guest *g,uint32_t number) {
    uint32_t *r=g->cpu.r;
    if(number==0x21) {
        uint32_t handle=0;
        for(unsigned i=0;i<8;++i)if(!g->arbiters[i].active) {
            g->arbiters[i]=(ctr_guest_arbiter){.active=true};handle=ctr_guest_handle_new(g,CTR_HANDLE_ARBITER,i);
            if(!handle)memset(&g->arbiters[i],0,sizeof(g->arbiters[i]));
            break;
        }
        r[0]=handle?0:NO_MEMORY;r[1]=handle;return;
    }
    if(number==0x22) { arbitrate(g);return; }
    if(number==0x09) { g->threads_enabled=true;thread_finish(g);return; }
    if(number!=0x08) {
        ctr_guest_thread *t=thread_for(g,number==0x0c?r[0]:r[1]);
        if(!t) { r[0]=BAD_HANDLE;if(number!=0x0c)r[1]=0;return; }
        if(number==0x0c) {
            if(r[1]<0x18 || r[1]>0x3f)r[0]=BAD_COMBINATION;
            else { t->priority=(int)r[1];r[0]=0; }
        } else { r[1]=number==0x37?t->id:(uint32_t)t->priority;r[0]=0; }
        return;
    }
    uint32_t priority=r[0],entry=r[1],argument=r[2],stack=r[3],processor=r[4],result=0;
    unsigned slot=CTR_GUEST_MAX_THREADS;
    for(unsigned i=1;i<CTR_GUEST_MAX_THREADS;++i)if(!g->threads[i].active) { slot=i;break; }
    if(priority<0x18 || priority>0x3f || (processor!=0 && processor!=0xffffffffu && processor!=0xfffffffeu))result=BAD_COMBINATION;
    else if((entry&1u?entry&~1u:entry)%((entry&1u)?2u:4u) ||
        ctr_memory_check_range(&g->memory,entry&~1u,(entry&1u)?2u:4u,CTR_MEM_EXEC)!=CTR_MEM_OK ||
        stack<8 || stack%8 || ctr_memory_check_range(&g->memory,stack-8,8,3)!=CTR_MEM_OK)result=BAD_ADDRESS;
    else if(slot==CTR_GUEST_MAX_THREADS || g->next_thread_id==UINT32_MAX)result=NO_MEMORY;
    uint32_t handle=0;
    if(!result) {
        ctr_guest_thread *t=&g->threads[slot];
        *t=(ctr_guest_thread){.active=true,.references=1,.id=g->next_thread_id,.priority=(int)priority,
            .stack_allocation=CTR_GUEST_MAX_ALLOCATIONS,.state=CTR_GUEST_READY,.tls_address=0x1ff82000u+slot*512u,
            .cpsr=0x10u|((entry&1u)?CTR_FLAG_T:0)};
        t->r[0]=argument;t->r[13]=stack;t->r[15]=entry&~1u;
        handle=ctr_guest_handle_new(g,CTR_HANDLE_THREAD,slot);
        if(!handle) { memset(t,0,sizeof(*t));result=NO_MEMORY; }
        else {
            ++g->next_thread_id;memset(g->storage[4]+slot*512u,0,512);++g->memory.generation;
            for(unsigned i=0;i<CTR_GUEST_MAX_ALLOCATIONS;++i) {
                ctr_guest_allocation *a=&g->allocations[i];
                if(a->storage && stack-8>=a->base && (uint64_t)stack<=(uint64_t)a->base+a->size) {
                    t->stack_allocation=i;++a->pins;break;
                }
            }
            g->threads_enabled=true;
        }
    }
    r[0]=result;r[1]=handle;
}
