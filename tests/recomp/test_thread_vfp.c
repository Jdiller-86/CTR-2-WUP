#include "ctr_recomp.h"
#include "abi_image.h"
#include <stdio.h>
#include <stdlib.h>
static unsigned assertions;
#define CHECK(x) do { ++assertions;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
int main(void) {
    ctr_guest g;uint8_t data[4096]={0};
    ctr_guest_image image={abi_code,sizeof(abi_code),{0x100000,1,ABI_CODE_SIZE},{0,0,0},{0,0,0},0,16384,"original-thread-vfp"};
    CHECK(ctr_guest_open(&g,&image,NULL,0));CHECK(ctr_memory_map(&g.memory,0x08000000,4096,3,data,sizeof(data))==CTR_MEM_OK);
    CHECK(ctr_recomp_attach(&g,false));
    for(unsigned n=0;n<100 && g.state!=CTR_GUEST_EXITED;++n) {
        ctr_guest_run(&g,1,n,true);CHECK(g.state!=CTR_GUEST_FAULTED);
        if(g.current_thread==1)CHECK(g.cpu.fpscr==0);
    }
    CHECK(g.state==CTR_GUEST_EXITED && !g.cpu.fallback_instructions && g.context_switches==2);
    const uint32_t expected[]={0x40400000,0x40000000,0,0x41f00000};uint32_t value;
    for(unsigned i=0;i<4;++i)CHECK(ctr_memory_read(&g.memory,0x08000000+i*4,4,1,&value)==CTR_MEM_OK && value==expected[i]);
    CHECK(g.cpu.vfp[0]==0x3f800000 && g.cpu.vfp[1]==0x40000000 && g.cpu.vfp[2]==0x40400000);
    ctr_guest_close(&g);
    printf("{\"ok\":true,\"assertions\":%u,\"thread_vfp_words_and_fpscr_preserved\":true,\"console_verified\":false}\n",assertions);return 0;
}
