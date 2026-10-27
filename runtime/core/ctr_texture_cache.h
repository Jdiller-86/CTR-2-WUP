#ifndef CTR_TEXTURE_CACHE_H
#define CTR_TEXTURE_CACHE_H
#include "ctr_texture.h"
#include <stdbool.h>

/* Process-local CPU storage; never guest or GX2 pointers. The bound includes
 * retained encoded keys and RGBA payloads, separately from the fixed owner. */
#define CTR_TEXTURE_CACHE_SLOTS 8u
#define CTR_TEXTURE_CACHE_MAX_BYTES (24u*1024u*1024u)
typedef struct ctr_texture_cache ctr_texture_cache;
typedef enum {
    CTR_TEXTURE_CACHE_OK, CTR_TEXTURE_CACHE_ARGUMENT, CTR_TEXTURE_CACHE_DECODE,
    CTR_TEXTURE_CACHE_NO_MEMORY, CTR_TEXTURE_CACHE_BUSY
} ctr_texture_cache_result;
typedef struct {
    const uint8_t *rgba;
    /* Private lease fields. Initialize to zero, do not copy or edit a live
     * lease, and release it exactly once. Borrowed bytes remain read-only. */
    ctr_texture_cache *owner;
    void *owned;
    unsigned slot;
} ctr_texture_cache_lease;
typedef struct {
    uint64_t lookups,hits,misses,decodes,evictions,rejections;
    size_t bytes,peak_bytes;
    unsigned entries;
    uint64_t pins;
    bool saturated;
} ctr_texture_cache_statistics;

ctr_texture_cache *ctr_texture_cache_create(void);
/* Refuse to invalidate outstanding leases. NULL destruction is successful. */
bool ctr_texture_cache_clear(ctr_texture_cache *cache);
bool ctr_texture_cache_destroy(ctr_texture_cache *cache);
/* Exact required source bytes, format, dimensions and decoder profile identify
 * a record; sampler, UV route and address remain current draw state. All inputs
 * must be owned/stable and disjoint from the owner, retained entries and lease.
 * Errors leave lease unchanged. Invalid source/decode leaves entries unchanged;
 * allocation failure can retain preceding safe evictions to honor the byte cap.
 * Empty slots are preferred; unpinned entries evict round-robin. Three largest images fit together;
 * more outstanding pins can return BUSY. A NULL cache creates a one-use owned
 * decode. This API is synchronous and not thread-safe. */
ctr_texture_cache_result ctr_texture_cache_acquire(ctr_texture_cache *cache,
    const uint8_t *source,size_t source_bytes,unsigned format,unsigned width,
    unsigned height,ctr_texture_cache_lease *lease);
void ctr_texture_cache_release(ctr_texture_cache_lease *lease);
/* Invalid/aliased output is unchanged; counters saturate without wrapping. */
void ctr_texture_cache_statistics_get(const ctr_texture_cache *cache,
                                     ctr_texture_cache_statistics *out);
#endif
