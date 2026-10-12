#ifndef CTR_GX_H
#define CTR_GX_H
#include "ctr_guest.h"
typedef enum { CTR_GX_OK,CTR_GX_UNSUPPORTED,CTR_GX_MEMORY,CTR_GX_NO_MEMORY } ctr_gx_result;
typedef struct { ctr_mem_result memory;uint32_t detail;uint8_t irq;bool signal; } ctr_gx_completion;
/* Single synchronous CPU-backed operation from the GSP virtual-address wire.
 * DMA/fill/equal-size display transfer/strided raw texture copy/cache checks.
 * Every range/allocation is checked before mutation; memory is staged for alias
 * safety. Relay backing is never a device output. PICA draws remain unsupported.
 * Does not consume queue headers or signal interrupts; the caller owns those. */
ctr_gx_result ctr_gx_execute(ctr_guest *guest,const uint32_t command[8],ctr_gx_completion *completion);
#endif
