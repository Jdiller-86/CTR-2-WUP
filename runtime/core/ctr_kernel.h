#ifndef CTR_KERNEL_H
#define CTR_KERNEL_H
#include "ctr_guest.h"
void ctr_sync_svc(ctr_guest *guest, uint32_t number);
void ctr_sync_poll(ctr_guest *guest);
uint32_t ctr_sync_create_semaphore(ctr_guest *guest, uint32_t count, uint32_t maximum);
/* Single emulated core: contexts switch only at bounded execution/SVC boundaries. */
void ctr_thread_svc(ctr_guest *guest, uint32_t number);
void ctr_threads_poll(ctr_guest *guest);
bool ctr_thread_schedule(ctr_guest *guest, bool rotate);
void ctr_threads_exit_process(ctr_guest *guest);
void ctr_shared_svc(ctr_guest *guest, uint32_t number);
/* Allocate guest-owned backing, charged to the application budget. */
uint32_t ctr_shared_create(ctr_guest *guest, uint32_t size, unsigned permissions);
/* Built-in services may allocate one page per object from the same object pool. */
uint32_t ctr_shared_create_service_page(ctr_guest *guest, unsigned permissions);
uint32_t ctr_shared_create_capture_buffer(ctr_guest *guest,uint32_t size);
/* Internal device publication to a live shared object, including alias invalidation. */
void ctr_shared_publish(ctr_guest *guest,unsigned object,const uint8_t *bytes);
const ctr_guest_shared *ctr_shared_mapping(const ctr_guest *guest, const ctr_mapping *mapping);
#endif
