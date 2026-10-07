#include "ctr_guest_fixture.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static uint32_t top_pixels[400*240], bottom_pixels[320*240];
static unsigned debug_count;
static void debug(void *context,const char *message) {
    (void)context; ++debug_count; fprintf(stderr,"guest-debug: %s\n",message);
}
static bool number(const char *value,uint32_t *out) {
    char *end; errno=0;
    unsigned long long n=strtoull(value,&end,10);
    if (errno || !*value || *end || n>UINT32_MAX) return false;
    *out=(uint32_t)n; return true;
}
static bool ppm(const char *name,const uint32_t *pixels,unsigned width,unsigned height) {
    FILE *file=fopen(name,"wb"); if (!file) return false;
    if (fprintf(file,"P6\n%u %u\n255\n",width,height)<0) { fclose(file); return false; }
    for (unsigned i=0;i<width*height;++i) {
        unsigned char rgb[3]={(unsigned char)(pixels[i]>>24),(unsigned char)(pixels[i]>>16),(unsigned char)(pixels[i]>>8)};
        if (fwrite(rgb,1,3,file)!=3) { fclose(file); return false; }
    }
    return fclose(file)==0;
}
int main(int argc,char **argv) {
    uint32_t args[6];
    if (argc!=7) return 2;
    for (unsigned i=0;i<6;++i) if (!number(argv[i+1],&args[i])) return 2;
    if (!args[0] || args[0]>30 || args[2]>1 || args[3]>=320 || args[4]>=240 || args[5]>1) return 2;
    ctr_guest guest;
    if (!ctr_frame_open(&guest)) return 3;
    guest.debug=debug;
    if (!ctr_frame_input(&guest,args[1],args[2]!=0,args[3],args[4])) { ctr_guest_close(&guest); return 4; }
    uint32_t frames=0; uint64_t now=0; clock_t start=clock();
    for (unsigned slice=0;frames<args[0] && slice<3000;++slice) {
        ctr_guest_run(&guest,50000,now,args[5]!=0); now+=1000000;
        if (guest.state==CTR_GUEST_FAULTED || !ctr_frame_count(&guest,&frames)) break;
    }
    if (frames!=args[0] || guest.state==CTR_GUEST_FAULTED ||
        !ctr_frame_pixels(&guest,true,top_pixels,400*240) || !ctr_frame_pixels(&guest,false,bottom_pixels,320*240)) {
        fprintf(stderr,"guest failure state=%s fault=%s detail=%08"PRIx32" frames=%"PRIu32"\n",ctr_guest_state_name(guest.state),ctr_cpu_result_name(guest.fault),guest.fault_detail,frames);
        ctr_guest_close(&guest); return 5;
    }
    if (!ctr_frame_exit(&guest)) { ctr_guest_close(&guest); return 6; }
    for (unsigned slice=0;slice<20 && guest.state!=CTR_GUEST_EXITED && guest.state!=CTR_GUEST_FAULTED;++slice) {
        ctr_guest_run(&guest,50000,now,args[5]!=0); now+=1000000;
    }
    bool ok=guest.state==CTR_GUEST_EXITED && debug_count==1 && ppm("top.ppm",top_pixels,400,240) && ppm("bottom.ppm",bottom_pixels,320,240);
    double seconds=(double)(clock()-start)/CLOCKS_PER_SEC;
    printf("{\"ok\":%s,\"frames\":%"PRIu32",\"exit_state\":\"%s\",\"instructions\":%"PRIu64",\"fallback_instructions\":%"PRIu64",\"svc_count\":%"PRIu64",\"debug_messages\":%u,\"desktop_cpu_seconds\":%.9f,\"top_first_rgba\":%"PRIu32",\"bottom_first_rgba\":%"PRIu32"}\n",ok?"true":"false",frames,ctr_guest_state_name(guest.state),guest.cpu.instructions,guest.cpu.fallback_instructions,guest.svc_count,debug_count,seconds,top_pixels[0],bottom_pixels[0]);
    ctr_guest_close(&guest); return ok?0:7;
}
