#ifndef CTR_WIIU_GX2_SCENE_H
#define CTR_WIIU_GX2_SCENE_H
#include "ctr_scene.h"
#include "ctr_gx2_shader.h"
typedef struct GX2ContextState GX2ContextState;

typedef enum {
    CTR_WIIU_GX2_SCENE_OK, CTR_WIIU_GX2_SCENE_ARGUMENT,
    CTR_WIIU_GX2_SCENE_ALLOCATION, CTR_WIIU_GX2_SCENE_LAYOUT,
    CTR_WIIU_GX2_SCENE_SHADER, CTR_WIIU_GX2_SCENE_WAIT,
    CTR_WIIU_GX2_SCENE_BUSY
} ctr_wiiu_gx2_scene_result;

typedef struct {
    void *private_state;
    GX2ContextState *restore_context;
    ctr_wiiu_gx2_scene_result result;
    ctr_gx2_shader_result shader_result;
    uint64_t begins, draws, submissions, readbacks, shader_instructions;
    uint64_t waits, wait_failures, allocations, releases;
    bool quarantined;
} ctr_wiiu_gx2_scene;

/* The caller supplies the native renderer's active WHB context. This backend
 * restores it before returning from every callback. Inputs remain PICA shader
 * inputs: the CPU reference never supplies transformed positions or colors. */
void ctr_wiiu_gx2_scene_init(ctr_wiiu_gx2_scene *scene,GX2ContextState *restore_context);
ctr_scene_backend ctr_wiiu_gx2_scene_backend(ctr_wiiu_gx2_scene *scene);
void ctr_wiiu_gx2_scene_abort(ctr_wiiu_gx2_scene *scene);
/* Only after GX2/WHB graphics shutdown: release quarantined storage if a failed
 * retirement wait prevented safe reclamation while the GPU was running. */
void ctr_wiiu_gx2_scene_after_shutdown(ctr_wiiu_gx2_scene *scene);
const char *ctr_wiiu_gx2_scene_result_name(ctr_wiiu_gx2_scene_result result);
#endif
