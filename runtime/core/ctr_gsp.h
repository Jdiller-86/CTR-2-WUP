#ifndef CTR_GSP_H
#define CTR_GSP_H
#include "ctr_guest.h"
/* Original scoped direct-display service. Command queues/PICA are unsupported. */
bool ctr_gsp_request(ctr_guest *guest,const uint32_t *words,uint32_t *reply,unsigned *count);
/* Revalidate the entire rotated source before writing any host output. */
ctr_mem_result ctr_gsp_pixels(const ctr_guest *guest,unsigned screen,uint32_t *rgba,size_t count);
#endif
