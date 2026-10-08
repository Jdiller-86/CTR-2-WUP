/* Actual pinned lifter, original AOT and interpreter: dynamic mapping boundaries. */
#include "ctr_heap_fixture.h"
#include "ctr_recomp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned assertions;
#define CHECK(x) do { ++assertions;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static void equal(const ctr_guest *a,const ctr_guest *b) {
    CHECK(a->state==b->state && a->cpu.instructions==b->cpu.instructions);
    CHECK(a->cpu.cpsr==b->cpu.cpsr && !memcmp(a->cpu.r,b->cpu.r,sizeof(a->cpu.r)));
    CHECK(a->svc_count==b->svc_count && a->memory_requests==b->memory_requests && a->memory_failures==b->memory_failures);
    CHECK(a->memory.generation==b->memory.generation && a->memory.code_generation==b->memory.code_generation);
    CHECK(a->memory.count==b->memory.count);
    for(size_t i=0;i<a->memory.count;++i) {
        const ctr_mapping *x=&a->memory.maps[i],*y=&b->memory.maps[i];
        CHECK(x->base==y->base && x->size==y->size && x->permissions==y->permissions);
        CHECK(!memcmp(x->storage,y->storage,x->size));
    }
    for(unsigned i=0;i<CTR_GUEST_MAX_ALLOCATIONS;++i) {
        const ctr_guest_allocation *x=&a->allocations[i],*y=&b->allocations[i];
        CHECK(x->base==y->base && x->size==y->size && x->linear==y->linear);
        CHECK((x->storage!=NULL)==(y->storage!=NULL));
        if(x->storage)CHECK(!memcmp(x->storage,y->storage,x->size));
    }
}
static void lifecycle(void) {
    ctr_guest a,b,c;
    CHECK(ctr_heap_fixture_open(&a) && ctr_heap_fixture_open(&b) && ctr_heap_fixture_open(&c));
    CHECK(ctr_recomp_attach(&a,false));uint64_t code=a.memory.code_generation;
    for(unsigned slice=0;slice<1000 && a.state!=CTR_GUEST_EXITED;++slice) {
        unsigned budget=slice%3==0?1:slice%3==1?7:23;
        ctr_guest_run(&a,budget,slice,true);ctr_guest_run(&b,budget,slice,true);ctr_guest_run(&c,budget,slice,false);
        CHECK(a.state!=CTR_GUEST_FAULTED);equal(&a,&b);equal(&a,&c);
        CHECK(a.memory.code_generation==code);
    }
    CHECK(ctr_heap_fixture_check(&a)==0 && ctr_heap_fixture_check(&b)==0 && ctr_heap_fixture_check(&c)==0);
    CHECK(!a.cpu.fallback_instructions && !b.cpu.fallback_instructions && a.cpu.instructions==151);
    ctr_guest_close(&a);ctr_guest_close(&b);ctr_guest_close(&c);
}
static void guest_access_fault(bool remove) {
    ctr_guest a,b,c;CHECK(ctr_heap_fixture_open(&a) && ctr_heap_fixture_open(&b) && ctr_heap_fixture_open(&c));
    CHECK(ctr_recomp_attach(&a,false));
    ctr_guest *paths[]={&a,&b,&c};
    for(unsigned i=0;i<3;++i) {
        ctr_guest *g=paths[i];CHECK(ctr_guest_run(g,16,0,i!=2)==CTR_GUEST_READY);
        CHECK(g->cpu.r[15]==0x100040 && g->cpu.r[8]==0x08001000);
        CHECK(ctr_memory_change_range(&g->memory,g->cpu.r[8],0x3000,1,remove)==CTR_MEM_OK);
        CHECK(ctr_guest_run(g,1,1,i!=2)==CTR_GUEST_FAULTED);
        CHECK(g->fault==CTR_CPU_MEMORY_FAULT && g->cpu.memory_fault==(remove?CTR_MEM_UNMAPPED:CTR_MEM_PERMISSION));
        CHECK(g->cpu.r[15]==0x100040 && g->cpu.instructions==17 && !g->cpu.fallback_instructions);
    }
    equal(&a,&b);equal(&a,&c);ctr_guest_close(&a);ctr_guest_close(&b);ctr_guest_close(&c);
}
int main(void) {
    lifecycle();guest_access_fault(false);guest_access_fault(true);
    printf("{\"ok\":true,\"assertions\":%u,\"instructions_per_path\":151,\"external_original_interpreter_full_state_match\":true,\"heap_changes_preserve_aot\":true,\"guest_permission_and_unmapped_faults\":true,\"console_verified\":false}\n",assertions);
    return 0;
}
