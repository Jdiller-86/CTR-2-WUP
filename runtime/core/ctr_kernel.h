#ifndef CTR_KERNEL_H
#define CTR_KERNEL_H
#include "ctr_guest.h"
void ctr_sync_svc(ctr_guest *guest, uint32_t number);
void ctr_sync_poll(ctr_guest *guest);
/* Single emulated core: contexts switch only at bounded execution/SVC boundaries. */
void ctr_thread_svc(ctr_guest *guest, uint32_t number);
void ctr_threads_poll(ctr_guest *guest);
bool ctr_thread_schedule(ctr_guest *guest, bool rotate);
void ctr_threads_exit_process(ctr_guest *guest);
void ctr_shared_svc(ctr_guest *guest, uint32_t number);
/* Create local device backing for a service; returns an owned handle. */
uint32_t ctr_shared_create(ctr_guest *guest, uint32_t size, unsigned permissions);
/* Internal device publication to a live shared object, including alias invalidation. */
void ctr_shared_publish(ctr_guest *guest,unsigned object,const uint8_t *bytes);
const ctr_guest_shared *ctr_shared_mapping(const ctr_guest *guest, const ctr_mapping *mapping);
#endif
