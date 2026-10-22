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
static void make(list *l,bool shorts,bool arithmetic) {
    memset(l,0,sizeof(*l));
    const unsigned disabled[]={0x080,0x08f,0x100,0x104,0x105,0x106,0x112,0x114,0x115,0x117,0x11b};
    for(unsigned i=0;i<sizeof(disabled)/sizeof(*disabled);++i)add(l,disabled[i],0);
    add(l,0x102,3);add(l,0x107,0xf00);add(l,0x113,15);
    const unsigned tev[]={0xc0,0xc8,0xd0,0xd8,0xf0,0xf8};
    for(unsigned i=0;i<6;++i) { add(l,tev[i],i?0xf000f:0);add(l,tev[i]+1u,0);add(l,tev[i]+2u,0);add(l,tev[i]+4u,0); }
    add(l,0x11d,(PHYSICAL+0x2000u)/8u);add(l,0x11e,UINT32_C(0x0103f040));
    ctr_scene_fixture f;
    if(arithmetic)ctr_scene_fixture_make_arithmetic(&f,shorts);else ctr_scene_fixture_make(&f,shorts);
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
    make(l,shorts,false);
}
static void arithmetic_setup(ctr_guest *g,list *l,bool shorts) {
    ctr_scene_fixture f;ctr_scene_fixture_make_arithmetic(&f,shorts);
    CHECK(ctr_memory_write_bytes(&g->memory,RESOURCE,f.resources,(uint32_t)f.resource_bytes)==CTR_MEM_OK);
    make(l,shorts,true);
}
static void numeric_failure(list *l) {
    /* The first arithmetic draw succeeds in the staging copy. The second ADD
     * would discard a tiny nonzero term if host FP silently rounded it away. */
    l->size=find(l,0x22f,1);add(l,0x2c0,UINT32_C(0x80000001));
    add(l,0x2c1,0);add(l,0x2c1,0);add(l,0x2c1,UINT32_C(0xbe800000));
    add(l,0x2c1,UINT32_C(0x20800000));add(l,0x22f,1);finish(l);
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
static void shader_capacity(void) {
    ctr_guest g;list l;ctr_gx_completion c;open(&g,&l,false);
    set(&l,0x2cb,509);set(&l,0x2ba,509);
    CHECK(execute(&g,&l,BASE,&c)==CTR_GX_OK && c.signal && c.irq==5);
    CHECK(g.gsp.pica_scene->shader.code[511]==UINT32_C(0x88000000) &&
          g.gsp.pica_scene->shader.code_defined[511] && g.gsp.pica_scene->registers[0x2cb]==512u);
    golden(&g,true);
    ctr_scene_state before=*g.gsp.pica_scene;ctr_pica_state state=g.gsp.pica;
    uint8_t pixels[BYTES];memcpy(pixels,backing(&g)+0x2000u,BYTES);
    /* The failed list stages two more draws, overwrites the final code word and
     * then crosses the capacity. Preserve storage, pixels, generations and IRQ. */
    trim(&l);add(&l,0x2cb,511);add(&l,0x2d3,UINT32_C(0x84000000));add(&l,0x2cc,UINT32_C(0x88000000));finish(&l);
    CHECK(ctr_memory_write_bytes(&g.memory,BASE,l.bytes,(uint32_t)l.size)==CTR_MEM_OK);
    uint64_t generation=g.memory.generation,code=g.memory.code_generation;g.cpu.exclusive_valid=true;
    uint32_t command[]={0x01000101,BASE,(uint32_t)l.size,0,0,0,0,1};
    CHECK(ctr_gx_execute(&g,command,&c)==CTR_GX_UNSUPPORTED && !c.signal);
    CHECK(g.gsp.last_scene_result==CTR_SCENE_LIMIT && g.gsp.last_pica_register==0x2cc);
    CHECK(!memcmp(&before,g.gsp.pica_scene,sizeof(before)) && !memcmp(&state,&g.gsp.pica,sizeof(state)));
    CHECK(!memcmp(pixels,backing(&g)+0x2000u,BYTES) && g.gsp.pica_draws==2u);
    CHECK(g.memory.generation==generation && g.memory.code_generation==code && g.cpu.exclusive_valid);
    ctr_guest_close(&g);
}
static void arithmetic_draws(void) {
    for(unsigned shorts=0;shorts<2;++shorts) {
        ctr_guest g;list l;ctr_gx_completion c;open(&g,&l,shorts!=0);
        arithmetic_setup(&g,&l,shorts!=0);
        CHECK(execute(&g,&l,BASE,&c)==CTR_GX_OK && c.signal && c.irq==5);
        CHECK(g.gsp.pica_draws==2 && g.gsp.pica_shader_instructions==30);golden(&g,true);
        ctr_scene_state before=*g.gsp.pica_scene;ctr_pica_state state=g.gsp.pica;
        uint8_t pixels[BYTES];memcpy(pixels,backing(&g)+0x2000u,BYTES);
        numeric_failure(&l);
        CHECK(ctr_memory_write_bytes(&g.memory,BASE,l.bytes,(uint32_t)l.size)==CTR_MEM_OK);
        uint64_t generation=g.memory.generation,code=g.memory.code_generation;g.cpu.exclusive_valid=true;
        uint32_t command[]={0x01000101,BASE,(uint32_t)l.size,0,0,0,0,1};
        CHECK(ctr_gx_execute(&g,command,&c)==CTR_GX_UNSUPPORTED && !c.signal);
        CHECK(g.gsp.last_scene_result==CTR_SCENE_SHADER && g.gsp.last_shader_result==CTR_SHADER_NUMERIC);
        CHECK(!memcmp(&before,g.gsp.pica_scene,sizeof(before)) && !memcmp(&state,&g.gsp.pica,sizeof(state)));
        CHECK(!memcmp(pixels,backing(&g)+0x2000u,BYTES) && g.gsp.pica_draws==2);
        CHECK(g.memory.generation==generation && g.memory.code_generation==code && g.cpu.exclusive_valid);
        ctr_guest_close(&g);
    }
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
    ctr_guest g;list l,bad;
    for(unsigned kind=0;kind<3;++kind) {
        open(&g,&l,false);unsigned owner=relay(&g);
        if(kind==1) { set(&l,0x2cb,509);set(&l,0x2ba,509); }
        if(kind==2)arithmetic_setup(&g,&l,false);
        bad=l;trim(&bad);
        if(kind==1) { add(&bad,0x2cb,511);add(&bad,0x2cc,0);add(&bad,0x2cc,0); }
        else if(kind==2)numeric_failure(&bad);
        else add(&bad,0x123,0);
        if(kind!=2)finish(&bad);
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
        if(kind==1)CHECK(g.gsp.pica_scene->shader.code[511]==UINT32_C(0x88000000) &&
                     g.gsp.pica_scene->registers[0x2cb]==512u && g.gsp.last_scene_result==CTR_SCENE_LIMIT);
        if(kind==2)CHECK(g.gsp.last_scene_result==CTR_SCENE_SHADER &&
                        g.gsp.last_shader_result==CTR_SHADER_NUMERIC &&
                        g.gsp.pica_scene->shader.uniforms[1][0]==UINT32_C(0x3e800000));
        CHECK(g.events[g.gsp.relay_event].signaled);golden(&g,true);ctr_guest_close(&g);
    }
    /* Relay alias through a device allocation is rejected before any mutation. */
    open(&g,&l,false);relay(&g);ctr_guest_shared *shared=&g.shared[g.gsp.relay_shared];uint8_t *old=shared->storage;
    shared->storage=backing(&g)+0x2000u;ctr_gx_completion c;
    uint8_t before[BYTES];memcpy(before,shared->storage,BYTES);
    CHECK(execute(&g,&l,BASE,&c)==CTR_GX_MEMORY && c.memory==CTR_MEM_OVERLAP && !c.signal);
    CHECK(!memcmp(before,shared->storage,BYTES) && !g.gsp.pica_scene);shared->storage=old;ctr_guest_close(&g);
}
typedef struct {
    unsigned begins,draws,ends,aborts;
    ctr_scene_backend_stage fail;
    bool active,arithmetic;
    uint8_t scratch[BYTES];
} backend_mock;
static bool backend_begin(void *context,const ctr_scene_surface *initial) {
    backend_mock *m=context;CHECK(!m->active);++m->begins;m->active=true;
    CHECK(initial->width==64 && initial->height==64 && initial->stride==256 && initial->size==BYTES);
    memcpy(m->scratch,initial->bytes,sizeof(m->scratch));
    return m->fail!=CTR_SCENE_BACKEND_BEGIN;
}
static bool backend_draw(void *context,const ctr_scene_state *s,const ctr_vertex *v,unsigned count,const ctr_scene_texture *texture) {
    CHECK(!texture);
    backend_mock *m=context;CHECK(m->active && !m->ends && count==3);++m->draws;
    CHECK(m->draws<=2 && s->shader.entry==0 && s->registers[0x2bd]==3 && s->registers[0x4f]==2);
    CHECK(s->registers[0x50]==UINT32_C(0x03020100) && s->registers[0x51]==UINT32_C(0x0b0a0908));
    CHECK(s->registers[0x040]==0 && s->registers[0x065]==0 && s->registers[0x041]==UINT32_C(0x440000));
    CHECK(s->shader.code[0]==(m->arithmetic?UINT32_C(0x4e020000):UINT32_C(0x4c000000)));
    if(m->arithmetic)CHECK(s->shader.uniforms[0][0]==UINT32_C(0x40000000) &&
                           s->shader.uniforms[1][0]==UINT32_C(0x3e800000));
    /* Inputs must remain original decoded attributes, not CPU-transformed clip
     * positions. Fixed primary color must reflect the current draw's upload. */
    const float positions[4][2]={{-0.75f,-0.75f},{0.75f,-0.75f},{-0.75f,0.75f},{0.75f,0.75f}};
    const float raw[4][2]={{-0.5f,-0.25f},{0.25f,-0.25f},{-0.5f,0.5f},{0.25f,0.5f}};
    const unsigned order[2][3]={{0,1,2},{1,3,2}};
    for(unsigned vertex=0;vertex<3;++vertex) {
        unsigned source=order[m->draws-1u][vertex];CHECK(v[vertex].enabled==3);
        CHECK(v[vertex].input[0][0]==(m->arithmetic?raw[source][0]:positions[source][0]));
        CHECK(v[vertex].input[0][1]==(m->arithmetic?raw[source][1]:positions[source][1]));
        CHECK(v[vertex].input[0][3]==(m->arithmetic?0.5f:1.0f));
        CHECK(v[vertex].input[1][0]==(m->draws==1?1.0f:0.0f) &&
              v[vertex].input[1][1]==(m->draws==2?1.0f:0.0f) && v[vertex].input[1][3]==1.0f);
    }
    if(m->fail==CTR_SCENE_BACKEND_DRAW && m->draws==2)return false;
    /* Independent analytic fixture coverage, no scene/shader implementation. */
    for(unsigned y=8;y<56;++y)for(unsigned x=8;x<56;++x)if((x+y<63u)==(m->draws==1u)) {
        uint8_t *pixel=m->scratch+(y*64u+x)*4u;
        pixel[0]=m->draws==1?255:0;pixel[1]=m->draws==2?255:0;pixel[2]=0;pixel[3]=255;
    }
    return true;
}
static bool backend_end(void *context,const ctr_scene_surface *readback) {
    backend_mock *m=context;CHECK(m->active && m->draws==2);++m->ends;
    CHECK(readback->width==64 && readback->height==64 && readback->stride==256 && readback->size==BYTES);
    memcpy(readback->bytes,m->scratch,sizeof(m->scratch));
    if(m->fail==CTR_SCENE_BACKEND_PARITY)((uint8_t *)readback->bytes)[0]^=1u;
    if(m->fail==CTR_SCENE_BACKEND_END)return false;
    m->active=false;return true;
}
static void backend_abort(void *context) { backend_mock *m=context;++m->aborts;m->active=false; }
static ctr_scene_backend mock_backend(backend_mock *m) {
    ctr_scene_backend backend={m,backend_begin,backend_draw,backend_end,backend_abort,NULL,NULL,NULL};return backend;
}
static void native_backend_success(void) {
    for(unsigned kind=0;kind<4;++kind) {
        ctr_guest g;list l;ctr_gx_completion c;open(&g,&l,(kind&1u)!=0);
        backend_mock m={0};m.arithmetic=kind>=2u;ctr_scene_backend backend=mock_backend(&m);
        if(m.arithmetic)arithmetic_setup(&g,&l,(kind&1u)!=0);
        ctr_guest_set_scene_backend(&g,&backend);CHECK(g.gsp.pica_backend==&backend);
        CHECK(ctr_memory_write_bytes(&g.memory,BASE,l.bytes,(uint32_t)l.size)==CTR_MEM_OK);
        uint64_t generation=g.memory.generation;uint32_t command[]={0x01000101,BASE,(uint32_t)l.size,0,0,0,0,1};
        CHECK(ctr_gx_execute(&g,command,&c)==CTR_GX_OK && c.signal && c.irq==5);
        CHECK(m.begins==1 && m.draws==2 && m.ends==1 && !m.aborts && !m.active);
        CHECK(g.gsp.pica_draws==2 && g.gsp.pica_triangles==2 && g.gsp.pica_fragments==2304);
        CHECK(g.gsp.pica_shader_instructions==(m.arithmetic?30u:18u) && g.memory.generation==generation+1u);
        CHECK(g.gsp.last_scene_result==CTR_SCENE_OK && g.gsp.last_scene_backend_stage==CTR_SCENE_BACKEND_NONE);
        golden(&g,true);ctr_guest_close(&g);CHECK(!g.gsp.pica_backend && !m.aborts);
    }
    /* State-only uploads require no GPU target or callbacks. */
    ctr_guest g;list l;ctr_gx_completion c;open(&g,&l,false);
    backend_mock m={0};ctr_scene_backend backend=mock_backend(&m);ctr_guest_set_scene_backend(&g,&backend);
    l.size=find(&l,0x22f,0);finish(&l);
    CHECK(execute(&g,&l,BASE,&c)==CTR_GX_OK && c.signal && !g.gsp.pica_draws);
    CHECK(!m.begins && !m.draws && !m.ends && !m.aborts);
    ctr_guest_set_scene_backend(&g,NULL);CHECK(!g.gsp.pica_backend);ctr_guest_close(&g);
}
static void native_backend_rollback(void) {
    for(unsigned retained=0;retained<2;++retained)for(unsigned stage=CTR_SCENE_BACKEND_BEGIN;stage<=CTR_SCENE_BACKEND_PARITY;++stage) {
        ctr_guest g;list l;ctr_gx_completion c;open(&g,&l,false);
        if(retained)CHECK(execute(&g,&l,BASE,&c)==CTR_GX_OK);
        ctr_scene_state before={0};if(g.gsp.pica_scene)before=*g.gsp.pica_scene;
        ctr_pica_state state=g.gsp.pica;uint8_t pixels[BYTES];memcpy(pixels,backing(&g)+0x2000u,BYTES);
        backend_mock m={0};m.fail=(ctr_scene_backend_stage)stage;
        ctr_scene_backend backend=mock_backend(&m);ctr_guest_set_scene_backend(&g,&backend);
        CHECK(ctr_memory_write_bytes(&g.memory,BASE,l.bytes,(uint32_t)l.size)==CTR_MEM_OK);
        uint64_t generation=g.memory.generation,code=g.memory.code_generation;g.cpu.exclusive_valid=true;
        uint32_t command[]={0x01000101,BASE,(uint32_t)l.size,0,0,0,0,1};
        CHECK(ctr_gx_execute(&g,command,&c)==CTR_GX_UNSUPPORTED && !c.signal);
        CHECK(g.gsp.last_scene_result==CTR_SCENE_BACKEND && g.gsp.last_scene_backend_stage==stage);
        CHECK(m.begins==1 && m.aborts==1 && !m.active);
        CHECK(m.draws==(stage==CTR_SCENE_BACKEND_BEGIN?0u:2u));
        CHECK(m.ends==(stage<=CTR_SCENE_BACKEND_DRAW?0u:1u));
        CHECK(!memcmp(pixels,backing(&g)+0x2000u,BYTES) && !memcmp(&state,&g.gsp.pica,sizeof(state)));
        CHECK(retained?g.gsp.pica_scene && !memcmp(&before,g.gsp.pica_scene,sizeof(before)):!g.gsp.pica_scene);
        CHECK(g.memory.generation==generation && g.memory.code_generation==code && g.cpu.exclusive_valid);
        ctr_guest_close(&g);CHECK(!g.gsp.pica_backend && m.aborts==1);
    }
    /* Late wire, shader, vertex and counter failures must be discovered before
     * any native callback, even after an earlier valid draw in software. */
    for(unsigned kind=0;kind<5;++kind) {
        ctr_guest g;list l;ctr_gx_completion c;open(&g,&l,false);
        backend_mock m={0};ctr_scene_backend backend=mock_backend(&m);ctr_guest_set_scene_backend(&g,&backend);
        if(kind==0) { trim(&l);add(&l,0x123,0);finish(&l); }
        if(kind==1) { arithmetic_setup(&g,&l,false);numeric_failure(&l); }
        if(kind==2)backing(&g)[0x1000u+69u]=255;
        if(kind==3)l.bytes[l.size-8u]^=1u;
        if(kind==4)g.gsp.pica.committed_lists=UINT64_MAX;
        uint8_t pixels[BYTES];memcpy(pixels,backing(&g)+0x2000u,BYTES);ctr_pica_state state=g.gsp.pica;
        CHECK(execute(&g,&l,BASE,&c)==CTR_GX_UNSUPPORTED && !c.signal);
        CHECK(!m.begins && !m.draws && !m.ends && !m.aborts);
        CHECK(!memcmp(pixels,backing(&g)+0x2000u,BYTES) && !memcmp(&state,&g.gsp.pica,sizeof(state)) && !g.gsp.pica_scene);
        ctr_guest_close(&g);
    }
}
/* Texture addresses deliberately live outside the geometry allocation/span. */
enum { TEXTURE_VA=BASE+0x10000u, TEXTURE_PA=PHYSICAL+0x10000u, TEXTURE_BYTES=16u*8u*4u };
static void insert_at(list *l,size_t at,unsigned reg,uint32_t value) {
    CHECK(at<=l->size && l->size+8u<=sizeof(l->bytes));
    memmove(l->bytes+at+8u,l->bytes+at,l->size-at);l->size+=8u;
    put32(l->bytes+at,value);put32(l->bytes+at+4u,UINT32_C(0xf0000)|reg);
}
static uint32_t float_word(float value) { uint32_t bits;memcpy(&bits,&value,4);return bits; }
static void texture_pixel(unsigned seed,unsigned x,unsigned y,uint8_t rgba[4]) {
    rgba[0]=(uint8_t)(13u*x+17u*y+seed);rgba[1]=(uint8_t)(5u*x+29u*y+3u*seed);
    rgba[2]=(uint8_t)(7u*x+3u*y+11u+5u*seed);rgba[3]=(uint8_t)(19u*x+23u*y+37u+7u*seed);
}
static void texture_source(uint8_t *raw,unsigned seed) {
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<16;++x) {
        unsigned morton=0;
        for(unsigned bit=0;bit<3;++bit)morton|=((x>>bit)&1u)<<(2u*bit)|((y>>bit)&1u)<<(2u*bit+1u);
        uint8_t rgba[4];texture_pixel(seed,x,y,rgba);
        for(unsigned lane=0;lane<4;++lane)raw[((x/8u)*64u+morton)*4u+lane]=rgba[3u-lane];
    }
}
static void textured_open(ctr_guest *g,list *l,bool shorts) {
    open(g,l,shorts);g->cpu.r[0]=0x10003;g->cpu.r[1]=TEXTURE_VA;g->cpu.r[2]=0;
    g->cpu.r[3]=4096;g->cpu.r[4]=3;ctr_heap_control(g);
    CHECK(!g->cpu.r[0] && g->allocations[1].base==TEXTURE_VA && g->allocations[1].linear);
    texture_source(g->allocations[1].storage,0);texture_source(g->allocations[1].storage+1024,29);
    ctr_scene_fixture f;ctr_scene_fixture_make(&f,shorts);uint8_t vertices[108]={0};
    for(unsigned i=0;i<4;++i) {
        memcpy(vertices+i*24u,f.resources+i*16u,16u);
        put32(vertices+i*24u+16u,float_word(i&1u?1.0f:0.0f));
        put32(vertices+i*24u+20u,float_word(i&2u?1.0f:0.0f));
    }
    const unsigned order[]={0,1,2,1,3,2};unsigned index_width=shorts?2u:1u;
    for(unsigned i=0;i<6;++i)vertices[96u+i*index_width]=(uint8_t)order[i];
    CHECK(ctr_memory_write_bytes(&g->memory,RESOURCE,vertices,96u+6u*index_width)==CTR_MEM_OK);
    set(l,0x201,UINT32_C(0x7ff));set(l,0x202,UINT32_C(0x20020000));
    set(l,0x204,UINT32_C(0x20));set(l,0x205,UINT32_C(0x20180000));
    set(l,0x2b9,UINT32_C(0xa0000002));set(l,0x2bd,7);set(l,0x04f,3);
    put32(l->bytes+find(l,0x227,0),(shorts?UINT32_C(0x80000000):0u)|96u);
    put32(l->bytes+find(l,0x227,1),(shorts?UINT32_C(0x80000000):0u)|(96u+3u*index_width));
    put32(l->bytes+find(l,0x2cc,2),UINT32_C(0x4c402000)); /* o2 = v2 */
    insert_at(l,find(l,0x2bf,0),0x2cc,UINT32_C(0x88000000));
    set(l,0x080,1);set(l,0xc0,UINT32_C(0x30003));
    const unsigned regs[]={0x052,0x064,0x06f,0x082,0x083,0x084,0x085,0x08e};
    const uint32_t values[]={UINT32_C(0x1f1f0d0c),1,0x103,UINT32_C(0x00100008),0,0,TEXTURE_PA/8u,0};
    size_t at=find(l,0x232,0);
    for(unsigned i=0;i<sizeof(regs)/sizeof(*regs);++i,at+=8u)insert_at(l,at,regs[i],values[i]);
    trim(l);finish(l);
}
static void material_setup(ctr_guest *g,list *l) {
    /* Independent producer facts: UV1 semantics14/15, UV2 semantics22/23.
     * All three coordinates use the same original varying input here; the
     * three asymmetric source images distinguish resource/unit selection. */
    texture_source(g->allocations[1].storage+2048u,71u);
    texture_source(g->allocations[1].storage+3072u,113u);
    set(l,0x080,7);set(l,0x2bd,31);set(l,0x04f,5);set(l,0x06f,0x703);
    put32(l->bytes+find(l,0x2cc,3),UINT32_C(0x4c602000)); /* o3 = v2 */
    size_t at=find(l,0x2bf,0);
    insert_at(l,at,0x2cc,UINT32_C(0x4c802000)); /* o4 = v2 */
    insert_at(l,at+8u,0x2cc,UINT32_C(0x88000000));
    const unsigned regs[]={0x053,0x054,0x092,0x093,0x094,0x095,0x096,
                          0x09a,0x09b,0x09c,0x09d,0x09e};
    const uint32_t values[]={UINT32_C(0x1f1f0f0e),UINT32_C(0x1f1f1716),
        UINT32_C(0x00100008),0,0,(TEXTURE_PA+1024u)/8u,0,
        UINT32_C(0x00100008),0,0,(TEXTURE_PA+2048u)/8u,0};
    at=find(l,0x232,0);
    for(unsigned i=0;i<sizeof(regs)/sizeof(*regs);++i,at+=8u)insert_at(l,at,regs[i],values[i]);
    set(l,0xc8,UINT32_C(0x0005004f)); /* RGB = Previous * Texture1; A = Texture2. */
    set(l,0xca,1u);
    trim(l);finish(l);
}
static void material_pixel(unsigned seed1,unsigned x,unsigned y,uint8_t rgba[4]) {
    uint8_t first[4],second[4],third[4];
    texture_pixel(0,x,y,first);texture_pixel(seed1,x,y,second);texture_pixel(71,x,y,third);
    for(unsigned lane=0;lane<3u;++lane)rgba[lane]=(uint8_t)((unsigned)first[lane]*second[lane]/255u);
    rgba[3]=third[3];
}
static void material_golden(ctr_guest *g,unsigned first_seed1,unsigned second_seed1) {
    uint8_t pixels[BYTES];
    CHECK(ctr_texture_decode(backing(g)+0x2000u,BYTES,0,64,64,pixels,sizeof(pixels))==CTR_TEXTURE_OK);
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
        uint8_t expected[]={16,32,48,255};
        if(x>=8u && x<56u && y>=8u && y<56u)
            material_pixel(x+y<63u?first_seed1:second_seed1,(2u*x-15u)/6u,7u-(2u*y-15u)/12u,expected);
        CHECK(!memcmp(pixels+(y*64u+x)*4u,expected,4));
    }
}
static void texture_expected(uint8_t pixels[BYTES],bool second,bool overlap,bool complete) {
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
        uint8_t *rgba=pixels+(y*64u+x)*4u;
        rgba[0]=16;rgba[1]=32;rgba[2]=48;rgba[3]=255;
        if(x<8 || x>=56 || y<8 || y>=56 || (!complete && x+y>=63u))continue;
        unsigned tx=(2u*x-15u)/6u,ty=7u-(2u*y-15u)/12u,seed=second && x+y>=63u?29u:0u;
        /* Overlapping source bindings share one 8x8 tile in raw memory. */
        if(overlap && x+y<63u && tx>=8u) { tx-=8u;seed=29; }
        texture_pixel(seed,tx,ty,rgba);
    }
}
static void texture_golden(ctr_guest *g,bool second,bool overlap,bool complete) {
    uint8_t actual[BYTES],expected[BYTES];texture_expected(expected,second,overlap,complete);
    CHECK(ctr_texture_decode(backing(g)+0x2000u,BYTES,0,64,64,actual,sizeof(actual))==CTR_TEXTURE_OK);
    CHECK(!memcmp(actual,expected,sizeof(actual)));
}
static ctr_gx_result submit(ctr_guest *g,const list *l,ctr_gx_completion *c) {
    uint32_t commands[]={0x01000101,BASE,(uint32_t)l->size,0,0,0,0,1};
    return ctr_gx_execute(g,commands,c);
}
static void guest_texture_success(void) {
    for(unsigned shorts=0;shorts<2;++shorts) {
        ctr_guest g;list l;ctr_gx_completion c;textured_open(&g,&l,shorts!=0);
        /* A read-only texture is valid; only target writes invalidate executable aliases. */
        CHECK(ctr_memory_change_range(&g.memory,TEXTURE_VA,TEXTURE_BYTES,CTR_MEM_READ,false)==CTR_MEM_OK);
        CHECK(ctr_memory_map(&g.memory,0x30000000u,BYTES,CTR_MEM_READ|CTR_MEM_EXEC,backing(&g)+0x2000u,BYTES)==CTR_MEM_OK);
        CHECK(ctr_memory_write_bytes(&g.memory,BASE,l.bytes,(uint32_t)l.size)==CTR_MEM_OK);
        uint64_t generation=g.memory.generation,code=g.memory.code_generation;g.cpu.exclusive_valid=true;
        CHECK(submit(&g,&l,&c)==CTR_GX_OK && c.signal && c.irq==5);
        CHECK(g.gsp.pica_draws==2 && g.gsp.pica_triangles==2 && g.gsp.pica_fragments==2304 && g.gsp.pica_shader_instructions==24);
        CHECK(g.memory.generation==generation+1u && g.memory.code_generation==code+1u && !g.cpu.exclusive_valid);
        texture_golden(&g,false,false,true);ctr_guest_close(&g);
    }
    /* Configuration, UV shader and texture binding survive separate draw lists. */
    ctr_guest g;list all,setup,red,green;ctr_gx_completion c;textured_open(&g,&all,true);
    setup=all;size_t first=find(&all,0x22f,0);setup.size=first;finish(&setup);
    CHECK(execute(&g,&setup,BASE,&c)==CTR_GX_OK && !g.gsp.pica_draws);
    memset(&red,0,sizeof(red));add(&red,0x22f,1);finish(&red);
    CHECK(execute(&g,&red,BASE,&c)==CTR_GX_OK && g.gsp.pica_draws==1);texture_golden(&g,false,false,false);
    memset(&green,0,sizeof(green));green.size=all.size-first-8u;memcpy(green.bytes,all.bytes+first+8u,green.size);
    trim(&green);finish(&green);
    CHECK(execute(&g,&green,BASE,&c)==CTR_GX_OK && g.gsp.pica.committed_lists==3 && g.gsp.pica_shader_instructions==24);
    texture_golden(&g,false,false,true);ctr_guest_close(&g);
    /* Per-draw bindings use distinct, overlapping, and geometry-contained spans. */
    for(unsigned kind=0;kind<3;++kind) {
        textured_open(&g,&all,kind!=0);uint32_t second=TEXTURE_PA+1024u;
        if(kind==1) { second=TEXTURE_PA+256u;texture_source(g.allocations[1].storage+256u,29); }
        if(kind==2) { texture_source(backing(&g)+0x1400u,0);set(&all,0x085,(PHYSICAL+0x1400u)/8u); }
        insert_at(&all,find(&all,0x22f,1),0x085,second/8u);trim(&all);finish(&all);
        CHECK(execute(&g,&all,BASE,&c)==CTR_GX_OK && g.gsp.pica_draws==2);
        texture_golden(&g,true,kind==1,true);ctr_guest_close(&g);
    }
}
static void guest_linear_texture(void) {
    for(unsigned shorts=0;shorts<2;++shorts)for(unsigned chain=0;chain<2;++chain) {
        ctr_guest g;list l;ctr_gx_completion c;textured_open(&g,&l,shorts!=0);
        set(&l,0x083,6u);
        if(chain) {
            static const uint32_t stages[6][5]={
                {0x0e3e00e3,0,0x00040001,0x65e949b3,0},
                {0x00ef00ef,0,0x00050002,0x21139725,0},
                {0x0e3f0e3f,0x00120210,0x00080004,0x532fd35b,0},
                {0x00ef00ef,0,0x00010003,0xb1f14395,1},
                {0x03ef03ef,0,0x00040009,0x77b52f53,0},
                {0x03ef03ef,0,0x00020008,0x113bc771,0}
            };
            size_t at=find(&l,0x22f,0);
            for(unsigned stage=0;stage<6;++stage)for(unsigned field=0;field<5;++field,at+=8u)
                insert_at(&l,at,ctr_tev_registers[stage]+field,stages[stage][field]);
            trim(&l);finish(&l);
        }
        for(unsigned vertex=0;vertex<4;++vertex) {
            write_word(&g,RESOURCE+vertex*24u+16u,float_word(0.125f));
            write_word(&g,RESOURCE+vertex*24u+20u,float_word(0.0625f));
        }
        CHECK(execute(&g,&l,BASE,&c)==CTR_GX_OK && c.signal && c.irq==5);
        CHECK(g.gsp.pica_scene->registers[0x083]==6u && g.gsp.pica_draws==2 && g.gsp.pica_fragments==2304);
        uint8_t pixels[BYTES];
        CHECK(ctr_texture_decode(backing(&g)+0x2000u,BYTES,0,64,64,pixels,sizeof(pixels))==CTR_TEXTURE_OK);
        for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
            /* Constant UV samples raw row 7, halfway between x1/x2. Each
             * odd channel sum rounds up once: (139,211,43,227). */
            const uint8_t inside[]={139,211,43,227},combined[]={200,255,52,184},outside[]={16,32,48,255};
            CHECK(!memcmp(pixels+(y*64u+x)*4u,x>=8 && x<56 && y>=8 && y<56?chain?combined:inside:outside,4));
        }
        ctr_guest_close(&g);
    }
}
typedef struct {
    ctr_guest *guest;unsigned begins,draws,ends,aborts;uint8_t pixels[BYTES];
} guest_texture_backend;
static bool guest_texture_begin(void *context,const ctr_scene_surface *initial) {
    guest_texture_backend *m=context;++m->begins;CHECK(initial->size==BYTES);
    memcpy(m->pixels,initial->bytes,BYTES);
    /* Simulate source changes after preflight. Callbacks must see owned snapshots. */
    memset(m->guest->allocations[1].storage,0,4096);return true;
}
static bool guest_texture_draw(void *context,const ctr_scene_state *s,const ctr_vertex *v,unsigned count,
                               const ctr_scene_texture *texture) {
    guest_texture_backend *m=context;unsigned draw=m->draws++;CHECK(draw<2 && count==3 && texture);
    CHECK(texture->address==TEXTURE_PA+(draw?1024u:0u) && texture->width==16 && texture->height==8 && !texture->format);
    CHECK(s->shader.code[2]==UINT32_C(0x4c402000) && s->shader.code[3]==UINT32_C(0x88000000));
    const unsigned order[2][3]={{0,1,2},{1,3,2}};
    for(unsigned i=0;i<3;++i) {
        CHECK(v[i].enabled==7 && v[i].input[2][0]==(order[draw][i]&1u?1.0f:0.0f));
        CHECK(v[i].input[2][1]==(order[draw][i]&2u?1.0f:0.0f));
    }
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<16;++x) {
        uint8_t expected[4];texture_pixel(draw?29u:0u,x,y,expected);
        CHECK(!memcmp(texture->rgba+(y*16u+x)*4u,expected,4));
    }
    for(unsigned y=8;y<56;++y)for(unsigned x=8;x<56;++x)if((x+y>=63u)==(draw!=0))
        texture_pixel(draw?29u:0u,(2u*x-15u)/6u,7u-(2u*y-15u)/12u,m->pixels+(y*64u+x)*4u);
    return true;
}
static bool guest_texture_end(void *context,const ctr_scene_surface *readback) {
    guest_texture_backend *m=context;++m->ends;CHECK(readback->size==BYTES);memcpy(readback->bytes,m->pixels,BYTES);return true;
}
static void guest_texture_abort(void *context) { ++((guest_texture_backend *)context)->aborts; }
static void guest_texture_snapshots(void) {
    ctr_guest g;list l;ctr_gx_completion c;textured_open(&g,&l,false);
    insert_at(&l,find(&l,0x22f,1),0x085,(TEXTURE_PA+1024u)/8u);trim(&l);finish(&l);
    guest_texture_backend m={0};m.guest=&g;
    ctr_scene_backend backend={&m,guest_texture_begin,guest_texture_draw,guest_texture_end,guest_texture_abort,NULL,NULL,NULL};
    ctr_guest_set_scene_backend(&g,&backend);
    CHECK(execute(&g,&l,BASE,&c)==CTR_GX_OK && c.signal && c.irq==5);
    CHECK(m.begins==1 && m.draws==2 && m.ends==1 && !m.aborts);texture_golden(&g,true,false,true);
    ctr_guest_close(&g);
}
static void guest_texture_rollback(void) {
    for(unsigned retained=0;retained<2;++retained)for(unsigned kind=0;kind<15;++kind) {
        ctr_guest g;list l;ctr_gx_completion c;textured_open(&g,&l,(kind&1u)!=0);
        if(retained)CHECK(execute(&g,&l,BASE,&c)==CTR_GX_OK);
        uint32_t second=TEXTURE_PA+1024u;ctr_gx_result expected=CTR_GX_UNSUPPORTED;
        if(kind==0) { second=TEXTURE_PA+0x20000u;expected=CTR_GX_MEMORY; }
        if(kind==1) {
            CHECK(ctr_memory_change_range(&g.memory,TEXTURE_VA+1024u+8u,8,0,false)==CTR_MEM_OK);expected=CTR_GX_MEMORY;
        }
        if(kind==2) { second=PHYSICAL+0x2000u;expected=CTR_GX_MEMORY; }
        if(kind==3)insert_at(&l,find(&l,0x22f,1),0x082,UINT32_C(0x000f0008));
        if(kind==4)insert_at(&l,find(&l,0x22f,1),0x08e,14);
        if(kind==5) { trim(&l);add(&l,0x083,2); }
        if(kind==6)second=PHYSICAL+0x1f00u;
        if(kind==7) { second=TEXTURE_PA+0xf00u;expected=CTR_GX_MEMORY; }
        if(kind==8) {
            second=TEXTURE_PA+0x20000u;expected=CTR_GX_MEMORY;
            CHECK(ctr_memory_map(&g.memory,TEXTURE_VA+0x20000u,TEXTURE_BYTES,CTR_MEM_READ,
                  g.allocations[1].storage,TEXTURE_BYTES)==CTR_MEM_OK);
        }
        if(kind==9) { trim(&l);add(&l,0xfa,6); }
        if(kind==10) { trim(&l);add(&l,0xf9,6); }
        if(kind==11) { trim(&l);add(&l,0xe0,1u); } /* Unsupported fog, not admitted buffer updates. */
        if(kind==12)insert_at(&l,find(&l,0x22f,1),0xf8,UINT32_C(0xe000e)); /* Undefined constant. */
        if(kind==13)insert_at(&l,find(&l,0x22f,1),0xf8,UINT32_C(0x40004));
        if(kind==14) { trim(&l);add(&l,0xc0,UINT32_C(0xf000f)); }
        insert_at(&l,find(&l,0x22f,1),0x085,second/8u);trim(&l);finish(&l);
        unsigned owner=relay(&g);(void)owner;ctr_guest_shared *shared=&g.shared[g.gsp.relay_shared];
        uint8_t relay_before[CTR_GUEST_PAGE];CHECK(shared->size==sizeof(relay_before));memcpy(relay_before,shared->storage,shared->size);
        ctr_scene_state before={0};if(retained)before=*g.gsp.pica_scene;
        ctr_pica_state pica=g.gsp.pica;uint8_t pixels[BYTES];memcpy(pixels,backing(&g)+0x2000u,BYTES);
        backend_mock m={0};ctr_scene_backend backend=mock_backend(&m);ctr_guest_set_scene_backend(&g,&backend);
        CHECK(ctr_memory_write_bytes(&g.memory,BASE,l.bytes,(uint32_t)l.size)==CTR_MEM_OK);
        uint64_t generation=g.memory.generation,code=g.memory.code_generation;g.cpu.exclusive_valid=true;
        CHECK(submit(&g,&l,&c)==expected && !c.signal);
        CHECK(!m.begins && !m.draws && !m.ends && !m.aborts);
        CHECK(!memcmp(pixels,backing(&g)+0x2000u,BYTES) && !memcmp(&pica,&g.gsp.pica,sizeof(pica)));
        CHECK(retained?!memcmp(&before,g.gsp.pica_scene,sizeof(before)):!g.gsp.pica_scene);
        CHECK(g.memory.generation==generation && g.memory.code_generation==code && g.cpu.exclusive_valid);
        CHECK(!memcmp(relay_before,shared->storage,shared->size) && !g.events[g.gsp.relay_event].signaled);
        CHECK(!g.gsp.gx_commands && !g.gsp.gx_pica);ctr_guest_close(&g);
    }
}
static void guest_texture_queues(void) {
    ctr_guest g;list good,bad;textured_open(&g,&good,true);bad=good;
    insert_at(&bad,find(&bad,0x22f,1),0x085,(TEXTURE_PA+0x20000u)/8u);trim(&bad);finish(&bad);
    unsigned owner=relay(&g);
    CHECK(ctr_memory_write_bytes(&g.memory,BASE,good.bytes,(uint32_t)good.size)==CTR_MEM_OK);
    CHECK(ctr_memory_write_bytes(&g.memory,BASE+0x800u,bad.bytes,(uint32_t)bad.size)==CTR_MEM_OK);
    write_word(&g,0x10002800,14u|(2u<<8));
    for(unsigned entry=0;entry<2;++entry) {
        uint32_t w[]={0x01000101,BASE+(entry?0x800u:0),(uint32_t)(entry?bad.size:good.size),0,0,0,0,1};
        for(unsigned i=0;i<8;++i)write_word(&g,0x10002820u+(entry?0:14u)*32u+i*4u,w[i]);
    }
    uint32_t trigger[]={0xc0000},out[8];unsigned count;
    CHECK(!ctr_gsp_request(&g,owner,trigger,out,&count) && g.state==CTR_GUEST_FAULTED);
    CHECK(read_word(&g,0x10002800)==0x100u && g.gsp.gx_commands==1 && g.gsp.gx_pica==1);
    CHECK(g.gsp.pica_draws==2 && g.gsp.pica_shader_instructions==24 && g.gsp.pica_scene->registers[0x085]==TEXTURE_PA/8u);
    CHECK(g.shared[g.gsp.relay_shared].storage[1]==1 && g.shared[g.gsp.relay_shared].storage[12]==5 && g.events[g.gsp.relay_event].signaled);
    texture_golden(&g,false,false,true);ctr_guest_close(&g);
}
static void guest_material_success(void) {
    for(unsigned shorts=0;shorts<2u;++shorts)for(unsigned binding=0;binding<4u;++binding) {
        ctr_guest g;list l;ctr_gx_completion c;textured_open(&g,&l,shorts!=0);material_setup(&g,&l);
        if(binding==1u)insert_at(&l,find(&l,0x22f,1),0x095,(TEXTURE_PA+3072u)/8u);
        if(binding==2u) {
            texture_source(backing(&g)+0x1400u,29u);set(&l,0x095,(PHYSICAL+0x1400u)/8u);
        }
        if(binding==3u)set(&l,0x095,TEXTURE_PA/8u); /* Shared image must use one stable span. */
        trim(&l);finish(&l);
        CHECK(execute(&g,&l,BASE,&c)==CTR_GX_OK && c.signal && c.irq==5);
        CHECK(g.gsp.pica_draws==2u && g.gsp.pica_triangles==2u &&
              g.gsp.pica_fragments==2304u && g.gsp.pica_shader_instructions==36u);
        material_golden(&g,binding==3u?0u:29u,binding==1u?113u:binding==3u?0u:29u);
        ctr_guest_close(&g);
    }
    /* Every active mask is resolved independently. Unowned inactive bindings
     * are not snapshotted; coordinate1 routing does not require unit1 enabled. */
    for(unsigned mask=1;mask<8u;++mask)for(unsigned route=0;route<((mask&4u)?2u:1u);++route) {
        ctr_guest g;list l;ctr_gx_completion c;textured_open(&g,&l,false);material_setup(&g,&l);
        set(&l,0x080,mask|(route?0x2000u:0u));
        unsigned selected=mask&4u?2u:mask&2u?1u:0u;
        set(&l,0xc0,(3u+selected)|((3u+selected)<<16));set(&l,0xc8,0xf000fu);set(&l,0xca,0u);
        const unsigned addresses[]={0x085,0x095,0x09d};
        for(unsigned unit=0;unit<3u;++unit)if(!(mask&(1u<<unit)))set(&l,addresses[unit],UINT32_C(0x08000000));
        CHECK(execute(&g,&l,BASE,&c)==CTR_GX_OK && c.signal);
        uint8_t pixels[BYTES];
        CHECK(ctr_texture_decode(backing(&g)+0x2000u,BYTES,0,64,64,pixels,sizeof(pixels))==CTR_TEXTURE_OK);
        for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
            uint8_t expected[]={16,32,48,255};
            if(x>=8u && x<56u && y>=8u && y<56u)
                texture_pixel(selected==2u?71u:selected==1u?29u:0u,(2u*x-15u)/6u,7u-(2u*y-15u)/12u,expected);
            CHECK(!memcmp(pixels+(y*64u+x)*4u,expected,4));
        }
        ctr_guest_close(&g);
    }
    /* A configuration-only list retains all three groups and UV maps. A later
     * guest write changes one image without rebuilding the other two bindings. */
    ctr_guest g;list all,setup,replay;ctr_gx_completion c;textured_open(&g,&all,true);material_setup(&g,&all);
    setup=all;setup.size=find(&setup,0x22f,0);finish(&setup);
    put32(setup.bytes+find(&setup,0x080,0)+4u,UINT32_C(0x10000)|0x080u);
    put32(setup.bytes+find(&setup,0x095,0)+4u,UINT32_C(0x10000)|0x095u);
    put32(setup.bytes+find(&setup,0x09e,0)+4u,UINT32_C(0x50000)|0x09eu);
    CHECK(execute(&g,&setup,BASE,&c)==CTR_GX_OK && !g.gsp.pica_draws);
    CHECK(g.gsp.pica_scene->register_defined[0x080]==1u &&
          g.gsp.pica_scene->register_defined[0x095]==1u && g.gsp.pica_scene->register_defined[0x09e]==5u);
    memset(&replay,0,sizeof(replay));add(&replay,0x080,7u);add(&replay,0x095,(TEXTURE_PA+1024u)/8u);add(&replay,0x09e,0u);
    put32(replay.bytes+find(&replay,0x080,0)+4u,UINT32_C(0xe0000)|0x080u);
    put32(replay.bytes+find(&replay,0x095,0)+4u,UINT32_C(0xe0000)|0x095u);
    put32(replay.bytes+find(&replay,0x09e,0)+4u,UINT32_C(0xa0000)|0x09eu);
    add(&replay,0x227,UINT32_C(0x80000060));add(&replay,0x22f,1);
    add(&replay,0x227,UINT32_C(0x80000066));add(&replay,0x22f,1);finish(&replay);
    CHECK(execute(&g,&replay,BASE,&c)==CTR_GX_OK && g.gsp.pica_draws==2u);material_golden(&g,29u,29u);
    uint8_t changed[TEXTURE_BYTES];texture_source(changed,53u);
    CHECK(ctr_memory_write_bytes(&g.memory,TEXTURE_VA+1024u,changed,sizeof(changed))==CTR_MEM_OK);
    CHECK(ctr_memory_change_range(&g.memory,TEXTURE_VA,4096u,CTR_MEM_READ,false)==CTR_MEM_OK);
    CHECK(ctr_memory_write_bytes(&g.memory,BASE,replay.bytes,(uint32_t)replay.size)==CTR_MEM_OK);
    uint64_t generation=g.memory.generation;
    CHECK(submit(&g,&replay,&c)==CTR_GX_OK && c.signal && g.memory.generation==generation+1u);
    CHECK(g.gsp.pica.committed_lists==3u && g.gsp.pica_draws==4u && g.gsp.pica_shader_instructions==72u);
    CHECK(g.gsp.pica_scene->registers[0x095]==(TEXTURE_PA+1024u)/8u &&
          g.gsp.pica_scene->registers[0x09d]==(TEXTURE_PA+2048u)/8u);
    material_golden(&g,53u,53u);ctr_guest_close(&g);
}
static bool guest_material_draw(void *context,const ctr_scene_state *s,const ctr_vertex *v,unsigned count,
                                const ctr_scene_texture textures[CTR_SCENE_TEXTURE_UNITS],unsigned mask) {
    guest_texture_backend *m=context;unsigned draw=m->draws++;CHECK(draw<2u && count==3u && mask==7u);
    CHECK(s->shader.code[4]==UINT32_C(0x4c802000) && s->shader.code[5]==UINT32_C(0x88000000));
    CHECK(v[0].enabled==7u);
    for(unsigned unit=0;unit<3u;++unit) {
        const ctr_scene_texture *texture=&textures[unit];unsigned offset=unit*1024u;
        if(unit==1u && draw)offset=3072u;
        CHECK(texture->address==TEXTURE_PA+offset && texture->width==16u && texture->height==8u &&
              texture->format==0u && texture->params==0u);
        CHECK(texture->rgba!=m->guest->allocations[1].storage+offset);
        unsigned seed=unit==0u?0u:unit==1u?(draw?113u:29u):71u;
        for(unsigned y=0;y<8u;++y)for(unsigned x=0;x<16u;++x) {
            uint8_t expected[4];texture_pixel(seed,x,y,expected);
            CHECK(!memcmp(texture->rgba+(y*16u+x)*4u,expected,4));
        }
    }
    for(unsigned y=8;y<56;++y)for(unsigned x=8;x<56;++x)if((x+y>=63u)==(draw!=0u))
        material_pixel(draw?113u:29u,(2u*x-15u)/6u,7u-(2u*y-15u)/12u,m->pixels+(y*64u+x)*4u);
    return true;
}
static void guest_material_snapshots(void) {
    ctr_guest g;list l;ctr_gx_completion c;textured_open(&g,&l,false);material_setup(&g,&l);
    insert_at(&l,find(&l,0x22f,1),0x095,(TEXTURE_PA+3072u)/8u);trim(&l);finish(&l);
    guest_texture_backend m={0};m.guest=&g;
    ctr_scene_backend backend={&m,guest_texture_begin,NULL,guest_texture_end,guest_texture_abort,NULL,NULL,guest_material_draw};
    ctr_guest_set_scene_backend(&g,&backend);
    CHECK(execute(&g,&l,BASE,&c)==CTR_GX_OK && c.signal && c.irq==5);
    CHECK(m.begins==1u && m.draws==2u && m.ends==1u && !m.aborts);
    for(unsigned i=0;i<4096u;++i)CHECK(g.allocations[1].storage[i]==0u);
    material_golden(&g,29u,113u);ctr_guest_close(&g);
}
static bool unexpected_material_draw(void *context,const ctr_scene_state *s,const ctr_vertex *v,unsigned count,
                                     const ctr_scene_texture textures[CTR_SCENE_TEXTURE_UNITS],unsigned mask) {
    (void)s;(void)v;(void)count;(void)textures;(void)mask;
    ++((backend_mock *)context)->draws;return false;
}
static void guest_material_rollback(void) {
    const unsigned dimensions[]={0x092,0x09a},parameters[]={0x093,0x09b};
    const unsigned addresses[]={0x095,0x09d},formats[]={0x096,0x09e};
    for(unsigned unit=1;unit<3u;++unit)for(unsigned retained=0;retained<2u;++retained)
        for(unsigned kind=0;kind<14u;++kind) {
            ctr_guest g;list l;ctr_gx_completion c;textured_open(&g,&l,(kind&1u)!=0);
            /* Retained failures start from the accepted Texture0 list; the new
             * units still need complete initial definitions in the attempted list. */
            if(retained)CHECK(execute(&g,&l,BASE,&c)==CTR_GX_OK);
            material_setup(&g,&l);unsigned slot=unit-1u;uint32_t second=TEXTURE_PA+unit*1024u;
            ctr_gx_result expected=CTR_GX_UNSUPPORTED;
            if(kind==0u) { second=TEXTURE_PA+0x20000u;expected=CTR_GX_MEMORY; }
            if(kind==1u) {
                CHECK(ctr_memory_change_range(&g.memory,TEXTURE_VA+unit*1024u+8u,8u,0,false)==CTR_MEM_OK);
                expected=CTR_GX_MEMORY;
            }
            if(kind==2u) { second=PHYSICAL+0x2000u;expected=CTR_GX_MEMORY; }
            if(kind==3u)second=PHYSICAL+0x1f00u;
            if(kind==4u) { second=TEXTURE_PA+0xf00u;expected=CTR_GX_MEMORY; }
            if(kind==5u) {
                uint32_t split=TEXTURE_VA+unit*1024u;
                CHECK(ctr_memory_change_range(&g.memory,split,TEXTURE_BYTES,CTR_MEM_READ,false)==CTR_MEM_OK);
                bool found=false;
                for(size_t i=0;i<g.memory.count;++i)if(g.memory.maps[i].base==split) {
                    g.memory.maps[i].storage=backing(&g)+0x2000u;found=true;
                }
                CHECK(found);expected=CTR_GX_MEMORY;
            }
            if(kind==6u)insert_at(&l,find(&l,0x22f,1),formats[slot],14u);
            if(kind==7u)insert_at(&l,find(&l,0x22f,1),dimensions[slot],UINT32_C(0x000f0008));
            if(kind==8u) { trim(&l);add(&l,parameters[slot],2u); }
            if(kind==9u)second=UINT32_C(0x80000000);
            if(kind==10u)put32(l.bytes+find(&l,formats[slot],0)+4u,UINT32_C(0xe0000)|formats[slot]);
            if(kind==11u)insert_at(&l,find(&l,0x22f,1),0x080,0x1007u);
            if(kind==12u) { set(&l,0x11d,PHYSICAL/8u);expected=CTR_GX_MEMORY; }
            if(kind==13u) {
                second=TEXTURE_PA+0x20000u;expected=CTR_GX_MEMORY;
                CHECK(ctr_memory_map(&g.memory,TEXTURE_VA+0x20000u,TEXTURE_BYTES,CTR_MEM_READ,
                      g.allocations[1].storage+unit*1024u,TEXTURE_BYTES)==CTR_MEM_OK);
            }
            insert_at(&l,find(&l,0x22f,1),addresses[slot],second/8u);trim(&l);finish(&l);
            unsigned owner=relay(&g);(void)owner;ctr_guest_shared *shared=&g.shared[g.gsp.relay_shared];
            uint8_t relay_before[CTR_GUEST_PAGE];CHECK(shared->size==sizeof(relay_before));
            memcpy(relay_before,shared->storage,shared->size);
            ctr_scene_state before={0};if(retained)before=*g.gsp.pica_scene;
            ctr_pica_state pica=g.gsp.pica;uint8_t pixels[BYTES];memcpy(pixels,backing(&g)+0x2000u,BYTES);
            backend_mock m={0};ctr_scene_backend backend=mock_backend(&m);backend.draw_textures=unexpected_material_draw;
            ctr_guest_set_scene_backend(&g,&backend);
            CHECK(ctr_memory_write_bytes(&g.memory,BASE,l.bytes,(uint32_t)l.size)==CTR_MEM_OK);
            uint64_t generation=g.memory.generation,code=g.memory.code_generation;g.cpu.exclusive_valid=true;
            CHECK(submit(&g,&l,&c)==expected && !c.signal);
            CHECK(!m.begins && !m.draws && !m.ends && !m.aborts);
            CHECK(!memcmp(pixels,backing(&g)+0x2000u,BYTES) && !memcmp(&pica,&g.gsp.pica,sizeof(pica)));
            CHECK(retained?!memcmp(&before,g.gsp.pica_scene,sizeof(before)):!g.gsp.pica_scene);
            CHECK(g.memory.generation==generation && g.memory.code_generation==code && g.cpu.exclusive_valid);
            CHECK(!memcmp(relay_before,shared->storage,shared->size) && !g.events[g.gsp.relay_event].signaled);
            CHECK(!g.gsp.gx_commands && !g.gsp.gx_pica);ctr_guest_close(&g);
        }
}
static void guest_material_queues(void) {
    ctr_guest g;list good,bad;textured_open(&g,&good,true);material_setup(&g,&good);bad=good;
    insert_at(&bad,find(&bad,0x22f,1),0x09d,(TEXTURE_PA+0x20000u)/8u);trim(&bad);finish(&bad);
    CHECK(good.size<0x800u && bad.size<0x800u);unsigned owner=relay(&g);
    CHECK(ctr_memory_write_bytes(&g.memory,BASE,good.bytes,(uint32_t)good.size)==CTR_MEM_OK);
    CHECK(ctr_memory_write_bytes(&g.memory,BASE+0x800u,bad.bytes,(uint32_t)bad.size)==CTR_MEM_OK);
    write_word(&g,0x10002800,14u|(2u<<8));
    for(unsigned entry=0;entry<2u;++entry) {
        uint32_t w[]={0x01000101,BASE+(entry?0x800u:0u),(uint32_t)(entry?bad.size:good.size),0,0,0,0,1};
        for(unsigned i=0;i<8u;++i)write_word(&g,0x10002820u+(entry?0u:14u)*32u+i*4u,w[i]);
    }
    uint32_t trigger[]={0xc0000},out[8];unsigned count;
    CHECK(!ctr_gsp_request(&g,owner,trigger,out,&count) && g.state==CTR_GUEST_FAULTED);
    CHECK(read_word(&g,0x10002800)==0x100u && g.gsp.gx_commands==1u && g.gsp.gx_pica==1u);
    CHECK(g.gsp.pica_draws==2u && g.gsp.pica_shader_instructions==36u &&
          g.gsp.pica_scene->registers[0x09d]==(TEXTURE_PA+2048u)/8u);
    CHECK(g.shared[g.gsp.relay_shared].storage[1]==1u && g.shared[g.gsp.relay_shared].storage[12]==5u &&
          g.events[g.gsp.relay_event].signaled);
    material_golden(&g,29u,29u);ctr_guest_close(&g);
}
static void material_allocation(ctr_guest *g,uint32_t bytes) {
    g->cpu.r[0]=1u;g->cpu.r[1]=TEXTURE_VA;g->cpu.r[2]=0;g->cpu.r[3]=4096u;g->cpu.r[4]=0;
    ctr_heap_control(g);CHECK(!g->cpu.r[0] && !g->allocations[1].storage);
    g->cpu.r[0]=0x10003u;g->cpu.r[1]=TEXTURE_VA;g->cpu.r[2]=0;g->cpu.r[3]=bytes;g->cpu.r[4]=3u;
    ctr_heap_control(g);CHECK(!g->cpu.r[0] && g->allocations[1].base==TEXTURE_VA && g->allocations[1].size==bytes);
}
static void material_limit_failure(ctr_guest *g,const list *bad) {
    ctr_gx_completion c;ctr_scene_state before=*g->gsp.pica_scene;ctr_pica_state pica=g->gsp.pica;
    uint8_t pixels[BYTES];memcpy(pixels,backing(g)+0x2000u,BYTES);
    backend_mock m={0};ctr_scene_backend backend=mock_backend(&m);backend.draw_textures=unexpected_material_draw;
    ctr_guest_set_scene_backend(g,&backend);
    CHECK(ctr_memory_write_bytes(&g->memory,BASE,bad->bytes,(uint32_t)bad->size)==CTR_MEM_OK);
    uint64_t generation=g->memory.generation,code=g->memory.code_generation;g->cpu.exclusive_valid=true;
    CHECK(submit(g,bad,&c)==CTR_GX_UNSUPPORTED && !c.signal && g->gsp.last_scene_result==CTR_SCENE_LIMIT);
    CHECK(!m.begins && !m.draws && !m.ends && !m.aborts);
    CHECK(!memcmp(&before,g->gsp.pica_scene,sizeof(before)) && !memcmp(&pica,&g->gsp.pica,sizeof(pica)));
    CHECK(!memcmp(pixels,backing(g)+0x2000u,BYTES) && generation==g->memory.generation &&
          code==g->memory.code_generation && g->cpu.exclusive_valid);
    ctr_guest_set_scene_backend(g,NULL);
}
static void guest_material_limits(void) {
    /* A small unit1 range contained in unit0's 4MiB image reuses that owned
     * snapshot. Enabling an additional independent 256-byte image exceeds the
     * aggregate limit, even though every range is valid guest-owned memory. */
    ctr_guest g;list l,bad;ctr_gx_completion c;textured_open(&g,&l,false);material_setup(&g,&l);
    material_allocation(&g,CTR_SCENE_MAX_TEXTURE_SOURCE_BYTES+4096u);
    set(&l,0x080,3u);set(&l,0x082,UINT32_C(0x04000400));set(&l,0xc8,UINT32_C(0x000f004f));
    CHECK(execute(&g,&l,BASE,&c)==CTR_GX_OK && c.signal && g.gsp.pica_draws==2u);
    bad=l;set(&bad,0x080,7u);set(&bad,0x09a,UINT32_C(0x00080008));
    set(&bad,0x09d,(TEXTURE_PA+CTR_SCENE_MAX_TEXTURE_SOURCE_BYTES)/8u);
    material_limit_failure(&g,&bad);ctr_guest_close(&g);

    /* Eleven draws can gather 33 independent unit bindings. Reusing the last
     * image fits exactly32 snapshots; changing only that binding must fail. */
    textured_open(&g,&l,false);material_setup(&g,&l);material_allocation(&g,0x9000u);
    l.size=find(&l,0x22f,0);
    for(unsigned draw=0;draw<11u;++draw) {
        const unsigned addresses[]={0x085,0x095,0x09d};
        for(unsigned unit=0;unit<3u;++unit) {
            unsigned image=draw*3u+unit;if(image==32u)image=31u;
            add(&l,addresses[unit],(TEXTURE_PA+image*1024u)/8u);
        }
        add(&l,0x22f,1u);
    }
    finish(&l);CHECK(execute(&g,&l,BASE,&c)==CTR_GX_OK && c.signal && g.gsp.pica_draws==11u);
    CHECK(g.gsp.pica_shader_instructions==198u);
    bad=l;put32(bad.bytes+find(&bad,0x09d,11u),(TEXTURE_PA+32u*1024u)/8u);
    material_limit_failure(&g,&bad);ctr_guest_close(&g);
}
static void guest_fragment_output(void) {
    /* Byte-domain constant passes alpha EQUAL127, then XOR updates only R/B.
     * Raw guest tiled storage and executable aliases commit together. */
    for(unsigned shorts=0;shorts<2;++shorts) {
        ctr_guest g;list l;ctr_gx_completion c;open(&g,&l,shorts!=0);
        set(&l,0xc0,UINT32_C(0xe000e));insert_at(&l,find(&l,0x22f,0),0xc3,UINT32_C(0x7fab5313));
        trim(&l);finish(&l);
        set(&l,0x104,1u|(2u<<4)|(127u<<8));set(&l,0x102,11);set(&l,0x107,5u<<8);set(&l,0x112,15);
        CHECK(ctr_memory_map(&g.memory,0x30000000u,BYTES,CTR_MEM_READ|CTR_MEM_EXEC,backing(&g)+0x2000u,BYTES)==CTR_MEM_OK);
        CHECK(ctr_memory_write_bytes(&g.memory,BASE,l.bytes,(uint32_t)l.size)==CTR_MEM_OK);
        uint64_t generation=g.memory.generation,code=g.memory.code_generation;g.cpu.exclusive_valid=true;
        CHECK(submit(&g,&l,&c)==CTR_GX_OK && c.signal && c.irq==5);
        CHECK(g.memory.generation==generation+1 && g.memory.code_generation==code+1 && !g.cpu.exclusive_valid);
        uint8_t pixels[BYTES];
        CHECK(ctr_texture_decode(backing(&g)+0x2000u,BYTES,0,64,64,pixels,sizeof(pixels))==CTR_TEXTURE_OK);
        for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
            const uint8_t inside[]={3,32,155,255},outside[]={16,32,48,255};
            CHECK(!memcmp(pixels+(y*64u+x)*4u,x>=8 && x<56 && y>=8 && y<56?inside:outside,4));
        }
        /* A retained list tests the same constant with GREATER127: no pixel
         * changes, while successful covered work and P3D completion advance. */
        memset(&l,0,sizeof(l));add(&l,0x104,1u|(6u<<4)|(127u<<8));add(&l,0x22f,1);finish(&l);
        CHECK(execute(&g,&l,BASE,&c)==CTR_GX_OK && c.signal && c.irq==5);
        uint8_t after[BYTES];CHECK(ctr_texture_decode(backing(&g)+0x2000u,BYTES,0,64,64,after,sizeof(after))==CTR_TEXTURE_OK);
        CHECK(!memcmp(after,pixels,sizeof(after)) && g.gsp.pica_draws==3 && g.gsp.pica_fragments==3480);
        ctr_guest_close(&g);
    }
    /* Invalid fields after valid staged draws must preserve both fresh and
     * retained guest state, memory generations, exclusive state and IRQ data. */
    const unsigned regs[]={0x100,0x102,0x104,0x107,0x112,0x113,0x102};
    const uint32_t values[]={1,16,2,0x1000,1,1,11};
    for(unsigned retained=0;retained<2;++retained)for(unsigned kind=0;kind<7;++kind) {
        ctr_guest g;list l;ctr_gx_completion c;open(&g,&l,false);
        if(retained)CHECK(execute(&g,&l,BASE,&c)==CTR_GX_OK);
        trim(&l);add(&l,regs[kind],values[kind]);if(kind==6)add(&l,0x22f,1);finish(&l);
        relay(&g);ctr_guest_shared *shared=&g.shared[g.gsp.relay_shared];uint8_t irq[CTR_GUEST_PAGE];
        CHECK(shared->size==sizeof(irq));memcpy(irq,shared->storage,sizeof(irq));
        ctr_scene_state before={0};if(retained)before=*g.gsp.pica_scene;
        ctr_pica_state pica=g.gsp.pica;uint8_t pixels[BYTES];memcpy(pixels,backing(&g)+0x2000u,BYTES);
        backend_mock m={0};ctr_scene_backend backend=mock_backend(&m);ctr_guest_set_scene_backend(&g,&backend);
        CHECK(ctr_memory_write_bytes(&g.memory,BASE,l.bytes,(uint32_t)l.size)==CTR_MEM_OK);
        uint64_t generation=g.memory.generation,code=g.memory.code_generation;g.cpu.exclusive_valid=true;
        CHECK(submit(&g,&l,&c)==CTR_GX_UNSUPPORTED && !c.signal);
        CHECK(!m.begins && !m.draws && !m.ends && !m.aborts);
        CHECK(!memcmp(pixels,backing(&g)+0x2000u,BYTES) && !memcmp(&pica,&g.gsp.pica,sizeof(pica)));
        CHECK(retained?!memcmp(&before,g.gsp.pica_scene,sizeof(before)):!g.gsp.pica_scene);
        CHECK(g.memory.generation==generation && g.memory.code_generation==code && g.cpu.exclusive_valid);
        CHECK(!memcmp(irq,shared->storage,sizeof(irq)) && !g.events[g.gsp.relay_event].signaled);
        ctr_guest_close(&g);
    }
}
static void guest_blending(void) {
    for(unsigned shorts=0;shorts<2;++shorts) {
        ctr_guest g;list l;ctr_gx_completion c;open(&g,&l,shorts!=0);
        set(&l,0xc0,UINT32_C(0xe000e));insert_at(&l,find(&l,0x22f,0),0xc3,UINT32_C(0x7fab5313));
        insert_at(&l,find(&l,0x22f,0),0x101,UINT32_C(0x01760000));
        set(&l,0x100,0x100);set(&l,0x112,15);trim(&l);finish(&l);
        CHECK(ctr_memory_map(&g.memory,0x30000000u,BYTES,CTR_MEM_READ|CTR_MEM_EXEC,backing(&g)+0x2000u,BYTES)==CTR_MEM_OK);
        CHECK(ctr_memory_write_bytes(&g.memory,BASE,l.bytes,(uint32_t)l.size)==CTR_MEM_OK);
        uint64_t generation=g.memory.generation,code=g.memory.code_generation;g.cpu.exclusive_valid=true;
        CHECK(submit(&g,&l,&c)==CTR_GX_OK && c.signal && c.irq==5);
        CHECK(g.memory.generation==generation+1 && g.memory.code_generation==code+1 && !g.cpu.exclusive_valid);
        uint8_t pixels[BYTES];
        CHECK(ctr_texture_decode(backing(&g)+0x2000u,BYTES,0,64,64,pixels,sizeof(pixels))==CTR_TEXTURE_OK);
        for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
            /* Source(19,83,171,127), destination(16,32,48,255), source-alpha
             * over: floor((S*127+D*128)/255), alpha replaced independently. */
            const uint8_t inside[]={17,57,109,127},outside[]={16,32,48,255};
            CHECK(!memcmp(pixels+(y*64u+x)*4u,x>=8 && x<56 && y>=8 && y<56?inside:outside,4));
        }
        /* Retain the second triangle and use constant-alpha128 for overdraw. */
        memset(&l,0,sizeof(l));add(&l,0x101,UINT32_C(0x01dc0000));
        add(&l,0x103,UINT32_C(0x800d0b09));add(&l,0x22f,1);finish(&l);
        CHECK(execute(&g,&l,BASE,&c)==CTR_GX_OK && c.signal);
        CHECK(ctr_texture_decode(backing(&g)+0x2000u,BYTES,0,64,64,pixels,sizeof(pixels))==CTR_TEXTURE_OK);
        for(unsigned y=8;y<56;++y)for(unsigned x=8;x<56;++x) {
            const uint8_t first[]={17,57,109,127},over[]={18,70,140,127};
            CHECK(!memcmp(pixels+(y*64u+x)*4u,x+y<63u?first:over,4));
        }
        /* Switching back to logic must stop blending; preserve green/alpha. */
        memset(&l,0,sizeof(l));add(&l,0x100,0);add(&l,0x107,5u<<8);add(&l,0x22f,1);finish(&l);
        CHECK(execute(&g,&l,BASE,&c)==CTR_GX_OK && c.signal);
        CHECK(ctr_texture_decode(backing(&g)+0x2000u,BYTES,0,64,64,pixels,sizeof(pixels))==CTR_TEXTURE_OK);
        for(unsigned y=8;y<56;++y)for(unsigned x=8;x<56;++x) {
            const uint8_t first[]={17,57,109,127},copied[]={19,70,171,127};
            CHECK(!memcmp(pixels+(y*64u+x)*4u,x+y<63u?first:copied,4));
        }
        CHECK(g.gsp.pica_draws==4 && g.gsp.pica_fragments==4656 &&
              g.gsp.pica_scene->registers[0x103]==UINT32_C(0x800d0b09));
        ctr_guest_close(&g);
    }
    for(unsigned retained=0;retained<2;++retained)for(unsigned kind=0;kind<7;++kind) {
        ctr_guest g;list l;ctr_gx_completion c;open(&g,&l,false);
        if(retained)CHECK(execute(&g,&l,BASE,&c)==CTR_GX_OK);
        trim(&l);
        if(kind<3) {
            const uint32_t invalid[]={8u,5u,UINT32_C(0x000f0000)};
            add(&l,0x101,invalid[kind]);
        } else {
            add(&l,0x100,0x100);
            if(kind==4 || kind==6) { add(&l,0x101,UINT32_C(0x01dc0000));add(&l,0x112,15); }
            if(kind==5)add(&l,0x101,UINT32_C(0x01110000)); /* Read gate remains zero. */
            if(kind==6) {
                add(&l,0x103,UINT32_C(0x800d0b09));
                put32(l.bytes+l.size-4u,UINT32_C(0x70000)|0x103u); /* Missing alpha byte. */
            }
            add(&l,0x22f,1);
        }
        finish(&l);relay(&g);ctr_guest_shared *shared=&g.shared[g.gsp.relay_shared];uint8_t irq[CTR_GUEST_PAGE];
        CHECK(shared->size==sizeof(irq));memcpy(irq,shared->storage,sizeof(irq));
        ctr_scene_state before={0};if(retained)before=*g.gsp.pica_scene;
        ctr_pica_state pica=g.gsp.pica;uint8_t pixels[BYTES];memcpy(pixels,backing(&g)+0x2000u,BYTES);
        backend_mock m={0};ctr_scene_backend backend=mock_backend(&m);ctr_guest_set_scene_backend(&g,&backend);
        CHECK(ctr_memory_write_bytes(&g.memory,BASE,l.bytes,(uint32_t)l.size)==CTR_MEM_OK);
        uint64_t generation=g.memory.generation,code=g.memory.code_generation;g.cpu.exclusive_valid=true;
        CHECK(submit(&g,&l,&c)==CTR_GX_UNSUPPORTED && !c.signal);
        CHECK(!m.begins && !m.draws && !m.ends && !m.aborts);
        CHECK(!memcmp(pixels,backing(&g)+0x2000u,BYTES) && !memcmp(&pica,&g.gsp.pica,sizeof(pica)));
        CHECK(retained?!memcmp(&before,g.gsp.pica_scene,sizeof(before)):!g.gsp.pica_scene);
        CHECK(g.memory.generation==generation && g.memory.code_generation==code && g.cpu.exclusive_valid);
        CHECK(!memcmp(irq,shared->storage,sizeof(irq)) && !g.events[g.gsp.relay_event].signaled);
        ctr_guest_close(&g);
    }
}
enum { DEPTH=BASE+0x6000u, DEPTH_PA=PHYSICAL+0x6000u, DEPTH_BYTES=64u*64u*2u };
static size_t depth_offset(unsigned x,unsigned y) {
    unsigned morton=0;
    for(unsigned bit=0;bit<3;++bit)morton|=((x>>bit)&1u)<<(2u*bit)|((y>>bit)&1u)<<(2u*bit+1u);
    return (((size_t)(y/8u)*8u+x/8u)*64u+morton)*2u;
}
static unsigned depth_seed(unsigned x,unsigned y) {
    const unsigned values[]={0u,0x12abu,0x8001u,65535u};return values[(17u*x+31u*y)&3u];
}
static void depth_setup(ctr_guest *g,list *l,bool textured) {
    set(l,0x114,2);set(l,0x115,2);set(l,0x107,0x1f41);
    const unsigned regs[]={0x116,0x11c,0x04d,0x04e,0x06d};
    const uint32_t values[]={0,DEPTH_PA/8u,0xbf0000,0,1};size_t at=find(l,0x22f,0);
    for(unsigned i=0;i<sizeof(regs)/sizeof(*regs);++i,at+=8u)insert_at(l,at,regs[i],values[i]);
    trim(l);finish(l);
    for(unsigned i=0;i<4;++i)write_word(g,RESOURCE+i*(textured?24u:16u)+8u,0);
    uint8_t *raw=backing(g)+0x6000u;
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
        unsigned z=depth_seed(x,y);size_t p=depth_offset(x,y);raw[p]=(uint8_t)z;raw[p+1]=(uint8_t)(z>>8);
    }
}
static void depth_golden(ctr_guest *g,unsigned phase) {
    uint8_t pixels[BYTES];CHECK(ctr_texture_decode(backing(g)+0x2000u,BYTES,0,64,64,pixels,sizeof(pixels))==CTR_TEXTURE_OK);
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
        bool covered=x>=8 && x<56 && y>=8 && y<56,upper=x+y>=63u;
        uint8_t expected[]={16,32,48,255};unsigned z=depth_seed(x,y);
        if(covered) {
            z=phase==1u && upper?65535u:0u;
            if(phase && upper) { expected[0]=204;expected[1]=221;expected[2]=238; }
            else if(depth_seed(x,y)) { expected[0]=upper?0:255;expected[1]=upper?255:0;expected[2]=0; }
        }
        CHECK(!memcmp(pixels+(y*64u+x)*4u,expected,4));size_t p=depth_offset(x,y);
        CHECK(backing(g)[0x6000u+p]==(uint8_t)z && backing(g)[0x6000u+p+1]==(uint8_t)(z>>8));
    }
}
static bool depth_unexpected_begin(void *context,const ctr_scene_surface *surface,const ctr_scene_depth_surface *depth) {
    (void)surface;(void)depth;++((backend_mock*)context)->begins;return false;
}
static bool depth_unexpected_end(void *context,const ctr_scene_surface *surface,const ctr_scene_depth_surface *depth) {
    (void)surface;(void)depth;++((backend_mock*)context)->ends;return false;
}
static void guest_depth_success(void) {
    for(unsigned shorts=0;shorts<2;++shorts) {
        ctr_guest g;list l;ctr_gx_completion c;open(&g,&l,shorts!=0);depth_setup(&g,&l,false);
        /* Only depth aliases executable storage; the paired publication must
           invalidate both generations once and clear the exclusive monitor. */
        CHECK(ctr_memory_map(&g.memory,0x30000000u,DEPTH_BYTES,CTR_MEM_READ|CTR_MEM_EXEC,
                             backing(&g)+0x6000u,DEPTH_BYTES)==CTR_MEM_OK);
        CHECK(ctr_memory_write_bytes(&g.memory,BASE,l.bytes,(uint32_t)l.size)==CTR_MEM_OK);
        uint64_t generation=g.memory.generation,code=g.memory.code_generation;g.cpu.exclusive_valid=true;
        CHECK(submit(&g,&l,&c)==CTR_GX_OK && c.signal && c.irq==5);
        CHECK(g.memory.generation==generation+1 && g.memory.code_generation==code+1 && !g.cpu.exclusive_valid);
        CHECK(g.gsp.pica_draws==2 && g.gsp.pica_triangles==2 && g.gsp.pica_fragments==2304);
        depth_golden(&g,0);
        /* Reuse retained indices/state with far depth and a different color.
           GREATER passes every previously prepared upper-triangle sample. */
        for(unsigned i=0;i<4;++i)write_word(&g,RESOURCE+i*16u+8u,UINT32_C(0xbf800000));
        memset(&l,0,sizeof(l));add(&l,0x107,0x1f61);add(&l,0xc0,0xe000e);
        add(&l,0xc3,UINT32_C(0xffeeddcc));add(&l,0x22f,1);finish(&l);
        CHECK(execute(&g,&l,BASE,&c)==CTR_GX_OK && c.signal);depth_golden(&g,1);
        uint8_t color[BYTES],depth[DEPTH_BYTES];memcpy(color,backing(&g)+0x2000u,BYTES);memcpy(depth,backing(&g)+0x6000u,DEPTH_BYTES);
        memset(&l,0,sizeof(l));add(&l,0x22f,1);finish(&l);
        CHECK(execute(&g,&l,BASE,&c)==CTR_GX_OK && c.signal);
        CHECK(!memcmp(color,backing(&g)+0x2000u,BYTES) && !memcmp(depth,backing(&g)+0x6000u,DEPTH_BYTES));
        /* Disabled depth testing still permits a depth-only write. */
        for(unsigned i=0;i<4;++i)write_word(&g,RESOURCE+i*16u+8u,0);
        memset(&l,0,sizeof(l));add(&l,0x107,0x1000);add(&l,0x114,0);add(&l,0x22f,1);finish(&l);
        CHECK(execute(&g,&l,BASE,&c)==CTR_GX_OK && c.signal);depth_golden(&g,2);
        CHECK(g.gsp.pica_draws==5 && g.gsp.pica_fragments==5832);ctr_guest_close(&g);
    }
    /* A fractional software depth distinguishes little-endian 0x12ab/0x8001
       reads from byte-swapped values, and truncates half-range to 32767. */
    ctr_guest g;list l;ctr_gx_completion c;open(&g,&l,false);depth_setup(&g,&l,false);
    for(unsigned i=0;i<4;++i)write_word(&g,RESOURCE+i*16u+8u,UINT32_C(0xbf000000));
    CHECK(execute(&g,&l,BASE,&c)==CTR_GX_OK && c.signal);uint8_t pixels[BYTES];
    CHECK(ctr_texture_decode(backing(&g)+0x2000u,BYTES,0,64,64,pixels,sizeof(pixels))==CTR_TEXTURE_OK);
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
        unsigned z=depth_seed(x,y);uint8_t expected[]={16,32,48,255};
        if(x>=8 && x<56 && y>=8 && y<56 && z>32767u) {
            z=32767;expected[0]=x+y<63u?255:0;expected[1]=x+y<63u?0:255;expected[2]=0;
        }
        CHECK(!memcmp(pixels+(y*64u+x)*4u,expected,4));size_t p=depth_offset(x,y);
        CHECK(backing(&g)[0x6000u+p]==(uint8_t)z && backing(&g)[0x6000u+p+1]==(uint8_t)(z>>8));
    }
    ctr_guest_close(&g);
}
static void guest_depth_rollback(void) {
    for(unsigned retained=0;retained<2;++retained)for(unsigned kind=0;kind<15;++kind) {
        ctr_guest g;list l;ctr_gx_completion c;
        bool textured=kind==5u;
        if(textured)textured_open(&g,&l,false);else open(&g,&l,false);
        depth_setup(&g,&l,textured);if(retained)CHECK(execute(&g,&l,BASE,&c)==CTR_GX_OK);
        ctr_gx_result expected=CTR_GX_UNSUPPORTED;uint32_t command_address=BASE;
        if(kind==0) { trim(&l);add(&l,0x04d,0x3f0000);finish(&l); }
        if(kind==1) { CHECK(ctr_memory_change_range(&g.memory,DEPTH+8u,8,CTR_MEM_READ,false)==CTR_MEM_OK);expected=CTR_GX_MEMORY; }
        if(kind==2) { set(&l,0x11c,(PHYSICAL+0x2000u)/8u);expected=CTR_GX_MEMORY; }
        if(kind==3) { set(&l,0x11c,PHYSICAL/8u);expected=CTR_GX_MEMORY; }
        if(kind==4) { set(&l,0x11c,(PHYSICAL+0x1000u)/8u);expected=CTR_GX_MEMORY; }
        if(kind==5) { set(&l,0x085,DEPTH_PA/8u);expected=CTR_GX_MEMORY; }
        if(kind==6) {
            CHECK(ctr_memory_change_range(&g.memory,DEPTH,DEPTH_BYTES,CTR_MEM_READ,false)==CTR_MEM_OK);
            for(size_t i=0;i<g.memory.count;++i)if(g.memory.maps[i].base==DEPTH) {
                g.memory.maps[i].permissions=3;g.memory.maps[i].storage=backing(&g)+0x2000u;
            }
            expected=CTR_GX_MEMORY;
        }
        if(kind==7) { insert_at(&l,find(&l,0x22f,1),0x11c,(PHYSICAL+0x8000u)/8u);trim(&l);finish(&l); }
        if(kind==8)set(&l,0x116,2);
        if(kind==9)set(&l,0x114,3);
        if(kind==10)set(&l,0x115,3);
        if(kind==11)set(&l,0x11c,0);
        if(kind==12) { trim(&l);add(&l,0x06d,0);finish(&l); }
        if(kind==14) {
            command_address=0x30002000u;expected=CTR_GX_MEMORY;
            CHECK(ctr_memory_map(&g.memory,command_address,DEPTH_BYTES,3,backing(&g)+0x6000u,DEPTH_BYTES)==CTR_MEM_OK);
        }
        relay(&g);ctr_guest_shared *shared=&g.shared[g.gsp.relay_shared];uint8_t irq[CTR_GUEST_PAGE];
        CHECK(shared->size==sizeof(irq));memcpy(irq,shared->storage,sizeof(irq));
        backend_mock m={0};ctr_scene_backend backend=mock_backend(&m);
        if(kind!=13) { backend.begin_depth=depth_unexpected_begin;backend.end_depth=depth_unexpected_end; }
        ctr_guest_set_scene_backend(&g,&backend);
        CHECK(ctr_memory_write_bytes(&g.memory,command_address,l.bytes,(uint32_t)l.size)==CTR_MEM_OK);
        ctr_scene_state before={0};if(retained)before=*g.gsp.pica_scene;ctr_pica_state pica=g.gsp.pica;
        uint8_t all[65536];memcpy(all,backing(&g),sizeof(all));
        uint64_t generation=g.memory.generation,code=g.memory.code_generation;g.cpu.exclusive_valid=true;
        uint32_t commands[]={0x01000101,command_address,(uint32_t)l.size,0,0,0,0,1};
        CHECK(ctr_gx_execute(&g,commands,&c)==expected && !c.signal);
        CHECK(!m.begins && !m.draws && !m.ends && !m.aborts);
        CHECK(!memcmp(all,backing(&g),sizeof(all)) && !memcmp(&pica,&g.gsp.pica,sizeof(pica)));
        CHECK(retained?g.gsp.pica_scene && !memcmp(&before,g.gsp.pica_scene,sizeof(before)):!g.gsp.pica_scene);
        CHECK(g.memory.generation==generation && g.memory.code_generation==code && g.cpu.exclusive_valid);
        CHECK(!memcmp(irq,shared->storage,sizeof(irq)) && !g.events[g.gsp.relay_event].signaled);
        ctr_guest_close(&g);
    }
}
static void guest_depth_queues(void) {
    ctr_guest g;list good,bad;open(&g,&good,false);depth_setup(&g,&good,false);bad=good;
    trim(&bad);add(&bad,0x115,3);finish(&bad);unsigned owner=relay(&g);
    CHECK(ctr_memory_write_bytes(&g.memory,BASE,good.bytes,(uint32_t)good.size)==CTR_MEM_OK);
    CHECK(ctr_memory_write_bytes(&g.memory,BASE+0x800u,bad.bytes,(uint32_t)bad.size)==CTR_MEM_OK);
    write_word(&g,0x10002800,14u|(2u<<8));
    for(unsigned entry=0;entry<2;++entry) {
        uint32_t w[]={0x01000101,BASE+(entry?0x800u:0),(uint32_t)(entry?bad.size:good.size),0,0,0,0,1};
        for(unsigned i=0;i<8;++i)write_word(&g,0x10002820u+(entry?0:14u)*32u+i*4u,w[i]);
    }
    uint32_t trigger[]={0xc0000},out[8];unsigned count;
    CHECK(!ctr_gsp_request(&g,owner,trigger,out,&count) && g.state==CTR_GUEST_FAULTED);
    CHECK(read_word(&g,0x10002800)==0x100u && g.gsp.gx_commands==1 && g.gsp.gx_pica==1 && g.gsp.pica_draws==2);
    CHECK(g.shared[g.gsp.relay_shared].storage[1]==1 && g.shared[g.gsp.relay_shared].storage[12]==5);
    CHECK(g.events[g.gsp.relay_event].signaled);depth_golden(&g,0);ctr_guest_close(&g);
}
enum { STENCIL_BYTES=64u*64u*4u };
static size_t stencil_offset(unsigned x,unsigned y) { return depth_offset(x,y)*2u; }
static uint32_t stencil_seed(unsigned x,unsigned y) {
    const uint32_t depth[]={0u,UINT32_C(0x1234ab),UINT32_C(0x800102),UINT32_C(0xffffff)};
    const uint32_t stencil[]={0u,UINT32_C(0x2b),UINT32_C(0xb1),UINT32_C(0xff)};
    return depth[(5u*x+3u*y)&3u]|stencil[(x/2u+7u*y)&3u]<<24;
}
static bool stencil_covered(unsigned x,unsigned y) { return x>=8u && x<56u && y>=8u && y<56u; }
static uint32_t stencil_prepared(unsigned x,unsigned y) {
    uint32_t value=stencil_seed(x,y);
    return stencil_covered(x,y)?(value&UINT32_C(0xf0ffffff))|UINT32_C(0x05000000):value;
}
static void stencil_setup(ctr_guest *g,list *l,bool textured) {
    depth_setup(g,l,textured);set(l,0x116,3);set(l,0x114,3);set(l,0x115,3);
    /* Color/depth writes off; ALWAYS/REPLACE changes only the stencil low
       nibble. The packed 24-bit depth is independently seeded and preserved. */
    set(l,0x107,0);set(l,0x105,UINT32_C(0xffa50f11));set(l,0x106,0x200);
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x)
        put32(backing(g)+0x6000u+stencil_offset(x,y),stencil_seed(x,y));
}
static void stencil_prepass_golden(ctr_guest *g) {
    uint8_t pixels[BYTES];CHECK(ctr_texture_decode(backing(g)+0x2000u,BYTES,0,64,64,pixels,sizeof(pixels))==CTR_TEXTURE_OK);
    const uint8_t background[]={16,32,48,255};
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
        CHECK(!memcmp(pixels+(y*64u+x)*4u,background,4));
        CHECK(word(backing(g)+0x6000u+stencil_offset(x,y))==stencil_prepared(x,y));
    }
}
static void stencil_draws(list *l,bool shorts) {
    unsigned flag=shorts?UINT32_C(0x80000000):0u,width=shorts?2u:1u;
    add(l,0x227,flag|64u);add(l,0x22f,1);add(l,0x227,flag|(64u+3u*width));add(l,0x22f,1);finish(l);
}
static void guest_stencil_success(void) {
    for(unsigned shorts=0;shorts<2;++shorts) {
        ctr_guest g;list l;ctr_gx_completion c;open(&g,&l,shorts!=0);stencil_setup(&g,&l,false);
        CHECK(ctr_memory_map(&g.memory,0x30000000u,STENCIL_BYTES,CTR_MEM_READ|CTR_MEM_EXEC,
                             backing(&g)+0x6000u,STENCIL_BYTES)==CTR_MEM_OK);
        CHECK(ctr_memory_write_bytes(&g.memory,BASE,l.bytes,(uint32_t)l.size)==CTR_MEM_OK);
        uint64_t generation=g.memory.generation,code=g.memory.code_generation;g.cpu.exclusive_valid=true;
        CHECK(submit(&g,&l,&c)==CTR_GX_OK && c.signal && c.irq==5);
        CHECK(g.memory.generation==generation+1 && g.memory.code_generation==code+1 && !g.cpu.exclusive_valid);
        CHECK(g.gsp.pica_draws==2 && g.gsp.pica_fragments==2304);stencil_prepass_golden(&g);
        /* Retained state compares masked reference 0x50 against stored high
           nibbles. Reversing that comparison admits the opposite seed classes.
           Fractional D24 depth separates low/high seeded depth bytes exactly. */
        for(unsigned i=0;i<4;++i)write_word(&g,RESOURCE+i*16u+8u,UINT32_C(0xbf000000));
        memset(&l,0,sizeof(l));add(&l,0x105,UINT32_C(0xf053ff41));add(&l,0x106,0x245);
        add(&l,0x107,0x1f41);add(&l,0xc0,UINT32_C(0xe000e));add(&l,0xc3,UINT32_C(0xffeeddcc));
        stencil_draws(&l,shorts!=0);CHECK(execute(&g,&l,BASE,&c)==CTR_GX_OK && c.signal);
        uint8_t pixels[BYTES];CHECK(ctr_texture_decode(backing(&g)+0x2000u,BYTES,0,64,64,pixels,sizeof(pixels))==CTR_TEXTURE_OK);
        unsigned stencil_failed=0,depth_failed=0,passed=0;
        for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
            uint32_t packed=stencil_prepared(x,y),z=packed&UINT32_C(0xffffff),stencil=packed>>24;
            uint8_t expected[]={16,32,48,255};
            if(stencil_covered(x,y)) {
                if((stencil&0xf0u)<=0x50u) { stencil^=255u;++stencil_failed; }
                else if(z<=UINT32_C(0x7fffff)) { --stencil;++depth_failed; }
                else { z=UINT32_C(0x7fffff);stencil=0x53;expected[0]=204;expected[1]=221;expected[2]=238;++passed; }
            }
            CHECK(!memcmp(pixels+(y*64u+x)*4u,expected,4));
            CHECK(word(backing(&g)+0x6000u+stencil_offset(x,y))==(z|(stencil<<24)));
        }
        CHECK(stencil_failed && depth_failed && passed && stencil_failed+depth_failed+passed==2304);
        CHECK(g.gsp.pica_draws==4 && g.gsp.pica_fragments==4608);
        uint8_t color[BYTES],packed[STENCIL_BYTES];memcpy(color,backing(&g)+0x2000u,BYTES);memcpy(packed,backing(&g)+0x6000u,STENCIL_BYTES);
        /* A failed stencil test with REPLACE still preserves both targets when
           the whole depth/stencil write gate is zero. */
        memset(&l,0,sizeof(l));add(&l,0x115,0);add(&l,0x105,UINT32_C(0xffa5ff01));add(&l,0x106,0x222);
        stencil_draws(&l,shorts!=0);CHECK(execute(&g,&l,BASE,&c)==CTR_GX_OK && c.signal);
        CHECK(!memcmp(color,backing(&g)+0x2000u,BYTES) && !memcmp(packed,backing(&g)+0x6000u,STENCIL_BYTES));
        /* Alpha NEVER precedes stencil/depth, even with every pass action and
           depth write enabled. */
        memset(&l,0,sizeof(l));add(&l,0x115,3);add(&l,0x104,1);add(&l,0x105,UINT32_C(0xff3cff11));add(&l,0x107,0x1f11);
        stencil_draws(&l,shorts!=0);CHECK(execute(&g,&l,BASE,&c)==CTR_GX_OK && c.signal);
        CHECK(!memcmp(color,backing(&g)+0x2000u,BYTES) && !memcmp(packed,backing(&g)+0x6000u,STENCIL_BYTES));
        /* Disabled tests permit depth-only writes without a read gate. The
           stencil byte remains untouched by each packed depth update. */
        for(unsigned i=0;i<4;++i)write_word(&g,RESOURCE+i*16u+8u,0);
        memset(&l,0,sizeof(l));add(&l,0x104,0);add(&l,0x105,0);add(&l,0x106,0);add(&l,0x114,0);add(&l,0x107,0x1000);
        stencil_draws(&l,shorts!=0);CHECK(execute(&g,&l,BASE,&c)==CTR_GX_OK && c.signal);
        CHECK(!memcmp(color,backing(&g)+0x2000u,BYTES));
        for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
            size_t offset=stencil_offset(x,y);uint32_t expected=word(packed+offset);
            if(stencil_covered(x,y))expected&=UINT32_C(0xff000000);
            CHECK(word(backing(&g)+0x6000u+offset)==expected);
        }
        CHECK(g.gsp.pica_draws==10 && g.gsp.pica_fragments==11520);ctr_guest_close(&g);
    }
}
static void guest_stencil_rollback(void) {
    for(unsigned retained=0;retained<2;++retained)for(unsigned kind=0;kind<18;++kind) {
        ctr_guest g;list l;ctr_gx_completion c;bool textured=kind==12u;
        if(textured)textured_open(&g,&l,false);else open(&g,&l,false);
        stencil_setup(&g,&l,textured);if(retained)CHECK(execute(&g,&l,BASE,&c)==CTR_GX_OK);
        ctr_gx_result expected=CTR_GX_UNSUPPORTED;uint32_t command_address=BASE;
        if(kind==0) { trim(&l);add(&l,0x105,2);finish(&l); }
        if(kind==1) { trim(&l);add(&l,0x106,8);finish(&l); }
        if(kind==2)set(&l,0x116,2);
        if(kind==3)set(&l,0x114,2);
        if(kind==4)set(&l,0x115,2);
        if(kind==5) { insert_at(&l,find(&l,0x22f,1),0x116,0);trim(&l);finish(&l); }
        if(kind==6) { insert_at(&l,find(&l,0x22f,1),0x11c,(PHYSICAL+0xa000u)/8u);trim(&l);finish(&l); }
        if(kind==7)set(&l,0x11c,0);
        if(kind==8) { CHECK(ctr_memory_change_range(&g.memory,DEPTH+8u,8,CTR_MEM_READ,false)==CTR_MEM_OK);expected=CTR_GX_MEMORY; }
        if(kind==9) { set(&l,0x11c,(PHYSICAL+0x2000u)/8u);expected=CTR_GX_MEMORY; }
        if(kind==10) { set(&l,0x11c,PHYSICAL/8u);expected=CTR_GX_MEMORY; }
        if(kind==11) { set(&l,0x11c,(PHYSICAL+0x1000u)/8u);expected=CTR_GX_MEMORY; }
        if(kind==12) { set(&l,0x085,DEPTH_PA/8u);expected=CTR_GX_MEMORY; }
        if(kind==13) {
            CHECK(ctr_memory_change_range(&g.memory,DEPTH,STENCIL_BYTES,CTR_MEM_READ,false)==CTR_MEM_OK);
            for(size_t i=0;i<g.memory.count;++i)if(g.memory.maps[i].base==DEPTH) {
                g.memory.maps[i].permissions=3;g.memory.maps[i].storage=backing(&g)+0x2000u;
            }
            expected=CTR_GX_MEMORY;
        }
        if(kind==14) { size_t at=find(&l,0x105,0);put32(l.bytes+at,0x13);put32(l.bytes+at+4u,UINT32_C(0x10000)|0x105u); }
        if(kind==16) { set(&l,0x11c,(PHYSICAL+0xe000u)/8u);expected=CTR_GX_MEMORY; }
        if(kind==17) {
            command_address=0x30002000u;expected=CTR_GX_MEMORY;
            CHECK(ctr_memory_map(&g.memory,command_address,STENCIL_BYTES,3,backing(&g)+0x6000u,STENCIL_BYTES)==CTR_MEM_OK);
        }
        relay(&g);ctr_guest_shared *shared=&g.shared[g.gsp.relay_shared];uint8_t irq[CTR_GUEST_PAGE];
        CHECK(shared->size==sizeof(irq));memcpy(irq,shared->storage,sizeof(irq));
        backend_mock m={0};ctr_scene_backend backend=mock_backend(&m);
        if(kind!=15) { backend.begin_depth=depth_unexpected_begin;backend.end_depth=depth_unexpected_end; }
        ctr_guest_set_scene_backend(&g,&backend);
        CHECK(ctr_memory_write_bytes(&g.memory,command_address,l.bytes,(uint32_t)l.size)==CTR_MEM_OK);
        ctr_scene_state before={0};if(retained)before=*g.gsp.pica_scene;ctr_pica_state pica=g.gsp.pica;
        uint8_t all[65536];memcpy(all,backing(&g),sizeof(all));
        uint64_t generation=g.memory.generation,code=g.memory.code_generation;g.cpu.exclusive_valid=true;
        uint32_t commands[]={0x01000101,command_address,(uint32_t)l.size,0,0,0,0,1};
        CHECK(ctr_gx_execute(&g,commands,&c)==expected && !c.signal);
        CHECK(!m.begins && !m.draws && !m.ends && !m.aborts);
        CHECK(!memcmp(all,backing(&g),sizeof(all)) && !memcmp(&pica,&g.gsp.pica,sizeof(pica)));
        CHECK(retained?g.gsp.pica_scene && !memcmp(&before,g.gsp.pica_scene,sizeof(before)):!g.gsp.pica_scene);
        CHECK(g.memory.generation==generation && g.memory.code_generation==code && g.cpu.exclusive_valid);
        CHECK(!memcmp(irq,shared->storage,sizeof(irq)) && !g.events[g.gsp.relay_event].signaled);ctr_guest_close(&g);
    }
}
static void guest_stencil_queues(void) {
    ctr_guest g;list good,bad;open(&g,&good,false);stencil_setup(&g,&good,false);bad=good;
    trim(&bad);add(&bad,0x106,8);finish(&bad);unsigned owner=relay(&g);
    CHECK(ctr_memory_write_bytes(&g.memory,BASE,good.bytes,(uint32_t)good.size)==CTR_MEM_OK);
    CHECK(ctr_memory_write_bytes(&g.memory,BASE+0x800u,bad.bytes,(uint32_t)bad.size)==CTR_MEM_OK);
    write_word(&g,0x10002800,14u|(2u<<8));
    for(unsigned entry=0;entry<2;++entry) {
        uint32_t w[]={0x01000101,BASE+(entry?0x800u:0),(uint32_t)(entry?bad.size:good.size),0,0,0,0,1};
        for(unsigned i=0;i<8;++i)write_word(&g,0x10002820u+(entry?0:14u)*32u+i*4u,w[i]);
    }
    uint32_t trigger[]={0xc0000},out[8];unsigned count;
    CHECK(!ctr_gsp_request(&g,owner,trigger,out,&count) && g.state==CTR_GUEST_FAULTED);
    CHECK(read_word(&g,0x10002800)==0x100u && g.gsp.gx_commands==1 && g.gsp.gx_pica==1 && g.gsp.pica_draws==2);
    CHECK(g.shared[g.gsp.relay_shared].storage[1]==1 && g.shared[g.gsp.relay_shared].storage[12]==5);
    CHECK(g.events[g.gsp.relay_event].signaled);stencil_prepass_golden(&g);ctr_guest_close(&g);
}
int main(void) {
    successful();rollback();shader_capacity();arithmetic_draws();queues();native_backend_success();native_backend_rollback();
    guest_texture_success();guest_linear_texture();guest_texture_snapshots();guest_texture_rollback();guest_texture_queues();
    guest_material_success();guest_material_snapshots();guest_material_rollback();guest_material_queues();guest_material_limits();
    guest_fragment_output();guest_blending();guest_depth_success();guest_depth_rollback();guest_depth_queues();
    guest_stencil_success();guest_stencil_rollback();guest_stencil_queues();
    printf("guest PICA draw tests: %u assertions passed\n",assertions);return 0;
}
