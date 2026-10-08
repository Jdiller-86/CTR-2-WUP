#ifndef CTR_SERVICE_FIXTURE_H
#define CTR_SERVICE_FIXTURE_H
#include "ctr_guest.h"

extern const ctr_guest_image ctr_service_image;
extern const ctr_block ctr_service_fixture_blocks[];
extern const size_t ctr_service_fixture_block_count;
bool ctr_service_fixture_open(ctr_guest *guest);
/* Original ARM TLS/SRV/CFG probe on interpreter and original AOT. Zero passes;
   nonzero identifies a loader, execution, result or state-parity failure. */
int ctr_service_selftest(void);
#endif
