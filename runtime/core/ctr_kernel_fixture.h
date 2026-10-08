#ifndef CTR_KERNEL_FIXTURE_H
#define CTR_KERNEL_FIXTURE_H
#include "ctr_kernel.h"
extern const ctr_guest_image ctr_kernel_image;
extern const ctr_block ctr_kernel_fixture_blocks[];
extern const size_t ctr_kernel_fixture_block_count;
extern const uint32_t ctr_kernel_expected[];
extern const size_t ctr_kernel_expected_count;
bool ctr_kernel_fixture_open(ctr_guest *guest);
int ctr_kernel_fixture_check(const ctr_guest *guest);
int ctr_kernel_selftest(void);
#endif
