#ifndef CTR_WIIU_GX2_SCENE_INTERNAL_H
#define CTR_WIIU_GX2_SCENE_INTERNAL_H
#include "gx2_scene.h"
#include "ctr_fragment.h"
#ifdef CTR_GX2_SCENE_MOCK
#include "gx2_mock.h"
#else
#include <coreinit/cache.h>
#include <gx2/clear.h>
#include <gx2/context.h>
#include <gx2/draw.h>
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/sampler.h>
#include <gx2/shaders.h>
#include <gx2/surface.h>
#include <gx2/texture.h>
#include <gx2/utils.h>
extern uint32_t GX2GetSurfaceMipPitch(GX2Surface *surface,uint32_t level);
extern void GX2ConvertDepthBufferToTextureSurface(GX2DepthBuffer *depth,GX2Surface *texture,
                                                 uint32_t mip,uint32_t slice);
#endif
#include <float.h>
#include <malloc.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAX_IMAGE_BYTES (2u*1024u*1024u)
#define MAX_TEXTURE_BYTES CTR_WIIU_GX2_TEXTURE_CACHE_MAX_IMAGE_BYTES
#define MAX_TEXTURE_TOTAL_BYTES CTR_WIIU_GX2_TEXTURE_CACHE_MAX_BYTES
#define CEMU_TEXTURE_SLOTS 48u
#define DIRECT_TEXTURE_SLOTS 256u
#define DIRECT_VS_SLOTS 32u
#define DIRECT_PS_SLOTS 256u
#define DIRECT_PS_INDEX_SLOTS (DIRECT_PS_SLOTS*2u)
#define DIRECT_PS_ARENA_BYTES (1024u*1024u)
#define DIRECT_VERTEX_STRIDE (7u*4u*sizeof(float))
#define DIRECT_VERTEX_RING_BYTES (8u*1024u*1024u)
#define VERTEX_STRIDE (16u*4u*sizeof(float))
#define VERTEX_UPLOAD_BYTES (CTR_SCENE_MAX_VERTICES*VERTEX_STRIDE)
#define MAX_FETCH_BYTES 4096u
#define PIXEL_BYTES 8192u
#define TRANSFORMED_PROGRAM_BYTES 256u
#define DRAW_UPLOAD_SLOTS 8u
#define DRAW_UPLOAD_BYTES (TRANSFORMED_PROGRAM_BYTES+PIXEL_BYTES+VERTEX_UPLOAD_BYTES)
#define PIXEL_ALU_OFFSET 256u
#define PIXEL_MAX_ALUS 928u
_Static_assert(PIXEL_ALU_OFFSET+PIXEL_MAX_ALUS*8u+15u+13u*16u<=PIXEL_BYTES,
               "Latte ALUs and texture gradients must fit in one shader");
enum {
    FOG_COORD=15u, LIGHT_PRIMARY, LIGHT_SECONDARY, LIGHT_QUATERNION, LIGHT_NORMAL, LIGHT_VIEW,
    LIGHT_HALF, LIGHT_MATH, LIGHT_COSINES, LUT_VALUE, LUT_LANES, DEPTH_PLANE,
    BLEND_UV, BLEND_DESTINATION, BLEND_FACTOR,
    MIP_COORDINATE, MIP_DX=MIP_COORDINATE+3u, MIP_DY=MIP_DX+3u,
    DEPTH_PRODUCTS=MIP_DY+3u, DEPTH_SUM, FILTER_WORK, FILTER_WEIGHT, FILTER_TAP=FILTER_WEIGHT+3u
};
enum {
    BLEND_UV_SCALE=11u, FOG_TABLE_FIRST=BLEND_UV_SCALE+1u,
    LUT_MASKS=FOG_TABLE_FIRST+128u/4u, LUT_FORMAT, LIGHT_FORMAT, LIGHT_DIRECTION,
    LIGHT_AMBIENT, LIGHT_DIFFUSE, LIGHT_SPECULAR0, LIGHT_SPECULAR1, LIGHT_SCALE,
    LIGHT_TABLE_FIRST, MIP_DIMENSIONS=LIGHT_TABLE_FIRST+3u*256u/4u,
    FILTER_SCALE=MIP_DIMENSIONS+3u, FILTER_LEVELS, BLEND_CONSTANT_SCALE=FILTER_LEVELS+3u, PIXEL_CONSTANTS=BLEND_CONSTANT_SCALE+1u
};
_Static_assert(PIXEL_CONSTANTS<=256u,"Latte constant register file overflow");
typedef enum {
    BLEND_FIXED, BLEND_SOURCE_OVER, BLEND_DESTINATION_SCALE, BLEND_ALPHA_ADD
} pixel_blend_mode;

typedef struct {
    GX2Texture texture;
    unsigned pitch[CTR_SCENE_MAX_MIP_LEVELS];
    size_t bytes;
} texture_image;
typedef struct {
    texture_image upload;
    uint32_t hash;
    unsigned pins;
    bool submitted;
    uint32_t identities[CTR_SCENE_MAX_MIP_LEVELS];
} texture_cache_entry;
#define GX2_TEXTURE_IDENTITY_SLOTS 1024u
struct ctr_wiiu_gx2_texture_cache {
    unsigned next,slots;
    texture_cache_entry *identity_index[GX2_TEXTURE_IDENTITY_SLOTS];
    texture_cache_entry entries[];
};
typedef struct {
    uint32_t hash;
    size_t key_size;
    uint8_t key[CTR_GX2_CACHE_PIXEL_KEY_MAX];
    GX2PixelShader shader;
} direct_ps_entry;
#define DIRECT_GPU_VS_SLOTS 128u
#define DIRECT_GPU_VS_ARENA_BYTES (1024u*1024u)
#define DIRECT_GPU_FETCH_SLOTS 128u
#define DIRECT_GPU_FETCH_BYTES 512u
#define CEMU_GPU_SHADER_SLOTS 512u
#define CEMU_GPU_VS_ARENA_BYTES (4u*1024u*1024u)
typedef struct {
    uint64_t hash;
    uint64_t code_hash2;
    uint32_t code_generation;
    unsigned entry;
    uint16_t boolean_uniforms;
    bool boolean_defined;
    uint8_t integer_defined;
    uint32_t integer_uniforms[4];
    uint8_t uniform_defined[CTR_SHADER_UNIFORMS];
    ctr_gx2_shader_game_options opt;
    GX2VertexShader shader;
    uint16_t input_mask;
    uint8_t uniform_used[CTR_SHADER_UNIFORMS];
    uint32_t pica_instructions;
} direct_gpu_vs_entry;
typedef struct {
    uint64_t hash;
    GX2FetchShader shader;
    unsigned attribute_count;
    ctr_vertex_attribute attributes[16];
} direct_gpu_fetch_entry;
typedef struct {
    GX2ContextState *context;
    ctr_gx2_shader *compiled;
    GX2FetchShader fetch;
    void *fetch_program;
    uint32_t fetch_size;
    void *vertex_ring;
    size_t vertex_cursor;
    struct {
        size_t offset;
        size_t sizes[12];
        const uint8_t *sources[12];
        unsigned streams;
    } vertex_upload_cache[16];
    unsigned vertex_upload_count,vertex_upload_cursor;
    GX2ColorBuffer target,color_candidate;
    GX2Surface linear;
    GX2DepthBuffer depth_target;
    unsigned width,height,depth_format;
    bool has_depth;
    bool color_resolved,color_pending;
    bool color_flipped;
    void *vs_arena;
    GX2VertexShader vs[DIRECT_VS_SLOTS];
    bool vs_valid[DIRECT_VS_SLOTS];
    void *ps_arena;
    size_t ps_arena_used;
    unsigned ps_count,last_ps;
    direct_ps_entry ps_entries[DIRECT_PS_SLOTS];
    uint16_t ps_index[DIRECT_PS_INDEX_SLOTS];
    uint8_t scratch_ps[PIXEL_BYTES];
    void *gpu_vs_arena;
    size_t gpu_vs_arena_used;
    unsigned gpu_vs_count;
    direct_gpu_vs_entry *gpu_vs_entries;
    const direct_gpu_vs_entry *last_gpu_vs;
    uint32_t last_vs_code[CTR_SHADER_MAX_CODE],last_vs_descriptors[CTR_SHADER_MAX_DESCRIPTORS];
    uint8_t last_vs_code_defined[CTR_SHADER_MAX_CODE],last_vs_descriptors_defined[CTR_SHADER_MAX_DESCRIPTORS];
    void *gpu_fetch_arena;
    unsigned gpu_fetch_count;
    direct_gpu_fetch_entry *gpu_fetch_entries;
    bool resident_depth_valid;
    uint32_t resident_depth_address;
    unsigned resident_depth_width,resident_depth_height,resident_depth_format;
    const GX2VertexShader *bound_vs;
    const GX2FetchShader *bound_fetch;
    GX2PixelShader bound_ps;
    const void *uploaded_pixel_program;
    uint32_t uploaded_pixel_constants[PIXEL_CONSTANTS][4];
    uint32_t uploaded_vertex_uniforms[CTR_SHADER_UNIFORMS][4];
    bool bound_ps_valid;
    bool list_bound;
} direct_pool_state;

typedef struct {
    GX2ContextState *context;
    GX2ColorBuffer target;
    GX2Surface linear;
    GX2DepthBuffer depth_target;
    GX2Surface depth_linear;
    GX2Surface depth_resolve,stencil_linear;
    GX2ColorBuffer stencil_target;
    GX2Texture blend_snapshot;
    GX2Sampler blend_sampler;
    GX2FetchShader fetch;
    GX2VertexShader vertex;
    GX2PixelShader pixel;
    texture_image texture[3];
    texture_cache_entry *texture_lease[3];
    GX2Sampler sampler[3];
    const GX2Texture *bound_textures[3];
    GX2Sampler bound_samplers[3];
    uint64_t bound_texture_uploads;
    unsigned bound_texture_mask,bound_sampler_mask;
    ctr_gx2_shader *compiled;
    void *vertex_storage,*pixel_storage;
    void *vertex_program,*pixel_program,*fetch_program,*vertices,*draw_uploads;
    unsigned shader_slots;
    unsigned next_upload,used_uploads;
    unsigned width,height,stride;
    bool flip_y;
    unsigned fetch_size;
    size_t size;
    unsigned depth_stride;
    size_t depth_size;
    uint32_t depth_address;
    unsigned depth_format,depth_bytes;
    uint8_t seed_stencil,stencil_written;
    bool seed_far,depth_written;
    bool pending,depth_enabled,buffered,resident,fog_table_valid,upload_batch_unavailable;
    bool draw_context_active,raster_current,merger_current;
    uint32_t raster_registers[9],merger_registers[10];
    uint32_t fog_table[128];
    uint32_t light_tables[3][256];
    unsigned light_table_mask;
    uint32_t pixel_constants[PIXEL_CONSTANTS][4];
    unsigned pixel_uniform_vectors,uploaded_pixel_vectors;
} scene_private;

void *ctr_gx2_allocate(ctr_wiiu_gx2_scene *s,size_t alignment,size_t bytes);
void ctr_gx2_dispose(ctr_wiiu_gx2_scene *s,void *p);
bool ctr_gx2_fail(ctr_wiiu_gx2_scene *s,ctr_wiiu_gx2_scene_result reason);
void ctr_gx2_cache_add(ctr_wiiu_gx2_scene *s,uint64_t *counter,uint64_t amount);
bool ctr_gx2_restore(ctr_wiiu_gx2_scene *s);
bool ctr_gx2_retire(ctr_wiiu_gx2_scene *s,scene_private *p);
void ctr_gx2_release(ctr_wiiu_gx2_scene *s,scene_private *p);
bool ctr_gx2_layout(GX2Surface *surface,unsigned width,unsigned height,GX2TileMode tile,unsigned format);
bool ctr_gx2_depth_layout(GX2Surface *surface,unsigned width,unsigned height,GX2TileMode tile,unsigned format);
unsigned ctr_gx2_target_row(const scene_private *p,unsigned y);
bool ctr_gx2_depth_clear_seed(const ctr_scene_depth_surface *depth,float *value,uint8_t *stencil);
bool ctr_gx2_pixel_shader(scene_private *p,const ctr_tev_program *tev,unsigned mask,bool texture2_coord1,
                          unsigned implicit_lod,unsigned explicit_lod,unsigned filter_mask,
                          unsigned mixed_filters,bool fog,bool transformed,const ctr_scene_light *light,
                          bool depth_plane,pixel_blend_mode blend);
void ctr_gx2_transformed_program(ctr_gx2_shader *compiled,unsigned coordinates,const ctr_scene_light *light,
                                 bool depth_plane,unsigned mip_coordinates);
void ctr_gx2_upload_vertex_program(scene_private *p,const ctr_scene_light *light,bool depth_plane,
                                   unsigned mip_coordinates);
bool ctr_gx2_begin_legacy(void *context,const ctr_scene_surface *initial);
bool ctr_gx2_begin_depth_legacy(void *context,const ctr_scene_surface *initial,const ctr_scene_depth_surface *depth);
bool ctr_gx2_end_legacy(void *context,const ctr_scene_surface *readback);
bool ctr_gx2_end_depth_legacy(void *context,const ctr_scene_surface *readback,const ctr_scene_depth_surface *depth);
bool ctr_gx2_draw_legacy(void *context,const ctr_scene_state *state,const ctr_vertex *vertices,unsigned count,
                         const ctr_scene_texture *texture);
bool ctr_gx2_draw_textures_legacy(void *context,const ctr_scene_state *state,const ctr_vertex *vertices,
                                  unsigned count,const ctr_scene_texture textures[3],unsigned mask);
bool ctr_gx2_draw_mips_legacy(void *context,const ctr_scene_state *state,const ctr_vertex *vertices,
                              unsigned count,const ctr_scene_sampler samplers[3],unsigned mask);
bool ctr_gx2_draw_transformed_legacy(void *context,const ctr_scene_state *state,
                                     const ctr_scene_transformed_vertex *vertices,unsigned count,
                                     const ctr_scene_sampler samplers[3],unsigned mask,const ctr_scene_light *light);
void ctr_gx2_abort_cb(void *context);

void ctr_gx2_direct_pool_destroy(ctr_wiiu_gx2_scene *s);
bool ctr_gx2_direct_begin_targets(ctr_wiiu_gx2_scene *s,const ctr_scene_surface *initial,
                                  const ctr_scene_depth_surface *depth,bool reuse_pending,bool deferred);
bool ctr_gx2_begin_deferred_color(void *context,const ctr_scene_surface *initial,
                                  const ctr_scene_depth_surface *depth,bool reuse_pending);
bool ctr_gx2_end_deferred_color(void *context);
bool ctr_gx2_resolve_deferred_color(void *context,const ctr_scene_surface *readback);
extern const ctr_scene_scanout ctr_gx2_scanout;
bool ctr_gx2_direct_pixel_program(ctr_wiiu_gx2_scene *s,scene_private *p,const ctr_tev_program *tev,
                                  unsigned mask,bool texture2_coord1,unsigned implicit_lod,
                                  unsigned explicit_lod,unsigned filter_mask,unsigned mixed_filters,
                                  bool fog,bool transformed,const ctr_scene_light *light,
                                  bool depth_plane,pixel_blend_mode blend,const uint8_t *key,size_t key_size);
void ctr_gx2_direct_prepare_draw(ctr_wiiu_gx2_scene *s,scene_private *p,unsigned direct_vs_id,
                                 unsigned coordinates,const ctr_scene_light *light);
void ctr_gx2_direct_bind_pipeline(scene_private *p,direct_pool_state *pool,unsigned direct_vs_id);
bool ctr_gx2_direct_setup_state(ctr_wiiu_gx2_scene *s,scene_private *p,const ctr_scene_state *state,
                                const ctr_scene_sampler sources[3],unsigned mask,const ctr_scene_light *light,
                                const ctr_texture_cache_lease (*leases)[CTR_SCENE_MAX_MIP_LEVELS],
                                const ctr_tev_program *validated_tev,bool *skip_draw);
bool ctr_gx2_draw_gpu_legacy(void *context,const ctr_scene_gpu_draw *draw);
#endif
