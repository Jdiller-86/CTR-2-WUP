#include "diagnostic_display.h"
#include "wut_mock.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

wut_mock_state g_wut_mock = {0};

static GX2ContextState mock_ctx = {42};
static GX2ColorBuffer mock_tv = {.surface = {.width = 1280, .height = 720, .format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8}};
static GX2ColorBuffer mock_drc = {.surface = {.width = 854, .height = 480, .format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8}};

void DCFlushRange(const void *addr, uint32_t size) {
    (void)addr;
    (void)size;
}

void *MEMAllocFromDefaultHeapEx(uint32_t size, int align) {
    (void)align;
    ++g_wut_mock.alloc_calls;
    if (g_wut_mock.fail_alloc_at && g_wut_mock.alloc_calls == g_wut_mock.fail_alloc_at) {
        return NULL;
    }
    void *p = calloc(1, size ? size : 1);
    if (p) {
        ++g_wut_mock.live_allocs;
    }
    return p;
}

void MEMFreeToDefaultHeap(void *ptr) {
    if (!ptr) return;
    assert(g_wut_mock.live_allocs > 0);
    --g_wut_mock.live_allocs;
    free(ptr);
}

void GX2Invalidate(int mode, void *buf, uint32_t size) {
    (void)mode;
    (void)buf;
    (void)size;
}

void GX2CalcSurfaceSizeAndAlignment(GX2Surface *surface) {
    surface->pitch = surface->width;
    surface->alignment = 256;
    surface->imageSize = surface->pitch * surface->height * 4u;
}

void GX2InitTextureRegs(GX2Texture *texture) {
    (void)texture;
}

OSTime GX2GetRetiredTimeStamp(void) {
    return g_wut_mock.retired_ts;
}

OSTime GX2GetLastSubmittedTimeStamp(void) {
    return ++g_wut_mock.submitted_ts;
}

bool GX2WaitTimeStamp(OSTime ts) {
    ++g_wut_mock.wait_ts_calls;
    g_wut_mock.retired_ts = ts;
    return true;
}

void GX2DrawDone(void) {
    ++g_wut_mock.draw_done_calls;
    g_wut_mock.retired_ts = g_wut_mock.submitted_ts;
}

void GX2SetSwapInterval(uint32_t interval) {
    (void)interval;
}

uint32_t GX2CalcFetchShaderSizeEx(uint32_t num_attrib, int fetch_mode, int tess_mode) {
    (void)num_attrib;
    (void)fetch_mode;
    (void)tess_mode;
    return 128;
}

void GX2InitFetchShaderEx(GX2FetchShader *fs, void *buf, uint32_t count, const GX2AttribStream *attribs, int fetch_mode, int tess_mode) {
    (void)buf;
    (void)count;
    (void)attribs;
    (void)fetch_mode;
    (void)tess_mode;
    fs->size = 128;
}

void GX2InitSampler(GX2Sampler *sampler, int clamp, int filter) {
    (void)sampler;
    (void)clamp;
    (void)filter;
}

void GX2SetShaderModeEx(int mode, uint32_t num_vs_gpr, uint32_t num_vs_stack, uint32_t num_gs_gpr, uint32_t num_gs_stack, uint32_t num_ps_gpr, uint32_t num_ps_stack) {
    (void)mode; (void)num_vs_gpr; (void)num_vs_stack; (void)num_gs_gpr;
    (void)num_gs_stack; (void)num_ps_gpr; (void)num_ps_stack;
}

void GX2SetViewport(float x, float y, float w, float h, float near_z, float far_z) {
    (void)x; (void)y; (void)w; (void)h; (void)near_z; (void)far_z;
}

void GX2SetScissor(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    (void)x; (void)y; (void)w; (void)h;
}

void GX2SetDepthStencilControl(bool depth_test, bool depth_write, int depth_func, bool stencil_test, bool backface_stencil, int a, int b, int c, int d, int e, int f, int g, int h) {
    (void)depth_test; (void)depth_write; (void)depth_func; (void)stencil_test;
    (void)backface_stencil; (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; (void)h;
}

void GX2SetColorControl(int logic_op, uint8_t blend_enable, bool alpha_to_mask, bool color_buffer_enable) {
    (void)logic_op; (void)blend_enable; (void)alpha_to_mask; (void)color_buffer_enable;
}

void GX2SetBlendControl(int target, int src_rgb, int dst_rgb, int op_rgb, bool sep_alpha, int src_a, int dst_a, int op_a) {
    (void)target; (void)src_rgb; (void)dst_rgb; (void)op_rgb;
    (void)sep_alpha; (void)src_a; (void)dst_a; (void)op_a;
}

void GX2SetTargetChannelMasks(int m0, int m1, int m2, int m3, int m4, int m5, int m6, int m7) {
    (void)m0; (void)m1; (void)m2; (void)m3; (void)m4; (void)m5; (void)m6; (void)m7;
}

void GX2SetAlphaTest(bool enable, int func, float ref) {
    (void)enable; (void)func; (void)ref;
}

void GX2SetCullOnlyControl(int front_face, bool cull_front, bool cull_back) {
    (void)front_face; (void)cull_front; (void)cull_back;
}

void GX2SetFetchShader(const GX2FetchShader *fs) { (void)fs; }
void GX2SetVertexShader(const GX2VertexShader *vs) { (void)vs; }
void GX2SetPixelShader(const GX2PixelShader *ps) { (void)ps; }
void GX2InitColorBufferRegs(GX2ColorBuffer *buffer) { (void)buffer; }
void GX2SetColorBuffer(const GX2ColorBuffer *buffer, int target) { (void)buffer; (void)target; }
void GX2SetPixelUniformReg(uint32_t offset, uint32_t count, const void *data) {
    (void)offset; (void)count; (void)data;
}
void GX2SetPixelTexture(const GX2Texture *tex, uint32_t unit) { (void)tex; (void)unit; }
void GX2SetPixelSampler(const GX2Sampler *sampler, uint32_t unit) { (void)sampler; (void)unit; }
void GX2SetAttribBuffer(uint32_t idx, uint32_t size, uint32_t stride, const void *buf) {
    (void)idx; (void)size; (void)stride; (void)buf;
}

void GX2DrawEx(int mode, uint32_t count, uint32_t first, uint32_t instances) {
    (void)mode; (void)count; (void)first; (void)instances;
    ++g_wut_mock.draw_calls;
}

bool WHBGfxInit(void) {
    if (g_wut_mock.whb_init_fail) return false;
    g_wut_mock.whb_initialized = true;
    return true;
}

void WHBGfxShutdown(void) {
    g_wut_mock.whb_initialized = false;
}

GX2ColorBuffer *WHBGfxGetTVColourBuffer(void) { return &mock_tv; }
GX2ColorBuffer *WHBGfxGetDRCColourBuffer(void) { return &mock_drc; }
GX2ContextState *WHBGfxGetTVContextState(void) { return &mock_ctx; }
void WHBGfxBeginRender(void) {}
void WHBGfxBeginRenderTV(void) { ++g_wut_mock.tv_renders; }
void WHBGfxFinishRenderTV(void) {}
void WHBGfxBeginRenderDRC(void) { ++g_wut_mock.drc_renders; }
void WHBGfxFinishRenderDRC(void) {}
void WHBGfxFinishRender(void) {}
void WHBGfxClearColor(float r, float g, float b, float a) { (void)r; (void)g; (void)b; (void)a; }

static void reset_mock(void) {
    memset(&g_wut_mock, 0, sizeof(g_wut_mock));
}

int main(void) {
    /* 1. WHBGfxInit failure */
    reset_mock();
    g_wut_mock.whb_init_fail = true;
    assert(!ctr_diagnostic_display_init());
    assert(ctr_diagnostic_display_context() == NULL);
    assert(g_wut_mock.live_allocs == 0);

    /* 2. Partial-init allocation failure + cleanup at every alloc point */
    for (unsigned fail_idx = 1; fail_idx <= 13; ++fail_idx) {
        reset_mock();
        g_wut_mock.fail_alloc_at = fail_idx;
        assert(!ctr_diagnostic_display_init());
        assert(!ctr_diagnostic_display_draw());
        ctr_diagnostic_display_close(true);
        assert(g_wut_mock.live_allocs == 0);
        assert(!g_wut_mock.whb_initialized);
    }

    /* 3. Full lifecycle: init -> draw -> guest -> touch -> invalidate -> close */
    reset_mock();
    assert(ctr_diagnostic_display_init());
    assert(ctr_diagnostic_display_init()); /* idempotent when ready */
    assert(ctr_diagnostic_display_context() == &mock_ctx);

    ctr_diagnostic_display_labels("TEST TITLE", "TEST FOOTER");
    assert(ctr_diagnostic_display_row(0, "row_zero_underscore"));
    assert(!ctr_diagnostic_display_row(99, "out_of_range"));
    ctr_diagnostic_display_line("line_one");

    assert(ctr_diagnostic_display_draw());
    assert(g_wut_mock.draw_calls == 2);
    assert(g_wut_mock.tv_renders == 1);
    assert(g_wut_mock.drc_renders == 1);

    /* Second draw without invalidate skips re-upload and re-presents cleanly */
    assert(ctr_diagnostic_display_draw());
    assert(g_wut_mock.draw_calls == 2);

    ctr_diagnostic_display_invalidate();
    assert(ctr_diagnostic_display_draw());
    assert(g_wut_mock.draw_calls == 4);

    static uint32_t top[400 * 240];
    static uint32_t bottom[320 * 240];
    top[0] = 0xff0000ffu;
    bottom[0] = 0x00ff00ffu;
    assert(ctr_diagnostic_display_guest(top, bottom));
    unsigned draws = g_wut_mock.draw_calls;
    assert(ctr_diagnostic_display_guest(top, bottom));
    assert(g_wut_mock.draw_calls == draws);
    ctr_diagnostic_display_invalidate();
    assert(ctr_diagnostic_display_guest(top, bottom));
    assert(g_wut_mock.draw_calls == draws + 2);
    top[0] = 0x0000ffffu;
    assert(ctr_diagnostic_display_guest(top, bottom));
    /* Third guest upload wraps back to slot 0 which has submitted_ts > retired_ts */
    top[0] = 0xffffffffu;
    assert(ctr_diagnostic_display_guest(top, bottom));
    assert(g_wut_mock.wait_ts_calls >= 1);

    uint16_t gx = 0, gy = 0;
    /* DRC is 854x480; 320x240 fits at 640x480 centered at x=[107..747), mapped in 1280x720 touch space */
    assert(!ctr_diagnostic_display_touch(0, 0, &gx, &gy));
    assert(ctr_diagnostic_display_touch(640, 360, &gx, &gy));
    assert(gx >= 158 && gx <= 161);
    assert(gy >= 118 && gy <= 121);

    /* Transition back from guest_mode to diagnostic draw */
    assert(ctr_diagnostic_display_draw());

    ctr_diagnostic_display_close(true);
    assert(g_wut_mock.live_allocs == 0);
    assert(g_wut_mock.draw_done_calls >= 1);
    assert(!g_wut_mock.whb_initialized);
    assert(ctr_diagnostic_display_context() == NULL);

    printf("cemu-display: all assertions passed\n");
    return 0;
}
