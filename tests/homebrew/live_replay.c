/* Original compiled-guest test with console-sized budgets and varied clocks.
 * Exercises held/released A before guest execution and repeated green frames. */
#define _POSIX_C_SOURCE 200809L
#include "boot-image.h"
#include "ctr_recomp.h"
#include "ctr_apt.h"
#include "ctr_fs.h"
#include "ctr_gsp.h"
#include "ctr_hid.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static bool pass;
static void message(void *context,const char *text) {
    (void)context;if(!strcmp(text,"CTR2WUP compiled interactive PASS"))pass=true;
}
static void deadline(int signal_number) {
    (void)signal_number;static const char text[]="FAIL: live guest replay stalled\n";
    write(STDERR_FILENO,text,sizeof(text)-1);_Exit(9);
}
int main(int argc,char **argv) {
    if(argc!=4)return 1;
    uint64_t delta=strtoull(argv[1],NULL,10);unsigned budget=(unsigned)strtoul(argv[2],NULL,10);
    unsigned delay=(unsigned)strtoul(argv[3],NULL,10);
    if(!delta || delta>UINT64_C(1000000000) || !budget || budget>50000 || delay>20)return 1;
    signal(SIGALRM,deadline);alarm(15);
    ctr_guest g;if(!ctr_guest_open(&g,&boot_image,NULL,0))return 2;
    ctr_fs_volume sd;
    if(!ctr_apt_configure(&g) || !ctr_fs_volume_init(&sd,9,false) || !ctr_fs_mount(&g,&sd) || !ctr_recomp_attach(&g,false))return 2;
    g.debug=message;
    uint32_t *pixels=malloc(400*240*sizeof(*pixels));if(!pixels)return 2;
    unsigned phase=0,red_seen_at=0,transitions=0,partial=0,blank=0,steps=0;uint64_t swaps=0,first_a=0,first_green=0;
    for(unsigned n=0;n<10000 && g.state!=CTR_GUEST_FAULTED && g.state!=CTR_GUEST_EXITED;++n) {
        uint64_t now=1+(uint64_t)n*delta;
        uint32_t buttons=(phase==1 && n>=red_seen_at+delay) || phase==3?1u:phase==4?2u:0u;
        if(buttons==1 && !first_a)first_a=now;
        if(!ctr_hid_update(&g,buttons,0,0,false,0,0,now))return 3;
        ctr_guest_run(&g,budget,now,true);steps=n+1;
        if(!g.gsp.black && g.gsp.swaps[0]>swaps && g.gsp.swaps[0]==g.gsp.swaps[1]) {
            if(ctr_gsp_pixels(&g,0,pixels,400*240)!=CTR_MEM_OK)return 4;
            uint32_t color=pixels[0];bool uniform=true;
            for(unsigned i=0;uniform && i<400*240;++i)uniform=pixels[i]==color;
            if(!uniform) { ++partial;continue; }
            if(color==0x000000ff) { ++blank;continue; }
            if(color!=0xff0000ff && color!=0x00ff00ff)return 4;
            if(ctr_gsp_pixels(&g,1,pixels,320*240)!=CTR_MEM_OK)return 4;
            for(unsigned i=0;i<320*240;++i)if(pixels[i]!=0x0000ffff)return 4;
            swaps=g.gsp.swaps[0];
            uint32_t expected=(phase==1 || phase==3)?0x00ff00ff:0xff0000ff;
            if(phase<4 && color==expected) {
                if(!phase)red_seen_at=n;
                if(phase==1)first_green=now;
                ++phase;++transitions;
            }
        }
    }
    unsigned depth=0;uint64_t unwinds=0;ctr_recomp_depth_stats(&g,&depth,&unwinds);
    bool ok=g.state==CTR_GUEST_EXITED && pass && transitions==4 && !partial && !g.cpu.fallback_instructions && first_green>=first_a;
    printf("{\"ok\":%s,\"state\":\"%s\",\"fault\":\"%s\",\"pc\":%u,\"steps\":%u,\"delta_ns\":%llu,\"budget\":%u,\"delay_steps\":%u,\"transitions\":%u,\"partial_samples\":%u,\"blank_samples\":%u,\"A_to_green_ns\":%llu,\"depth\":%u,\"unwinds\":%llu,\"console_verified\":false}\n",
        ok?"true":"false",ctr_guest_state_name(g.state),ctr_cpu_result_name(g.fault),g.cpu.r[15],steps,
        (unsigned long long)delta,budget,delay,transitions,partial,blank,(unsigned long long)(first_green>=first_a?first_green-first_a:0),depth,(unsigned long long)unwinds);
    free(pixels);ctr_guest_close(&g);ctr_fs_volume_close(&sd);alarm(0);return ok?0:5;
}
