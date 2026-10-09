#include "ctr_recomp.h"
#include "abi_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned assertions;
#define CHECK(x) do { ++assertions;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
int main(void) {
    for(unsigned i=0;i<64;++i) {
        ctr_guest a,b;ctr_guest_image image={abi_code,sizeof(abi_code),{0x100000,1,ABI_CODE_SIZE},{0,0,0},{0,0,0},0,16384,"original-byte-media"};
        CHECK(ctr_guest_open(&a,&image,NULL,0) && ctr_guest_open(&b,&image,NULL,0));CHECK(ctr_recomp_attach(&a,false));
        a.cpu.r[0]=b.cpu.r[0]=0x01020304u*i;a.cpu.r[1]=b.cpu.r[1]=0xfefdfcfbu-i*0x02050913u;
        a.cpu.cpsr=b.cpu.cpsr=0xb80a0010;
        for(unsigned n=0;n<10;++n) {
            ctr_guest_run(&a,1,n,true);ctr_guest_run(&b,1,n,false);
            CHECK(a.state==b.state && a.state!=CTR_GUEST_FAULTED);
            CHECK(a.cpu.cpsr==b.cpu.cpsr && !memcmp(a.cpu.r,b.cpu.r,sizeof(a.cpu.r)));
        }
        CHECK(a.state==CTR_GUEST_EXITED && !a.cpu.fallback_instructions && a.cpu.instructions==10);
        ctr_guest_close(&a);ctr_guest_close(&b);
    }
    printf("{\"ok\":true,\"assertions\":%u,\"unsigned_byte_media_interpreter_match\":true,\"console_verified\":false}\n",assertions);return 0;
}
