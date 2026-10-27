#ifndef CTR_TEXTURE_H
#define CTR_TEXTURE_H
#include <stddef.h>
#include <stdint.h>

typedef enum {
    CTR_TEXTURE_OK,CTR_TEXTURE_FORMAT,CTR_TEXTURE_DIMENSIONS,CTR_TEXTURE_SOURCE,
    CTR_TEXTURE_DESTINATION,CTR_TEXTURE_COMPRESSED,CTR_TEXTURE_ALIAS
} ctr_texture_result;
/* PICA formats 0..13, base levels with power-of-two dimensions 8..1024.
 * Source tiles are 8x8 Morton order; output is row-major straight RGBA bytes,
 * y=0 first, without a display-origin flip. No host-native guest-word loads.
 * The caller supplies checked/owned buffers, never an unchecked guest pointer.
 * All compressed blocks and buffer ranges are preflighted before output changes.
 * Mip tails, sampling, GPU resource lifetime and draw submission are separate. */
ctr_texture_result ctr_texture_size(unsigned format,unsigned width,unsigned height,size_t *size);
/* Validate source bounds and all compressed blocks without allocating or
 * changing output. Decoder and CPU cache share this preflight. */
ctr_texture_result ctr_texture_validate(const uint8_t *source,size_t source_bytes,unsigned format,
                                       unsigned width,unsigned height);
ctr_texture_result ctr_texture_decode(const uint8_t *source,size_t source_bytes,unsigned format,
                                     unsigned width,unsigned height,uint8_t *output,size_t output_bytes);
#endif
