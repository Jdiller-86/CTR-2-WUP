#ifndef CTR_THREAD_FIXTURE_H
#define CTR_THREAD_FIXTURE_H
#include "ctr_guest.h"
extern const ctr_guest_image ctr_thread_image;
extern const ctr_block ctr_thread_fixture_blocks[];
extern const size_t ctr_thread_fixture_block_count;
bool ctr_thread_fixture_open(ctr_guest *guest);
int ctr_thread_fixture_check(const ctr_guest *guest);
int ctr_thread_selftest(void);
#endif
