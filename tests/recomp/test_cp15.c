/* Unsupported CP15 cache instructions may not disappear from generated C. */
#include "ctr_recomp.h"
#include "abi_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned assertions;
#define CHECK(x) do { ++assertions;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static bool open_probe(ctr_guest *g) {
    const ctr_guest_image image={abi_code,sizeof(abi_code),{0x100000,1,ABI_CODE_SIZE},{0,0,0},{0,0,0},0,0x4000,"original_cp15_probe"};
    return ctr_guest_open(g,&image,NULL,0);
}
int main(void) {
    for(unsigned skip=0;skip<2;++skip) {
        ctr_guest a,b;CHECK(open_probe(&a) && open_probe(&b));CHECK(ctr_recomp_attach(&a,false));
        if(skip)a.cpu.cpsr=b.cpu.cpsr=0x40000010;
        ctr_guest_run(&a,1,0,true);ctr_guest_run(&b,1,0,false);
        CHECK(a.state==b.state && a.cpu.instructions==b.cpu.instructions && a.cpu.instructions==1);
        CHECK(!memcmp(a.cpu.r,b.cpu.r,sizeof(a.cpu.r)) && a.cpu.cpsr==b.cpu.cpsr);
        CHECK(a.state==(skip?CTR_GUEST_READY:CTR_GUEST_FAULTED));
        if(skip) {
            CHECK(a.cpu.r[15]==0x100004);
            ctr_guest_run(&a,1,1,true);ctr_guest_run(&b,1,1,false);
            CHECK(a.state==CTR_GUEST_FAULTED && b.state==CTR_GUEST_FAULTED && a.cpu.instructions==2);
            CHECK(a.cpu.r[15]==0x100004 && a.cpu.r[15]==b.cpu.r[15]);
        }
        CHECK(a.fault==CTR_CPU_UNSUPPORTED && b.fault==CTR_CPU_UNSUPPORTED);
        CHECK(!a.cpu.fallback_instructions && !a.svc_count && !a.ipc_requests);
        ctr_guest_close(&a);ctr_guest_close(&b);
    }
    for(unsigned offset=8;offset<=16;offset+=4) {
        ctr_guest a,b;CHECK(open_probe(&a) && open_probe(&b));CHECK(ctr_recomp_attach(&a,false));
        a.cpu.r[15]=b.cpu.r[15]=0x100000+offset;
        CHECK(ctr_guest_run(&a,1,0,true)==CTR_GUEST_FAULTED && ctr_guest_run(&b,1,0,false)==CTR_GUEST_FAULTED);
        CHECK(a.fault==CTR_CPU_UNSUPPORTED && b.fault==CTR_CPU_UNSUPPORTED);
        CHECK(a.cpu.instructions==1 && b.cpu.instructions==1 && a.cpu.r[15]==0x100000+offset);
        CHECK(!a.cpu.fallback_instructions && !a.svc_count);
        ctr_guest_close(&a);ctr_guest_close(&b);
    }
    printf("{\"ok\":true,\"assertions\":%u,\"cp15_cache_operations_fail_visibly\":true,\"conditional_skip_matches_interpreter\":true}\n",assertions);
    return 0;
}
