/* Original scene inputs and analytic golden pixels. */
#include "ctr_scene_fixture.h"
#include <stdlib.h>
#include <string.h>

static void put32(uint8_t *p,uint32_t value) {
    for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(value>>(i*8u));
}
static void command(ctr_scene_fixture *f,unsigned reg,uint32_t value) {
    put32(f->commands+f->command_bytes,value);
    put32(f->commands+f->command_bytes+4u,UINT32_C(0x000f0000)|reg);
    f->command_bytes+=8u;
}
static void color(ctr_scene_fixture *f,bool green) {
    command(f,0x232u,1u);
    command(f,0x233u,UINT32_C(0x3f000000)); /* W=1, high part of Z=0. */
    command(f,0x234u,green?UINT32_C(0x00003f00):0u);
    command(f,0x235u,green?0u:UINT32_C(0x003f0000));
}
static void make(ctr_scene_fixture *f,bool short_indices,bool arithmetic) {
    if(!f)return;
    memset(f,0,sizeof(*f));
    static const uint32_t positions[4][4]={
        {UINT32_C(0xbf400000),UINT32_C(0xbf400000),UINT32_C(0xbf000000),UINT32_C(0x3f800000)},
        {UINT32_C(0x3f400000),UINT32_C(0xbf400000),UINT32_C(0xbf000000),UINT32_C(0x3f800000)},
        {UINT32_C(0xbf400000),UINT32_C(0x3f400000),UINT32_C(0xbf000000),UINT32_C(0x3f800000)},
        {UINT32_C(0x3f400000),UINT32_C(0x3f400000),UINT32_C(0xbf000000),UINT32_C(0x3f800000)}};
    static const uint32_t transformed[4][4]={
        {UINT32_C(0xbf000000),UINT32_C(0xbe800000),UINT32_C(0xbe800000),UINT32_C(0x3f000000)},
        {UINT32_C(0x3e800000),UINT32_C(0xbe800000),UINT32_C(0xbe800000),UINT32_C(0x3f000000)},
        {UINT32_C(0xbf000000),UINT32_C(0x3f000000),UINT32_C(0xbe800000),UINT32_C(0x3f000000)},
        {UINT32_C(0x3e800000),UINT32_C(0x3f000000),UINT32_C(0xbe800000),UINT32_C(0x3f000000)}};
    for(unsigned vertex=0;vertex<4;++vertex)for(unsigned lane=0;lane<4;++lane)
        put32(f->resources+vertex*16u+lane*4u,arithmetic?transformed[vertex][lane]:positions[vertex][lane]);
    const uint8_t order[]={0,1,2,1,3,2};unsigned width=short_indices?2u:1u;
    for(unsigned i=0;i<6;++i)f->resources[64u+i*width]=order[i];
    f->resource_bytes=64u+6u*width;
    command(f,0x040u,0u);command(f,0x041u,UINT32_C(0x440000));
    command(f,0x043u,UINT32_C(0x440000));command(f,0x065u,0u);command(f,0x068u,0u);
    command(f,0x04fu,2u);command(f,0x050u,UINT32_C(0x03020100));command(f,0x051u,UINT32_C(0x0b0a0908));
    command(f,0x200u,UINT32_C(0x03000000));command(f,0x201u,255u);command(f,0x202u,UINT32_C(0x10020000));
    command(f,0x203u,0u);command(f,0x204u,0u);command(f,0x205u,UINT32_C(0x10100000));
    command(f,0x228u,3u);command(f,0x229u,0u);command(f,0x22au,123u); /* Indexed draws ignore it. */
    command(f,0x25eu,0u);command(f,0x2b9u,UINT32_C(0xa0000001));command(f,0x2bau,0u);
    command(f,0x2bbu,UINT32_C(0x76543210));command(f,0x2bcu,UINT32_C(0xfedcba98));command(f,0x2bdu,3u);
    command(f,0x2cbu,0u);
    if(arithmetic) {
        /* r0=c0; r1=v0*r0; o0=c1+r1. Inputs encode (position-offset)/2. */
        command(f,0x2ccu,UINT32_C(0x4c000000)|(16u<<21)|(32u<<12));
        command(f,0x2ccu,(8u<<26)|(17u<<21)|(16u<<7)|1u);
        command(f,0x2ccu,(33u<<12)|(17u<<7)|1u);
    } else command(f,0x2ccu,UINT32_C(0x4c000000));
    command(f,0x2ccu,UINT32_C(0x4c201000));command(f,0x2ccu,UINT32_C(0x88000000));command(f,0x2bfu,1u);
    command(f,0x2d5u,0u);command(f,0x2d6u,UINT32_C(0x0000036f));
    if(arithmetic) {
        command(f,0x2d6u,UINT32_C(0x0006c36f));
        command(f,0x2c0u,UINT32_C(0x80000000));
        for(unsigned lane=0;lane<4;++lane)command(f,0x2c1u,UINT32_C(0x40000000));
        command(f,0x2c1u,0);command(f,0x2c1u,0);
        command(f,0x2c1u,UINT32_C(0xbe800000));command(f,0x2c1u,UINT32_C(0x3e800000));
    }
    color(f,false);command(f,0x227u,(short_indices?UINT32_C(0x80000000):0u)|64u);
    command(f,0x22fu,1u);
    color(f,true);command(f,0x227u,(short_indices?UINT32_C(0x80000000):0u)|(64u+3u*width));
    command(f,0x22fu,1u);command(f,CTR_PICA_FINALIZE,CTR_PICA_FINALIZE_VALUE);
    if(f->command_bytes&15u)command(f,CTR_PICA_FINALIZE,CTR_PICA_FINALIZE_VALUE);
}
void ctr_scene_fixture_make(ctr_scene_fixture *f,bool short_indices) { make(f,short_indices,false); }
void ctr_scene_fixture_make_arithmetic(ctr_scene_fixture *f,bool short_indices) { make(f,short_indices,true); }
void ctr_scene_fixture_background(void *rgba,size_t size) {
    uint8_t *bytes=rgba;if(!bytes)return;
    static const uint8_t background[]={16,32,48,255};
    for(size_t i=0;i<size;++i)bytes[i]=background[i&3u];
}
static ctr_scene_result render(bool short_indices,bool arithmetic,void *rgba,size_t capacity,
                               ctr_scene_report *report) {
    if(!rgba || capacity<CTR_SCENE_FIXTURE_BYTES)return CTR_SCENE_ARGUMENT;
    ctr_scene_fixture *f=malloc(sizeof(*f));ctr_scene_state *state=malloc(sizeof(*state));
    if(!f || !state) { free(f);free(state);return CTR_SCENE_NO_MEMORY; }
    make(f,short_indices,arithmetic);ctr_scene_init(state);
    ctr_scene_resources resources={UINT32_C(0x18000000),f->resources,f->resource_bytes,NULL,0};
    ctr_scene_surface surface={rgba,CTR_SCENE_FIXTURE_BYTES,64,64,256};
    ctr_scene_result result=ctr_scene_execute(state,f->commands,f->command_bytes,&resources,&surface,report);
    free(f);free(state);return result;
}
ctr_scene_result ctr_scene_fixture_render(bool shorts,void *rgba,size_t capacity,ctr_scene_report *report) {
    return render(shorts,false,rgba,capacity,report);
}
ctr_scene_result ctr_scene_fixture_render_arithmetic(bool shorts,void *rgba,size_t capacity,ctr_scene_report *report) {
    return render(shorts,true,rgba,capacity,report);
}
static int selftest(bool arithmetic) {
    uint8_t *pixels=malloc(CTR_SCENE_FIXTURE_BYTES);if(!pixels)return 1;
    int result=0;
    for(unsigned short_indices=0;short_indices<2 && !result;++short_indices) {
        ctr_scene_fixture_background(pixels,CTR_SCENE_FIXTURE_BYTES);ctr_scene_report report={0};
        if(render(short_indices!=0,arithmetic,pixels,CTR_SCENE_FIXTURE_BYTES,&report)!=CTR_SCENE_OK ||
           report.draws!=2u || report.triangles!=2u || report.fragments!=2304u || report.shader_instructions!=(arithmetic?30u:18u))
            result=2;
        for(unsigned y=0;y<64 && !result;++y)for(unsigned x=0;x<64;++x) {
            uint8_t expected[4]={16,32,48,255};
            if(x>=8 && x<56 && y>=8 && y<56) {
                expected[0]=x+y<63u?255u:0u;expected[1]=x+y<63u?0u:255u;expected[2]=0;
            }
            if(memcmp(pixels+(y*64u+x)*4u,expected,4u)) { result=3;break; }
        }
    }
    free(pixels);return result;
}
int ctr_scene_selftest(void) { return selftest(false); }
int ctr_scene_arithmetic_selftest(void) { return selftest(true); }
