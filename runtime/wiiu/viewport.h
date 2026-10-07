#ifndef CTR2WUP_VIEWPORT_H
#define CTR2WUP_VIEWPORT_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t x, y, width, height;
    uint32_t output_width, output_height, guest_width, guest_height;
} CtrViewport;

/* Integer, centered, aspect-preserving viewport. Zero dimensions are invalid. */
bool ctr_viewport_fit(CtrViewport *out, uint32_t output_width,
                      uint32_t output_height, uint32_t guest_width,
                      uint32_t guest_height);

/* Coordinates must first be calibrated by VPADGetTPCalibratedPointEx.
 * Translate that explicitly selected coordinate space into the actual GX2
 * surface, then reject the half-open black border and map to guest pixels. */
bool ctr_viewport_touch(const CtrViewport *view, uint32_t calibrated_x,
                       uint32_t calibrated_y, uint32_t calibration_width,
                       uint32_t calibration_height, uint16_t *guest_x,
                       uint16_t *guest_y);
#endif
