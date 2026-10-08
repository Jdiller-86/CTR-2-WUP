#include "ctr_recomp.h"
#include "abi_image.h"
#include <stdio.h>
#include <stdlib.h>
static unsigned assertions;
#define CHECK(x) do {++assertions;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}} while(0)
int main(void) {
    ctr_guest g;
    ctr_guest_image image={abi_code,sizeof(abi_code),{0x100000,1,ABI_CODE_SIZE},{0,0,0},{0,0,0},0,0x4000,"original-abi-probe"};
    CHECK(ctr_guest_open(&g,&image,NULL,0));CHECK(ctr_recomp_attach(&g,false));
    /* Resume at every instruction and across ARM/Thumb and VFP boundaries. */
    for(unsigned i=0;i<100 && g.state==CTR_GUEST_READY;++i)ctr_guest_run(&g,1,i,true);
    CHECK(g.state==CTR_GUEST_EXITED);CHECK(g.svc_count==1);CHECK(g.cpu.instructions==30);
    CHECK(g.cpu.fallback_instructions==0);CHECK(g.cpu.r[0]==1);CHECK(g.cpu.r[2]==0x40000000);CHECK(g.cpu.r[3]==17);CHECK(g.cpu.cpsr&CTR_FLAG_T);
    const uint32_t expected[]={0x3fc00000,0x3f000000,0x40000000,0x3f800000,0x3f400000,0x40400000,0,0,0,0x40000000,0,0x40080000};
    for(unsigned i=0;i<sizeof(expected)/sizeof(*expected);++i){uint32_t value;CHECK(ctr_recomp_vfp(&g,i,&value));CHECK(value==expected[i]);}
    ctr_guest_close(&g);
    printf("{\"ok\":true,\"assertions\":%u,\"arm_thumb_vfp_scalar_probe\":true,\"single_instruction_resume\":true}\n",assertions);return 0;
}
