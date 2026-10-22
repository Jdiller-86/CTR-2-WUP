#include "ctr_gx2_cache.h"
#include <stdlib.h>
#include <string.h>

#ifndef CTR_SOURCE_COMMIT
/* There is no persistent/shared cache in builds without a source namespace. */
#define CTR_SOURCE_COMMIT "process-local-development"
#endif
#ifndef CTR_SOURCE_DIRTY
#define CTR_SOURCE_DIRTY 0
#endif

typedef struct { uint32_t key_size,payload_size; } slot_header;
typedef struct {
    slot_header header;
    uint8_t key[CTR_GX2_CACHE_VERTEX_KEY_MAX],payload[sizeof(ctr_gx2_shader)];
} vertex_slot;
typedef struct {
    slot_header header;
    uint8_t key[CTR_GX2_CACHE_PIXEL_KEY_MAX],payload[CTR_GX2_CACHE_PIXEL_PAYLOAD_MAX];
} pixel_slot;
struct ctr_gx2_cache {
    vertex_slot vertex[CTR_GX2_CACHE_VERTEX_SLOTS];
    pixel_slot pixel[CTR_GX2_CACHE_PIXEL_SLOTS];
    unsigned next[2];
    ctr_gx2_cache_statistics statistics;
};
_Static_assert(sizeof(struct ctr_gx2_cache)<=CTR_GX2_CACHE_MAX_BYTES,"bounded CPU program cache");

static bool span(const void *p,size_t n) {
    return p && n<=UINTPTR_MAX-(uintptr_t)p;
}
static bool overlap(const void *a,size_t an,const void *b,size_t bn) {
    return an && bn && (uintptr_t)a<(uintptr_t)b+bn && (uintptr_t)b<(uintptr_t)a+an;
}
static bool kind_valid(ctr_gx2_cache_kind kind) {
    return kind==CTR_GX2_CACHE_VERTEX || kind==CTR_GX2_CACHE_PIXEL;
}
static size_t key_limit(ctr_gx2_cache_kind kind) {
    return kind==CTR_GX2_CACHE_VERTEX?CTR_GX2_CACHE_VERTEX_KEY_MAX:CTR_GX2_CACHE_PIXEL_KEY_MAX;
}
static size_t payload_limit(ctr_gx2_cache_kind kind) {
    return kind==CTR_GX2_CACHE_VERTEX?sizeof(ctr_gx2_shader):CTR_GX2_CACHE_PIXEL_PAYLOAD_MAX;
}
static unsigned slot_count(ctr_gx2_cache_kind kind) {
    return kind==CTR_GX2_CACHE_VERTEX?CTR_GX2_CACHE_VERTEX_SLOTS:CTR_GX2_CACHE_PIXEL_SLOTS;
}
static slot_header *slot(ctr_gx2_cache *cache,ctr_gx2_cache_kind kind,unsigned i,
                          uint8_t **key,uint8_t **payload) {
    if(kind==CTR_GX2_CACHE_VERTEX) {
        *key=cache->vertex[i].key;*payload=cache->vertex[i].payload;return &cache->vertex[i].header;
    }
    *key=cache->pixel[i].key;*payload=cache->pixel[i].payload;return &cache->pixel[i].header;
}
static void increment(ctr_gx2_cache *cache,uint64_t *counter) {
    if(*counter<UINT64_MAX)++*counter;
    if(*counter==UINT64_MAX)cache->statistics.saturated=true;
}
static bool owner(const ctr_gx2_cache *cache) {
    return span(cache,sizeof(*cache)) && !((uintptr_t)cache%_Alignof(ctr_gx2_cache));
}
static bool external(const ctr_gx2_cache *cache,const void *p,size_t size) {
    return span(p,size) && !overlap(cache,sizeof(*cache),p,size);
}
ctr_gx2_cache *ctr_gx2_cache_create(void) { return calloc(1,sizeof(ctr_gx2_cache)); }
void ctr_gx2_cache_destroy(ctr_gx2_cache *cache) { free(cache); }

bool ctr_gx2_cache_lookup(ctr_gx2_cache *cache,ctr_gx2_cache_kind kind,const void *key,
                          size_t key_size,void *payload,size_t capacity,size_t *out_size) {
    if(!owner(cache) || !kind_valid(kind))return false;
    /* An invalid caller may name the counter storage itself as an output.
     * Reject owner aliases before recording even an attempted lookup. */
    if(!external(cache,key,key_size) || !external(cache,payload,capacity) ||
       !external(cache,out_size,sizeof(*out_size)))return false;
    increment(cache,&cache->statistics.lookups[kind]);
    if(!key_size || key_size>key_limit(kind) || !capacity ||
       (uintptr_t)out_size%_Alignof(size_t) || overlap(payload,capacity,key,key_size) ||
       overlap(out_size,sizeof(*out_size),key,key_size) || overlap(out_size,sizeof(*out_size),payload,capacity)) {
        increment(cache,&cache->statistics.bypasses[kind]);return false;
    }
    for(unsigned i=0;i<slot_count(kind);++i) {
        uint8_t *saved_key,*saved_payload;slot_header *record=slot(cache,kind,i,&saved_key,&saved_payload);
        if(record->key_size!=key_size || memcmp(saved_key,key,key_size))continue;
        if(capacity<record->payload_size) {
            increment(cache,&cache->statistics.bypasses[kind]);return false;
        }
        memcpy(payload,saved_payload,record->payload_size);*out_size=record->payload_size;
        increment(cache,&cache->statistics.hits[kind]);return true;
    }
    increment(cache,&cache->statistics.misses[kind]);return false;
}
bool ctr_gx2_cache_store(ctr_gx2_cache *cache,ctr_gx2_cache_kind kind,const void *key,
                         size_t key_size,const void *payload,size_t payload_size) {
    if(!owner(cache) || !kind_valid(kind))return false;
    if(!key_size || key_size>key_limit(kind) || !payload_size || payload_size>payload_limit(kind) ||
       !external(cache,key,key_size) || !external(cache,payload,payload_size)) {
        increment(cache,&cache->statistics.bypasses[kind]);return false;
    }
    unsigned selected=cache->next[kind];bool replacing=false;
    for(unsigned i=0;i<slot_count(kind);++i) {
        uint8_t *saved_key,*saved_payload;slot_header *record=slot(cache,kind,i,&saved_key,&saved_payload);
        if(record->key_size==key_size && !memcmp(saved_key,key,key_size)) { selected=i;replacing=true;break; }
    }
    uint8_t *saved_key,*saved_payload;slot_header *record=slot(cache,kind,selected,&saved_key,&saved_payload);
    size_t old_bytes=(size_t)record->key_size+record->payload_size;
    if(record->key_size && !replacing)increment(cache,&cache->statistics.evictions[kind]);
    memcpy(saved_key,key,key_size);memcpy(saved_payload,payload,payload_size);
    record->key_size=(uint32_t)key_size;record->payload_size=(uint32_t)payload_size;
    if(!replacing)cache->next[kind]=(selected+1u)%slot_count(kind);
    cache->statistics.bytes=cache->statistics.bytes-old_bytes+key_size+payload_size;
    if(cache->statistics.bytes>cache->statistics.peak_bytes)cache->statistics.peak_bytes=cache->statistics.bytes;
    increment(cache,&cache->statistics.stores[kind]);return true;
}
void ctr_gx2_cache_statistics_get(const ctr_gx2_cache *cache,ctr_gx2_cache_statistics *out) {
    if(owner(cache) && external(cache,out,sizeof(*out)) && !((uintptr_t)out%_Alignof(ctr_gx2_cache_statistics)))
        *out=cache->statistics;
}

typedef struct { uint8_t *bytes;size_t used,capacity;bool valid; } key_writer;
static void byte(key_writer *writer,uint8_t value) {
    if(writer->used>=writer->capacity) { writer->valid=false;return; }
    writer->bytes[writer->used++]=value;
}
static void word(key_writer *writer,uint32_t value) {
    for(unsigned i=0;i<4;++i)byte(writer,(uint8_t)(value>>(i*8u)));
}
static void prefix(key_writer *writer,ctr_gx2_cache_kind kind) {
    static const uint8_t tag[]={'C','T','R','2','W','U','P','G','X','2'};
    static const char namespace[]=CTR_SOURCE_COMMIT;
    for(size_t i=0;i<sizeof(tag);++i)byte(writer,tag[i]);
    word(writer,2u); /* Key schema, including explicit texture unit/routing identity. */
    word(writer,2u); /* Original Latte encoder profile with three UV exports. */
    word(writer,1u); /* Exact finite float24/byte-domain numeric profile. */
    word(writer,2u); /* Position/color/Texture0..2, bounded TEV/depth renderer ABI. */
    word(writer,CTR_SOURCE_DIRTY?1u:0u);
    byte(writer,(uint8_t)kind);word(writer,(uint32_t)(sizeof(namespace)-1u));
    for(size_t i=0;i<sizeof(namespace)-1u;++i)byte(writer,(uint8_t)namespace[i]);
}
static bool key_outputs(const void *input,size_t input_size,const void *maps,size_t map_size,
                         uint8_t *key,size_t capacity,size_t *out_size) {
    return span(input,input_size) && span(key,capacity) && span(out_size,sizeof(*out_size)) &&
        !((uintptr_t)out_size%_Alignof(size_t)) &&
        (!map_size || span(maps,map_size)) &&
        !overlap(key,capacity,input,input_size) && !overlap(out_size,sizeof(*out_size),input,input_size) &&
        !overlap(key,capacity,maps,map_size) && !overlap(out_size,sizeof(*out_size),maps,map_size) &&
        !overlap(key,capacity,out_size,sizeof(*out_size));
}
bool ctr_gx2_cache_vertex_key(const ctr_shader_program *program,uint16_t mask,
                              const uint32_t *maps,unsigned count,
                              uint8_t key[CTR_GX2_CACHE_VERTEX_KEY_MAX],size_t *out_size) {
    if(count>7u || !program || !maps || (uintptr_t)program%_Alignof(ctr_shader_program) ||
       (uintptr_t)maps%_Alignof(uint32_t) ||
       !key_outputs(program,sizeof(*program),maps,(size_t)count*sizeof(*maps),key,CTR_GX2_CACHE_VERTEX_KEY_MAX,out_size))return false;
    unsigned bits=0;for(unsigned value=mask;value;value>>=1)bits+=value&1u;
    if(!mask || count!=bits || program->entry>=CTR_SHADER_MAX_CODE)return false;
    uint8_t next[CTR_GX2_CACHE_VERTEX_KEY_MAX];key_writer writer={next,0,sizeof(next),true};
    prefix(&writer,CTR_GX2_CACHE_VERTEX);word(&writer,program->entry);word(&writer,mask);word(&writer,count);
    for(unsigned i=0;i<CTR_SHADER_MAX_CODE;++i) { word(&writer,program->code[i]);byte(&writer,program->code_defined[i]); }
    for(unsigned i=0;i<CTR_SHADER_MAX_DESCRIPTORS;++i) { word(&writer,program->descriptors[i]);byte(&writer,program->descriptor_defined[i]); }
    for(unsigned i=0;i<count;++i)word(&writer,maps[i]);
    if(!writer.valid)return false;
    memcpy(key,next,writer.used);*out_size=writer.used;return true;
}
bool ctr_gx2_cache_pixel_key(const ctr_tev_program *program,unsigned texture_mask,
                             bool texture2_coord1,bool depth,
                             uint8_t key[CTR_GX2_CACHE_PIXEL_KEY_MAX],size_t *out_size) {
    if(texture_mask>7u || !key_outputs(program,sizeof(*program),NULL,0,key,CTR_GX2_CACHE_PIXEL_KEY_MAX,out_size))return false;
    uint8_t next[CTR_GX2_CACHE_PIXEL_KEY_MAX];key_writer writer={next,0,sizeof(next),true};
    prefix(&writer,CTR_GX2_CACHE_PIXEL);byte(&writer,(uint8_t)texture_mask);byte(&writer,texture2_coord1);
    byte(&writer,depth);byte(&writer,ctr_tev_buffered(program));
    byte(&writer,program->buffer_update[0]);byte(&writer,program->buffer_update[1]);
    for(unsigned stage=0;stage<CTR_TEV_STAGES;++stage)for(unsigned side=0;side<2;++side) {
        const ctr_tev_stage *p=&program->stages[stage];
        for(unsigned arg=0;arg<3;++arg) { byte(&writer,p->source[side][arg]);byte(&writer,p->operand[side][arg]); }
        byte(&writer,p->operation[side]);byte(&writer,p->scale[side]);
    }
    if(!writer.valid)return false;
    memcpy(key,next,writer.used);*out_size=writer.used;return true;
}
