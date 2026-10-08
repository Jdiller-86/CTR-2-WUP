#ifndef CTR_KERNEL_H
#define CTR_KERNEL_H
#include "ctr_guest.h"
void ctr_sync_svc(ctr_guest *guest, uint32_t number);
void ctr_sync_poll(ctr_guest *guest);
void ctr_shared_svc(ctr_guest *guest, uint32_t number);
/* Create local device backing for a service; returns an owned handle. */
uint32_t ctr_shared_create(ctr_guest *guest, uint32_t size, unsigned permissions);
const ctr_guest_shared *ctr_shared_mapping(const ctr_guest *guest, const ctr_mapping *mapping);
#endif
