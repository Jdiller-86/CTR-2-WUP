#ifndef CTR_GX2_CACHE_H
#define CTR_GX2_CACHE_H
#include "ctr_gx2_shader.h"
#include "ctr_tev.h"
#include <stddef.h>

/* CPU-owned records only. Each hit is copied into fresh transaction storage;
 * this cache owns no GX2 allocation, pointer into guest state, or disk format. */
#define CTR_GX2_CACHE_VERTEX_SLOTS 4u
#define CTR_GX2_CACHE_PIXEL_SLOTS 8u
#define CTR_GX2_CACHE_VERTEX_KEY_MAX 4096u
#define CTR_GX2_CACHE_PIXEL_KEY_MAX 256u
#define CTR_GX2_CACHE_PIXEL_PAYLOAD_MAX 12288u
#define CTR_GX2_CACHE_MAX_BYTES (384u*1024u)
typedef enum { CTR_GX2_CACHE_VERTEX, CTR_GX2_CACHE_PIXEL } ctr_gx2_cache_kind;
typedef struct ctr_gx2_cache ctr_gx2_cache;
typedef struct {
    uint64_t lookups[2],hits[2],misses[2],stores[2],evictions[2],bypasses[2];
    size_t bytes,peak_bytes; /* Resident key and payload bytes, excluding capacity. */
    bool saturated; /* At least one counter reached UINT64_MAX. */
} ctr_gx2_cache_statistics;

ctr_gx2_cache *ctr_gx2_cache_create(void);
void ctr_gx2_cache_destroy(ctr_gx2_cache *cache);
/* Full-key comparison; misses/errors leave payload and out_size unchanged.
 * Inputs must be stable and cannot alias cache storage or either output.
 * Keys/payloads must be nonempty. A hit needs capacity >= stored payload size. */
bool ctr_gx2_cache_lookup(ctr_gx2_cache *cache,ctr_gx2_cache_kind kind,
                          const void *key,size_t key_size,void *payload,
                          size_t capacity,size_t *out_size);
/* No allocation. Invalid records leave existing entries unchanged. A matching
 * key is replaced in place; distinct keys evict in insertion order per kind. */
bool ctr_gx2_cache_store(ctr_gx2_cache *cache,ctr_gx2_cache_kind kind,
                         const void *key,size_t key_size,const void *payload,
                         size_t payload_size);
/* Invalid/aliased arguments leave output unchanged. */
void ctr_gx2_cache_statistics_get(const ctr_gx2_cache *cache,
                                  ctr_gx2_cache_statistics *out);

/* Versioned byte keys include the build namespace and encoder/numeric/renderer
 * profiles. Unused output-map words and dynamic uniform values/defined flags
 * are excluded. A vertex hit still requires current used-uniform validation.
 * Pixel input must be successfully decoded TEV; stage/initial buffer colors
 * are uniforms, except the computed buffered-program choice is part of its key.
 * Pixel keys include the complete unit mask (0..7), Texture2's UV1 routing and
 * depth profile; sampler values remain dynamic bindings, outside compiled code.
 * Failures, including output/input aliasing, leave key and out_size unchanged. */
bool ctr_gx2_cache_vertex_key(const ctr_shader_program *program,uint16_t output_mask,
                              const uint32_t *output_maps,unsigned output_count,
                              uint8_t key[CTR_GX2_CACHE_VERTEX_KEY_MAX],size_t *out_size);
bool ctr_gx2_cache_pixel_key(const ctr_tev_program *program,unsigned texture_mask,
                             bool texture2_coord1,bool depth,
                             uint8_t key[CTR_GX2_CACHE_PIXEL_KEY_MAX],size_t *out_size);
#endif
