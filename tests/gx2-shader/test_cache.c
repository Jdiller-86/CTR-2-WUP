#include "ctr_gx2_cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned assertions,allocations,deallocations;
static size_t allocation_size;
static bool allocation_failure;
#define CHECK(x) do { ++assertions;if(!(x)) { fprintf(stderr,"cache line %u: %s\n",__LINE__,#x);exit(1); } } while(0)
/* Only the cache translation unit uses these wrappers. Real encoder scratch
 * allocation is independent, so no-allocation store/hit checks are meaningful. */
void *test_cache_calloc(size_t count,size_t size) {
    ++allocations;allocation_size=count*size;return allocation_failure?NULL:calloc(count,size);
}
void test_cache_free(void *p) { if(p)++deallocations;free(p); }

static ctr_shader_program program,other,original_program;
static ctr_gx2_shader compiled,cached,previous;
static uint32_t maps[7],other_maps[7];
static uint8_t key[CTR_GX2_CACHE_VERTEX_KEY_MAX],other_key[CTR_GX2_CACHE_VERTEX_KEY_MAX];
static uint8_t saved_key[CTR_GX2_CACHE_VERTEX_KEY_MAX],pixel_payload[CTR_GX2_CACHE_PIXEL_PAYLOAD_MAX];
static uint8_t owner_snapshot[CTR_GX2_CACHE_MAX_BYTES];
static uint32_t registers[0x300];
static uint8_t defined[0x300];
static ctr_tev_program tev,other_tev;
static uint32_t mov(unsigned source,unsigned dest) {
    return UINT32_C(0x4c000000)|(source<<12)|(dest<<21);
}
static void initialize(bool uniforms) {
    memset(&program,0,sizeof(program));memset(maps,0,sizeof(maps));
    program.code[0]=mov(0,0);program.code[1]=mov(uniforms?127u:1u,1);
    program.code[2]=UINT32_C(0x88000000);
    program.code_defined[0]=program.code_defined[1]=program.code_defined[2]=program.descriptor_defined[0]=1;
    program.descriptors[0]=UINT32_C(0x36f);
    maps[0]=UINT32_C(0x03020100);maps[1]=UINT32_C(0x0b0a0908);
    program.uniform_defined[95]=15;
    for(unsigned lane=0;lane<4;++lane)program.uniforms[95][lane]=UINT32_C(0x3f800000);
}
static ctr_gx2_cache *create(void) {
    unsigned before=allocations;ctr_gx2_cache *cache=ctr_gx2_cache_create();
    CHECK(cache && allocations==before+1u && allocation_size<=CTR_GX2_CACHE_MAX_BYTES);
    return cache;
}
static ctr_gx2_cache_statistics statistics(ctr_gx2_cache *cache) {
    ctr_gx2_cache_statistics result;memset(&result,0xa5,sizeof(result));
    ctr_gx2_cache_statistics_get(cache,&result);return result;
}
static bool vertex_key(size_t *size) {
    return ctr_gx2_cache_vertex_key(&program,3,maps,2,key,size);
}
static void cold_warm_and_uniforms(void) {
    initialize(true);size_t size=0,returned=123;ctr_gx2_cache *cache=create();
    CHECK(vertex_key(&size) && size<sizeof(key));
    memset(&cached,0xa5,sizeof(cached));previous=cached;
    CHECK(!ctr_gx2_cache_lookup(cache,CTR_GX2_CACHE_VERTEX,key,size,&cached,sizeof(cached),&returned));
    CHECK(returned==123 && !memcmp(&cached,&previous,sizeof(cached)));
    CHECK(ctr_gx2_shader_compile(&program,3,maps,2,&compiled)==CTR_GX2_SHADER_OK);
    unsigned before=allocations;
    CHECK(ctr_gx2_cache_store(cache,CTR_GX2_CACHE_VERTEX,key,size,&compiled,sizeof(compiled)));
    other=program;memcpy(other_maps,maps,sizeof(maps));size_t other_size=0;
    CHECK(ctr_gx2_cache_vertex_key(&other,3,other_maps,2,other_key,&other_size));
    CHECK(size==other_size && !memcmp(key,other_key,size));
    CHECK(ctr_gx2_cache_lookup(cache,CTR_GX2_CACHE_VERTEX,other_key,other_size,&cached,sizeof(cached),&returned));
    CHECK(returned==sizeof(compiled) && !memcmp(&cached,&compiled,sizeof(cached)) && allocations==before);
    /* Dynamic constants and their definition flags never change program bytes.
     * A warm record must nevertheless report the same current numeric error. */
    program.uniforms[95][0]=UINT32_C(0x40000000);program.uniforms[2][1]=1;program.uniform_defined[2]=0;
    CHECK(vertex_key(&other_size) && other_size==size && !memcmp(key,other_key,size));
    CHECK(ctr_gx2_cache_lookup(cache,CTR_GX2_CACHE_VERTEX,key,size,&cached,sizeof(cached),&returned));
    CHECK(ctr_gx2_shader_validate_uniforms(&program,&cached)==CTR_GX2_SHADER_OK);
    CHECK(ctr_gx2_shader_compile(&program,3,maps,2,&previous)==CTR_GX2_SHADER_OK);
    CHECK(!memcmp(&previous,&compiled,sizeof(previous)));
    program.uniform_defined[95]=7;
    CHECK(vertex_key(&other_size) && !memcmp(key,other_key,size));
    CHECK(ctr_gx2_shader_validate_uniforms(&program,&cached)==CTR_GX2_SHADER_UNDEFINED);
    CHECK(ctr_gx2_shader_compile(&program,3,maps,2,&previous)==CTR_GX2_SHADER_UNDEFINED);
    program.uniform_defined[95]=15;program.uniforms[95][3]=1;
    CHECK(vertex_key(&other_size) && !memcmp(key,other_key,size));
    CHECK(ctr_gx2_shader_validate_uniforms(&program,&cached)==CTR_GX2_SHADER_NUMERIC);
    CHECK(ctr_gx2_shader_compile(&program,3,maps,2,&previous)==CTR_GX2_SHADER_NUMERIC);
    ctr_gx2_cache_statistics stats=statistics(cache);
    CHECK(stats.lookups[0]==3 && stats.hits[0]==2 && stats.misses[0]==1 && stats.stores[0]==1);
    CHECK(stats.bytes==size+sizeof(compiled) && stats.peak_bytes==stats.bytes && !stats.saturated);
    /* Both input buffers can disappear/change after store: cache records own
     * all bytes and never retain the source program/key object's address. */
    previous=compiled;memset(&compiled,0,sizeof(compiled));memset(key,0,sizeof(key));
    CHECK(ctr_gx2_cache_lookup(cache,CTR_GX2_CACHE_VERTEX,other_key,size,&cached,sizeof(cached),&returned));
    CHECK(!memcmp(&cached,&previous,sizeof(cached)) && allocations==before);
    ctr_gx2_cache_destroy(cache);
}
static void key_changes(void) {
    initialize(false);size_t size=0,next_size=0;CHECK(vertex_key(&size));memcpy(saved_key,key,sizeof(key));
    CHECK(key[10]==1 && !key[11] && !key[12] && !key[13]); /* Schema LE32. */
    CHECK(key[size-8u]==0 && key[size-7u]==1 && key[size-6u]==2 && key[size-5u]==3);
    CHECK(key[size-4u]==8 && key[size-3u]==9 && key[size-2u]==10 && key[size-1u]==11);
#define DIFFERENT() do { CHECK(vertex_key(&next_size));CHECK(next_size!=size || memcmp(key,saved_key,size)); } while(0)
    program.code[511]=1;DIFFERENT();program.code[511]=0;
    program.code_defined[511]=1;DIFFERENT();program.code_defined[511]=0;
    program.descriptors[127]=1;DIFFERENT();program.descriptors[127]=0;
    program.descriptor_defined[127]=1;DIFFERENT();program.descriptor_defined[127]=0;
    program.entry=1;DIFFERENT();program.entry=0;
    maps[0]^=1;DIFFERENT();maps[0]^=1;
    CHECK(ctr_gx2_cache_vertex_key(&program,5,maps,2,key,&next_size));CHECK(memcmp(key,saved_key,size));
    CHECK(ctr_gx2_cache_vertex_key(&program,1,maps,1,key,&next_size));CHECK(next_size!=size);
    maps[6]=UINT32_MAX;CHECK(vertex_key(&next_size) && next_size==size && !memcmp(key,saved_key,size));
    program.uniforms[0][0]=1;program.uniform_defined[0]=255;
    CHECK(vertex_key(&next_size) && !memcmp(key,saved_key,size));
#undef DIFFERENT
    /* Rejected compiler input cannot replace the last valid record. */
    initialize(false);CHECK(vertex_key(&size));CHECK(ctr_gx2_shader_compile(&program,3,maps,2,&compiled)==CTR_GX2_SHADER_OK);
    ctr_gx2_cache *cache=create();CHECK(ctr_gx2_cache_store(cache,CTR_GX2_CACHE_VERTEX,key,size,&compiled,sizeof(compiled)));
    program.code[0]=UINT32_MAX;CHECK(ctr_gx2_shader_compile(&program,3,maps,2,&previous)==CTR_GX2_SHADER_UNSUPPORTED);
    CHECK(vertex_key(&next_size));size_t returned=71;
    CHECK(!ctr_gx2_cache_lookup(cache,CTR_GX2_CACHE_VERTEX,key,next_size,&cached,sizeof(cached),&returned) && returned==71);
    initialize(false);CHECK(vertex_key(&size));
    CHECK(ctr_gx2_cache_lookup(cache,CTR_GX2_CACHE_VERTEX,key,size,&cached,sizeof(cached),&returned));
    CHECK(!memcmp(&cached,&compiled,sizeof(cached)));ctr_gx2_cache_destroy(cache);
}
static void pixel_keys(void) {
    memset(registers,0,sizeof(registers));memset(defined,0,sizeof(defined));
    for(unsigned i=0;i<CTR_TEV_STAGES;++i)for(unsigned field=0;field<5;++field)defined[ctr_tev_registers[i]+field]=15;
    defined[0xe0]=defined[0xfd]=15;
    CHECK(ctr_tev_decode(registers,defined,false,&tev));size_t size=0,next_size=0;
    CHECK(ctr_gx2_cache_pixel_key(&tev,false,false,key,&size));memcpy(saved_key,key,size);
    other_tev=tev;for(unsigned i=0;i<CTR_TEV_STAGES;++i)for(unsigned c=0;c<4;++c)other_tev.stages[i].constant[c]=(uint8_t)(i+c+1u);
    CHECK(ctr_gx2_cache_pixel_key(&other_tev,false,false,key,&next_size) && next_size==size && !memcmp(key,saved_key,size));
    CHECK(ctr_gx2_cache_pixel_key(&tev,true,false,key,&next_size) && memcmp(key,saved_key,size));
    CHECK(ctr_gx2_cache_pixel_key(&tev,false,true,key,&next_size) && memcmp(key,saved_key,size));
    tev.buffer_color[0]=1;CHECK(ctr_gx2_cache_pixel_key(&tev,false,false,key,&next_size) && memcmp(key,saved_key,size));
    memcpy(saved_key,key,size);tev.buffer_color[0]=2;
    CHECK(ctr_gx2_cache_pixel_key(&tev,false,false,key,&next_size) && !memcmp(key,saved_key,size));
#define PIXEL_DIFFERENT() do { CHECK(ctr_gx2_cache_pixel_key(&other_tev,false,false,key,&next_size));CHECK(next_size!=size || memcmp(key,saved_key,size)); } while(0)
    other_tev=tev;other_tev.buffer_update[0]=1;PIXEL_DIFFERENT();
    other_tev=tev;other_tev.buffer_update[1]=1;PIXEL_DIFFERENT();
    for(unsigned stage=0;stage<CTR_TEV_STAGES;++stage)for(unsigned side=0;side<2;++side) {
        for(unsigned arg=0;arg<3;++arg) {
            other_tev=tev;other_tev.stages[stage].source[side][arg]=14;PIXEL_DIFFERENT();
            other_tev=tev;other_tev.stages[stage].operand[side][arg]=1;PIXEL_DIFFERENT();
        }
        other_tev=tev;other_tev.stages[stage].operation[side]=1;PIXEL_DIFFERENT();
        other_tev=tev;other_tev.stages[stage].scale[side]=1;PIXEL_DIFFERENT();
    }
#undef PIXEL_DIFFERENT
    ctr_gx2_cache *cache=create();memset(pixel_payload,0x36,sizeof(pixel_payload));
    CHECK(ctr_gx2_cache_store(cache,CTR_GX2_CACHE_PIXEL,saved_key,size,pixel_payload,sizeof(pixel_payload)));
    CHECK(ctr_gx2_cache_pixel_key(&tev,false,false,key,&next_size));size_t returned=0;
    CHECK(ctr_gx2_cache_lookup(cache,CTR_GX2_CACHE_PIXEL,key,next_size,&cached,sizeof(cached),&returned));
    CHECK(returned==sizeof(pixel_payload) && !memcmp(&cached,pixel_payload,sizeof(pixel_payload)));
    ctr_gx2_cache_destroy(cache);
}
static void fifo_and_bounds(void) {
    initialize(false);CHECK(ctr_gx2_shader_compile(&program,3,maps,2,&compiled)==CTR_GX2_SHADER_OK);
    ctr_gx2_cache *cache=create();unsigned before=allocations;size_t returned=0;
    for(uint8_t id=0;id<4;++id)CHECK(ctr_gx2_cache_store(cache,CTR_GX2_CACHE_VERTEX,&id,1,&compiled,sizeof(compiled)));
    uint8_t id=0;CHECK(ctr_gx2_cache_lookup(cache,CTR_GX2_CACHE_VERTEX,&id,1,&cached,sizeof(cached),&returned));
    id=4;CHECK(ctr_gx2_cache_store(cache,CTR_GX2_CACHE_VERTEX,&id,1,&compiled,sizeof(compiled)));
    id=0;CHECK(!ctr_gx2_cache_lookup(cache,CTR_GX2_CACHE_VERTEX,&id,1,&cached,sizeof(cached),&returned));
    id=1;CHECK(ctr_gx2_cache_store(cache,CTR_GX2_CACHE_VERTEX,&id,1,&compiled,sizeof(compiled)));
    id=5;CHECK(ctr_gx2_cache_store(cache,CTR_GX2_CACHE_VERTEX,&id,1,&compiled,sizeof(compiled)));
    id=1;CHECK(!ctr_gx2_cache_lookup(cache,CTR_GX2_CACHE_VERTEX,&id,1,&cached,sizeof(cached),&returned));
    for(id=0;id<9;++id)CHECK(ctr_gx2_cache_store(cache,CTR_GX2_CACHE_PIXEL,&id,1,pixel_payload,sizeof(pixel_payload)));
    id=0;CHECK(!ctr_gx2_cache_lookup(cache,CTR_GX2_CACHE_PIXEL,&id,1,&cached,sizeof(cached),&returned));
    id=1;CHECK(ctr_gx2_cache_lookup(cache,CTR_GX2_CACHE_PIXEL,&id,1,&cached,sizeof(cached),&returned));
    ctr_gx2_cache_statistics stats=statistics(cache);
    CHECK(stats.evictions[0]==2 && stats.evictions[1]==1 && allocations==before);
    CHECK(stats.bytes==4u*(1u+sizeof(compiled))+8u*(1u+sizeof(pixel_payload)));
    ctr_gx2_cache_destroy(cache);cache=create();
    /* Exercise every byte of both maximum key/payload profiles. */
    memset(key,0x41,sizeof(key));memset(other_key,0x41,sizeof(other_key));
    for(id=0;id<4;++id) { key[sizeof(key)-1u]=id;CHECK(ctr_gx2_cache_store(cache,CTR_GX2_CACHE_VERTEX,key,sizeof(key),&compiled,sizeof(compiled))); }
    for(id=0;id<8;++id) { key[255]=id;CHECK(ctr_gx2_cache_store(cache,CTR_GX2_CACHE_PIXEL,key,256,pixel_payload,sizeof(pixel_payload))); }
    stats=statistics(cache);
    CHECK(stats.bytes==4u*(sizeof(key)+sizeof(compiled))+8u*(256u+sizeof(pixel_payload)));
    CHECK(stats.bytes<=allocation_size && stats.peak_bytes==stats.bytes && allocation_size<=CTR_GX2_CACHE_MAX_BYTES);
    key[sizeof(key)-1u]=0;
    CHECK(!ctr_gx2_cache_store(cache,CTR_GX2_CACHE_VERTEX,key,sizeof(key)+1u,&compiled,sizeof(compiled)));
    CHECK(!ctr_gx2_cache_store(cache,CTR_GX2_CACHE_VERTEX,key,sizeof(key),&compiled,sizeof(compiled)+1u));
    key[255]=0x41;
    CHECK(ctr_gx2_cache_lookup(cache,CTR_GX2_CACHE_VERTEX,key,sizeof(key),&cached,sizeof(cached),&returned));
    CHECK(!memcmp(&cached,&compiled,sizeof(cached)));
    ctr_gx2_cache_statistics after=statistics(cache);
    CHECK(after.bytes==stats.bytes && after.evictions[0]==0 && after.bypasses[0]==2);
    ctr_gx2_cache_destroy(cache);
}
static void immutable_failures(void) {
    initialize(false);size_t size=0;CHECK(vertex_key(&size));ctr_gx2_cache *cache=create();
    CHECK(ctr_gx2_shader_compile(&program,3,maps,2,&compiled)==CTR_GX2_SHADER_OK);
    CHECK(ctr_gx2_cache_store(cache,CTR_GX2_CACHE_VERTEX,key,size,&compiled,sizeof(compiled)));
    memset(&cached,0xa5,sizeof(cached));previous=cached;size_t returned=123;
    CHECK(!ctr_gx2_cache_lookup(cache,CTR_GX2_CACHE_VERTEX,key,size,&cached,sizeof(cached)-1u,&returned));
    CHECK(returned==123 && !memcmp(&cached,&previous,sizeof(cached)));
    CHECK(!ctr_gx2_cache_lookup(cache,(ctr_gx2_cache_kind)2,key,size,&cached,sizeof(cached),&returned));
    CHECK(!ctr_gx2_cache_lookup(cache,CTR_GX2_CACHE_VERTEX,key,size,&cached,sizeof(cached),(size_t *)&cached));
    CHECK(!ctr_gx2_cache_lookup(cache,CTR_GX2_CACHE_VERTEX,key,size,key,sizeof(key),&returned));
    CHECK(!ctr_gx2_cache_lookup(cache,CTR_GX2_CACHE_VERTEX,(void *)(UINTPTR_MAX-1u),size,&cached,sizeof(cached),&returned));
    CHECK(!ctr_gx2_cache_lookup(cache,CTR_GX2_CACHE_VERTEX,key,size,&cached,SIZE_MAX,&returned));
    CHECK(!ctr_gx2_cache_lookup(NULL,CTR_GX2_CACHE_VERTEX,key,size,&cached,sizeof(cached),&returned));
    CHECK(!memcmp(&cached,&previous,sizeof(cached)) && returned==123);
    memcpy(owner_snapshot,cache,allocation_size);
    CHECK(!ctr_gx2_cache_lookup(cache,CTR_GX2_CACHE_VERTEX,key,size,cache,allocation_size,&returned));
    CHECK(!memcmp(owner_snapshot,cache,allocation_size) && returned==123);
    CHECK(!ctr_gx2_cache_lookup(cache,CTR_GX2_CACHE_VERTEX,key,size,cache,SIZE_MAX,&returned));
    CHECK(!memcmp(owner_snapshot,cache,allocation_size) && returned==123);
    CHECK(!ctr_gx2_cache_lookup(cache,CTR_GX2_CACHE_VERTEX,key,size,&cached,sizeof(cached),(size_t *)cache));
    CHECK(!memcmp(owner_snapshot,cache,allocation_size) && !memcmp(&cached,&previous,sizeof(cached)));
    CHECK(!ctr_gx2_cache_store(cache,CTR_GX2_CACHE_VERTEX,key,0,&compiled,sizeof(compiled)));
    CHECK(!ctr_gx2_cache_store(cache,CTR_GX2_CACHE_PIXEL,key,257,pixel_payload,sizeof(pixel_payload)));
    CHECK(!ctr_gx2_cache_store(cache,CTR_GX2_CACHE_PIXEL,key,1,pixel_payload,sizeof(pixel_payload)+1u));
    CHECK(!ctr_gx2_cache_store(cache,CTR_GX2_CACHE_VERTEX,key,1,NULL,1));
    CHECK(!ctr_gx2_cache_store(cache,CTR_GX2_CACHE_VERTEX,key,1,&compiled,0));
    CHECK(ctr_gx2_cache_lookup(cache,CTR_GX2_CACHE_VERTEX,key,size,&cached,sizeof(cached),&returned));
    CHECK(!memcmp(&cached,&compiled,sizeof(cached)));
    ctr_gx2_cache_statistics stats=statistics(cache),untouched=stats;
    ctr_gx2_cache_statistics_get(NULL,&stats);CHECK(!memcmp(&stats,&untouched,sizeof(stats)));
    memset(key,0xa5,sizeof(key));memcpy(saved_key,key,sizeof(key));returned=123;original_program=program;
    CHECK(!ctr_gx2_cache_vertex_key(&program,3,maps,8,key,&returned));
    CHECK(!ctr_gx2_cache_vertex_key(&program,3,maps,1,key,&returned));
    CHECK(!ctr_gx2_cache_vertex_key(&program,0,maps,0,key,&returned));
    CHECK(!ctr_gx2_cache_vertex_key(NULL,3,maps,2,key,&returned));
    CHECK(!ctr_gx2_cache_vertex_key((void *)(UINTPTR_MAX-3u),3,maps,2,key,&returned));
    CHECK(!ctr_gx2_cache_vertex_key(&program,3,(void *)(UINTPTR_MAX-3u),2,key,&returned));
    CHECK(!ctr_gx2_cache_vertex_key(&program,3,maps,2,(uint8_t *)&program,&returned));
    CHECK(!ctr_gx2_cache_vertex_key(&program,3,maps,2,key,(size_t *)&program));
    CHECK(!memcmp(&program,&original_program,sizeof(program)) && !memcmp(key,saved_key,sizeof(key)) && returned==123);
    CHECK(!ctr_gx2_cache_pixel_key(NULL,false,false,key,&returned));
    CHECK(!ctr_gx2_cache_pixel_key((void *)(UINTPTR_MAX-1u),false,false,key,&returned));
    union { ctr_tev_program program;uint8_t bytes[CTR_GX2_CACHE_PIXEL_KEY_MAX]; } aliased;
    memset(&aliased,0,sizeof(aliased));aliased.program=tev;
    CHECK(!ctr_gx2_cache_pixel_key(&aliased.program,false,false,aliased.bytes,&returned));
    CHECK(!memcmp(&aliased.program,&tev,sizeof(tev)) && !memcmp(key,saved_key,sizeof(key)) && returned==123);
    ctr_gx2_cache_destroy(cache);
}
int main(void) {
    allocation_failure=true;CHECK(!ctr_gx2_cache_create());allocation_failure=false;
    cold_warm_and_uniforms();key_changes();pixel_keys();fifo_and_bounds();immutable_failures();
    ctr_gx2_cache_destroy(NULL);CHECK(deallocations+1u==allocations);
    printf("gx2 cache: %u assertions; bounded owner %zu bytes\n",assertions,allocation_size);return 0;
}
