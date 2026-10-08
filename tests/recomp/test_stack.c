#include "ctr_stack_fixture.h"
#include "ctr_recomp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned assertions;
static void record(void *context,const char *event,const char *hash,uint32_t pc,bool thumb,uint32_t detail) {
    (void)event;(void)hash;(void)pc;(void)thumb;*(uint32_t *)context=detail;
}
#define CHECK(x) do { ++assertions;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static void equal(const ctr_guest *a,const ctr_guest *b) {
    CHECK(a->state==b->state && a->cpu.instructions==b->cpu.instructions && a->svc_count==b->svc_count);
    CHECK(a->cpu.cpsr==b->cpu.cpsr && !memcmp(a->cpu.r,b->cpu.r,sizeof(a->cpu.r)));
    CHECK(a->fault==b->fault && a->fault_detail==b->fault_detail && a->cpu.memory_fault==b->cpu.memory_fault);
    CHECK(a->memory.count==b->memory.count && a->memory.generation==b->memory.generation && a->memory.code_generation==b->memory.code_generation);
    CHECK(a->cpu.exclusive_valid==b->cpu.exclusive_valid);
    for(size_t i=0;i<a->memory.count;++i) {
        const ctr_mapping *x=&a->memory.maps[i],*y=&b->memory.maps[i];
        CHECK(x->base==y->base && x->size==y->size && x->permissions==y->permissions && !memcmp(x->storage,y->storage,x->size));
    }
}
static void open_paths(ctr_guest paths[3]) {
    for(unsigned i=0;i<3;++i)CHECK(ctr_stack_fixture_open(&paths[i]));
    CHECK(ctr_recomp_attach(&paths[0],false));
}
static void close_paths(ctr_guest paths[3]) { for(unsigned i=0;i<3;++i)ctr_guest_close(&paths[i]); }
static void calls(void) {
    ctr_guest paths[3];open_paths(paths);uint32_t initial_sp=paths[0].cpu.r[13];
    for(unsigned slice=0;slice<100 && paths[0].state!=CTR_GUEST_EXITED;++slice) {
        unsigned budget=slice%3==0?1:slice%3==1?7:23;
        for(unsigned i=0;i<3;++i)ctr_guest_run(&paths[i],budget,slice,i!=2);
        CHECK(paths[0].state!=CTR_GUEST_FAULTED);equal(&paths[0],&paths[1]);equal(&paths[0],&paths[2]);
    }
    CHECK(paths[0].state==CTR_GUEST_EXITED && paths[0].cpu.instructions==47 && paths[0].svc_count==1 && paths[0].cpu.r[13]==initial_sp);
    const uint32_t expected[]={10,10,3,7,0x4444,0x5555,0x08000000,0x7777};uint32_t value;
    for(unsigned i=0;i<8;++i)CHECK(ctr_memory_read(&paths[0].memory,0x08000000+i*4,4,1,&value)==CTR_MEM_OK && value==expected[i]);
    for(unsigned i=0;i<3;++i)CHECK(!paths[i].cpu.fallback_instructions);
    close_paths(paths);
}
static void faults(void) {
    const uint32_t offsets[]={0x110,0x118,0x200,0x208,0x20c,0x216};
    for(unsigned kind=0;kind<6;++kind) {
        ctr_guest paths[3];uint32_t details[3]={0};open_paths(paths);
        for(unsigned i=0;i<3;++i) {
            ctr_guest *g=&paths[i];g->cpu.r[15]=0x100000+offsets[kind];
            g->cpu.log=record;g->cpu.log_context=&details[i];
            if(kind>=2)g->cpu.cpsr|=CTR_FLAG_T;
            uint32_t base=(kind==1 || kind==2)?0x08001004:kind==5?0x08000ff8:0x08000ffc;
            g->cpu.r[6]=base;g->cpu.r[13]=base;g->cpu.r[0]=0x11;g->cpu.r[1]=0x22;g->cpu.r[2]=0x33;
            g->cpu.r[3]=0x44;g->cpu.r[4]=0x55;g->cpu.r[5]=0x66;g->cpu.r[14]=0x77;
            CHECK(ctr_memory_write(&g->memory,0x08000ff8,4,0xaabbccdd)==CTR_MEM_OK);
            CHECK(ctr_memory_write(&g->memory,0x08000ffc,4,0x12345678)==CTR_MEM_OK);
            CHECK(ctr_guest_run(g,1,0,i!=2)==CTR_GUEST_FAULTED);
            CHECK(g->fault==CTR_CPU_MEMORY_FAULT && g->cpu.memory_fault==CTR_MEM_UNMAPPED && details[i]==0x08001000 &&
                g->fault_detail==g->cpu.r[15] && g->cpu.r[6]==base && g->cpu.r[13]==base);
            CHECK(g->cpu.r[15]==0x100000+offsets[kind] && g->cpu.instructions==1 && !g->cpu.fallback_instructions);
        }
        equal(&paths[0],&paths[1]);equal(&paths[0],&paths[2]);close_paths(paths);
    }
}
int main(void) {
    calls();faults();printf("{\"ok\":true,\"assertions\":%u,\"original_stack_instructions\":47,\"partial_word_fault_parity\":true,\"external_original_interpreter_full_state_match\":true,\"console_verified\":false}\n",assertions);return 0;
}
