#ifndef CTR_HID_FIXTURE_H
#define CTR_HID_FIXTURE_H
#include "ctr_hid.h"
extern const ctr_guest_image ctr_hid_image;
extern const ctr_block ctr_hid_fixture_blocks[];
extern const size_t ctr_hid_fixture_block_count;
bool ctr_hid_fixture_open(ctr_guest *guest);
bool ctr_hid_fixture_snapshot(const ctr_guest *guest,uint32_t words[5]);
bool ctr_hid_fixture_exit(ctr_guest *guest);
int ctr_hid_selftest(void);
#endif
