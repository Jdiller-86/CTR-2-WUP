#ifndef CTR_GUEST_PICA_H
#define CTR_GUEST_PICA_H
#include "ctr_gx.h"
#include "ctr_scene.h"

/* Original synchronous guest adapter. Resolves only owned Old-family linear
 * allocations (VA 0x14000000 -> PA 0x20000000). One vertex base and a stable tiled
 * RGBA8/color + optional D16 or D24S8 depth/stencil pair per list; target feedback and
 * command/relay aliases fail. Both outputs publish in one memory transaction.
 * Validated command/resources/target snapshots survive guest aliases safely.
 * Publishes guest bytes/generations, shader state and counters only after the
 * entire list succeeds. Caller owns GPU right, queue and P3D completion. */
bool ctr_guest_pica_prepare_caches(ctr_guest *guest);
ctr_gx_result ctr_guest_pica_execute(ctr_guest *guest,const void *commands,size_t size,
                                    uint32_t command_address,ctr_gx_completion *completion);
#endif
