/* Boundary regressions for the real external lifter and checked C runtime. */
#include "ctr_guest_fixture.h"
#include "ctr_recomp.h"
#include "recomp.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
static unsigned assertions;
#define CHECK(x) do { ++assertions;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static void parity(void) {
    ctr_guest a,b;
    CHECK(ctr_frame_open(&a));
    CHECK(ctr_guest_open(&b,&ctr_frame_image,NULL,0));
    CHECK(ctr_frame_input(&a,1,true,100,80));CHECK(ctr_frame_input(&b,1,true,100,80));
    uint32_t count=0;uint64_t now=0;
    for(unsigned slice=0;count<2 && slice<100;++slice) {
        /* Uneven small budgets exercise mid-function resume. */
        uint64_t budget=slice%3==0?1:49999;
        CHECK(ctr_guest_run(&a,budget,now,true)==ctr_guest_run(&b,budget,now,false));now+=1000000;
        CHECK(a.state!=CTR_GUEST_FAULTED);
        CHECK(a.cpu.instructions==b.cpu.instructions);
        CHECK(!memcmp(a.cpu.r,b.cpu.r,sizeof(a.cpu.r)));
        CHECK(a.cpu.cpsr==b.cpu.cpsr);
        CHECK(a.svc_count==b.svc_count);
        for(unsigned i=0;i<5;++i)CHECK(!memcmp(a.storage[i],b.storage[i],a.storage_size[i]));
        CHECK(ctr_frame_count(&a,&count));
    }
    CHECK(count==2);CHECK(a.cpu.fallback_instructions==0);
    CHECK(ctr_frame_exit(&a));CHECK(ctr_frame_exit(&b));
    CHECK(ctr_guest_run(&a,50000,now,true)==ctr_guest_run(&b,50000,now,false));
    CHECK(a.state==CTR_GUEST_EXITED);
    ctr_guest_close(&a);ctr_guest_close(&b);
}
static void faults(void) {
    for(unsigned kind=0;kind<3;++kind) {
        ctr_guest g;CHECK(ctr_frame_open(&g));
        g.cpu.r[15]=0x10006c;g.cpu.r[0]=0x12345678;g.cpu.r[4]=kind==0?0xdead0000:kind==1?CTR_FRAME_TOP+1:0x100000;
        uint64_t generation=g.memory.generation;
        CHECK(ctr_guest_run(&g,128,0,true)==CTR_GUEST_FAULTED);
        CHECK(g.fault==CTR_CPU_MEMORY_FAULT);CHECK(g.cpu.r[15]==0x10006c);
        CHECK(g.cpu.memory_fault==(kind==0?CTR_MEM_UNMAPPED:kind==1?CTR_MEM_ALIGNMENT:CTR_MEM_PERMISSION));
        CHECK(g.memory.generation==generation);CHECK(g.cpu.instructions==1);CHECK(g.cpu.r[4]==(kind==0?0xdead0000:kind==1?CTR_FRAME_TOP+1:0x100000));
        ctr_guest_close(&g);
    }
    ctr_guest g;CHECK(ctr_frame_open(&g));
    ++g.memory.code_generation;
    CHECK(ctr_guest_run(&g,128,0,true)==CTR_GUEST_FAULTED);
    CHECK(g.fault==CTR_CPU_CODE_INVALIDATED);CHECK(g.cpu.instructions==0);
    ctr_guest_close(&g);
    CHECK(ctr_guest_open(&g,&ctr_frame_image,NULL,0));g.storage[0][0]^=1;
    CHECK(!ctr_recomp_attach(&g,false));CHECK(g.executor==NULL);ctr_guest_close(&g);
    CHECK(ctr_frame_open(&g));g.cpu.r[15]=0x12340000;
    CHECK(ctr_guest_run(&g,128,0,true)==CTR_GUEST_FAULTED);CHECK(g.fault==CTR_CPU_MISSING_BLOCK);ctr_guest_close(&g);
    CHECK(ctr_guest_open(&g,&ctr_frame_image,NULL,0));CHECK(ctr_recomp_attach(&g,true));
    /* An executable padding word has no generated block. The opt-in bounded
       interpreter records exactly one miss, then faults at the unmapped end. */
    g.cpu.r[15]=0x100ffc;
    CHECK(ctr_guest_run(&g,1,0,true)==CTR_GUEST_READY);CHECK(g.cpu.r[15]==0x101000);
    CHECK(g.cpu.instructions==1);CHECK(g.cpu.fallback_instructions==1);
    CHECK(ctr_guest_run(&g,1,1,true)==CTR_GUEST_FAULTED);CHECK(g.fault==CTR_CPU_MEMORY_FAULT);
    ctr_guest_close(&g);
    CHECK(ctr_frame_open(&g));
    /* Writable alias to code must invalidate even though its guest address
       is outside the executable mapping. No following instruction executes. */
    CHECK(ctr_memory_map(&g.memory,0x09000000,4096,CTR_MEM_READ|CTR_MEM_WRITE,g.storage[0],4096)==CTR_MEM_OK);
    /* Adding a mapping invalidates the snapshot; attach a fresh engine. */
    g.executor_close(g.executor_context);g.executor=NULL;g.executor_context=NULL;g.executor_close=NULL;
    CHECK(ctr_recomp_attach(&g,false));g.cpu.r[15]=0x10006c;g.cpu.r[4]=0x09000000;g.cpu.r[0]=0;
    CHECK(ctr_guest_run(&g,128,0,true)==CTR_GUEST_FAULTED);CHECK(g.fault==CTR_CPU_CODE_INVALIDATED);CHECK(g.cpu.instructions==1);
    ctr_guest_close(&g);
}
int main(void) {
    parity();faults();
    uint8_t b[4];store32(b,0x12345678);CHECK(b[0]==0x78 && b[3]==0x12);CHECK(load32(b)==0x12345678);
    CHECK(vfp_from_d(0,0x3ff80000)==1.5);CHECK(vfp_to_d_in(0,1.5)==UINT64_C(0x3ff8000000000000));
    printf("{\"ok\":true,\"assertions\":%u,\"external_aot_interpreter_full_state_match\":true,\"fault_pc_and_alias_invalidation\":true}\n",assertions);return 0;
}
