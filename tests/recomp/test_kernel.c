#include "ctr_kernel_fixture.h"
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
    CHECK(a->memory.count==b->memory.count && a->memory.generation==b->memory.generation && a->memory.code_generation==b->memory.code_generation);
    CHECK(!memcmp(a->handles,b->handles,sizeof(a->handles)));
    CHECK(!memcmp(a->mutexes,b->mutexes,sizeof(a->mutexes)) && !memcmp(a->semaphores,b->semaphores,sizeof(a->semaphores)));
    for(size_t i=0;i<a->memory.count;++i) {
        const ctr_mapping *x=&a->memory.maps[i],*y=&b->memory.maps[i];
        CHECK(x->base==y->base && x->size==y->size && x->permissions==y->permissions && !memcmp(x->storage,y->storage,x->size));
    }
    for(unsigned i=0;i<8;++i) {
        const ctr_guest_shared *x=&a->shared[i],*y=&b->shared[i];
        CHECK(x->active==y->active && x->references==y->references && x->size==y->size && !memcmp(x->mappings,y->mappings,sizeof(x->mappings)));
        if(x->active)CHECK(!memcmp(x->storage,y->storage,x->size));
    }
}
int main(void) {
    ctr_guest a,b,c;CHECK(ctr_kernel_fixture_open(&a) && ctr_kernel_fixture_open(&b) && ctr_kernel_fixture_open(&c));
    CHECK(ctr_recomp_attach(&a,false));uint64_t code=a.memory.code_generation;
    for(unsigned slice=0;slice<1000 && a.state!=CTR_GUEST_EXITED;++slice) {
        unsigned budget=slice%3==0?1:slice%3==1?7:23;
        ctr_guest_run(&a,budget,slice,true);ctr_guest_run(&b,budget,slice,true);ctr_guest_run(&c,budget,slice,false);
        CHECK(a.state!=CTR_GUEST_FAULTED);equal(&a,&b);equal(&a,&c);CHECK(a.memory.code_generation==code);
    }
    CHECK(!ctr_kernel_fixture_check(&a) && !ctr_kernel_fixture_check(&b) && !ctr_kernel_fixture_check(&c));
    uint64_t instructions=a.cpu.instructions;ctr_guest_close(&a);ctr_guest_close(&b);ctr_guest_close(&c);
    printf("{\"ok\":true,\"assertions\":%u,\"instructions_per_path\":%llu,\"external_original_interpreter_full_state_match\":true,\"shared_memory_synchronization\":true,\"console_verified\":false}\n",assertions,(unsigned long long)instructions);return 0;
}
