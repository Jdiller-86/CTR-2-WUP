#ifndef CTR_SCENE_FIXTURE_H
#define CTR_SCENE_FIXTURE_H
#include "ctr_scene.h"

#define CTR_SCENE_FIXTURE_WIDTH 64u
#define CTR_SCENE_FIXTURE_HEIGHT 64u
#define CTR_SCENE_FIXTURE_BYTES (64u*64u*4u)
typedef struct {
    uint8_t resources[128],commands[1024];
    size_t resource_bytes,command_bytes;
} ctr_scene_fixture;
/* Original PICA MOV shader, fixed red/green attributes, four array positions
 * and six byte/short indices. Produces a 48x48 two-triangle square inside 64x64.
 * These are original owned-resource fixtures, never a commercial game. */
void ctr_scene_fixture_make(ctr_scene_fixture *fixture,bool short_indices);
/* Nonidentity MUL/ADD transform via uniforms/temporaries; same analytic pixels.
 * This original host/reference fixture is separate from existing console guides. */
void ctr_scene_fixture_make_arithmetic(ctr_scene_fixture *fixture,bool short_indices);
void ctr_scene_fixture_background(void *rgba,size_t size);
ctr_scene_result ctr_scene_fixture_render(bool short_indices,void *rgba,size_t capacity,
                                         ctr_scene_report *report);
ctr_scene_result ctr_scene_fixture_render_arithmetic(bool short_indices,void *rgba,size_t capacity,
                                                    ctr_scene_report *report);
int ctr_scene_selftest(void);
int ctr_scene_arithmetic_selftest(void);
#endif
