#include "ctr_gsp_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned assertions;
#define CHECK(x) do { ++assertions;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static void put(uint8_t *p,uint32_t value,unsigned width) { for(unsigned i=0;i<width;++i)p[i]=(uint8_t)(value>>(8*i)); }
static void send(ctr_guest *g,uint32_t session,const uint32_t *words,unsigned count) {
    for(unsigned i=0;i<count;++i)CHECK(ctr_memory_write(&g->memory,g->cpu.tls_address+0x80+i*4,4,words[i])==CTR_MEM_OK);
    g->cpu.r[0]=session;ctr_guest_svc(g,0x32);
}
static void display_formats(void) {
    ctr_guest g;CHECK(ctr_gsp_fixture_open(&g));uint32_t session=ctr_guest_session_open(&g,CTR_SERVICE_GSP);CHECK(session);
    const uint32_t acquire[]={0x160042,0,0,0xffff8001};send(&g,session,acquire,4);
    CHECK(g.gsp.right && g.state==CTR_GUEST_READY);uint32_t word;
    CHECK(ctr_memory_read(&g.memory,g.cpu.tls_address+0x80,4,1,&word)==CTR_MEM_OK && word==0x160040);
    CHECK(ctr_memory_read(&g.memory,g.cpu.tls_address+0x84,4,1,&word)==CTR_MEM_OK && !word);
    uint8_t *source=calloc(1,400*4096);uint32_t *pixels=malloc(400*240*sizeof(*pixels));CHECK(source && pixels);
    /* Adjacent maps split even the first pixel; no cast to a host guest pointer. */
    CHECK(ctr_memory_map(&g.memory,0x20000000,1,3,source,1)==CTR_MEM_OK);
    CHECK(ctr_memory_map(&g.memory,0x20000001,400*4096-1,3,source+1,400*4096-1)==CTR_MEM_OK);
    static const uint32_t raw[][5]={{0xffffffff,0xff0000ff,0x00ff00ff,0x0000ffff,0x12345678},
        {0xffffff,0xff0000,0x00ff00,0x0000ff,0x112233},{0xffff,0xf800,0x07e0,0x001f,0xf81f},
        {0xffff,0xf801,0x07c1,0x003f,0xf800},{0xffff,0xf00f,0x0f0f,0x00ff,0x1234}};
    static const uint32_t interior[]={0x12345678,0x112233ff,0xff00ffff,0xff000000,0x11223344};
    for(unsigned screen=0;screen<2;++screen)for(unsigned format=0;format<5;++format) {
        unsigned bpp=format==0?4:format==1?3:2,width=screen?320:400,stride=240*bpp+8;memset(source,0,400*4096);
        put(source+239*bpp,raw[format][0],bpp);put(source+(width-1)*stride+239*bpp,raw[format][1],bpp);
        put(source,raw[format][2],bpp);put(source+(width-1)*stride,raw[format][3],bpp);
        put(source+stride+238*bpp,raw[format][4],bpp);
        uint32_t request[]={0x50200,screen,1,0x20000000,0x20000000,stride,0x100|format|(screen?0:0x40),1,0};
        send(&g,session,request,9);CHECK(g.state==CTR_GUEST_READY && !g.cpu.r[0] && g.gsp.swaps[screen]==format+1);
        CHECK(ctr_memory_read(&g.memory,g.cpu.tls_address+0x80,4,1,&word)==CTR_MEM_OK && word==0x50040);
        CHECK(ctr_gsp_pixels(&g,screen,pixels,width*240)==CTR_MEM_OK);
        CHECK(pixels[0]==0xffffffff && pixels[width-1]==0xff0000ff && pixels[239*width]==0x00ff00ff && pixels[width*240-1]==0x0000ffff);
        CHECK(pixels[width+1]==interior[format]);
    }
    const uint32_t black[]={0xb0040,1};send(&g,session,black,2);CHECK(g.gsp.black);
    CHECK(ctr_gsp_pixels(&g,0,pixels,400*240)==CTR_MEM_OK && pixels[0]==0xff && pixels[400*240-1]==0xff);
    const uint32_t visible[]={0xb0040,0};send(&g,session,visible,2);CHECK(!g.gsp.black);
    memset(pixels,0xab,400*240*sizeof(*pixels));CHECK(ctr_memory_change_range(&g.memory,0x20000001,1,0,false)==CTR_MEM_OK);
    CHECK(ctr_gsp_pixels(&g,0,pixels,400*240)==CTR_MEM_PERMISSION && pixels[0]==0xabababab && pixels[400*240-1]==0xabababab);
    CHECK(ctr_gsp_pixels(&g,2,pixels,400*240)==CTR_MEM_UNMAPPED);
    CHECK(ctr_gsp_pixels(&g,0,pixels,1)==CTR_MEM_UNMAPPED);
    uint32_t duplicate=ctr_guest_handle_new(&g,CTR_HANDLE_SESSION,ctr_guest_handle_for(&g,session)->object);CHECK(duplicate);
    CHECK(ctr_guest_handle_close(&g,session) && g.gsp.right);
    CHECK(ctr_guest_handle_close(&g,duplicate) && !g.gsp.right);
    ctr_guest_close(&g);free(source);free(pixels);
}
static void rejection(void) {
    for(unsigned kind=0;kind<15;++kind) {
        ctr_guest g;CHECK(ctr_gsp_fixture_open(&g));uint32_t session=ctr_guest_session_open(&g,CTR_SERVICE_GSP);CHECK(session);
        g.gsp.right=kind!=0;uint32_t request[]={0x50200,0,0,0x08001000,0x08001000,480,0x142,0,0};
        if(kind==1)request[1]=2;
        if(kind==2)request[2]=2;
        if(kind==3)request[7]=1;
        if(kind==4)request[4]+=4;
        if(kind==5)request[5]=478;
        if(kind==6)request[5]=4097;
        if(kind==7)request[6]=0x145;
        if(kind==8)request[6]=0x162;
        if(kind==9)request[6]=0x342;
        if(kind==10)request[8]=1;
        if(kind==11)request[3]=request[4]=0xfffffff0;
        if(kind==12)request[3]=request[4]=0xdead0000;
        if(kind==13)request[0]=0xc0000; /* No generic-success command queue. */
        if(kind==14)request[0]=0x130042;
        ctr_guest_gsp before=g.gsp;send(&g,session,request,9);
        CHECK(g.state==CTR_GUEST_FAULTED && g.ipc_failures==1 && !memcmp(&before,&g.gsp,sizeof(before)));
        CHECK(g.fault==(kind==11 || kind==12?CTR_CPU_MEMORY_FAULT:CTR_CPU_UNSUPPORTED));ctr_guest_close(&g);
    }
}
int main(void) {
    display_formats();rejection();int result=ctr_gsp_selftest();if(result)fprintf(stderr,"gsp selftest code=%d\n",result);CHECK(!result);
    printf("{\"ok\":true,\"assertions\":%u,\"original_arm_hid_gsp_display\":true,\"rotated_framebuffer_formats\":5,\"console_verified\":false}\n",assertions);return 0;
}
