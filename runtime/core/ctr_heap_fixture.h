#ifndef CTR_HEAP_FIXTURE_H
#define CTR_HEAP_FIXTURE_H
#include "ctr_heap.h"
extern const ctr_guest_image ctr_heap_image;
extern const ctr_block ctr_heap_fixture_blocks[];
extern const size_t ctr_heap_fixture_block_count;
bool ctr_heap_fixture_open(ctr_guest *guest);
/* Verify independent expected SVC outputs, not just backend equality. */
int ctr_heap_fixture_check(const ctr_guest *guest);
int ctr_heap_selftest(void);
#endif
