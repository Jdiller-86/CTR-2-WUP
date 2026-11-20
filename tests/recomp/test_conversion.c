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
    const uint32_t values[]={0,0x80000000,0x3fe00000,0xbfe00000,
        0x4f800000,0xcf800000,0x7f800000,0xff800000,0x7fc00000};
    ctr_guest_image image={abi_code,sizeof(abi_code),{0x100000,1,ABI_CODE_SIZE},
        {0,0,0},{0,0,0},0,16384,"original-conversion"};
    for(unsigned kind=0;kind<2;++kind)for(unsigned control=0;control<2;++control)
    for(unsigned i=0;i<sizeof(values)/sizeof(*values);++i) {
        ctr_guest a,b;
        CHECK(ctr_guest_open(&a,&image,NULL,0));CHECK(ctr_guest_open(&b,&image,NULL,0));
        a.cpu.r[15]=b.cpu.r[15]=0x100000+kind*4;
        a.cpu.vfp[0]=b.cpu.vfp[0]=values[i];
        a.cpu.fpscr=b.cpu.fpscr=control?0x03000000:0;
        CHECK(ctr_recomp_attach(&a,true));
        CHECK(ctr_guest_run(&a,1,0,true)==ctr_guest_run(&b,1,0,false));
        CHECK(a.state==CTR_GUEST_READY && a.cpu.r[15]==0x100004+kind*4);
        CHECK(a.cpu.instructions==1 && a.cpu.fallback_instructions==1);
        CHECK(!memcmp(a.cpu.r,b.cpu.r,sizeof(a.cpu.r)));
        CHECK(!memcmp(a.cpu.vfp,b.cpu.vfp,sizeof(a.cpu.vfp)) && a.cpu.fpscr==b.cpu.fpscr);
        ctr_guest_close(&a);ctr_guest_close(&b);
        CHECK(ctr_guest_open(&a,&image,NULL,0));
        a.cpu.r[15]=0x100000+kind*4;a.cpu.vfp[0]=values[i];
        CHECK(ctr_recomp_attach(&a,false));
        CHECK(ctr_guest_run(&a,1,0,true)==CTR_GUEST_FAULTED);
        CHECK(a.fault==CTR_CPU_UNSUPPORTED && a.cpu.r[15]==0x100000+kind*4);
        CHECK(a.cpu.vfp[0]==values[i] && a.cpu.fpscr==0);
        CHECK(a.cpu.instructions==1 && !a.cpu.fallback_instructions);
        ctr_guest_close(&a);
    }
    printf("{\"ok\":true,\"assertions\":%u,\"conversion_recovery_parity\":true,\"strict_conversion_fault\":true}\n",assertions);
    return 0;
}
