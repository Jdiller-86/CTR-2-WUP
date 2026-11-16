/* Original modules call main ARM code, indexed exports and Thumb code. */
#include "ctr_module.h"
#ifdef CTR_MODULE_EXTERNAL
#include "ctr_recomp.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const ctr_guest_image ctr_module_fixture_image;
extern const ctr_block ctr_module_fixture_blocks[];
extern const size_t ctr_module_fixture_block_count,ctr_module_fixture_result_count;
extern const uint32_t ctr_module_fixture_results[];
extern const ctr_module_image ctr_module_package[];
extern const size_t ctr_module_package_count;
static unsigned checks;
#define CHECK(x) do { ++checks;if(!(x)) { fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,#x);exit(1); } } while(0)
static void open_guest(ctr_guest *g) {
    CHECK(ctr_guest_open(g,&ctr_module_fixture_image,ctr_module_fixture_blocks,ctr_module_fixture_block_count));
    CHECK(ctr_modules_configure(g,ctr_module_package,ctr_module_package_count));
}
static uint32_t word(ctr_guest *g,uint32_t at) {
    uint32_t value=0;CHECK(ctr_memory_read(&g->memory,at,4,CTR_MEM_READ,&value)==CTR_MEM_OK);return value;
}
static void run(ctr_guest *g,bool translated,uint64_t budget) {
    bool checked_batch=false;
    for(unsigned i=0;i<3000 && g->state!=CTR_GUEST_EXITED && g->state!=CTR_GUEST_FAULTED;++i) {
        ctr_guest_run(g,budget,0,translated);
        if(!checked_batch && ctr_modules_has_code(g,0x300180)) {
            uint32_t first=word(g,0x3000f8),value;
            CHECK(ctr_memory_read(&g->memory,first+6,1,CTR_MEM_READ,&value)==CTR_MEM_OK && value==1);
            CHECK(ctr_memory_read(&g->memory,first+18,1,CTR_MEM_READ,&value)==CTR_MEM_OK && value==0);
            checked_batch=true;
        }
    }
    CHECK(checked_batch);
    if(g->state!=CTR_GUEST_EXITED)fprintf(stderr,"state=%s fault=%s detail=%08x pc=%08x ipc=%s/%08x result=%08x\n",
        ctr_guest_state_name(g->state),ctr_cpu_result_name(g->fault),g->fault_detail,g->cpu.r[15],g->ipc_service,g->ipc_header,g->ipc_result);
    CHECK(g->state==CTR_GUEST_EXITED && !g->cpu.fallback_instructions);
    for(size_t i=0;i<ctr_module_fixture_result_count;++i) {
        uint32_t value=word(g,0x08000000u+(uint32_t)i*4);
        if(value!=ctr_module_fixture_results[i])fprintf(stderr,"result[%zu]=%08x expected=%08x\n",i,value,ctr_module_fixture_results[i]);
        CHECK(value==ctr_module_fixture_results[i]);
    }
    const uint32_t values[]={15,15,12,14,12,0xe0a01bf5u};
    for(unsigned i=0;i<6;++i)CHECK(word(g,0x08000200+i*4)==values[i]);
    CHECK(g->ipc_failures==2 && !ctr_modules_bytes(g));
    for(unsigned i=0;i<CTR_GUEST_MAX_ALLOCATIONS;++i)CHECK(!g->allocations[i].storage);
    for(uint32_t address=0x200000;address<=0x400000;address+=0x100000)
        CHECK(ctr_memory_check_range(&g->memory,address,1,CTR_MEM_READ)==CTR_MEM_UNMAPPED);
}
static void parity(void) {
    ctr_guest *a=malloc(sizeof(*a)),*b=malloc(sizeof(*b));CHECK(a && b);
    open_guest(a);open_guest(b);
#ifdef CTR_MODULE_EXTERNAL
    CHECK(ctr_recomp_attach(b,false));
#endif
    run(a,false,17);run(b,true,7);
    CHECK(a->cpu.instructions==b->cpu.instructions && a->cpu.cpsr==b->cpu.cpsr && !memcmp(a->cpu.r,b->cpu.r,sizeof(a->cpu.r)));
    CHECK(a->memory.count==b->memory.count);
    for(size_t i=0;i<a->memory.count;++i) {
        const ctr_mapping *x=&a->memory.maps[i],*y=&b->memory.maps[i];
        /* Uneven host budgets publish the same time through different sequence
           counts; compare the clock payload and every other mapped byte. */
        unsigned skip=x->base==0x1ff81000u?4u:0u;
        CHECK(x->base==y->base && x->size==y->size && x->permissions==y->permissions && !memcmp(x->storage+skip,y->storage+skip,x->size-skip));
    }
    /* Even a retained translated entry address cannot execute after unloading. */
    a->cpu.r[15]=b->cpu.r[15]=0x300180;
    CHECK(ctr_cpu_step(&a->cpu)==CTR_CPU_MEMORY_FAULT);
    CHECK(ctr_modules_dispatch(b,1)==CTR_CPU_MEMORY_FAULT);
    ctr_guest_close(a);ctr_guest_close(b);free(a);free(b);
}
static void rejection_and_close(void) {
    ctr_guest *g=malloc(sizeof(*g));CHECK(g);open_guest(g);
    uint64_t generation=g->memory.generation;size_t maps=g->memory.count;
    CHECK(ctr_memory_write(&g->memory,0x08001000,1,1)==CTR_MEM_OK);
    CHECK(ctr_modules_load(g,0x08001000,0x200000,4096,0,0,0,0,true,true)==CTR_MODULE_IDENTITY);
    CHECK(!ctr_modules_bytes(g) && g->memory.count==maps && g->memory.generation==generation+1);
    CHECK(ctr_memory_write(&g->memory,0x08001000,1,0)==CTR_MEM_OK);
    CHECK(ctr_modules_load(g,0x08001000,0x200000,4096,0,0,0,0,true,true)==CTR_MODULE_OK);
    uint32_t address=0;CHECK(ctr_modules_lookup(g,0x200000,"main_helper",&address)==CTR_MODULE_OK && address==0x101800);
    /* A foreign executable mutation must never be blessed by loading/unloading. */
    CHECK(ctr_memory_backing_changed(&g->memory,g->storage[0],4)==CTR_MEM_OK);
    CHECK(ctr_modules_unload(g,0x200000,0x08001000,true)==CTR_MODULE_INVALIDATED);
    CHECK(ctr_modules_dispatch(g,1)==CTR_CPU_CODE_INVALIDATED);
    ctr_modules_close(g);
    CHECK(!g->modules && ctr_memory_check_range(&g->memory,0x200000,1,CTR_MEM_READ)==CTR_MEM_UNMAPPED);
    ctr_guest_close(g);CHECK(!g->modules);free(g);
}
static void malformed_relocation(void) {
    ctr_guest *g=malloc(sizeof(*g));CHECK(g);
    ctr_module_image images[3];memcpy(images,ctr_module_package,sizeof(images));
    uint8_t *bad=malloc(images[1].size);CHECK(bad);memcpy(bad,images[1].bytes,images[1].size);
    /* CRO ExternalRelocationTableOffset is header word at 0xf8. */
    uint32_t offset=(uint32_t)bad[0xf8]|(uint32_t)bad[0xf9]<<8|(uint32_t)bad[0xfa]<<16|(uint32_t)bad[0xfb]<<24;
    memset(bad+offset,0xff,4);images[1].bytes=bad;
    CHECK(ctr_guest_open(g,&ctr_module_fixture_image,ctr_module_fixture_blocks,ctr_module_fixture_block_count));
    CHECK(ctr_modules_configure(g,images,3));
    CHECK(ctr_memory_write_bytes(&g->memory,0x08002000,bad,images[1].size)==CTR_MEM_OK);
    CHECK(ctr_modules_load(g,0x08001000,0x200000,4096,0,0,0,0,true,true)==CTR_MODULE_OK);
    g->cpu.r[0]=3;g->cpu.r[1]=0x08010000;g->cpu.r[2]=0;g->cpu.r[3]=4096;g->cpu.r[4]=3;
    CHECK(ctr_guest_svc(g,1)==CTR_GUEST_READY && !g->cpu.r[0]);
    uint64_t generation=g->memory.generation,code=g->memory.code_generation;size_t maps=g->memory.count;
    CHECK(ctr_modules_load(g,0x08002000,0x300000,4096,0x08010000,8,0x08010080,4,true,false)==CTR_MODULE_FORMAT);
    CHECK(g->memory.generation==generation && g->memory.code_generation==code && g->memory.count==maps);
    CHECK(word(g,0x08010000)==0 && word(g,0x08010080)==0 && !g->allocations[0].pins);
    ctr_guest_close(g);free(g);free(bad);
}
int main(void) {
    parity();rejection_and_close();malformed_relocation();
    printf("{\"ok\":true,\"assertions\":%u,\"interpreter_aot_parity\":true,\"cro_crs_load_link_call_unload\":true,\"console_verified\":false}\n",checks);
    return 0;
}
