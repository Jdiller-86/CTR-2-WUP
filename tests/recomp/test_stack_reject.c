#include "ctr_recomp.h"
#include "abi_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned assertions;
static void record(void *context,const char *event,const char *hash,uint32_t pc,bool thumb,uint32_t detail) {
    (void)event;(void)hash;(void)pc;(void)thumb;*(uint32_t *)context=detail;
}
#define CHECK(x) do { ++assertions;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
int main(void) {
    const uint32_t opcodes[]={0xb400,0xbc00,0xc000,0xc800,0xc103,0x47f8};
    for(unsigned instruction=0;instruction<6;++instruction) {
        ctr_guest a,b;ctr_guest_image image={abi_code,sizeof(abi_code),{0x100000,1,ABI_CODE_SIZE},{0,0,0},{0,0,0},0,0x4000,"original-invalid-stack"};
        CHECK(ctr_guest_open(&a,&image,NULL,0) && ctr_guest_open(&b,&image,NULL,0));CHECK(ctr_recomp_attach(&a,false));
        ctr_guest *paths[]={&a,&b};uint32_t initial[16],detail=0;
        for(unsigned i=0;i<2;++i) {
            ctr_guest *g=paths[i];g->cpu.r[15]=0x100010+instruction*2;g->cpu.cpsr|=CTR_FLAG_T;
            g->cpu.log=record;g->cpu.log_context=&detail;
            memcpy(initial,g->cpu.r,sizeof(initial));uint64_t generation=g->memory.generation;
            CHECK(ctr_guest_run(g,1,0,i==0)==CTR_GUEST_FAULTED);
            CHECK(g->fault==CTR_CPU_UNSUPPORTED && detail==opcodes[instruction] && g->fault_detail==g->cpu.r[15] && g->cpu.instructions==1 && !g->cpu.fallback_instructions);
            CHECK(!memcmp(initial,g->cpu.r,sizeof(initial)) && g->memory.generation==generation);
        }
        ctr_guest_close(&a);ctr_guest_close(&b);
    }
    printf("{\"ok\":true,\"assertions\":%u,\"erased_invalid_thumb_transfers_rejected\":true,\"console_verified\":false}\n",assertions);return 0;
}
