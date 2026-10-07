#include "viewport.h"

bool ctr_viewport_fit(CtrViewport *v, uint32_t ow, uint32_t oh,
                      uint32_t gw, uint32_t gh) {
    if (!v || !ow || !oh || !gw || !gh || gw > UINT16_MAX || gh > UINT16_MAX)
        return false;
    *v = (CtrViewport){0, 0, ow, oh, ow, oh, gw, gh};
    if ((uint64_t)ow * gh > (uint64_t)oh * gw)
        v->width = (uint32_t)((uint64_t)oh * gw / gh);
    else
        v->height = (uint32_t)((uint64_t)ow * gh / gw);
    if (!v->width || !v->height) return false;
    v->x = (ow - v->width) / 2;
    v->y = (oh - v->height) / 2;
    return true;
}

bool ctr_viewport_touch(const CtrViewport *v, uint32_t tx, uint32_t ty,
                       uint32_t cw, uint32_t ch, uint16_t *gx, uint16_t *gy) {
    if (!v || !gx || !gy || !cw || !ch || tx >= cw || ty >= ch ||
        !v->width || !v->height) return false;
    uint32_t x = (uint32_t)((uint64_t)tx * v->output_width / cw);
    uint32_t y = (uint32_t)((uint64_t)ty * v->output_height / ch);
    if (x < v->x || y < v->y || x - v->x >= v->width ||
        y - v->y >= v->height) return false;
    *gx = (uint16_t)((uint64_t)(x - v->x) * v->guest_width / v->width);
    *gy = (uint16_t)((uint64_t)(y - v->y) * v->guest_height / v->height);
    return true;
}
