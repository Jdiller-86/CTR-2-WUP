#ifndef CTR_SCENE_H
#define CTR_SCENE_H
#include "ctr_pica.h"
#include "ctr_shader.h"

#define CTR_SCENE_MAX_VERTICES 256u
#define CTR_SCENE_MAX_DRAWS 32u
#define CTR_SCENE_MAX_SURFACE_BYTES (1024u*1024u)
typedef enum {
    CTR_SCENE_OK, CTR_SCENE_ARGUMENT, CTR_SCENE_OVERFLOW, CTR_SCENE_ALIAS,
    CTR_SCENE_LIMIT, CTR_SCENE_NO_MEMORY, CTR_SCENE_WIRE, CTR_SCENE_UNSUPPORTED,
    CTR_SCENE_LAYOUT, CTR_SCENE_VERTEX, CTR_SCENE_SHADER, CTR_SCENE_NUMERIC
} ctr_scene_result;
typedef struct ctr_scene_state {
    uint32_t registers[CTR_PICA_REGISTER_COUNT];
    uint8_t register_defined[CTR_PICA_REGISTER_COUNT];
    ctr_shader_program shader;
    uint32_t fixed[12][4];uint16_t fixed_defined;
    uint32_t pending_fixed[3],pending_uniform[4];
    unsigned fixed_words,uniform_words;
    uint64_t lists,draws,triangles,fragments,shader_instructions;
} ctr_scene_state;
typedef struct {
    /* Explicit caller-resolved PICA physical base; never dereferenced. All array
     * and index offsets resolve inside this single owned stable resource span. */
    uint32_t base_address;
    const void *bytes;size_t size;
} ctr_scene_resources;
typedef struct {
    /* Explicit resolved backend target: linear RGBA8, row zero at bottom.
     * No PICA framebuffer address, tiling or GPU coherency is inferred. */
    void *bytes;size_t size;
    unsigned width,height,stride;
} ctr_scene_surface;
typedef struct {
    ctr_scene_result result;
    size_t byte_offset;
    unsigned reg,draws,triangles,fragments,shader_instructions;
    ctr_vertex_result vertex_result;
    ctr_shader_result shader_result;
} ctr_scene_report;

void ctr_scene_init(ctr_scene_state *state);
/* Experimental owned-resource rendering boundary, separate from guest GSP.
 * Accept known PICA loader/index/move-shader/output-map/viewport/scissor/cull
 * registers, complete packed fixed/uniform uploads and full-mask draw triggers.
 * Draw complete triangle lists (<=256 vertices each), W=1, inside clip volume,
 * exact 1/16-pixel positions and constant opaque binary primary color per
 * triangle. Fragment policy is untextured replacement with depth/blend disabled.
 * Unknown/texture/TEV/depth/framebuffer registers, arithmetic and geometry fail.
 * Every list stages state and the complete target before any commit, so late
 * errors preserve previous state/pixels. Inputs must be owned/aligned/stable.
 * Reports may change on failure unless aliased. No guest address is followed,
 * no GPU draw is submitted, and generic guest GSP draws remain unsupported. */
ctr_scene_result ctr_scene_execute(ctr_scene_state *state,const void *commands,
                                  size_t command_bytes,
                                  const ctr_scene_resources *resources,
                                  const ctr_scene_surface *surface,
                                  ctr_scene_report *report);
/* Same owned staging boundary with explicit PICA framebuffer/fragment state.
 * Requires defined untextured primary-color/pass-through TEV, logic COPY,
 * full opaque color writes, no depth/stencil/alpha/blend, RGBA8, 8x8 tiles,
 * and unflipped dimensions/address matching the caller-resolved linear target.
 * The caller resolves, snapshots and commits guest tiled storage separately;
 * this API never follows an address or reads/writes a guest mapping. */
ctr_scene_result ctr_scene_execute_target(ctr_scene_state *state,const void *commands,
                                         size_t command_bytes,const ctr_scene_resources *resources,
                                         const ctr_scene_surface *surface,uint32_t color_address,
                                         ctr_scene_report *report);
const char *ctr_scene_result_name(ctr_scene_result result);
#endif
