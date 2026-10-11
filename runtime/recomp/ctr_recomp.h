#ifndef CTR_RECOMP_ENGINE_H
#define CTR_RECOMP_ENGINE_H
#include "ctr_guest.h"
/* Attach the statically linked executable. CRO relocation is not yet supported.
 * Missing code can use the existing bounded interpreter only when requested.
 * Allocation, code hash and ABI failures leave the guest without an executor. */
bool ctr_recomp_attach(ctr_guest *guest,bool fallback);
/* Diagnostic access to persistent VFP words for differential tests. */
bool ctr_recomp_vfp(const ctr_guest *guest,unsigned word,uint32_t *value);
bool ctr_recomp_depth_stats(const ctr_guest *guest,unsigned *maximum,uint64_t *unwinds);
#endif
