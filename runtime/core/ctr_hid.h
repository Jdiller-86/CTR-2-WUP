#ifndef CTR_HID_H
#define CTR_HID_H
#include "ctr_kernel.h"
/* Buttons use Old-3DS bits 0..11; Circle Pad values -156..156; touch 320x240.
   Updates are deterministic, monotonically timed, and little-endian. */
bool ctr_hid_update(ctr_guest *guest,uint32_t buttons,int circle_x,int circle_y,
                    bool touched,uint16_t x,uint16_t y,uint64_t now_ns);
bool ctr_hid_handles(ctr_guest *guest,uint32_t handles[6]);
bool ctr_hid_accelerometer(ctr_guest *guest,bool enable);
/* Calibrated guest-axis acceleration, in 512 units per g. */
void ctr_hid_acceleration(ctr_guest *guest,int16_t x,int16_t y,int16_t z);
bool ctr_hid_gyroscope(ctr_guest *guest,bool enable);
#define CTR_HID_GYRO_COUNTS_PER_DPS 14.375f
/* Calibrated guest-axis angular velocity, in raw gyro counts. */
void ctr_hid_angular_velocity(ctr_guest *guest,int16_t x,int16_t y,int16_t z);
/* Inspect the standard shared ring, including guest-visible mapped permissions. */
bool ctr_hid_read(const ctr_guest *guest,uint32_t address,uint32_t *buttons,
                  int *circle_x,int *circle_y,bool *touched,uint16_t *x,uint16_t *y);
#endif
