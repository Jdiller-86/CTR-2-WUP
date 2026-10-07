#ifndef CTR_GUEST_FIXTURE_H
#define CTR_GUEST_FIXTURE_H
#include "ctr_guest.h"
#define CTR_FRAME_DATA UINT32_C(0x08000000)
#define CTR_FRAME_TOP (CTR_FRAME_DATA+4096u)
#define CTR_FRAME_BOTTOM (CTR_FRAME_TOP+400u*240u*4u)
extern const ctr_guest_image ctr_frame_image;
extern const ctr_block ctr_frame_fixture_blocks[];
extern const size_t ctr_frame_fixture_block_count;
bool ctr_frame_open(ctr_guest *g);
bool ctr_frame_input(ctr_guest *g,uint32_t buttons,bool touched,uint32_t x,uint32_t y);
bool ctr_frame_count(const ctr_guest *g,uint32_t *count);
bool ctr_frame_pixels(const ctr_guest *g,bool top,uint32_t *rgba,size_t pixel_count);
bool ctr_frame_exit(ctr_guest *g);
#endif
