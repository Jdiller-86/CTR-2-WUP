#ifndef CTR_GUEST_PICA_H
#define CTR_GUEST_PICA_H
#include "ctr_gx.h"
#include "ctr_scene.h"

/* Original synchronous guest adapter. Resolves only owned Old-family linear
 * allocations (VA 0x14000000 -> PA 0x20000000). One vertex base and one tiled
 * RGBA8 target per list; target feedback and command/relay aliases fail.
 * Validated command/resources/target snapshots survive guest aliases safely.
 * Publishes guest bytes/generations, shader state and counters only after the
 * entire list succeeds. Caller owns GPU right, queue and P3D completion. */
ctr_gx_result ctr_guest_pica_execute(ctr_guest *guest,const void *commands,size_t size,
                                    uint32_t command_address,ctr_gx_completion *completion);
#endif
