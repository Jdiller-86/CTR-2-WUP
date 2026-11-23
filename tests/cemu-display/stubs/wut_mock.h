#ifndef CTR2WUP_CEMU_DISPLAY_WUT_MOCK_H
#define CTR2WUP_CEMU_DISPLAY_WUT_MOCK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint64_t OSTime;

typedef struct GX2ContextState {
    int dummy;
} GX2ContextState;

typedef struct GX2Surface {
    int dim;
    unsigned width;
    unsigned height;
    unsigned depth;
    unsigned mipLevels;
    int format;
    int aa;
    int use;
    int tileMode;
    uint32_t imageSize;
    uint32_t alignment;
    uint32_t pitch;
    void *image;
} GX2Surface;

typedef struct GX2ColorBuffer {
    GX2Surface surface;
    unsigned viewNumSlices;
} GX2ColorBuffer;

typedef struct GX2Texture {
    GX2Surface surface;
    unsigned viewNumMips;
    unsigned viewNumSlices;
    uint32_t compMap;
} GX2Texture;

typedef struct GX2VertexShader {
    uint32_t size;
    void *program;
    int mode;
    struct {
        uint32_t sq_pgm_resources_vs;
        uint32_t spi_vs_out_config;
        uint32_t num_spi_vs_out_id;
        uint32_t spi_vs_out_id[10];
        uint32_t sq_vtx_semantic_clear;
        uint32_t num_sq_vtx_semantic;
        uint32_t sq_vtx_semantic[32];
        uint32_t vgt_vertex_reuse_block_cntl;
        uint32_t vgt_hos_reuse_depth;
    } regs;
} GX2VertexShader;

typedef struct GX2PixelShader {
    uint32_t size;
    void *program;
    int mode;
    struct {
        uint32_t sq_pgm_resources_ps;
        uint32_t sq_pgm_exports_ps;
        uint32_t spi_ps_in_control_0;
        uint32_t num_spi_ps_input_cntl;
        uint32_t spi_ps_input_cntls[32];
        uint32_t cb_shader_mask;
        uint32_t cb_shader_control;
        uint32_t db_shader_control;
    } regs;
} GX2PixelShader;

typedef struct GX2FetchShader {
    uint32_t size;
} GX2FetchShader;

typedef struct GX2Sampler {
    uint32_t dummy;
} GX2Sampler;

typedef struct GX2AttribStream {
    uint32_t location;
    uint32_t buffer;
    uint32_t offset;
    int format;
    int type;
    uint32_t mask;
    int endianSwap;
} GX2AttribStream;

enum {
    GX2_SURFACE_DIM_TEXTURE_2D = 1,
    GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8 = 0x1a,
    GX2_AA_MODE1X = 0,
    GX2_SURFACE_USE_TEXTURE = 1,
    GX2_SURFACE_USE_COLOR_BUFFER = 2,
    GX2_TILE_MODE_LINEAR_ALIGNED = 1,
    GX2_TILE_MODE_TILED_2D_THIN1 = 4,
    GX2_SQ_SEL_X = 0,
    GX2_SQ_SEL_Y = 1,
    GX2_SQ_SEL_Z = 2,
    GX2_SQ_SEL_W = 3,
    GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER = 1,
    GX2_INVALIDATE_MODE_CPU_TEXTURE = 2,
    GX2_INVALIDATE_MODE_CPU_SHADER = 4,
    GX2_INVALIDATE_MODE_COLOR_BUFFER = 16,
    GX2_SHADER_MODE_UNIFORM_REGISTER = 1,
    GX2_COMPARE_FUNC_ALWAYS = 7,
    GX2_LOGIC_OP_COPY = 0xcc,
    GX2_RENDER_TARGET_0 = 0,
    GX2_BLEND_MODE_ONE = 1,
    GX2_BLEND_MODE_ZERO = 0,
    GX2_BLEND_COMBINE_MODE_ADD = 0,
    GX2_CHANNEL_MASK_RGBA = 0xf,
    GX2_FRONT_FACE_CCW = 0,
    GX2_PRIMITIVE_MODE_TRIANGLES = 4,
    GX2_FETCH_SHADER_TESSELLATION_NONE = 0,
    GX2_TESSELLATION_MODE_DISCRETE = 0,
    GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32 = 0x80f,
    GX2_ATTRIB_INDEX_PER_VERTEX = 0,
    GX2_ENDIAN_SWAP_8_IN_32 = 2,
    GX2_TEX_CLAMP_MODE_CLAMP = 2,
    GX2_TEX_XY_FILTER_MODE_POINT = 0,
    GX2_TEX_XY_FILTER_MODE_LINEAR = 1
};

#define GX2_COMP_MAP(x, y, z, w) \
    (((uint32_t)(x) << 24) | ((uint32_t)(y) << 16) | ((uint32_t)(z) << 8) | (uint32_t)(w))
#define GX2_SEL_MASK(x, y, z, w) GX2_COMP_MAP(x, y, z, w)

typedef struct {
    unsigned fail_alloc_at;
    unsigned alloc_calls;
    unsigned live_allocs;
    bool whb_init_fail;
    bool whb_initialized;
    unsigned draw_calls;
    unsigned tv_renders;
    unsigned drc_renders;
    unsigned wait_ts_calls;
    unsigned draw_done_calls;
    OSTime submitted_ts;
    OSTime retired_ts;
} wut_mock_state;

extern wut_mock_state g_wut_mock;

void DCFlushRange(const void *addr, uint32_t size);
void *MEMAllocFromDefaultHeapEx(uint32_t size, int align);
void MEMFreeToDefaultHeap(void *ptr);

void GX2Invalidate(int mode, void *buf, uint32_t size);
void GX2CalcSurfaceSizeAndAlignment(GX2Surface *surface);
void GX2InitTextureRegs(GX2Texture *texture);
void GX2InitColorBufferRegs(GX2ColorBuffer *buffer);
void GX2SetColorBuffer(const GX2ColorBuffer *buffer, int target);
void GX2SetPixelUniformReg(uint32_t offset, uint32_t count, const void *data);
OSTime GX2GetRetiredTimeStamp(void);
OSTime GX2GetLastSubmittedTimeStamp(void);
bool GX2WaitTimeStamp(OSTime ts);
void GX2DrawDone(void);
void GX2SetSwapInterval(uint32_t interval);
uint32_t GX2CalcFetchShaderSizeEx(uint32_t num_attrib, int fetch_mode, int tess_mode);
void GX2InitFetchShaderEx(GX2FetchShader *fs, void *buf, uint32_t count, const GX2AttribStream *attribs, int fetch_mode, int tess_mode);
void GX2InitSampler(GX2Sampler *sampler, int clamp, int filter);
void GX2SetShaderModeEx(int mode, uint32_t num_vs_gpr, uint32_t num_vs_stack, uint32_t num_gs_gpr, uint32_t num_gs_stack, uint32_t num_ps_gpr, uint32_t num_ps_stack);
void GX2SetViewport(float x, float y, float w, float h, float near_z, float far_z);
void GX2SetScissor(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
void GX2SetDepthStencilControl(bool depth_test, bool depth_write, int depth_func, bool stencil_test, bool backface_stencil, int a, int b, int c, int d, int e, int f, int g, int h);
void GX2SetColorControl(int logic_op, uint8_t blend_enable, bool alpha_to_mask, bool color_buffer_enable);
void GX2SetBlendControl(int target, int src_rgb, int dst_rgb, int op_rgb, bool sep_alpha, int src_a, int dst_a, int op_a);
void GX2SetTargetChannelMasks(int m0, int m1, int m2, int m3, int m4, int m5, int m6, int m7);
void GX2SetAlphaTest(bool enable, int func, float ref);
void GX2SetCullOnlyControl(int front_face, bool cull_front, bool cull_back);
void GX2SetFetchShader(const GX2FetchShader *fs);
void GX2SetVertexShader(const GX2VertexShader *vs);
void GX2SetPixelShader(const GX2PixelShader *ps);
void GX2SetPixelTexture(const GX2Texture *tex, uint32_t unit);
void GX2SetPixelSampler(const GX2Sampler *sampler, uint32_t unit);
void GX2SetAttribBuffer(uint32_t idx, uint32_t size, uint32_t stride, const void *buf);
void GX2DrawEx(int mode, uint32_t count, uint32_t first, uint32_t instances);

bool WHBGfxInit(void);
void WHBGfxShutdown(void);
GX2ColorBuffer *WHBGfxGetTVColourBuffer(void);
GX2ColorBuffer *WHBGfxGetDRCColourBuffer(void);
GX2ContextState *WHBGfxGetTVContextState(void);
void WHBGfxBeginRender(void);
void WHBGfxBeginRenderTV(void);
void WHBGfxFinishRenderTV(void);
void WHBGfxBeginRenderDRC(void);
void WHBGfxFinishRenderDRC(void);
void WHBGfxFinishRender(void);
void WHBGfxClearColor(float r, float g, float b, float a);

#endif
