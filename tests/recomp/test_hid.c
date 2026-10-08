#include "ctr_hid_fixture.h"
#include "ctr_recomp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned assertions;
#define CHECK(x) do { ++assertions;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static void equal(const ctr_guest *a,const ctr_guest *b) {
    CHECK(a->state==b->state && a->cpu.instructions==b->cpu.instructions && a->svc_count==b->svc_count);
    CHECK(a->cpu.cpsr==b->cpu.cpsr && !memcmp(a->cpu.r,b->cpu.r,sizeof(a->cpu.r)));
    CHECK(a->ipc_requests==b->ipc_requests && a->ipc_failures==b->ipc_failures);
    CHECK(a->memory.count==b->memory.count && a->memory.generation==b->memory.generation && a->memory.code_generation==b->memory.code_generation);
    CHECK(!memcmp(a->handles,b->handles,sizeof(a->handles)) && !memcmp(a->events,b->events,sizeof(a->events)));
    CHECK(!memcmp(&a->hid,&b->hid,sizeof(a->hid)) && a->wait_count==b->wait_count);
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
    ctr_guest a,b,c;CHECK(ctr_hid_fixture_open(&a) && ctr_hid_fixture_open(&b) && ctr_hid_fixture_open(&c));
    CHECK(ctr_recomp_attach(&a,false));ctr_guest *paths[]={&a,&b,&c};unsigned phase=0;
    for(unsigned slice=0;slice<2000 && a.state!=CTR_GUEST_EXITED;++slice) {
        if(!slice || a.state==CTR_GUEST_WAITING) {
            for(unsigned i=0;i<3;++i) {
                if(phase==0)CHECK(ctr_hid_update(paths[i],0x401,12,-34,true,100,80,1000000));
                else if(phase==1)CHECK(ctr_hid_update(paths[i],2,-156,156,false,0,0,2000000));
                else if(phase==2)CHECK(ctr_hid_fixture_exit(paths[i]));
            }
            ++phase;
        }
        unsigned budget=slice%3==0?1:slice%3==1?7:23;
        for(unsigned i=0;i<3;++i)ctr_guest_run(paths[i],budget,phase*1000000,i!=2);
        CHECK(a.state!=CTR_GUEST_FAULTED);equal(&a,&b);equal(&a,&c);
    }
    CHECK(phase==3 && a.state==CTR_GUEST_EXITED && !a.cpu.fallback_instructions && !b.cpu.fallback_instructions);
    for(unsigned i=0;i<3;++i) {
        uint32_t words[5];CHECK(ctr_hid_fixture_snapshot(paths[i],words));
        CHECK(words[0]==0x60000002 && words[1]==0x009cff64 && !words[2] && !words[3] && words[4]==2);
        unsigned handles=0;for(unsigned j=0;j<64;++j)handles+=paths[i]->handles[j].active;
        CHECK(handles==6 && !paths[i]->shared[0].mappings[0] && !paths[i]->wait_count);
    }
    uint64_t instructions=a.cpu.instructions,svcs=a.svc_count;for(unsigned i=0;i<3;++i)ctr_guest_close(paths[i]);
    printf("{\"ok\":true,\"assertions\":%u,\"instructions_per_path\":%llu,\"svcs_per_path\":%llu,\"external_original_interpreter_full_state_match\":true,\"hid_shared_input\":true,\"console_verified\":false}\n",assertions,(unsigned long long)instructions,(unsigned long long)svcs);return 0;
}
