#include "ctr_resource_cache.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t *bytes;
    uint32_t address,size;
    unsigned permissions,pins;
    ctr_resource_role role;
    ctr_memory_resource_key key;
} resource_record;
struct ctr_resource_cache {
    const ctr_memory *memory;
    resource_record records[CTR_RESOURCE_CACHE_SLOTS];
    unsigned cursor;
    ctr_resource_cache_statistics statistics;
};
static bool overlap(const void *a,size_t a_size,const void *b,size_t b_size) {
    uintptr_t first=(uintptr_t)a,second=(uintptr_t)b;
    if(a_size>UINTPTR_MAX-first || b_size>UINTPTR_MAX-second)return true;
    return first<second+b_size && second<first+a_size;
}
static bool output_alias(const ctr_resource_cache *cache,const void *output,size_t size) {
    if(!output || overlap(output,size,cache,sizeof(*cache)) ||
       overlap(output,size,cache->memory,sizeof(*cache->memory)))return true;
    for(unsigned i=0;i<CTR_RESOURCE_CACHE_SLOTS;++i) {
        const resource_record *record=&cache->records[i];
        if(record->bytes && overlap(output,size,record->bytes,record->size))return true;
    }
    for(size_t i=0;i<cache->memory->count;++i) {
        const ctr_mapping *map=&cache->memory->maps[i];
        if(overlap(output,size,map->storage,map->size))return true;
    }
    return false;
}
static void increment(ctr_resource_cache *cache,uint64_t *counter) {
    if(*counter<UINT64_MAX)++*counter;
    if(*counter==UINT64_MAX)cache->statistics.saturated=true;
}
static bool equal_backing(const ctr_memory *memory,const resource_record *record) {
    uint64_t cursor=record->address,end=cursor+record->size;
    size_t offset=0;
    while(cursor<end) {
        const ctr_mapping *found=NULL;
        for(size_t i=0;i<memory->count;++i) {
            const ctr_mapping *map=&memory->maps[i];
            if(cursor>=map->base && cursor<(uint64_t)map->base+map->size) { found=map;break; }
        }
        /* acquire already checked every byte, but comparison still fails
         * closed if a caller breaks the stable-memory contract. */
        if(!found)return false;
        uint64_t stop=(uint64_t)found->base+found->size;
        size_t count=(size_t)((stop<end?stop:end)-cursor);
        if(memcmp(record->bytes+offset,found->storage+(size_t)(cursor-found->base),count))return false;
        cursor+=count;offset+=count;
    }
    return true;
}
static void evict(ctr_resource_cache *cache,unsigned slot) {
    resource_record *record=&cache->records[slot];
    if(record->bytes) {
        cache->statistics.bytes-=record->size;
        free(record->bytes);memset(record,0,sizeof(*record));
        increment(cache,&cache->statistics.evictions);
    }
}
ctr_resource_cache *ctr_resource_cache_create(const ctr_memory *memory) {
    if(!memory)return NULL;
    ctr_resource_cache *cache=calloc(1,sizeof(*cache));
    if(cache)cache->memory=memory;
    return cache;
}
ctr_mem_result ctr_resource_cache_acquire(ctr_resource_cache *cache,ctr_resource_role role,
                                          uint32_t address,uint32_t size,unsigned permissions,
                                          ctr_resource_lease *lease) {
    if(!cache || output_alias(cache,lease,sizeof(*lease)) ||
       (uintptr_t)lease%_Alignof(ctr_resource_lease))return CTR_MEM_OVERLAP;
    if(lease->bytes || lease->size || lease->owner || lease->slot || lease->temporary)
        return CTR_MEM_OVERLAP;
    memset(lease,0,sizeof(*lease));
    if((unsigned)role>=CTR_RESOURCE_ROLE_COUNT || !size)return CTR_MEM_OVERFLOW;
    increment(cache,&cache->statistics.lookups[role]);
    ctr_mem_result result=ctr_memory_check_range(cache->memory,address,size,permissions);
    if(result==CTR_MEM_OK && !(permissions&CTR_MEM_READ))result=CTR_MEM_PERMISSION;
    if(result!=CTR_MEM_OK) { increment(cache,&cache->statistics.failures[role]);return result; }
    if(cache->statistics.pins==UINT64_MAX) {
        increment(cache,&cache->statistics.failures[role]);return CTR_MEM_CAPACITY;
    }
    ctr_memory_resource_key key={0};
    bool keyed=ctr_memory_resource_key_capture(cache->memory,address,size,permissions,&key);
    bool invalidated=false;
    for(unsigned i=0;i<CTR_RESOURCE_CACHE_SLOTS;++i) {
        resource_record *record=&cache->records[i];
        if(!record->bytes || record->role!=role || record->address!=address ||
           record->size!=size || record->permissions!=permissions)continue;
        if(keyed && ctr_memory_resource_key_current(cache->memory,&record->key) &&
           equal_backing(cache->memory,record) && record->pins<UINT_MAX) {
            ++record->pins;++cache->statistics.pins;increment(cache,&cache->statistics.hits[role]);
            *lease=(ctr_resource_lease){record->bytes,size,cache,i,NULL};return CTR_MEM_OK;
        }
        invalidated=true;
    }
    increment(cache,&cache->statistics.misses[role]);
    if(invalidated)increment(cache,&cache->statistics.invalidations[role]);
    uint8_t *snapshot=malloc(size);
    if(!snapshot) { increment(cache,&cache->statistics.failures[role]);return CTR_MEM_CAPACITY; }
    result=ctr_memory_read_bytes(cache->memory,address,snapshot,size);
    if(result!=CTR_MEM_OK) {
        free(snapshot);increment(cache,&cache->statistics.failures[role]);return result;
    }
    /* Allocate and read before eviction. Failed reads retain every old entry.
     * Pinned records cannot be evicted by another unit/resource acquisition. */
    unsigned slot=CTR_RESOURCE_CACHE_SLOTS;
    size_t evictable=0;
    if(keyed && size<=CTR_RESOURCE_CACHE_MAX_BYTES) {
        unsigned empty=CTR_RESOURCE_CACHE_SLOTS;
        for(unsigned n=0;n<CTR_RESOURCE_CACHE_SLOTS;++n) {
            unsigned i=(cache->cursor+n)%CTR_RESOURCE_CACHE_SLOTS;
            resource_record *record=&cache->records[i];
            if(!record->pins) {
                evictable+=record->size;
                if(slot==CTR_RESOURCE_CACHE_SLOTS)slot=i;
                if(!record->bytes && empty==CTR_RESOURCE_CACHE_SLOTS)empty=i;
            }
        }
        if(empty!=CTR_RESOURCE_CACHE_SLOTS)slot=empty;
        if(size>CTR_RESOURCE_CACHE_MAX_BYTES-(cache->statistics.bytes-evictable))
            slot=CTR_RESOURCE_CACHE_SLOTS;
    }
    if(slot==CTR_RESOURCE_CACHE_SLOTS) {
        ++cache->statistics.pins;increment(cache,&cache->statistics.bypasses[role]);
        *lease=(ctr_resource_lease){snapshot,size,cache,CTR_RESOURCE_CACHE_SLOTS,snapshot};return CTR_MEM_OK;
    }
    /* Prefer empty slots; otherwise bounded round-robin eviction stays
     * independent of diagnostic counter saturation. */
    evict(cache,slot);
    for(unsigned n=0;size>CTR_RESOURCE_CACHE_MAX_BYTES-cache->statistics.bytes && n<CTR_RESOURCE_CACHE_SLOTS;++n) {
        unsigned i=(cache->cursor+n)%CTR_RESOURCE_CACHE_SLOTS;
        if(!cache->records[i].pins)evict(cache,i);
    }
    resource_record *record=&cache->records[slot];
    *record=(resource_record){snapshot,address,size,permissions,1,role,key};
    cache->cursor=(slot+1u)%CTR_RESOURCE_CACHE_SLOTS;
    cache->statistics.bytes+=size;
    if(cache->statistics.bytes>cache->statistics.peak_bytes)cache->statistics.peak_bytes=cache->statistics.bytes;
    ++cache->statistics.pins;
    *lease=(ctr_resource_lease){snapshot,size,cache,slot,NULL};return CTR_MEM_OK;
}
void ctr_resource_cache_release(ctr_resource_lease *lease) {
    if(!lease || (uintptr_t)lease%_Alignof(ctr_resource_lease) || !lease->owner)return;
    ctr_resource_cache *cache=lease->owner;
    if(!cache->statistics.pins)return;
    if(lease->temporary) {
        if(lease->slot!=CTR_RESOURCE_CACHE_SLOTS || lease->bytes!=lease->temporary)return;
        free(lease->temporary);
    } else {
        if(lease->slot>=CTR_RESOURCE_CACHE_SLOTS)return;
        resource_record *record=&cache->records[lease->slot];
        if(record->bytes!=lease->bytes || record->size!=lease->size || !record->pins)return;
        --record->pins;
    }
    --cache->statistics.pins;memset(lease,0,sizeof(*lease));
}
bool ctr_resource_cache_clear(ctr_resource_cache *cache) {
    if(!cache || cache->statistics.pins)return false;
    for(unsigned i=0;i<CTR_RESOURCE_CACHE_SLOTS;++i) {
        free(cache->records[i].bytes);memset(&cache->records[i],0,sizeof(cache->records[i]));
    }
    cache->statistics.bytes=0;cache->cursor=0;return true;
}
bool ctr_resource_cache_destroy(ctr_resource_cache *cache) {
    if(!cache)return true;
    if(!ctr_resource_cache_clear(cache))return false;
    free(cache);return true;
}
void ctr_resource_cache_statistics_get(const ctr_resource_cache *cache,
                                        ctr_resource_cache_statistics *out) {
    if(!cache || output_alias(cache,out,sizeof(*out)) ||
       (uintptr_t)out%_Alignof(ctr_resource_cache_statistics))return;
    *out=cache->statistics;
}
