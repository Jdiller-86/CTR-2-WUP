#include "ctr_texture_cache.h"
#include <limits.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define DECODER_PROFILE 1u /* Base-level Morton/ETC1 to raw-row RGBA8. */
typedef struct {
    uint8_t *bytes;
    size_t source_size,rgba_size;
    uint32_t hash;
    unsigned format,width,height,profile,pins;
    uint32_t identity;
} texture_entry;
struct ctr_texture_cache {
    texture_entry entries[CTR_TEXTURE_CACHE_SLOTS];
    /* Live allocations are disjoint and sorted by start address. */
    texture_entry *by_address[CTR_TEXTURE_CACHE_SLOTS];
    unsigned next;
    ctr_texture_cache_statistics statistics;
};

static uint32_t allocate_texture_identity(void) {
    static atomic_uint_least32_t sequence=1u;
    uint_least32_t id=atomic_load_explicit(&sequence,memory_order_relaxed);
    while(id<UINT32_MAX) {
        if(atomic_compare_exchange_weak_explicit(&sequence,&id,id+1u,
                                                 memory_order_relaxed,memory_order_relaxed))
            return (uint32_t)id;
    }
    /* Saturation disables the optimization without recycling identities. */
    return 0;
}

static uint32_t hash_source(const uint8_t *bytes,size_t size) {
    uint32_t h=UINT32_C(2166136261)^(uint32_t)size;
    /* The key only filters candidates. Cache hits still compare every byte. */
    size_t words=size/4u,step=(words+15u)/16u;
    for(size_t i=0;i<words;i+=step) {
        uint32_t w;memcpy(&w,bytes+i*4u,sizeof(w));
        h=(h^w)*UINT32_C(16777619);
    }
    return h;
}

static bool span(const void *p,size_t n) {
    return p && n<=UINTPTR_MAX-(uintptr_t)p;
}
static bool overlap(const void *a,size_t an,const void *b,size_t bn) {
    return an && bn && (uintptr_t)a<(uintptr_t)b+bn && (uintptr_t)b<(uintptr_t)a+an;
}
static bool owner(const ctr_texture_cache *cache) {
    return span(cache,sizeof(*cache)) && !((uintptr_t)cache%_Alignof(ctr_texture_cache));
}
static unsigned resident_lower_bound(const ctr_texture_cache *cache,uintptr_t address) {
    unsigned first=0,last=cache->statistics.entries;
    while(first<last) {
        unsigned middle=first+(last-first)/2u;
        if((uintptr_t)cache->by_address[middle]->bytes<address)first=middle+1u;
        else last=middle;
    }
    return first;
}
static bool external(const ctr_texture_cache *cache,const void *p,size_t bytes) {
    if(!span(p,bytes))return false;
    if(!cache)return true;
    if(overlap(cache,sizeof(*cache),p,bytes))return false;
    if(!bytes)return true;
    uintptr_t start=(uintptr_t)p;
    unsigned next=resident_lower_bound(cache,start);
    if(next<cache->statistics.entries && (uintptr_t)cache->by_address[next]->bytes<start+bytes)
        return false;
    if(!next)return true;
    const texture_entry *previous=cache->by_address[next-1u];
    return !overlap(previous->bytes,previous->source_size+previous->rgba_size,p,bytes);
}
static void increment(ctr_texture_cache *cache,uint64_t *counter) {
    if(*counter<UINT64_MAX)++*counter;
    if(*counter==UINT64_MAX)cache->statistics.saturated=true;
}
static void discard(ctr_texture_cache *cache,unsigned index) {
    texture_entry *e=&cache->entries[index];
    if(!e->bytes)return;
    unsigned at=resident_lower_bound(cache,(uintptr_t)e->bytes);
    cache->statistics.bytes-=e->source_size+e->rgba_size;
    --cache->statistics.entries;
    memmove(&cache->by_address[at],&cache->by_address[at+1u],
            (cache->statistics.entries-at)*sizeof(cache->by_address[0]));
    cache->by_address[cache->statistics.entries]=NULL;
    free(e->bytes);memset(e,0,sizeof(*e));
}
ctr_texture_cache *ctr_texture_cache_create(void) { return calloc(1,sizeof(ctr_texture_cache)); }
bool ctr_texture_cache_clear(ctr_texture_cache *cache) {
    if(!owner(cache))return false;
    for(unsigned i=0;i<CTR_TEXTURE_CACHE_SLOTS;++i)if(cache->entries[i].pins)return false;
    for(unsigned i=0;i<CTR_TEXTURE_CACHE_SLOTS;++i)discard(cache,i);
    cache->next=0;return true;
}
bool ctr_texture_cache_destroy(ctr_texture_cache *cache) {
    if(!cache)return true;
    if(!ctr_texture_cache_clear(cache))return false;
    free(cache);return true;
}
ctr_texture_cache_result ctr_texture_cache_acquire(ctr_texture_cache *cache,
    const uint8_t *source,size_t source_bytes,unsigned format,unsigned width,
    unsigned height,ctr_texture_cache_lease *lease) {
    if((cache && !owner(cache)) || !external(cache,lease,sizeof(*lease)) ||
       (uintptr_t)lease%_Alignof(ctr_texture_cache_lease) ||
       !external(cache,source,source_bytes) ||
       overlap(lease,sizeof(*lease),source,source_bytes))return CTR_TEXTURE_CACHE_ARGUMENT;
    if(lease->rgba || lease->owner || lease->owned)return CTR_TEXTURE_CACHE_ARGUMENT;
    if(cache)increment(cache,&cache->statistics.lookups);
    size_t need;
    if(ctr_texture_size(format,width,height,&need)!=CTR_TEXTURE_OK || source_bytes<need) {
        if(cache)increment(cache,&cache->statistics.rejections);
        return CTR_TEXTURE_CACHE_DECODE;
    }
    size_t rgba_size=(size_t)width*height*4u;
    if(!cache) {
        if(ctr_texture_validate(source,need,format,width,height)!=CTR_TEXTURE_OK)
            return CTR_TEXTURE_CACHE_DECODE;
        uint8_t *rgba=malloc(rgba_size);
        if(!rgba)return CTR_TEXTURE_CACHE_NO_MEMORY;
        if(ctr_texture_decode(source,need,format,width,height,rgba,rgba_size)!=CTR_TEXTURE_OK) {
            free(rgba);return CTR_TEXTURE_CACHE_DECODE;
        }
        *lease=(ctr_texture_cache_lease){rgba,NULL,rgba,0,0};return CTR_TEXTURE_CACHE_OK;
    }
    uint32_t source_hash=hash_source(source,need);
    for(unsigned i=0;i<CTR_TEXTURE_CACHE_SLOTS;++i) {
        texture_entry *e=&cache->entries[i];
        if(!e->bytes || e->profile!=DECODER_PROFILE || e->format!=format ||
           e->width!=width || e->height!=height || e->source_size!=need ||
           e->hash!=source_hash || memcmp(e->bytes,source,need))continue;
        if(e->pins==UINT_MAX)return CTR_TEXTURE_CACHE_BUSY;
        ++e->pins;increment(cache,&cache->statistics.hits);
        *lease=(ctr_texture_cache_lease){e->bytes+need,cache,NULL,i,e->identity};return CTR_TEXTURE_CACHE_OK;
    }
    /* A matching entry already passed preflight. Validate new bytes before eviction. */
    if(ctr_texture_validate(source,need,format,width,height)!=CTR_TEXTURE_OK) {
        increment(cache,&cache->statistics.rejections);return CTR_TEXTURE_CACHE_DECODE;
    }
    increment(cache,&cache->statistics.misses);
    size_t required=need+rgba_size,available=CTR_TEXTURE_CACHE_MAX_BYTES-cache->statistics.bytes;
    unsigned selected=CTR_TEXTURE_CACHE_SLOTS,empty=CTR_TEXTURE_CACHE_SLOTS;
    /* First prove enough unpinned capacity exists, so BUSY preserves entries. */
    for(unsigned offset=0;offset<CTR_TEXTURE_CACHE_SLOTS;++offset) {
        unsigned i=(cache->next+offset)%CTR_TEXTURE_CACHE_SLOTS;
        const texture_entry *e=&cache->entries[i];
        if(e->pins)continue;
        if(selected==CTR_TEXTURE_CACHE_SLOTS)selected=i;
        if(!e->bytes && empty==CTR_TEXTURE_CACHE_SLOTS)empty=i;
        available+=e->source_size+e->rgba_size;
    }
    if(selected==CTR_TEXTURE_CACHE_SLOTS || available<required)return CTR_TEXTURE_CACHE_BUSY;
    if(empty<CTR_TEXTURE_CACHE_SLOTS)selected=empty;
    if(cache->entries[selected].bytes) {
        discard(cache,selected);increment(cache,&cache->statistics.evictions);
    }
    for(unsigned offset=0;offset<CTR_TEXTURE_CACHE_SLOTS &&
        cache->statistics.bytes>CTR_TEXTURE_CACHE_MAX_BYTES-required;++offset) {
        unsigned i=(cache->next+offset)%CTR_TEXTURE_CACHE_SLOTS;
        if(!cache->entries[i].bytes || cache->entries[i].pins)continue;
        discard(cache,i);increment(cache,&cache->statistics.evictions);
    }
    uint8_t *bytes=malloc(required);
    if(!bytes)return CTR_TEXTURE_CACHE_NO_MEMORY;
    memcpy(bytes,source,need);
    if(ctr_texture_decode(bytes,need,format,width,height,bytes+need,rgba_size)!=CTR_TEXTURE_OK) {
        free(bytes);return CTR_TEXTURE_CACHE_DECODE;
    }
    uint32_t identity=allocate_texture_identity();
    cache->entries[selected]=(texture_entry){bytes,need,rgba_size,source_hash,format,width,height,DECODER_PROFILE,1,identity};
    unsigned at=resident_lower_bound(cache,(uintptr_t)bytes);
    memmove(&cache->by_address[at+1u],&cache->by_address[at],
            (cache->statistics.entries-at)*sizeof(cache->by_address[0]));
    cache->by_address[at]=&cache->entries[selected];
    cache->statistics.bytes+=required;++cache->statistics.entries;
    if(cache->statistics.bytes>cache->statistics.peak_bytes)cache->statistics.peak_bytes=cache->statistics.bytes;
    increment(cache,&cache->statistics.decodes);
    cache->next=(selected+1u)%CTR_TEXTURE_CACHE_SLOTS;
    *lease=(ctr_texture_cache_lease){bytes+need,cache,NULL,selected,identity};return CTR_TEXTURE_CACHE_OK;
}
void ctr_texture_cache_release(ctr_texture_cache_lease *lease) {
    if(!span(lease,sizeof(*lease)) || (uintptr_t)lease%_Alignof(ctr_texture_cache_lease))return;
    if(lease->owner) {
        ctr_texture_cache *cache=lease->owner;
        if(!owner(cache) || lease->slot>=CTR_TEXTURE_CACHE_SLOTS)return;
        texture_entry *e=&cache->entries[lease->slot];
        if(!e->bytes || lease->rgba!=e->bytes+e->source_size || !e->pins)return;
        --e->pins;
    } else free(lease->owned);
    memset(lease,0,sizeof(*lease));
}
void ctr_texture_cache_statistics_get(const ctr_texture_cache *cache,ctr_texture_cache_statistics *out) {
    if(!owner(cache) || !external(cache,out,sizeof(*out)) ||
       (uintptr_t)out%_Alignof(ctr_texture_cache_statistics))return;
    *out=cache->statistics;out->pins=0;
    for(unsigned i=0;i<CTR_TEXTURE_CACHE_SLOTS;++i)out->pins+=cache->entries[i].pins;
}
