/* Real pinned lifter vs original AOT vs interpreter at uneven boundaries. */
#include "ctr_service_fixture.h"
#include "ctr_recomp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned assertions;
#define CHECK(x) do { ++assertions;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static void equal(const ctr_guest *a,const ctr_guest *b) {
    CHECK(a->state==b->state && a->cpu.instructions==b->cpu.instructions);
    CHECK(a->cpu.cpsr==b->cpu.cpsr && !memcmp(a->cpu.r,b->cpu.r,sizeof(a->cpu.r)));
    CHECK(a->svc_count==b->svc_count && a->ipc_requests==b->ipc_requests && a->ipc_failures==b->ipc_failures);
    for(unsigned i=0;i<5;++i)if(a->storage_size[i])CHECK(!memcmp(a->storage[i],b->storage[i],a->storage_size[i]));
}
int main(void) {
    ctr_guest a,b,c;
    CHECK(ctr_service_fixture_open(&a) && ctr_service_fixture_open(&b) && ctr_service_fixture_open(&c));
    CHECK(ctr_guest_configure(&a,(ctr_guest_configuration){2,7,3}));
    CHECK(ctr_guest_configure(&b,(ctr_guest_configuration){2,7,3}));
    CHECK(ctr_guest_configure(&c,(ctr_guest_configuration){2,7,3}));
    CHECK(ctr_recomp_attach(&a,false));
    /* Change the thread context after attach: every execute import must refresh
       the ABI's TLS word rather than retain the loader's initial address. */
    a.cpu.tls_address=b.cpu.tls_address=c.cpu.tls_address=0x1ff82200;
    for(unsigned slice=0;slice<1000 && a.state!=CTR_GUEST_EXITED;++slice) {
        unsigned budget=slice%3==0?1:7;
        ctr_guest_run(&a,budget,slice,true);ctr_guest_run(&b,budget,slice,true);ctr_guest_run(&c,budget,slice,false);
        CHECK(a.state!=CTR_GUEST_FAULTED);equal(&a,&b);equal(&a,&c);
    }
    CHECK(a.state==CTR_GUEST_EXITED && !a.cpu.fallback_instructions && !b.cpu.fallback_instructions);
    CHECK(a.svc_count==9 && a.ipc_requests==5 && !a.ipc_failures);
    uint32_t value;
    CHECK(ctr_memory_read(&a.memory,0x08000040,4,CTR_MEM_READ,&value)==CTR_MEM_OK && value==0x1ff82200);
    CHECK(ctr_memory_read(&a.memory,0x08000064,4,CTR_MEM_READ,&value)==CTR_MEM_OK && value==2);
    CHECK(ctr_memory_read(&a.memory,0x08000070,4,CTR_MEM_READ,&value)==CTR_MEM_OK && value==3);
    CHECK(ctr_memory_read(&a.memory,0x08000020,1,CTR_MEM_READ,&value)==CTR_MEM_OK && value==7);
    for(unsigned i=0;i<16;++i)CHECK(!a.sessions[i].active);
    uint64_t instructions=a.cpu.instructions;
    ctr_guest_close(&a);ctr_guest_close(&b);ctr_guest_close(&c);
    printf("{\"ok\":true,\"assertions\":%u,\"instructions_per_path\":%llu,\"external_original_interpreter_full_state_match\":true,\"tls_refreshed_after_attach\":true,\"console_verified\":false}\n",assertions,(unsigned long long)instructions);
    return 0;
}
