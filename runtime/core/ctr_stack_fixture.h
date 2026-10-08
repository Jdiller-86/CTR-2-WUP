#ifndef CTR_STACK_FIXTURE_H
#define CTR_STACK_FIXTURE_H
#include "ctr_guest.h"
extern const ctr_guest_image ctr_stack_image;
extern const ctr_block ctr_stack_fixture_blocks[];
extern const size_t ctr_stack_fixture_block_count;
bool ctr_stack_fixture_open(ctr_guest *guest);
int ctr_stack_selftest(void);
#endif
