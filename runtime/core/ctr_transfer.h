#ifndef CTR_TRANSFER_H
#define CTR_TRANSFER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
    unsigned width,height,input_format,output_format;
    bool input_linear,output_tiled,flip;
    size_t input_bytes,output_bytes;
} ctr_transfer;
/* Equal-sized base images, five display formats; no scaling/crop/raw-copy flags.
 * Linear/tiled/same-layout and vertical flip are supported. Tiled dimensions
 * must be multiples of 8; all dimensions are in 1..1024. Explicit LE words.
 * Decode checks all buffers/aliases before mutation. No GPU allocation/timing. */
bool ctr_transfer_init(ctr_transfer *transfer,uint32_t input_dim,uint32_t output_dim,uint32_t flags);
bool ctr_transfer_convert(uint32_t input_dim,uint32_t output_dim,uint32_t flags,
                          const uint8_t *source,size_t source_size,uint8_t *output,size_t output_size);
#endif
