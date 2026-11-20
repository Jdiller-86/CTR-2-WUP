#ifndef CTR_TRANSFER_H
#define CTR_TRANSFER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
    unsigned width,height,input_width,input_format,output_format;
    bool input_linear,output_tiled,flip;
    size_t input_bytes,output_bytes;
} ctr_transfer;
/* Shared PICA framebuffer/display encoding, before little-endian storage. */
static inline uint16_t ctr_rgb5a1_pack(const uint8_t rgba[4]) {
    return (uint16_t)(((rgba[0]>>3)<<11)|((rgba[1]>>3)<<6)|((rgba[2]>>3)<<1)|(rgba[3]>>7));
}
static inline void ctr_rgb5a1_unpack(uint16_t word,uint8_t rgba[4]) {
    for(unsigned lane=0;lane<3u;++lane) {
        unsigned component=(word>>(11u-5u*lane))&31u;
        rgba[lane]=(uint8_t)((component<<3)|(component>>2));
    }
    rgba[3]=(word&1u)?255u:0u;
}
static inline uint16_t ctr_rgb565_pack(const uint8_t rgba[4]) {
    return (uint16_t)(((rgba[0]>>3)<<11)|((rgba[1]>>2)<<5)|(rgba[2]>>3));
}
static inline void ctr_rgb565_unpack(uint16_t word,uint8_t rgba[4]) {
    unsigned r=word>>11,g=(word>>5)&63u,b=word&31u;
    rgba[0]=(uint8_t)((r<<3)|(r>>2));rgba[1]=(uint8_t)((g<<2)|(g>>4));
    rgba[2]=(uint8_t)((b<<3)|(b>>2));rgba[3]=255u;
}
static inline uint16_t ctr_rgba4_pack(const uint8_t rgba[4]) {
    return (uint16_t)(((rgba[0]>>4)<<12)|((rgba[1]>>4)<<8)|((rgba[2]>>4)<<4)|(rgba[3]>>4));
}
static inline void ctr_rgba4_unpack(uint16_t word,uint8_t rgba[4]) {
    for(unsigned lane=0;lane<4u;++lane)rgba[lane]=(uint8_t)(((word>>(12u-lane*4u))&15u)*17u);
}
/* Five display formats, conversion and explicit flag-2 crop without scaling.
 * Linear/tiled/same-layout and equal-size vertical flip are supported. Tiled dimensions
 * must be multiples of 8; all dimensions are in 1..1024. Explicit LE words.
 * Decode checks all buffers/aliases before mutation. No GPU allocation/timing. */
bool ctr_transfer_init(ctr_transfer *transfer,uint32_t input_dim,uint32_t output_dim,uint32_t flags);
bool ctr_transfer_convert(uint32_t input_dim,uint32_t output_dim,uint32_t flags,
                          const uint8_t *source,size_t source_size,uint8_t *output,size_t output_size);
#endif
