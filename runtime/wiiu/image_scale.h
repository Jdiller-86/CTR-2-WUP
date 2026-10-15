#ifndef CTR_IMAGE_SCALE_H
#define CTR_IMAGE_SCALE_H
#include "viewport.h"
#include <stddef.h>
#define CTR_SCALE_CACHE_MAX_PIXELS (1024u*1024u)
typedef struct {
    CtrViewport view;
    uint16_t *columns,*rows;
} ctr_scale_plan;
/* Zero-initialized plan, bounded dimensions, cached nearest-neighbor indices.
 * Output remains byte-order-independent RGBA with black borders/padding. */
bool ctr_scale_plan_init(ctr_scale_plan *plan,const CtrViewport *view);
void ctr_scale_plan_close(ctr_scale_plan *plan);
bool ctr_scale_rgba(const ctr_scale_plan *plan,const uint32_t *image,size_t image_pixels,
                    unsigned char *output,size_t bytes,uint32_t pitch);

typedef struct { size_t offset,bytes; } ctr_scale_range;
typedef struct {
    bool full;
    uint32_t source_rows,output_rows;
    uint64_t scaled_pixels,publish_bytes;
} ctr_scale_update;
typedef struct {
    const ctr_scale_plan *plan;
    CtrViewport view;
    uint32_t *previous;
    ctr_scale_range *ranges;
    size_t source_pixels,range_capacity,range_count;
    unsigned char *output;
    size_t output_bytes;
    uint32_t pitch;
    bool valid;
} ctr_scale_cache;
/* A zero-initialized cache owns the preceding source pixels and damage ranges.
 * The plan and output must remain exclusively owned and unmodified except by
 * this API; GPU reads are allowed. Close the cache before releasing the plan.
 * First use, invalidation, or a changed destination rebuilds every output byte.
 * Warm updates preserve all borders/padding and write changed mapped spans only.
 * Publish every returned coalesced full-pitch row range before the GPU reads.
 * An unchanged image needs no publication, but may still be presented normally.
 * Bad arguments fail before changing source history, destination, or damage.
 * Source/destination/cache-history overlap is rejected. Cached inputs are
 * bounded to CTR_SCALE_CACHE_MAX_PIXELS; the uncached API retains its limits. */
bool ctr_scale_cache_init(ctr_scale_cache *cache,const ctr_scale_plan *plan);
void ctr_scale_cache_invalidate(ctr_scale_cache *cache);
void ctr_scale_cache_close(ctr_scale_cache *cache);
bool ctr_scale_cache_update(ctr_scale_cache *cache,const uint32_t *image,size_t image_pixels,
                           unsigned char *output,size_t bytes,uint32_t pitch,
                           ctr_scale_update *update);
#endif
