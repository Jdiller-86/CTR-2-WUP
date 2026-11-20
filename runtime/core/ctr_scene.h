#ifndef CTR_SCENE_H
#define CTR_SCENE_H
#include "ctr_pica.h"
#include "ctr_shader.h"
#include "ctr_tev.h"
#include "ctr_fragment.h"
#include "ctr_texture_cache.h"

#define CTR_SCENE_MAX_VERTICES 256u
#define CTR_SCENE_MAX_DRAWS 32u
#define CTR_SCENE_MAX_GAME_DRAWS 512u
#define CTR_SCENE_MAX_GAME_DRAW_VERTICES 12288u
#define CTR_SCENE_IMMEDIATE_INDEX 15u
#define CTR_SCENE_MAX_SURFACE_BYTES (1024u*1024u)
#define CTR_SCENE_MAX_DEPTH_BYTES (512u*1024u)
#define CTR_SCENE_MAX_D24S8_BYTES (1024u*1024u)
enum { CTR_SCENE_DEPTH_D16=0u, CTR_SCENE_DEPTH_D24S8=3u };
enum { CTR_SCENE_COLOR_RGBA8=0u, CTR_SCENE_COLOR_RGB5A1=2u, CTR_SCENE_COLOR_RGB565=3u, CTR_SCENE_COLOR_RGBA4=4u };
bool ctr_scene_color_format(uint32_t register_value,unsigned *format);
#define CTR_SCENE_MAX_TEXTURE_SPANS 32u
#define CTR_SCENE_MAX_GAME_TEXTURE_SPANS 96u
#define CTR_SCENE_MAX_TEXTURE_SOURCE_BYTES (4u*1024u*1024u)
#define CTR_SCENE_TEXTURE_UNITS 3u
#define CTR_SCENE_MAX_MIP_LEVELS 8u
enum {
    CTR_SCENE_TEXTURE_DIMENSIONS, CTR_SCENE_TEXTURE_PARAMETERS, CTR_SCENE_TEXTURE_LOD,
    CTR_SCENE_TEXTURE_ADDRESS, CTR_SCENE_TEXTURE_FORMAT
};
extern const unsigned ctr_scene_texture_registers[CTR_SCENE_TEXTURE_UNITS][5];
typedef enum {
    CTR_SCENE_OK, CTR_SCENE_ARGUMENT, CTR_SCENE_OVERFLOW, CTR_SCENE_ALIAS,
    CTR_SCENE_LIMIT, CTR_SCENE_NO_MEMORY, CTR_SCENE_WIRE, CTR_SCENE_UNSUPPORTED,
    CTR_SCENE_LAYOUT, CTR_SCENE_VERTEX, CTR_SCENE_SHADER, CTR_SCENE_NUMERIC,
    CTR_SCENE_BACKEND
} ctr_scene_result;
typedef struct {
    unsigned width,height,min_level,max_level;
    size_t byte_count;
} ctr_scene_texture_layout;
/* Shared bounds for the guest snapshot and software sampler. */
ctr_scene_result ctr_scene_texture_layout_read(uint32_t dimensions,uint32_t params,uint32_t lod,
    unsigned format,bool mipmaps,ctr_scene_texture_layout *layout);
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
    uint32_t fog_lut[128];
    uint32_t lighting_lut[3][256];
    unsigned fixed_words,uniform_words,geometry_uniform_words;
    uint64_t lists,draws,triangles,fragments,shader_instructions;
} ctr_scene_state;
typedef struct {
    uint32_t base_address;
    const void *bytes;size_t size;
} ctr_scene_resource_span;
typedef struct {
    /* Explicit caller-resolved PICA physical base; never dereferenced. Array
     * and index offsets are relative to this address. */
    uint32_t base_address;
    const void *bytes;size_t size;
    /* Optional stable owned snapshots. A texture, or a game vertex or index
     * read, must fit wholly in one span. Physical spans cannot overlap.
     * At most 32 extra spans (96 for the game software path) and 4 MiB total. */
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
    ctr_scene_texture levels[CTR_SCENE_MAX_MIP_LEVELS];
    unsigned min_level,max_level;
    int bias_q8;
    bool track_lod;
} ctr_scene_sampler;
typedef struct {
    /* Explicit resolved backend target: linear RGBA8, row zero at bottom.
     * No PICA framebuffer address, tiling or GPU coherency is inferred. */
    void *bytes;size_t size;
    unsigned width,height,stride;
} ctr_scene_surface;
typedef struct {
    /* Owned linear little-endian D16 or D24S8, row zero at bottom. PICA address is an
     * explicit binding identity, never dereferenced. Padding/trailing bytes
     * participate in staging and exact parity just like the color surface. */
    void *bytes;size_t size;
    unsigned width,height,stride;
    uint32_t address;
    unsigned format; /* D16=0:2 bytes; D24S8=3:depth low24, stencil high8. */
} ctr_scene_depth_surface;
typedef struct {
    float position[4],color[4],uv[CTR_SCENE_TEXTURE_UNITS][2],orientation[4],view[3];
} ctr_scene_transformed_vertex;
typedef struct {
    const uint32_t *table[3];
    double scale[3],direction[3],ambient[3],diffuse[3],specular[2][3];
    unsigned input[3],flags,alpha,bump_unit;
    bool active,needs_view,absolute[3],clamp_highlights,reconstruct_normal;
} ctr_scene_light;
enum { CTR_SCENE_BACKEND_DEPTH_FOG=1u, CTR_SCENE_BACKEND_DIRECT=2u };
/* Optional synchronous renderer. All commands, resources and software pixels
 * are validated before begin. begin initializes private scratch from initial;
 * draw receives original inputs, complete current PICA/shader state and an
 * optional decoded texture. Borrowed data is valid only during the callback and must not be
 * modified. end fills linear RGBA8 readback and releases renderer resources.
 * abort must be safe after failed begin/draw/end and after successful end; it
 * releases scratch without publishing pixels. The caller owns this descriptor
 * and context and keeps them stable throughout execution. No callbacks run for
 * configuration-only lists. */
typedef struct ctr_scene_gpu_draw {
    const ctr_scene_state *state;
    const ctr_vertex_attribute *attributes;
    size_t attribute_count;
    const ctr_vertex_stream *streams;
    size_t stream_count;
    const ctr_vertex_indices *indices;
    const float (*fixed_values)[4];
    const unsigned *fixed_slots;
    unsigned total_inputs,fixed_mask;
    unsigned vertex_offset,vertex_count;
    bool indexed;
    const ctr_scene_sampler *samplers;
    unsigned texture_mask;
    const ctr_scene_light *light;
} ctr_scene_gpu_draw;
typedef struct ctr_scene_backend {
    void *context;
    bool (*begin)(void *context,const ctr_scene_surface *initial);
    bool (*draw)(void *context,const ctr_scene_state *state,const ctr_vertex *vertices,unsigned count,
                 const ctr_scene_texture *texture);
    bool (*end)(void *context,const ctr_scene_surface *readback);
    void (*abort)(void *context);
    /* Optional paired-target extension; both are required when depth is bound.
     * Legacy color-only callbacks retain their existing contract. */
    bool (*begin_depth)(void *context,const ctr_scene_surface *initial,
                        const ctr_scene_depth_surface *depth);
    bool (*end_depth)(void *context,const ctr_scene_surface *readback,
                      const ctr_scene_depth_surface *depth);
    /* Optional multiunit extension. Enabled mask bits0..2 index exactly three
     * borrowed decoded descriptors; disabled entries are zero. Legacy draw
     * handles masks0/1. Unsupported multiunit use rejects in software preflight
     * before begin, including when a later suffix disables the units. */
    bool (*draw_textures)(void *context,const ctr_scene_state *state,
                          const ctr_vertex *vertices,unsigned count,
                          const ctr_scene_texture textures[CTR_SCENE_TEXTURE_UNITS],
                          unsigned texture_mask);
    /* Optional mip sampler extension. Enabled units borrow every decoded
     * level from zero through max_level. min_level clamps sampling, not
     * storage. Disabled units are zero. The comparison covers the full target. */
    bool (*draw_mips)(void *context,const ctr_scene_state *state,
                     const ctr_vertex *vertices,unsigned count,
                     const ctr_scene_sampler samplers[CTR_SCENE_TEXTURE_UNITS],
                     unsigned texture_mask);
    unsigned features;
    /* Optional for rounded game shaders. The CPU executes, clips and snaps
     * vertices first, then culls on the PICA screen grid. Submit each borrowed
     * triangle batch in order. Full software comparison precedes publication. */
    bool (*draw_transformed)(void *context,const ctr_scene_state *state,
                            const ctr_scene_transformed_vertex *vertices,unsigned count,
                            const ctr_scene_sampler samplers[CTR_SCENE_TEXTURE_UNITS],
                            unsigned texture_mask,const ctr_scene_light *light);
    bool (*draw_gpu)(void *context,const ctr_scene_gpu_draw *draw);
} ctr_scene_backend;
typedef struct {
    ctr_scene_result result;
    size_t byte_offset;
    unsigned reg,draws,triangles,fragments,shader_instructions;
    ctr_vertex_result vertex_result;
    ctr_shader_result shader_result;
    ctr_scene_backend_stage backend_stage;
    bool backend_fallback;
} ctr_scene_report;

void ctr_scene_init(ctr_scene_state *state);
/* Guest adapter only. Apply a configuration-only list to private staged state
 * using its validated inspector output. Failure may alter the stage, which the
 * caller discards. No targets, draw triggers or command jumps are admitted. */
ctr_scene_result ctr_scene_apply_setup(ctr_scene_state *stage,const void *commands,
                                     size_t command_bytes,const ctr_pica_write *writes,
                                     size_t count,ctr_scene_report *report);
/* Experimental owned-resource rendering boundary, separate from guest GSP.
 * Accept known PICA loader/index/move-shader/output-map/viewport/scissor/cull
 * registers, complete packed fixed/uniform uploads and full-mask draw triggers.
 * Draw complete triangle lists (<=256 vertices each), W=1, inside clip volume,
 * exact 1/16-pixel positions and constant opaque binary primary color per
 * triangle. Optional Texture0/1/2 support all 14 base formats, matching min/mag
 * nearest or bilinear filtering (exact affine weights, round half up),
 * clamp/repeat/mirror and signed exact Q16 UVs in [-16,16]. Complete coordinate
 * pairs use semantics12/13,14/15,22/23; Texture2 may select coordinate1. Six TEV
 * stages use primary, Texture0/1/2, constants, Previous and PreviousBuffer with separate RGB/alpha operations,
 * operands and scale; see ctr_tev.h for byte-domain quantization. V=0 samples
 * the last raw texture row. Alpha comparisons, RGBA masks, explicit color gates
 * and all sixteen bytewise logic operations or checked RGB/alpha blending run
 * after TEV. Blend equations use one byte-domain division before saturation;
 * native admission is narrower and remains guarded by full parity. The paired
 * target entry admits bounded D16/D24S8 depth and stencil; DOT3, Texture3/procedural,
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
 * Requires a completely defined supported six-stage TEV/output-merger pipeline,
 * no depth/stencil, RGBA8, 8x8 tiles,
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
/* Paired-target variant. Optional depth is owned D16/D24S8 with canonical Z buffering
 * (scale -1, offset signed zero), W=1 and constant Z per triangle. All eight
 * comparisons, D24S8 stencil comparisons/actions and independent read/write
 * gates are checked. Both complete
 * surfaces and state commit together; no depth address is followed. Owned
 * color is bounded to 1 MiB; depth to 512 KiB D16 or 1 MiB D24S8 (2 MiB combined
 * maximum). The paired
 * backend profile admits only endpoint Z=0/-1 and rejects other values before
 * begin; software-only execution admits exact constant fractional depth. Legacy
 * renderers without a paired-target interface reject depth before begin. */
ctr_scene_result ctr_scene_execute_targets(ctr_scene_state *state,const void *commands,
                                           size_t command_bytes,const ctr_scene_resources *resources,
                                           const ctr_scene_surface *surface,uint32_t color_address,
                                           const ctr_scene_depth_surface *depth,ctr_scene_report *report);
ctr_scene_result ctr_scene_execute_targets_backend(ctr_scene_state *state,const void *commands,
                                                   size_t command_bytes,const ctr_scene_resources *resources,
                                                   const ctr_scene_surface *surface,uint32_t color_address,
                                                   const ctr_scene_depth_surface *depth,
                                                   const ctr_scene_backend *backend,ctr_scene_report *report);
/* Optional process-local decoded-image owner, kept outside copied scene state.
 * Entries own exact encoded bytes and RGBA images; borrowed images are pinned
 * through each draw. Current state/resource validation and full software/native
 * parity remain mandatory. Cache warmth/statistics may change on a rejected
 * list; guest-visible scene state and targets retain the normal atomic contract.
 * Caller exclusively owns the cache during this synchronous execution. */
ctr_scene_result ctr_scene_execute_targets_cached(ctr_scene_state *state,const void *commands,
                                                  size_t command_bytes,const ctr_scene_resources *resources,
                                                  const ctr_scene_surface *surface,uint32_t color_address,
                                                  const ctr_scene_depth_surface *depth,
                                                  const ctr_scene_backend *backend,ctr_texture_cache *cache,
                                                  ctr_scene_report *report);
/* Guest target format controls the physical span and color write precision.
 * The guest adapter can provide an 8-byte-aligned stream after resolving links.
 * The staging surface continues to contain four decoded RGBA bytes per pixel. */
ctr_scene_result ctr_scene_execute_targets_format_cached(ctr_scene_state *state,const void *commands,
                                                         size_t command_bytes,const ctr_scene_resources *resources,
                                                         const ctr_scene_surface *surface,uint32_t color_address,
                                                         unsigned color_format,const ctr_scene_depth_surface *depth,
                                                         const ctr_scene_backend *backend,ctr_texture_cache *cache,
                                                         ctr_scene_report *report);
const char *ctr_scene_result_name(ctr_scene_result result);
#endif
