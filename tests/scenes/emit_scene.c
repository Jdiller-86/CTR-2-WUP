/* Original fixture-only evidence producer; no guest or external input. */
#include "ctr_scene_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static int save(const char *name,const void *bytes,size_t size) {
    FILE *file=fopen(name,"wb");if(!file)return 0;
    int ok=fwrite(bytes,1,size,file)==size;
    return fclose(file)==0 && ok;
}
int main(int argc,char **argv) {
    if(argc!=5 || argv[1][0]<'0' || argv[1][0]>'3' || argv[1][1])return 1;
    bool shorts=((unsigned)(argv[1][0]-'0')&1u)!=0,arithmetic=argv[1][0]>='2';
    ctr_scene_fixture fixture;ctr_scene_report report={0};ctr_scene_state state;
    uint8_t *pixels=malloc(CTR_SCENE_FIXTURE_BYTES);if(!pixels)return 2;
    if(arithmetic)ctr_scene_fixture_make_arithmetic(&fixture,shorts);
    else ctr_scene_fixture_make(&fixture,shorts);
    ctr_scene_init(&state);ctr_scene_fixture_background(pixels,CTR_SCENE_FIXTURE_BYTES);
    ctr_scene_resources resources={UINT32_C(0x18000000),fixture.resources,fixture.resource_bytes};
    ctr_scene_surface surface={pixels,CTR_SCENE_FIXTURE_BYTES,64,64,256};
    clock_t start=clock();
    ctr_scene_result result=ctr_scene_execute(&state,fixture.commands,fixture.command_bytes,&resources,&surface,&report);
    clock_t end=clock();
    if(start==(clock_t)-1 || end==(clock_t)-1 || end<start) { free(pixels);return 4; }
    unsigned long long elapsed=(unsigned long long)(end-start)*1000000u/CLOCKS_PER_SEC;
    int ok=result==CTR_SCENE_OK && save(argv[2],pixels,CTR_SCENE_FIXTURE_BYTES) &&
        save(argv[3],fixture.commands,fixture.command_bytes) && save(argv[4],fixture.resources,fixture.resource_bytes);
    free(pixels);
    printf("{\"result\":\"%s\",\"draws\":%u,\"triangles\":%u,\"fragments\":%u,\"shader_instructions\":%u,\"desktop_process_cpu_us\":%llu}\n",
        ctr_scene_result_name(result),report.draws,report.triangles,report.fragments,report.shader_instructions,elapsed);
    return ok?0:3;
}
