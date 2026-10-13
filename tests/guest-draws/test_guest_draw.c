/* Original guest-memory/queue integration tests, no external/game input. */
#include "ctr_guest_pica.h"
#include "ctr_guest_fixture.h"
#include "ctr_scene_fixture.h"
#include "ctr_heap.h"
#include "ctr_gsp.h"
#include "ctr_kernel.h"
#include "ctr_texture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned assertions;
#define CHECK(x) do { ++assertions;if(!(x)) { fprintf(stderr,"line %u: %s\n",__LINE__,#x);exit(1); } } while(0)
enum { BASE=0x14000000u, PHYSICAL=0x20000000u, RESOURCE=BASE+0x1000u, TARGET=BASE+0x2000u, BYTES=64u*64u*4u };
typedef struct { uint8_t bytes[4096];size_t size; } list;
static uint32_t word(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static void put32(uint8_t *p,uint32_t value) { for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(value>>(i*8u)); }
static void add(list *l,unsigned reg,uint32_t value) {
    CHECK(l->size+8u<=sizeof(l->bytes));put32(l->bytes+l->size,value);
    put32(l->bytes+l->size+4u,UINT32_C(0xf0000)|reg);l->size+=8u;
}
static void finish(list *l) { add(l,CTR_PICA_FINALIZE,CTR_PICA_FINALIZE_VALUE);if(l->size&15u)add(l,CTR_PICA_FINALIZE,CTR_PICA_FINALIZE_VALUE); }
static void trim(list *l) { while(l->size && (word(l->bytes+l->size-4u)&65535u)==CTR_PICA_FINALIZE)l->size-=8u; }
static size_t find(const list *l,unsigned reg,unsigned instance) {
    for(size_t at=0;at<l->size;at+=8u)if((word(l->bytes+at+4u)&65535u)==reg) {
        if(!instance)return at;
        --instance;
    }
    CHECK(false);return 0;
}
static void set(list *l,unsigned reg,uint32_t value) { put32(l->bytes+find(l,reg,0),value); }
static void make(list *l,bool shorts) {
    memset(l,0,sizeof(*l));
    const unsigned disabled[]={0x080,0x08f,0x100,0x104,0x105,0x106,0x112,0x114,0x115,0x117,0x11b};
    for(unsigned i=0;i<sizeof(disabled)/sizeof(*disabled);++i)add(l,disabled[i],0);
    add(l,0x102,3);add(l,0x107,0xf00);add(l,0x113,15);
    const unsigned tev[]={0xc0,0xc8,0xd0,0xd8,0xf0,0xf8};
    for(unsigned i=0;i<6;++i) { add(l,tev[i],i?0xf000f:0);add(l,tev[i]+1u,0);add(l,tev[i]+2u,0);add(l,tev[i]+4u,0); }
    add(l,0x11d,(PHYSICAL+0x2000u)/8u);add(l,0x11e,UINT32_C(0x0103f040));
    ctr_scene_fixture f;ctr_scene_fixture_make(&f,shorts);
    CHECK(l->size+f.command_bytes<sizeof(l->bytes));memcpy(l->bytes+l->size,f.commands,f.command_bytes);l->size+=f.command_bytes;
    set(l,0x200,((PHYSICAL+0x1000u)/16u)*2u);
}
static uint8_t *backing(ctr_guest *g) { return g->allocations[0].storage; }
static void open(ctr_guest *g,list *l,bool shorts) {
    CHECK(ctr_frame_open(g));g->cpu.r[0]=0x10003;g->cpu.r[1]=BASE;g->cpu.r[2]=0;g->cpu.r[3]=65536;g->cpu.r[4]=3;
    ctr_heap_control(g);CHECK(!g->cpu.r[0] && g->cpu.r[1]==BASE);CHECK(g->allocations[0].linear);
    ctr_scene_fixture f;ctr_scene_fixture_make(&f,shorts);
    CHECK(ctr_memory_write_bytes(&g->memory,RESOURCE,f.resources,(uint32_t)f.resource_bytes)==CTR_MEM_OK);
    for(unsigned i=0;i<BYTES;i+=4u) { uint8_t *p=backing(g)+0x2000u+i;p[0]=255;p[1]=48;p[2]=32;p[3]=16; }
    make(l,shorts);
}
static ctr_gx_result execute(ctr_guest *g,const list *l,uint32_t address,ctr_gx_completion *c) {
    CHECK(ctr_memory_write_bytes(&g->memory,address,l->bytes,(uint32_t)l->size)==CTR_MEM_OK);
    uint32_t command[]={0x01000101,address,(uint32_t)l->size,0,0,0,0,1};
    return ctr_gx_execute(g,command,c);
}
static void golden(ctr_guest *g,bool complete) {
    uint8_t pixels[BYTES];CHECK(ctr_texture_decode(backing(g)+0x2000u,BYTES,0,64,64,pixels,sizeof(pixels))==CTR_TEXTURE_OK);
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
        uint8_t expected[]={16,32,48,255};
        if(x>=8 && x<56 && y>=8 && y<56 && (complete || x+y<63u)) {
            bool red=x+y<63u;expected[0]=red?255:0;expected[1]=red?0:255;expected[2]=0;
        }
        for(unsigned lane=0;lane<4;++lane)CHECK(pixels[(y*64u+x)*4u+lane]==expected[lane]);
    }
}
static void successful(void) {
    for(unsigned shorts=0;shorts<2;++shorts) {
        ctr_guest g;list l;ctr_gx_completion c;open(&g,&l,shorts!=0);
        uint8_t *target=backing(&g)+0x2000u;
        CHECK(ctr_memory_map(&g.memory,0x30000000u,BYTES,CTR_MEM_READ|CTR_MEM_EXEC,target,BYTES)==CTR_MEM_OK);
        CHECK(ctr_memory_write_bytes(&g.memory,BASE,l.bytes,(uint32_t)l.size)==CTR_MEM_OK);
        uint64_t generation=g.memory.generation,code=g.memory.code_generation;g.cpu.exclusive_valid=true;
        uint32_t command[]={0x01000101,BASE,(uint32_t)l.size,0,0,0,0,1};
        CHECK(ctr_gx_execute(&g,command,&c)==CTR_GX_OK && c.signal && c.irq==5);
        CHECK(g.memory.generation==generation+1 && g.memory.code_generation==code+1 && !g.cpu.exclusive_valid);
        CHECK(g.gsp.pica_scene && g.gsp.pica.committed_lists==1 && g.gsp.pica_draws==2 &&
              g.gsp.pica_triangles==2 && g.gsp.pica_fragments==2304 && g.gsp.pica_shader_instructions==18);
        golden(&g,true);ctr_guest_close(&g);CHECK(!g.gsp.pica_scene);
    }
    /* Complete setup, shader and fixed/index state persist across command lists. */
    ctr_guest g;list all,setup,red,green;ctr_gx_completion c;open(&g,&all,false);setup=all;
    setup.size=find(&setup,0x22f,0);finish(&setup);
    CHECK(execute(&g,&setup,BASE,&c)==CTR_GX_OK && !g.gsp.pica_draws && g.gsp.pica_scene);
    memset(&red,0,sizeof(red));add(&red,0x22f,1);finish(&red);
    CHECK(execute(&g,&red,BASE,&c)==CTR_GX_OK && g.gsp.pica_fragments==1128);golden(&g,false);
    memset(&green,0,sizeof(green));add(&green,0x232,1);add(&green,0x233,0x3f000000);
    add(&green,0x233,0x3f00);add(&green,0x233,0);add(&green,0x227,67);add(&green,0x22f,1);finish(&green);
    CHECK(execute(&g,&green,BASE,&c)==CTR_GX_OK && g.gsp.pica.committed_lists==3 &&
          g.gsp.pica_draws==2 && g.gsp.pica_shader_instructions==18);golden(&g,true);
    ctr_guest_close(&g);
}
static void rollback(void) {
    for(unsigned kind=0;kind<17;++kind) {
        ctr_guest g;list l;ctr_gx_completion c;open(&g,&l,false);ctr_gx_result expected=CTR_GX_UNSUPPORTED;
        if(kind==0) { trim(&l);add(&l,0x123,0);finish(&l); }
        if(kind==1)backing(&g)[0x1000u+69u]=255;
        if(kind==2)set(&l,0x100,1);
        if(kind==3)set(&l,0x107,0xf01);
        if(kind==4)set(&l,0x080,1);
        if(kind==5)set(&l,0xc0,3);
        if(kind==6)set(&l,0x117,1u<<16);
        if(kind==7) { set(&l,0x200,0x03000000);expected=CTR_GX_MEMORY; }
        if(kind==8) { set(&l,0x11d,(PHYSICAL+0x1000u)/8u);expected=CTR_GX_MEMORY; }
        if(kind==9) { set(&l,0x11d,PHYSICAL/8u);expected=CTR_GX_MEMORY; }
        if(kind==10) { CHECK(ctr_memory_change_range(&g.memory,TARGET+8u,8u,CTR_MEM_READ,false)==CTR_MEM_OK);expected=CTR_GX_MEMORY; }
        if(kind==11) { CHECK(ctr_memory_change_range(&g.memory,RESOURCE,128u,0,false)==CTR_MEM_OK);expected=CTR_GX_MEMORY; }
        if(kind==12) { put32(l.bytes+find(&l,0x11d,0)+4u,UINT32_C(0xe0000)|0x11du); }
        if(kind==13) { trim(&l);add(&l,0x233,0);finish(&l); }
        if(kind==14) { g.gsp.pica.committed_lists=UINT64_MAX; }
        if(kind==15) {
            size_t at=find(&l,0x22f,1);memmove(l.bytes+at+8u,l.bytes+at,l.size-at);l.size+=8u;
            put32(l.bytes+at,(PHYSICAL+0x6000u)/8u);put32(l.bytes+at+4u,UINT32_C(0xf0000)|0x11du);
            trim(&l);finish(&l);
        }
        if(kind==16) {
            size_t at=find(&l,0x22f,1);memmove(l.bytes+at+8u,l.bytes+at,l.size-at);l.size+=8u;
            put32(l.bytes+at,((PHYSICAL+0x1080u)/16u)*2u);put32(l.bytes+at+4u,UINT32_C(0xf0000)|0x200u);
            trim(&l);finish(&l);
        }
        CHECK(ctr_memory_write_bytes(&g.memory,BASE,l.bytes,(uint32_t)l.size)==CTR_MEM_OK);
        uint8_t before[BYTES];memcpy(before,backing(&g)+0x2000u,sizeof(before));ctr_pica_state previous=g.gsp.pica;
        uint64_t generation=g.memory.generation,code=g.memory.code_generation;g.cpu.exclusive_valid=true;
        uint32_t command[]={0x01000101,BASE,(uint32_t)l.size,0,0,0,0,0};
        CHECK(ctr_gx_execute(&g,command,&c)==expected && !c.signal);
        CHECK(!memcmp(before,backing(&g)+0x2000u,sizeof(before)) && !memcmp(&previous,&g.gsp.pica,sizeof(previous)));
        CHECK(!g.gsp.pica_scene && !g.gsp.pica_draws && generation==g.memory.generation && code==g.memory.code_generation && g.cpu.exclusive_valid);
        ctr_guest_close(&g);
    }
    /* A failed retained-state list preserves uploaded code and committed draws. */
    ctr_guest g;list l;ctr_gx_completion c;open(&g,&l,false);CHECK(execute(&g,&l,BASE,&c)==CTR_GX_OK);
    ctr_scene_state before=*g.gsp.pica_scene;ctr_pica_state state=g.gsp.pica;uint8_t pixels[BYTES];memcpy(pixels,backing(&g)+0x2000u,BYTES);
    trim(&l);add(&l,0x111,0);finish(&l);
    CHECK(execute(&g,&l,BASE,&c)==CTR_GX_UNSUPPORTED && !c.signal);
    CHECK(!memcmp(&before,g.gsp.pica_scene,sizeof(before)) && !memcmp(&state,&g.gsp.pica,sizeof(state)));
    CHECK(!memcmp(pixels,backing(&g)+0x2000u,BYTES) && g.gsp.pica_draws==2);
    ctr_guest_close(&g);
}
static uint32_t read_word(ctr_guest *g,uint32_t address) { uint32_t value;CHECK(ctr_memory_read(&g->memory,address,4,CTR_MEM_READ,&value)==CTR_MEM_OK);return value; }
static void write_word(ctr_guest *g,uint32_t address,uint32_t value) { CHECK(ctr_memory_write(&g->memory,address,4,value)==CTR_MEM_OK); }
static unsigned relay(ctr_guest *g) {
    uint32_t handle=ctr_guest_session_open(g,CTR_SERVICE_GSP),out[16];unsigned count;
    unsigned owner=ctr_guest_handle_for(g,handle)->object;
    uint32_t acquire[]={0x160042,0,0,0xffff8001};CHECK(ctr_gsp_request(g,owner,acquire,out,&count));
    uint32_t event=ctr_guest_event_create(g,0),register_words[]={0x130042,1,0,event};
    CHECK(ctr_gsp_request(g,owner,register_words,out,&count) && !out[1]);
    g->cpu.r[0]=out[4];g->cpu.r[1]=0x10002000;g->cpu.r[2]=3;g->cpu.r[3]=3;ctr_guest_svc(g,0x1f);CHECK(!g->cpu.r[0]);return owner;
}
static void queues(void) {
    ctr_guest g;list l,bad;open(&g,&l,false);unsigned owner=relay(&g);bad=l;trim(&bad);add(&bad,0x123,0);finish(&bad);
    CHECK(ctr_memory_write_bytes(&g.memory,BASE,l.bytes,(uint32_t)l.size)==CTR_MEM_OK);
    CHECK(ctr_memory_write_bytes(&g.memory,BASE+0x800u,bad.bytes,(uint32_t)bad.size)==CTR_MEM_OK);
    write_word(&g,0x10002800,14u|(2u<<8));
    for(unsigned entry=0;entry<2;++entry) {
        uint32_t w[]={0x01000101,BASE+(entry?0x800u:0),(uint32_t)(entry?bad.size:l.size),0,0,0,0,1};
        for(unsigned i=0;i<8;++i)write_word(&g,0x10002820u+(entry?0:14u)*32u+i*4u,w[i]);
    }
    uint32_t trigger[]={0xc0000},out[8];unsigned count;
    CHECK(!ctr_gsp_request(&g,owner,trigger,out,&count) && g.state==CTR_GUEST_FAULTED);
    CHECK(read_word(&g,0x10002800)==0x100u && g.gsp.gx_commands==1 && g.gsp.gx_pica==1 && g.gsp.pica_draws==2);
    CHECK(g.shared[g.gsp.relay_shared].storage[1]==1 && g.shared[g.gsp.relay_shared].storage[12]==5);
    CHECK(g.events[g.gsp.relay_event].signaled);golden(&g,true);ctr_guest_close(&g);
    /* Relay alias through a device allocation is rejected before any mutation. */
    open(&g,&l,false);relay(&g);ctr_guest_shared *shared=&g.shared[g.gsp.relay_shared];uint8_t *old=shared->storage;
    shared->storage=backing(&g)+0x2000u;ctr_gx_completion c;
    uint8_t before[BYTES];memcpy(before,shared->storage,BYTES);
    CHECK(execute(&g,&l,BASE,&c)==CTR_GX_MEMORY && c.memory==CTR_MEM_OVERLAP && !c.signal);
    CHECK(!memcmp(before,shared->storage,BYTES) && !g.gsp.pica_scene);shared->storage=old;ctr_guest_close(&g);
}
int main(void) { successful();rollback();queues();printf("guest PICA draw tests: %u assertions passed\n",assertions);return 0; }
