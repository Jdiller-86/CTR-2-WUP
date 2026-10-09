#include "ctr_thread_fixture.h"
#include "ctr_recomp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned assertions;
#define CHECK(x) do { ++assertions;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static void equal(const ctr_guest *a,const ctr_guest *b) {
    CHECK(a->state==b->state && a->process_exited==b->process_exited && a->current_thread==b->current_thread);
    CHECK(a->cpu.instructions==b->cpu.instructions && a->svc_count==b->svc_count && a->context_switches==b->context_switches);
    CHECK(a->cpu.cpsr==b->cpu.cpsr && !memcmp(a->cpu.r,b->cpu.r,sizeof(a->cpu.r)));
    CHECK(!memcmp(a->cpu.vfp,b->cpu.vfp,sizeof(a->cpu.vfp)) && a->cpu.fpscr==b->cpu.fpscr);
    CHECK(!memcmp(a->threads,b->threads,sizeof(a->threads)) && !memcmp(a->handles,b->handles,sizeof(a->handles)));
    CHECK(!memcmp(a->mutexes,b->mutexes,sizeof(a->mutexes)) && !memcmp(a->events,b->events,sizeof(a->events)));
    CHECK(a->memory.generation==b->memory.generation && a->memory.code_generation==b->memory.code_generation);
    for(unsigned i=0;i<5;++i)if(a->storage_size[i])CHECK(!memcmp(a->storage[i],b->storage[i],a->storage_size[i]));
}
int main(void) {
    ctr_guest a,b,c;CHECK(ctr_thread_fixture_open(&a) && ctr_thread_fixture_open(&b) && ctr_thread_fixture_open(&c));
    CHECK(ctr_recomp_attach(&a,false));
    for(unsigned n=0;n<500 && a.state!=CTR_GUEST_EXITED;++n) {
        unsigned budget=n%3==0?1:n%3==1?7:23;uint64_t time=n*10000u;
        ctr_guest_run(&a,budget,time,true);ctr_guest_run(&b,budget,time,true);ctr_guest_run(&c,budget,time,false);
        if(a.state==CTR_GUEST_FAULTED)fprintf(stderr,"fault=%s pc=%08x detail=%08x thread=%u instructions=%llu\n",ctr_cpu_result_name(a.fault),a.cpu.r[15],a.fault_detail,a.current_thread,(unsigned long long)a.cpu.instructions);
        CHECK(a.state!=CTR_GUEST_FAULTED);equal(&a,&b);equal(&a,&c);
    }
    CHECK(!ctr_thread_fixture_check(&a) && !ctr_thread_fixture_check(&b) && !ctr_thread_fixture_check(&c));
    uint64_t instructions=a.cpu.instructions,switches=a.context_switches,svcs=a.svc_count;
    ctr_guest_close(&a);ctr_guest_close(&b);ctr_guest_close(&c);
    printf("{\"ok\":true,\"assertions\":%u,\"instructions_per_path\":%llu,\"context_switches\":%llu,\"svcs_per_path\":%llu,\"external_original_interpreter_full_state_match\":true,\"original_arm_thread_scheduler\":true,\"console_verified\":false}\n",assertions,(unsigned long long)instructions,(unsigned long long)switches,(unsigned long long)svcs);return 0;
}
