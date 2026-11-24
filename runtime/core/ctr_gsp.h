#ifndef CTR_GSP_H
#define CTR_GSP_H
#include "ctr_guest.h"
#define CTR_GSP_FRAME_NS UINT64_C(16666667)
#define CTR_VRAM_BASE UINT32_C(0x1f000000)
#define CTR_VRAM_BYTES UINT32_C(0x00600000)
#define CTR_VRAM_PHYSICAL_BASE UINT32_C(0x18000000)
/* Install the read-only CPU view declared by the supported title. */
bool ctr_gsp_open_vram(ctr_guest *guest);
/* Direct/shared framebuffer service, checked GX queue operations and PICA lists. */
bool ctr_gsp_request(ctr_guest *guest,unsigned session,const uint32_t *words,uint32_t *reply,unsigned *count);
void ctr_gsp_session_closed(ctr_guest *guest,unsigned session);
/* Host-time poll at a guest execution boundary; late display ticks are coalesced. */
bool ctr_gsp_poll(ctr_guest *guest,uint64_t now_ns);
/* Revalidate the entire rotated source before writing any host output. */
ctr_mem_result ctr_gsp_pixels(const ctr_guest *guest,unsigned screen,uint32_t *rgba,size_t count);
typedef struct {
    ctr_memory_resource_key source;
    uint32_t descriptor[7];
    uint32_t *rgba;
    size_t count;
} ctr_gsp_pixel_cache;
/* The caller retains the same unmodified output buffer for this cache. */
ctr_mem_result ctr_gsp_pixels_cached(const ctr_guest *guest,unsigned screen,uint32_t *rgba,
                                     size_t count,ctr_gsp_pixel_cache *cache);
#endif
