#include "ctr_gx.h"
#include "ctr_gsp.h"
#include "ctr_kernel.h"
#include "ctr_guest_fixture.h"
#include "ctr_transfer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned assertions;
#define CHECK(x) do { ++assertions;if(!(x)) { fprintf(stderr,"line %u: %s\n",__LINE__,#x);exit(1); } } while(0)
static void put(ctr_guest *g,uint32_t at,uint32_t word) { CHECK(ctr_memory_write(&g->memory,at,4,word)==CTR_MEM_OK); }
static uint32_t get(ctr_guest *g,uint32_t at) { uint32_t word;CHECK(ctr_memory_read(&g->memory,at,4,1,&word)==CTR_MEM_OK);return word; }
static uint8_t buffers[8192];
static void open(ctr_guest *g) { CHECK(ctr_frame_open(g));memset(buffers,0xa5,sizeof(buffers));CHECK(ctr_memory_map(&g->memory,0x30000000,sizeof(buffers),3,buffers,sizeof(buffers))==CTR_MEM_OK); }
static uint32_t register_queue(ctr_guest *g) {
    uint32_t s=ctr_guest_session_open(g,CTR_SERVICE_GSP),out[16];unsigned count;
    uint32_t a[]={0x160042,0,0,0xffff8001};CHECK(ctr_gsp_request(g,ctr_guest_handle_for(g,s)->object,a,out,&count));
    uint32_t e=ctr_guest_event_create(g,0),r[]={0x130042,1,0,e};unsigned owner=ctr_guest_handle_for(g,s)->object;
    CHECK(ctr_gsp_request(g,owner,r,out,&count) && !out[1]);
    g->cpu.r[0]=out[4];g->cpu.r[1]=0x10002000;g->cpu.r[2]=3;g->cpu.r[3]=3;ctr_guest_svc(g,0x1f);CHECK(!g->cpu.r[0]);
    return owner;
}
static void enqueue(ctr_guest *g,unsigned index,unsigned count,const uint32_t *w) {
    put(g,0x10002800,index|(count<<8));for(unsigned i=0;i<8;++i)put(g,0x10002820+index*32+i*4,w[i]);
}
static bool trigger(ctr_guest *g,unsigned owner) { uint32_t w[]={0xc0000},out[8];unsigned count;return ctr_gsp_request(g,owner,w,out,&count) && !out[1]; }
static void bulk(void) {
    ctr_memory m;ctr_memory_init(&m);uint8_t first[16]={0},second[16]={0},data[32],read[32];
    for(unsigned i=0;i<32;++i)data[i]=(uint8_t)i;
    CHECK(ctr_memory_map(&m,0x1000,16,3,first,sizeof(first))==CTR_MEM_OK);CHECK(ctr_memory_map(&m,0x1010,16,3,second,sizeof(second))==CTR_MEM_OK);
    uint64_t generation=m.generation;CHECK(ctr_memory_write_bytes(&m,0x1000,data,32)==CTR_MEM_OK && m.generation==generation+1);
    CHECK(ctr_memory_read_bytes(&m,0x1000,read,32)==CTR_MEM_OK && !memcmp(read,data,32));
    CHECK(ctr_memory_read_bytes(&m,0x1000,first,16)==CTR_MEM_OVERLAP);
    CHECK(ctr_memory_write_bytes(&m,0x1000,second,16)==CTR_MEM_OVERLAP);
    CHECK(ctr_memory_map(&m,0x2000,16,5,second,sizeof(second))==CTR_MEM_OK);uint64_t code=m.code_generation;
    CHECK(ctr_memory_write_bytes(&m,0x100f,data,2)==CTR_MEM_OK && m.code_generation==code+1);
    CHECK(ctr_memory_change_range(&m,0x1010,16,1,false)==CTR_MEM_OK);generation=m.generation;code=m.code_generation;uint8_t before[16];memcpy(before,first,16);
    CHECK(ctr_memory_write_bytes(&m,0x1000,data,32)==CTR_MEM_PERMISSION && m.generation==generation && m.code_generation==code && !memcmp(first,before,16));
    memset(read,0xa5,32);CHECK(ctr_memory_read_bytes(&m,0x1000,read,33)==CTR_MEM_UNMAPPED);for(unsigned i=0;i<32;++i)CHECK(read[i]==0xa5);
    CHECK(ctr_memory_write_bytes(&m,0xfffffff0,data,32)==CTR_MEM_OVERFLOW);
    CHECK(ctr_memory_write_bytes(&m,0x1000,NULL,0)==CTR_MEM_OK && m.generation==generation);
    CHECK(ctr_memory_read_bytes(&m,0x1000,NULL,1)==CTR_MEM_OVERFLOW);
}
static void transfers(void) {
    uint8_t source[512],out[512],back[512];for(unsigned i=0;i<512;++i)source[i]=(uint8_t)(i*19);
    for(unsigned format=0;format<5;++format) {
        uint32_t flags=2|(format<<8)|(format<<12),dim=16|(8<<16);ctr_transfer t;
        CHECK(ctr_transfer_init(&t,dim,dim,flags));CHECK(ctr_transfer_convert(dim,dim,flags,source,sizeof(source),out,sizeof(out)));
        CHECK(ctr_transfer_convert(dim,dim,flags&~2u,out,sizeof(out),back,sizeof(back)));CHECK(!memcmp(source,back,t.input_bytes));
        CHECK(ctr_transfer_convert(dim,dim,flags|1,source,sizeof(source),out,sizeof(out)));
        CHECK(ctr_transfer_convert(dim,dim,(flags&~2u)|1,out,sizeof(out),back,sizeof(back)));CHECK(!memcmp(source,back,t.input_bytes));
    }
    memset(source,0,256);for(unsigned i=0;i<64;++i) { source[i*4]=255;source[i*4+2]=255; }
    CHECK(ctr_transfer_convert(8|(8<<16),8|(8<<16),0x2000,source,256,out,128));
    for(unsigned i=0;i<128;i+=2)CHECK(out[i]==0xe0 && out[i+1]==7);
    memset(out,0xa5,512);
    const uint32_t invalid[]={8,0x1000000,0x10,0x80000000,0x500,0x5000};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);++i)CHECK(!ctr_transfer_convert(8|(8<<16),8|(8<<16),invalid[i],source,256,out,512));
    CHECK(!ctr_transfer_convert(8|(8<<16),8|(8<<16),0,source,255,out,512));
    CHECK(!ctr_transfer_convert(8|(8<<16),8|(8<<16),0,source,256,source+4,256));
    CHECK(!ctr_transfer_convert(8|(8<<16),8|(16<<16),0,source,256,out,512));
    for(unsigned i=0;i<512;++i)CHECK(out[i]==0xa5);
}
static void operations(void) {
    ctr_guest g;open(&g);ctr_gx_completion c;
    uint32_t fill[]={0x01000102,0x30000000,0x12345678,0x30000018,0,0,0,0x201};
    CHECK(ctr_gx_execute(&g,fill,&c)==CTR_GX_OK && c.signal && c.irq==0);
    for(unsigned i=0;i<24;++i)CHECK(buffers[i]==(uint8_t)(fill[2]>>((i%4)*8)));
    fill[7]=0x101;CHECK(ctr_gx_execute(&g,fill,&c)==CTR_GX_OK);for(unsigned i=0;i<24;++i)CHECK(buffers[i]==(uint8_t)(fill[2]>>((i%3)*8)));
    fill[7]=1;CHECK(ctr_gx_execute(&g,fill,&c)==CTR_GX_OK);for(unsigned i=0;i<24;++i)CHECK(buffers[i]==(uint8_t)(fill[2]>>((i%2)*8)));
    uint32_t dma[]={0x01000100,0x30000000,0x30000008,24,0,0,0,0};uint8_t before[24];memcpy(before,buffers,24);
    CHECK(ctr_gx_execute(&g,dma,&c)==CTR_GX_OK && c.irq==6 && !memcmp(buffers+8,before,24));
    uint32_t copy[]={0x01000104,0x30000000,0x30001000,64,1|(1<<16),2|(1<<16),8,0};
    for(unsigned i=0;i<128;++i)buffers[i]=(uint8_t)i;
    CHECK(ctr_gx_execute(&g,copy,&c)==CTR_GX_OK && c.irq==4);
    for(unsigned k=0;k<64;++k)CHECK(buffers[4096+k+(k/32)*16]==k+(k/16)*16);
    for(unsigned i=32;i<48;++i)CHECK(buffers[4096+i]==0xa5);
    uint32_t cache[]={0x00000105,0x30000000,24,0,0,0,0,0};uint64_t generation=g.memory.generation;
    CHECK(ctr_gx_execute(&g,cache,&c)==CTR_GX_OK && !c.signal && g.memory.generation==generation);
    ctr_guest_close(&g);
}
static void rejection(void) {
    for(unsigned kind=0;kind<14;++kind) {
        ctr_guest g;open(&g);register_queue(&g);ctr_gx_completion c;
        uint32_t w[]={0x01000100,0x30000000,0x30001000,64,0,0,0,0};ctr_gx_result expected=CTR_GX_UNSUPPORTED;
        if(kind==0)w[0]=0x01000101;
        if(kind==1)w[4]=1;
        if(kind==2)w[3]=0;
        if(kind==3)w[3]=4*1024*1024+1;
        if(kind==4) { w[1]=0xdead0000;expected=CTR_GX_MEMORY; }
        if(kind==5) { w[2]=0xfffffff0;expected=CTR_GX_MEMORY; }
        if(kind==6) { CHECK(ctr_memory_change_range(&g.memory,0x30001010,16,1,false)==CTR_MEM_OK);expected=CTR_GX_MEMORY; }
        if(kind==7) { w[2]=0x10002000;expected=CTR_GX_MEMORY; }
        if(kind==8) { uint32_t f[]={0x01000102,0x30000000,0,0x30000018,0xdead0000,0,0xdead0018,0x2010201};memcpy(w,f,sizeof(w));expected=CTR_GX_MEMORY; }
        if(kind==9) { uint32_t f[]={0x01000102,0,0,8,0x30001000,0,0x30001018,0x2010000};memcpy(w,f,sizeof(w)); }
        if(kind==10) { w[0]=0x01000103;w[3]=w[4]=8|(8<<16);w[5]=1<<24; }
        if(kind==11) { w[0]=0x01000104;w[4]=0x10000;w[6]=8; }
        if(kind==12) { w[0]=0x01000104;w[3]=63;w[6]=8; }
        if(kind==13) { uint32_t f[]={0x01000102,0x30000000,0,0x30000018,0,0,0,0x200};memcpy(w,f,sizeof(w)); }
        uint8_t before[8192];memcpy(before,buffers,sizeof(before));uint64_t generation=g.memory.generation;
        CHECK(ctr_gx_execute(&g,w,&c)==expected);CHECK(!memcmp(before,buffers,sizeof(before)) && generation==g.memory.generation);
        ctr_guest_close(&g);
    }
}
static void queue(void) {
    ctr_guest g;open(&g);unsigned owner=register_queue(&g);uint32_t w[]={0x01000100,0x30000000,0x30001000,64,0,0,0,0};
    for(unsigned i=0;i<64;++i)buffers[i]=(uint8_t)i;
    enqueue(&g,14,1,w);CHECK(trigger(&g,owner));CHECK(!get(&g,0x10002800) && g.gsp.gx_dma==1 && g.gsp.gx_commands==1);
    CHECK(get(&g,0x10002000)==0x100 && g.shared[g.gsp.relay_shared].storage[12]==6 && !memcmp(buffers,buffers+4096,64));
    CHECK(g.events[g.gsp.relay_event].signaled);
    /* Successful prefix remains committed; unsupported draw remains pending. */
    enqueue(&g,13,2,w);uint32_t draw[8]={0x01000101};for(unsigned i=0;i<8;++i)put(&g,0x10002820+14*32+i*4,draw[i]);
    CHECK(!trigger(&g,owner) && g.state==CTR_GUEST_FAULTED && g.fault==CTR_CPU_UNSUPPORTED);
    CHECK(get(&g,0x10002800)==0x10e && g.gsp.gx_dma==2 && g.gsp.gx_commands==2 && g.gsp.last_gx_header==draw[0]);ctr_guest_close(&g);
    open(&g);owner=register_queue(&g);w[0]|=0x10000;enqueue(&g,14,2,w);
    CHECK(trigger(&g,owner) && get(&g,0x10002800)==0x10100 && g.gsp.gx_commands==1);ctr_guest_close(&g);
    open(&g);owner=register_queue(&g);w[0]&=~0x10000u;enqueue(&g,14,1,w);put(&g,0x10002800,0x0100010e);
    CHECK(trigger(&g,owner) && get(&g,0x10002800)==0x0101010e && !g.gsp.gx_commands);ctr_guest_close(&g);
    for(unsigned bad=0;bad<5;++bad) {
        open(&g);owner=register_queue(&g);enqueue(&g,0,1,w);
        put(&g,0x10002800,bad==0?15:bad==1?16<<8:bad==2?2<<16:bad==3?2<<24:0x100);
        if(bad==4)put(&g,0x10002804,1);
        uint8_t before[4096];memcpy(before,g.shared[g.gsp.relay_shared].storage,4096);CHECK(!trigger(&g,owner));
        CHECK(!memcmp(before,g.shared[g.gsp.relay_shared].storage,4096) && !g.gsp.gx_commands && !g.events[g.gsp.relay_event].signaled);ctr_guest_close(&g);
    }
    open(&g);owner=register_queue(&g);enqueue(&g,0,1,w);put(&g,0x10002000,51|(52<<8));CHECK(trigger(&g,owner));
    CHECK(g.shared[g.gsp.relay_shared].storage[2]==1 && g.gsp.dropped_interrupts==1 && g.gsp.gx_commands==1 && !g.events[g.gsp.relay_event].signaled);ctr_guest_close(&g);
}
int main(void) {
    bulk();transfers();operations();rejection();queue();
    printf("{\"ok\":true,\"assertions\":%u,\"gx_queue\":true,\"cpu_transfers\":true,\"console_verified\":false}\n",assertions);return 0;
}
