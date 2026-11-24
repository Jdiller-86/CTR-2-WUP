/* Original conversion probe: real emitted helpers, strict fault and recovery. */
#include "ctr_guest.h"
#include "ctr_recomp.h"
#include "abi_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned assertions;
#define CHECK(x) do { ++assertions;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
int main(void) {
    const uint64_t values[][2]={
        {0,0},{0x80000000,UINT64_C(0x8000000000000000)},
        {1,1},{0x80000001,UINT64_C(0x8000000000000001)},
        {0x3fe00000,UINT64_C(0x3ffc000000000000)},{0xbfe00000,UINT64_C(0xbffc000000000000)},
        {0x4f800000,UINT64_C(0x41f0000000000000)},{0xcf800000,UINT64_C(0xc1f0000000000000)},
        {0x7f800000,UINT64_C(0x7ff0000000000000)},{0xff800000,UINT64_C(0xfff0000000000000)},
        {0x7fc00000,UINT64_C(0x7ff8000000000000)},{0x7f800001,UINT64_C(0x7ff0000000000001)}
    };
    ctr_guest_image image={abi_code,sizeof(abi_code),{0x100000,1,ABI_CODE_SIZE},
        {0,0,0},{0,0,0},0,16384,"original-conversion"};
    for(unsigned kind=0;kind<4;++kind)for(unsigned control=0;control<2;++control)
    for(unsigned i=0;i<sizeof(values)/sizeof(*values);++i) {
        ctr_guest a,b;
        CHECK(ctr_guest_open(&a,&image,NULL,0));CHECK(ctr_guest_open(&b,&image,NULL,0));
        a.cpu.r[15]=b.cpu.r[15]=0x100000+kind*4;
        uint64_t bits=values[i][kind>=2];
        a.cpu.vfp[0]=b.cpu.vfp[0]=(uint32_t)bits;
        a.cpu.vfp[1]=b.cpu.vfp[1]=(uint32_t)(bits>>32);
        a.cpu.fpscr=b.cpu.fpscr=control?0x03000000:0;
        CHECK(ctr_recomp_attach(&a,true));
        CHECK(ctr_guest_run(&a,1,0,true)==ctr_guest_run(&b,1,0,false));
        CHECK(a.state==CTR_GUEST_READY && a.cpu.r[15]==0x100004+kind*4);
        CHECK(a.cpu.instructions==1 && a.cpu.fallback_instructions==1);
        CHECK(!memcmp(a.cpu.r,b.cpu.r,sizeof(a.cpu.r)));
        CHECK(!memcmp(a.cpu.vfp,b.cpu.vfp,sizeof(a.cpu.vfp)) && a.cpu.fpscr==b.cpu.fpscr);
        ctr_guest_close(&a);ctr_guest_close(&b);
        CHECK(ctr_guest_open(&a,&image,NULL,0));
        a.cpu.r[15]=0x100000+kind*4;
        a.cpu.vfp[0]=(uint32_t)bits;a.cpu.vfp[1]=(uint32_t)(bits>>32);
        CHECK(ctr_recomp_attach(&a,false));
        CHECK(ctr_guest_run(&a,1,0,true)==CTR_GUEST_FAULTED);
        CHECK(a.fault==CTR_CPU_UNSUPPORTED && a.cpu.r[15]==0x100000+kind*4);
        CHECK(a.cpu.vfp[0]==(uint32_t)bits && a.cpu.vfp[1]==(uint32_t)(bits>>32) && a.cpu.fpscr==0);
        CHECK(a.cpu.instructions==1 && !a.cpu.fallback_instructions);
        ctr_guest_close(&a);
    }
    ctr_guest a,b;
    CHECK(ctr_guest_open(&a,&image,NULL,0));CHECK(ctr_guest_open(&b,&image,NULL,0));
    a.cpu.vfp[0]=b.cpu.vfp[0]=0x3fe00000u;
    a.cpu.fpscr=b.cpu.fpscr=0x03000000u;
    CHECK(ctr_recomp_attach(&a,true));
    CHECK(ctr_guest_run(&a,5,0,true)==ctr_guest_run(&b,5,0,false));
    CHECK(a.state==CTR_GUEST_EXITED && a.cpu.instructions==5 && a.cpu.fallback_instructions==4);
    CHECK(!memcmp(a.cpu.vfp,b.cpu.vfp,sizeof(a.cpu.vfp)) && a.cpu.fpscr==b.cpu.fpscr);
    unsigned max_depth;
    uint64_t unwinds;
    CHECK(ctr_recomp_depth_stats(&a,&max_depth,&unwinds) && !unwinds);
    ctr_guest_close(&a);ctr_guest_close(&b);
    ctr_guest trapped;
    CHECK(ctr_guest_open(&trapped,&image,NULL,0));
    trapped.cpu.vfp[0]=0x3fe00000u;trapped.cpu.fpscr=0x100u;
    CHECK(ctr_recomp_attach(&trapped,true));
    CHECK(ctr_guest_run(&trapped,1,0,true)==CTR_GUEST_FAULTED && trapped.fault==CTR_CPU_UNSUPPORTED);
    CHECK(trapped.cpu.r[15]==0x100000 && trapped.cpu.vfp[0]==0x3fe00000u && trapped.cpu.fpscr==0x100u);
    CHECK(trapped.cpu.instructions==1 && !trapped.cpu.fallback_instructions);
    ctr_guest_close(&trapped);
    printf("{\"ok\":true,\"assertions\":%u,\"conversion_recovery_parity\":true,\"strict_conversion_fault\":true}\n",assertions);
    return 0;
}
