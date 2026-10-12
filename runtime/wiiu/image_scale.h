#ifndef CTR_IMAGE_SCALE_H
#define CTR_IMAGE_SCALE_H
#include "viewport.h"
#include <stddef.h>
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
#endif
