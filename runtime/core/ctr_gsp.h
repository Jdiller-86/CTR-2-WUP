#ifndef CTR_GSP_H
#define CTR_GSP_H
#include "ctr_guest.h"
#define CTR_GSP_FRAME_NS UINT64_C(16666667)
/* Direct/shared framebuffer service and scoped CPU-backed GX queue operations.
 * Finalized PICA viewport/scissor configuration lists are supported;
 * PICA draws and native GX2 render translation remain absent. */
bool ctr_gsp_request(ctr_guest *guest,unsigned session,const uint32_t *words,uint32_t *reply,unsigned *count);
void ctr_gsp_session_closed(ctr_guest *guest,unsigned session);
/* Host-time poll at a guest execution boundary; late display ticks are coalesced. */
bool ctr_gsp_poll(ctr_guest *guest,uint64_t now_ns);
/* Revalidate the entire rotated source before writing any host output. */
ctr_mem_result ctr_gsp_pixels(const ctr_guest *guest,unsigned screen,uint32_t *rgba,size_t count);
#endif
