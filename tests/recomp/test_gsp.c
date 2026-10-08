#include "ctr_gsp_fixture.h"
#include "ctr_recomp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned assertions;
#define CHECK(x) do { ++assertions;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static void equal(const ctr_guest *a,const ctr_guest *b) {
    CHECK(a->state==b->state && a->cpu.instructions==b->cpu.instructions && a->svc_count==b->svc_count);
    CHECK(a->cpu.cpsr==b->cpu.cpsr && !memcmp(a->cpu.r,b->cpu.r,sizeof(a->cpu.r)));
    CHECK(a->ipc_requests==b->ipc_requests && a->ipc_failures==b->ipc_failures);
    CHECK(a->memory.count==b->memory.count && a->memory.generation==b->memory.generation && a->memory.code_generation==b->memory.code_generation);
    CHECK(!memcmp(a->handles,b->handles,sizeof(a->handles)) && !memcmp(a->events,b->events,sizeof(a->events)));
    CHECK(!memcmp(&a->hid,&b->hid,sizeof(a->hid)) && !memcmp(&a->gsp,&b->gsp,sizeof(a->gsp)));
    for(size_t i=0;i<a->memory.count;++i) {
        const ctr_mapping *x=&a->memory.maps[i],*y=&b->memory.maps[i];
        CHECK(x->base==y->base && x->size==y->size && x->permissions==y->permissions && !memcmp(x->storage,y->storage,x->size));
    }
    for(unsigned i=0;i<8;++i) {
        const ctr_guest_shared *x=&a->shared[i],*y=&b->shared[i];
        CHECK(x->active==y->active && x->references==y->references && x->size==y->size && !memcmp(x->mappings,y->mappings,sizeof(x->mappings)));
        if(x->active)CHECK(!memcmp(x->storage,y->storage,x->size));
    }
}
static void reference(const char *directory,unsigned frame,const char *screen,const uint32_t *pixels,unsigned width) {
    if(!directory)return;
    char path[4096];CHECK(snprintf(path,sizeof(path),"%s/frame-%u-%s.ppm",directory,frame,screen)>0);
    FILE *file=fopen(path,"wb");CHECK(file);CHECK(fprintf(file,"P6\n%u 240\n255\n",width)>0);
    for(unsigned i=0;i<width*240;++i) {
        uint8_t rgb[]={(uint8_t)(pixels[i]>>24),(uint8_t)(pixels[i]>>16),(uint8_t)(pixels[i]>>8)};
        if(fwrite(rgb,1,3,file)!=3){fprintf(stderr,"reference write failed\n");exit(1);}
    }
    CHECK(!fclose(file));
}
int main(int argc,char **argv) {
    ctr_guest a,b,c;CHECK(ctr_gsp_fixture_open(&a) && ctr_gsp_fixture_open(&b) && ctr_gsp_fixture_open(&c));
    CHECK(ctr_recomp_attach(&a,false));ctr_guest *paths[]={&a,&b,&c};unsigned phase=0;
    uint32_t *pixels=malloc(400*240*sizeof(*pixels));CHECK(pixels);
    for(unsigned slice=0;slice<1000 && a.state!=CTR_GUEST_EXITED;++slice) {
        if(!slice || a.state==CTR_GUEST_WAITING) {
            if(phase) {
                CHECK(a.gsp.swaps[0]==phase && a.gsp.swaps[1]==phase);
                CHECK(ctr_gsp_pixels(&a,0,pixels,400*240)==CTR_MEM_OK);
                CHECK(pixels[0]==0xffffffff && pixels[399]==0xff0000ff && pixels[239*400]==0x00ff00ff && pixels[240*400-1]==0x0000ffff);
                CHECK(pixels[401]==(phase==1?0x00ff00ff:0x0000ffff));
                reference(argc==2?argv[1]:NULL,phase,"top",pixels,400);
                CHECK(ctr_gsp_pixels(&a,1,pixels,320*240)==CTR_MEM_OK);
                CHECK(pixels[0]==(phase==1?0xff00ffff:0x00ffffff) && (phase!=1 || pixels[80*320+100]==0xffffffff));
                reference(argc==2?argv[1]:NULL,phase,"bottom",pixels,320);
            }
            for(unsigned i=0;i<3;++i) {
                if(phase==0)CHECK(ctr_hid_update(paths[i],1,0,0,true,100,80,1000000));
                else if(phase==1)CHECK(ctr_hid_update(paths[i],2,-156,156,false,0,0,2000000));
                else if(phase==2)CHECK(ctr_hid_fixture_exit(paths[i]));
            }
            ++phase;
        }
        unsigned budget=slice%3==0?1:slice%3==1?7:50000;
        for(unsigned i=0;i<3;++i)ctr_guest_run(paths[i],budget,phase*1000000,i!=2);
        CHECK(a.state!=CTR_GUEST_FAULTED);equal(&a,&b);equal(&a,&c);
    }
    CHECK(phase==3 && a.state==CTR_GUEST_EXITED && !a.cpu.fallback_instructions && !b.cpu.fallback_instructions && !a.gsp.right);
    uint32_t value;CHECK(ctr_memory_read(&a.memory,0x08000014,4,1,&value)==CTR_MEM_OK && value==31);
    uint64_t instructions=a.cpu.instructions,svcs=a.svc_count;for(unsigned i=0;i<3;++i)ctr_guest_close(paths[i]);free(pixels);
    printf("{\"ok\":true,\"assertions\":%u,\"instructions_per_path\":%llu,\"svcs_per_path\":%llu,\"external_original_interpreter_full_state_match\":true,\"standard_hid_gsp_frames\":true,\"console_verified\":false}\n",assertions,(unsigned long long)instructions,(unsigned long long)svcs);return 0;
}
