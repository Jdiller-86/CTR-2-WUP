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
    ctr_scene_backend backend={m,backend_begin,backend_draw,backend_end,backend_abort};return backend;
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
    ctr_scene_backend backend={&m,guest_texture_begin,guest_texture_draw,guest_texture_end,guest_texture_abort};
    ctr_guest_set_scene_backend(&g,&backend);
    CHECK(execute(&g,&l,BASE,&c)==CTR_GX_OK && c.signal && c.irq==5);
    CHECK(m.begins==1 && m.draws==2 && m.ends==1 && !m.aborts);texture_golden(&g,true,false,true);
    ctr_guest_close(&g);
}
static void guest_texture_rollback(void) {
    for(unsigned retained=0;retained<2;++retained)for(unsigned kind=0;kind<9;++kind) {
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
int main(void) {
    successful();rollback();shader_capacity();arithmetic_draws();queues();native_backend_success();native_backend_rollback();
    guest_texture_success();guest_texture_snapshots();guest_texture_rollback();guest_texture_queues();
    printf("guest PICA draw tests: %u assertions passed\n",assertions);return 0;
}
