#ifndef CTR_HEAP_H
#define CTR_HEAP_H
#include "ctr_guest.h"

enum { CTR_HEAP_BASE=0x08000000u, CTR_HEAP_END=0x10000000u,
       CTR_LINEAR_BASE=0x14000000u, CTR_LINEAR_END=0x1c000000u,
       CTR_PROCESS_END=0x40000000u };
/* Checked single-process Old-3DS address-space subset. Linear memory is CPU
   backing only; no GPU/DSP physical-address mapping is supplied. */
void ctr_heap_control(ctr_guest *guest);
void ctr_heap_query(ctr_guest *guest);
#endif
