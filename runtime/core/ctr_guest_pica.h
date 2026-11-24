#ifndef CTR_GUEST_PICA_H
#define CTR_GUEST_PICA_H
#include "ctr_gx.h"
#include "ctr_scene.h"

/* Original synchronous guest adapter. Resolves only owned Old-family linear
 * allocations (VA 0x14000000 -> PA 0x20000000). One vertex base and a stable tiled
 * RGBA8/color + optional D16 or D24S8 depth/stencil pair per list; target feedback and
 * command/relay aliases fail. Both outputs publish in one memory transaction.
 * Validated command/resources/target snapshots survive guest aliases safely.
 * Publishes guest bytes/generations, shader state and counters only after the
 * entire list succeeds. Caller owns GPU right, queue and P3D completion. */
enum {
    CTR_GX_SCANOUT_WIDTH=240,
    CTR_GX_SCANOUT_HEIGHT=400,
    CTR_GX_BOTTOM_HEIGHT=320,
    CTR_GX_BOTTOM_BYTES=CTR_GX_SCANOUT_WIDTH*CTR_GX_BOTTOM_HEIGHT*2,
    CTR_GX_SCANOUT_BYTES=CTR_GX_SCANOUT_WIDTH*CTR_GX_SCANOUT_HEIGHT*2,
    CTR_GX_SCANOUT_SLOTS=CTR_MEMORY_WATCH_SLOTS-1
};
typedef struct {
    ctr_guest *guest;
    uint8_t *storage;
    uint32_t address;
    unsigned first_row;
} ctr_scene_scanout_slot;
struct ctr_scene_scanout_state {
    ctr_memory_watch_group watches;
    ctr_guest *guest;
    ctr_scene_scanout_slot slots[CTR_GX_SCANOUT_SLOTS];
    unsigned source_slot; /* One-based slot retaining the pending PICA source. */
    bool bottom_valid;
    uint8_t bottom_source[2u*CTR_GX_BOTTOM_BYTES];
    uint8_t bottom_output[CTR_GX_BOTTOM_BYTES];
};

ctr_scene_scanout_state *ctr_guest_scanout_state(const ctr_guest *guest);
unsigned ctr_guest_scanout_screen(const ctr_guest *guest,unsigned screen);
bool ctr_guest_scanout_flush(ctr_guest *guest);
void ctr_guest_scanout_discard_source(ctr_guest *guest);

bool ctr_guest_pica_prepare_caches(ctr_guest *guest);
bool ctr_guest_pica_flush_color(ctr_guest *guest);
/* Publish the pending target and return its linear RGBA8 pixels to an owned buffer. */
bool ctr_guest_pica_publish_color(ctr_guest *guest,uint8_t *pixels,size_t size);
ctr_gx_result ctr_guest_pica_execute(ctr_guest *guest,const void *commands,size_t size,
                                    uint32_t command_address,ctr_gx_completion *completion);
#endif
