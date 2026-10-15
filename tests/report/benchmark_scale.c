/* Original generated pixels. CPU scaling only; no GX2 or Wii U speed claim. */
#include "image_scale.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int run(unsigned display,unsigned scenario,unsigned frames) {
    unsigned gw=display?320:400,gh=240,ow=display?854:1920,oh=display?480:1080,pitch=display?896:1920;
    size_t pixels=(size_t)gw*gh,bytes=(size_t)pitch*oh*4;
    uint32_t *source=malloc(pixels*4);
    unsigned char *reference=malloc(bytes),*actual=malloc(bytes);
    ctr_scale_plan plan={0};ctr_scale_cache cache={0};CtrViewport view;
    if(!source || !reference || !actual || !ctr_viewport_fit(&view,ow,oh,gw,gh) ||
       !ctr_scale_plan_init(&plan,&view) || !ctr_scale_cache_init(&cache,&plan))return 1;
    for(size_t i=0;i<pixels;++i)source[i]=(uint32_t)i*UINT32_C(2654435761);
    clock_t baseline_ticks=0,cached_ticks=0;
    uint64_t publish=0,scaled=0,ranges=0,full=0,reused=0;
    for(unsigned frame=0;frame<frames;++frame) {
        if(frame && scenario==1 && !display) {
            const unsigned rows[]={110,190,220};
            for(unsigned box=0;box<3;++box)for(unsigned y=rows[box];y<rows[box]+8;++y)
                for(unsigned x=16;x<64;++x)source[(size_t)y*gw+x]^=frame|1u;
        } else if(frame && scenario==2) {
            for(size_t i=0;i<pixels;++i)source[i]^=UINT32_C(0x01010100);
        }
        clock_t start=clock();
        if(!ctr_scale_rgba(&plan,source,pixels,reference,bytes,pitch))return 1;
        baseline_ticks+=clock()-start;ctr_scale_update update;
        start=clock();
        if(!ctr_scale_cache_update(&cache,source,pixels,actual,bytes,pitch,&update))return 1;
        cached_ticks+=clock()-start;
        /* Compare every byte outside the timed regions, including pitch/borders. */
        if(memcmp(actual,reference,bytes))return 1;
        publish+=update.publish_bytes;scaled+=update.scaled_pixels;ranges+=cache.range_count;
        full+=update.full;reused+=cache.range_count==0;
    }
    const char *names[]={"static","small-counters","full-motion"};
    printf("%s{\"display\":\"%s\",\"scenario\":\"%s\",\"frames\":%u,"
        "\"baseline_cpu_ms\":%.3f,\"cached_cpu_ms\":%.3f,\"baseline_publish_bytes\":%llu,"
        "\"cached_publish_bytes\":%llu,\"baseline_scaled_pixels\":%llu,\"cached_scaled_pixels\":%llu,"
        "\"publish_ranges\":%llu,\"full_updates\":%llu,\"reused_updates\":%llu,\"cache_heap_bytes\":%llu}",
        display || scenario?",\n":"",display?"DRC":"TV",names[scenario],frames,
        baseline_ticks*1000.0/CLOCKS_PER_SEC,cached_ticks*1000.0/CLOCKS_PER_SEC,
        (unsigned long long)bytes*frames,(unsigned long long)publish,
        (unsigned long long)view.width*view.height*frames,(unsigned long long)scaled,
        (unsigned long long)ranges,(unsigned long long)full,(unsigned long long)reused,
        (unsigned long long)(pixels*4+cache.range_capacity*sizeof(*cache.ranges)));
    ctr_scale_cache_close(&cache);ctr_scale_plan_close(&plan);free(source);free(reference);free(actual);
    return 0;
}
int main(int argc,char **argv) {
    char *end=NULL;unsigned long count=argc==2?strtoul(argv[1],&end,10):60;
    if(argc>2 || !count || count>1000 || (argc==2 && (!*argv[1] || *end)))return 2;
    printf("{\"ok\":true,\"exact_pixels\":true,\"console_verified\":false,"
           "\"timing_scope\":\"host process CPU; excludes comparison, cache APIs and GPU copies\",\"cases\":[\n");
    for(unsigned scenario=0;scenario<3;++scenario)for(unsigned display=0;display<2;++display)
        if(run(display,scenario,(unsigned)count))return 1;
    puts("\n]}");return 0;
}
