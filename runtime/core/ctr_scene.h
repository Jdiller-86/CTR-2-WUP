#ifndef CTR_SCENE_H
#define CTR_SCENE_H
#include "ctr_pica.h"
#include "ctr_shader.h"
#include "ctr_tev.h"

#define CTR_SCENE_MAX_VERTICES 256u
#define CTR_SCENE_MAX_DRAWS 32u
#define CTR_SCENE_MAX_SURFACE_BYTES (1024u*1024u)
#define CTR_SCENE_MAX_TEXTURE_SPANS 32u
#define CTR_SCENE_MAX_TEXTURE_SOURCE_BYTES (4u*1024u*1024u)
typedef enum {
    CTR_SCENE_OK, CTR_SCENE_ARGUMENT, CTR_SCENE_OVERFLOW, CTR_SCENE_ALIAS,
    CTR_SCENE_LIMIT, CTR_SCENE_NO_MEMORY, CTR_SCENE_WIRE, CTR_SCENE_UNSUPPORTED,
    CTR_SCENE_LAYOUT, CTR_SCENE_VERTEX, CTR_SCENE_SHADER, CTR_SCENE_NUMERIC,
    CTR_SCENE_BACKEND
} ctr_scene_result;
typedef enum {
    CTR_SCENE_BACKEND_NONE, CTR_SCENE_BACKEND_BEGIN, CTR_SCENE_BACKEND_DRAW,
    CTR_SCENE_BACKEND_END, CTR_SCENE_BACKEND_PARITY
} ctr_scene_backend_stage;
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
    uint32_t base_address;
    const void *bytes;size_t size;
} ctr_scene_resource_span;
typedef struct {
    /* Explicit caller-resolved PICA physical base; never dereferenced. All array
     * and index offsets resolve inside this single owned stable resource span. */
    uint32_t base_address;
    const void *bytes;size_t size;
    /* Optional stable owned texture snapshots. A texture must fit completely
     * inside this geometry span or one of these additional physical spans.
     * Physical spans cannot overlap. At most 32 extra spans / 4 MiB total. */
    const ctr_scene_resource_span *textures;
    unsigned texture_count;
} ctr_scene_resources;
typedef struct {
    /* Decoded raw-memory row order: PICA V=0 samples row height-1. Borrowed
     * only during draw; never retained in ctr_scene_state or guest mappings. */
    const uint8_t *rgba;
    unsigned width,height;
    uint32_t address,format,params;
} ctr_scene_texture;
typedef struct {
    /* Explicit resolved backend target: linear RGBA8, row zero at bottom.
     * No PICA framebuffer address, tiling or GPU coherency is inferred. */
    void *bytes;size_t size;
    unsigned width,height,stride;
} ctr_scene_surface;
/* Optional synchronous renderer. All commands, resources and software pixels
 * are validated before begin. begin initializes private scratch from initial;
 * draw receives original inputs, complete current PICA/shader state and an
 * optional decoded texture. Borrowed data is valid only during the callback and must not be
 * modified. end fills linear RGBA8 readback and releases renderer resources.
 * abort must be safe after failed begin/draw/end and after successful end; it
 * releases scratch without publishing pixels. The caller owns this descriptor
 * and context and keeps them stable throughout execution. No callbacks run for
 * configuration-only lists. */
typedef struct ctr_scene_backend {
    void *context;
    bool (*begin)(void *context,const ctr_scene_surface *initial);
    bool (*draw)(void *context,const ctr_scene_state *state,const ctr_vertex *vertices,unsigned count,
                 const ctr_scene_texture *texture);
    bool (*end)(void *context,const ctr_scene_surface *readback);
    void (*abort)(void *context);
} ctr_scene_backend;
typedef struct {
    ctr_scene_result result;
    size_t byte_offset;
    unsigned reg,draws,triangles,fragments,shader_instructions;
    ctr_vertex_result vertex_result;
    ctr_shader_result shader_result;
    ctr_scene_backend_stage backend_stage;
} ctr_scene_report;

void ctr_scene_init(ctr_scene_state *state);
/* Experimental owned-resource rendering boundary, separate from guest GSP.
 * Accept known PICA loader/index/move-shader/output-map/viewport/scissor/cull
 * registers, complete packed fixed/uniform uploads and full-mask draw triggers.
 * Draw complete triangle lists (<=256 vertices each), W=1, inside clip volume,
 * exact 1/16-pixel positions and constant opaque binary primary color per
 * triangle. Optional texture0 supports all 14 base formats, matching min/mag
 * nearest or bilinear filtering (exact affine weights, round half up),
 * clamp/repeat/mirror and signed exact Q16 UVs in [-16,16]. Six TEV stages use
 * primary, Texture0, constants and Previous with separate RGB/alpha operations,
 * operands and scale; see ctr_tev.h for byte-domain quantization. V=0 samples
 * the last raw texture row. Depth/blend, DOT3/buffers, other textures,
 * unsupported arithmetic and geometry fail.
 * Every list stages state and the complete target before any commit, so late
 * errors preserve previous state/pixels. Inputs must be owned/aligned/stable.
 * Reports may change on failure unless aliased. This software entry point never
 * follows a guest address or submits GPU work; the guest adapter uses the
 * explicit target/backend entry points below. */
ctr_scene_result ctr_scene_execute(ctr_scene_state *state,const void *commands,
                                  size_t command_bytes,
                                  const ctr_scene_resources *resources,
                                  const ctr_scene_surface *surface,
                                  ctr_scene_report *report);
/* Same owned staging boundary with explicit PICA framebuffer/fragment state.
 * Requires a completely defined supported six-stage TEV pipeline, logic
 * COPY, full RGBA writes, no depth/stencil/alpha/blend, RGBA8, 8x8 tiles,
 * and unflipped dimensions/address matching the caller-resolved linear target.
 * The caller resolves, snapshots and commits guest tiled storage separately;
 * this API never follows an address or reads/writes a guest mapping. */
ctr_scene_result ctr_scene_execute_target(ctr_scene_state *state,const void *commands,
                                         size_t command_bytes,const ctr_scene_resources *resources,
                                         const ctr_scene_surface *surface,uint32_t color_address,
                                         ctr_scene_report *report);
/* Target execution with an optional native renderer. A complete software
 * preflight precedes every callback. Native readback must match every staged
 * target byte exactly before state or pixels commit; any backend failure or
 * mismatch reports CTR_SCENE_BACKEND and aborts without software fallback. */
ctr_scene_result ctr_scene_execute_target_backend(ctr_scene_state *state,const void *commands,
                                                  size_t command_bytes,const ctr_scene_resources *resources,
                                                  const ctr_scene_surface *surface,uint32_t color_address,
                                                  const ctr_scene_backend *backend,ctr_scene_report *report);
const char *ctr_scene_result_name(ctr_scene_result result);
#endif
