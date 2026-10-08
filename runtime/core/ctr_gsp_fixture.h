#ifndef CTR_GSP_FIXTURE_H
#define CTR_GSP_FIXTURE_H
#include "ctr_gsp.h"
#include "ctr_hid_fixture.h"
extern const ctr_guest_image ctr_gsp_image;
extern const ctr_block ctr_gsp_fixture_blocks[];
extern const size_t ctr_gsp_fixture_block_count;
bool ctr_gsp_fixture_open(ctr_guest *guest);
int ctr_gsp_selftest(void);
#endif
