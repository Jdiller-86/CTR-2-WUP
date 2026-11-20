#ifndef CTR_GX_H
#define CTR_GX_H
#include "ctr_guest.h"
typedef enum { CTR_GX_OK,CTR_GX_UNSUPPORTED,CTR_GX_MEMORY,CTR_GX_NO_MEMORY } ctr_gx_result;
typedef struct { ctr_mem_result memory;uint32_t detail;uint8_t irq;bool signal; } ctr_gx_completion;
/* Single synchronous CPU-backed operation from the GSP virtual-address wire.
 * DMA/fill/equal-size display transfer/strided raw texture copy/cache checks,
 * plus finalized PICA configuration and supported draws. Every range/allocation
 * is checked before mutation; memory is staged for alias safety. Relay backing
 * is never a device output.
 * Does not consume queue headers or signal interrupts; the caller owns those. */
ctr_gx_result ctr_gx_execute(ctr_guest *guest,const uint32_t command[8],ctr_gx_completion *completion);
/* Shared synchronous device-range guard; includes full relay-backing aliases. */
ctr_gx_result ctr_gx_check_range(ctr_guest *guest,uint32_t address,uint32_t size,
                               unsigned permissions,ctr_gx_completion *completion);
/* Dedicated VRAM backing for device operations using the read-only CPU view. */
uint8_t *ctr_gx_vram_backing(const ctr_guest *guest,uint32_t address,uint32_t size);
/* Device commit after a successful WRITE range check and independent staging. */
void ctr_gx_write_target(ctr_guest *guest,uint32_t address,const uint8_t *bytes,uint32_t size);
#endif
