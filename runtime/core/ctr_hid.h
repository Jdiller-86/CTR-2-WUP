#ifndef CTR_HID_H
#define CTR_HID_H
#include "ctr_kernel.h"
/* Buttons use Old-3DS bits 0..11; Circle Pad values -156..156; touch 320x240.
   Updates are deterministic, monotonically timed, and little-endian. */
bool ctr_hid_update(ctr_guest *guest,uint32_t buttons,int circle_x,int circle_y,
                    bool touched,uint16_t x,uint16_t y,uint64_t now_ns);
bool ctr_hid_handles(ctr_guest *guest,uint32_t handles[6]);
/* Inspect the standard shared ring, including guest-visible mapped permissions. */
bool ctr_hid_read(const ctr_guest *guest,uint32_t address,uint32_t *buttons,
                  int *circle_x,int *circle_y,bool *touched,uint16_t *x,uint16_t *y);
#endif
