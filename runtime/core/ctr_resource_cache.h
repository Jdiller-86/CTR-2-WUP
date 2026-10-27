#ifndef CTR_RESOURCE_CACHE_H
#define CTR_RESOURCE_CACHE_H
#include "ctr_memory.h"

/* CPU-owned immutable guest snapshots. This owner must be destroyed before
 * resetting/freeing its memory owner; it contains no GPU resource or pointer
 * into guest backing. Live mapping/permission checks and exact source-byte
 * comparisons precede reuse even when a resource version remains current. */
#define CTR_RESOURCE_CACHE_SLOTS 40u
#define CTR_RESOURCE_CACHE_MAX_BYTES (12u*1024u*1024u)
typedef enum {
    CTR_RESOURCE_COMMAND, CTR_RESOURCE_VERTEX, CTR_RESOURCE_TEXTURE,
    CTR_RESOURCE_COLOR, CTR_RESOURCE_DEPTH, CTR_RESOURCE_ROLE_COUNT
} ctr_resource_role;
typedef struct ctr_resource_cache ctr_resource_cache;
typedef struct {
    const uint8_t *bytes;
    uint32_t size;
    /* Initialize the complete lease to zero before its first acquisition.
     * Private bookkeeping: do not copy, modify or release a live lease twice. */
    ctr_resource_cache *owner;
    unsigned slot;
    uint8_t *temporary;
} ctr_resource_lease;
typedef struct {
    uint64_t lookups[CTR_RESOURCE_ROLE_COUNT],hits[CTR_RESOURCE_ROLE_COUNT];
    uint64_t misses[CTR_RESOURCE_ROLE_COUNT],invalidations[CTR_RESOURCE_ROLE_COUNT];
    uint64_t bypasses[CTR_RESOURCE_ROLE_COUNT],failures[CTR_RESOURCE_ROLE_COUNT];
    uint64_t evictions,pins;
    size_t bytes,peak_bytes;
    bool saturated;
} ctr_resource_cache_statistics;

ctr_resource_cache *ctr_resource_cache_create(const ctr_memory *memory);
/* Every acquired lease pins its immutable bytes until release. Permissions
 * must include READ. A zero-initialized output stays empty on failed reads,
 * without changing existing records. A live/nonempty lease is rejected intact.
 * CAPACITY reports allocation failure; capacity pressure or unavailable
 * version keys instead use a private one-use snapshot. Outputs
 * cannot alias cache storage, the memory owner or guest backing. Invalid output
 * aliases remain untouched. Caller keeps mapping/backing stable during acquire. */
ctr_mem_result ctr_resource_cache_acquire(ctr_resource_cache *cache,ctr_resource_role role,
                                          uint32_t address,uint32_t size,unsigned permissions,
                                          ctr_resource_lease *lease);
void ctr_resource_cache_release(ctr_resource_lease *lease);
/* Clear retains statistics; neither clear nor destroy succeeds while leases
 * remain. No failed clear/destroy changes the cache. NULL destroy succeeds. */
bool ctr_resource_cache_clear(ctr_resource_cache *cache);
bool ctr_resource_cache_destroy(ctr_resource_cache *cache);
void ctr_resource_cache_statistics_get(const ctr_resource_cache *cache,
                                        ctr_resource_cache_statistics *out);
#endif
