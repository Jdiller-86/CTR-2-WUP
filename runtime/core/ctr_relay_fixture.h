#ifndef CTR_RELAY_FIXTURE_H
#define CTR_RELAY_FIXTURE_H
#include "ctr_gsp_fixture.h"
extern const ctr_guest_image ctr_relay_image;
extern const ctr_block ctr_relay_fixture_blocks[];
extern const size_t ctr_relay_fixture_block_count;
bool ctr_relay_fixture_open(ctr_guest *guest);
bool ctr_relay_fixture_exit(ctr_guest *guest);
int ctr_relay_selftest(void);
#endif
