#include "gx2_scene.h"
#include "gx2_mock.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <malloc.h>
#endif

static unsigned alloc_attempts,fail_allocation,live_allocations,fetch_bytes=512;
static bool wait_ok=true,compile_ok=true,layout_ok=true;
static GX2ContextState native_context,*active_context;
static const GX2ColorBuffer *bound_target;
static const ctr_shader_program *expected_shader;
static const ctr_vertex *expected_vertices;
static unsigned expected_count,gpu_draws,read_invalidates,uniform_uploads;
static unsigned context_switches;
static unsigned wait_calls,fail_wait_call;
static unsigned scissor[4],rectangles[4][4],rectangle_count;
static float half_x,half_y,offset_x,offset_y;
static bool cull_front,cull_back;

void *ctr_gx2_test_memalign(size_t alignment,size_t bytes) {
    ++alloc_attempts;
    if(alloc_attempts==fail_allocation)return NULL;
    size_t rounded=(bytes+alignment-1u)&~(alignment-1u);
#ifdef _WIN32
    void *p=_aligned_malloc(rounded,alignment);
#else
    void *p=aligned_alloc(alignment,rounded);
#endif
    assert(p);++live_allocations;return p;
}
void ctr_gx2_test_free(void *p) {
    if(p) {
        assert(live_allocations);--live_allocations;
#ifdef _WIN32
        _aligned_free(p);
#else
        free(p);
#endif
    }
}
void GX2SetContextState(GX2ContextState *context) { assert(context);active_context=context;++context_switches; }
bool GX2DrawDone(void) { ++wait_calls;return wait_ok && wait_calls!=fail_wait_call; }
void GX2CalcSurfaceSizeAndAlignment(GX2Surface *surface) {
    assert(surface->depth==1 && surface->mipLevels==1);
    surface->pitch=surface->width+8u;surface->alignment=256;
    surface->imageSize=surface->pitch*surface->height*4u;
    if(!layout_ok)surface->imageSize=0;
}
void GX2CopySurface(const GX2Surface *src,unsigned a,unsigned b,GX2Surface *dst,unsigned c,unsigned d) {
    assert(!a && !b && !c && !d && src->width==dst->width && src->height==dst->height);
    assert(src->image && dst->image && active_context!=&native_context);
    for(unsigned y=0;y<src->height;++y)
        memcpy((uint8_t *)dst->image+y*dst->pitch*4u,(uint8_t *)src->image+y*src->pitch*4u,src->width*4u);
}
void DCFlushRange(void *bytes,unsigned size) { assert(bytes && size); }
void DCInvalidateRange(void *bytes,unsigned size) { assert(bytes && size);++read_invalidates; }
void GX2Invalidate(unsigned mode,void *bytes,unsigned size) { assert(mode && bytes && size); }
unsigned GX2CalcFetchShaderSizeEx(unsigned count,unsigned type,unsigned tess) {
    assert(count==16 && !type && !tess);return fetch_bytes;
}
void GX2InitFetchShaderEx(GX2FetchShader *fetch,void *bytes,unsigned count,const GX2AttribStream *attributes,unsigned type,unsigned tess) {
    assert(fetch && bytes && count==16 && !type && !tess);
    assert(((uintptr_t)bytes&255u)==0u);
    for(unsigned i=0;i<16;++i) {
        assert(attributes[i].location==i && attributes[i].buffer==0 && attributes[i].offset==i*16u);
        assert(attributes[i].format==GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32);
        assert(attributes[i].endianSwap==GX2_ENDIAN_SWAP_8_IN_32);
        assert(attributes[i].mask==UINT32_C(0x00010203));
    }
}
void GX2InitViewportReg(GX2ViewportReg *v,float x,float y,float w,float h,float n,float f) {
    assert(x==0 && y==0 && w==16 && h==16 && n==0 && f==1);memset(v,0,sizeof(*v));
}
void GX2SetViewportReg(const GX2ViewportReg *v) {
    memcpy(&half_x,&v->pa_cl_vport_xscale,4);memcpy(&half_y,&v->pa_cl_vport_yscale,4);
    memcpy(&offset_x,&v->pa_cl_vport_xoffset,4);memcpy(&offset_y,&v->pa_cl_vport_yoffset,4);
}
void GX2SetRasterizerClipControlEx(bool raster,bool clip,bool half) { assert(raster && clip && !half); }
void GX2SetScissor(unsigned x,unsigned y,unsigned w,unsigned h) {
    assert(x<16 && y<16 && w && h && x+w<=16 && y+h<=16);
    scissor[0]=x;scissor[1]=y;scissor[2]=w;scissor[3]=h;
    assert(rectangle_count<4);memcpy(rectangles[rectangle_count++],scissor,sizeof(scissor));
}
void GX2DrawEx(unsigned type,unsigned count,unsigned first,unsigned instances) {
    assert(type==GX2_PRIMITIVE_MODE_TRIANGLES && count==expected_count && !first && instances==1);
    assert(bound_target && active_context!=&native_context);++gpu_draws;
    /* A visible GPU-side model mutation lets readback tests distinguish the
     * rendered target from a stale or substituted CPU reference buffer. */
    if(!scissor[0] && !scissor[1]) {
        uint8_t *p=bound_target->surface.image;p[0]=17;p[1]=33;p[2]=49;p[3]=255;
    }
}
void GX2SetColorBuffer(const GX2ColorBuffer *target,unsigned index) { assert(!index);bound_target=target; }
void GX2SetShaderModeEx(unsigned mode,unsigned vs,unsigned vsstack,unsigned gs,unsigned gsstack,unsigned ps,unsigned psstack) {
    assert(!mode && vs==64 && vsstack==64 && !gs && !gsstack && ps==184 && psstack==192);
}
void GX2SetVertexShader(const GX2VertexShader *shader) {
    assert(shader->size==256 && ((uintptr_t)shader->program&255u)==0);
    assert(shader->regs.sq_pgm_resources_vs==0x135 && shader->regs.num_sq_vtx_semantic==16);
    assert(shader->regs.sq_vtx_semantic_clear==0xffff0000);
    assert(shader->regs.spi_vs_out_id[0]==0xffffff00);
    for(unsigned i=0;i<16;++i)assert(shader->regs.sq_vtx_semantic[i]==i);
    assert(((uint8_t *)shader->program)[0]==0x12);
}
void GX2SetPixelShader(const GX2PixelShader *shader) {
    const uint8_t bytes[8]={0,0,0,0,0x88,0x06,0x20,0x94};
    assert(shader->size==256 && !memcmp(bytes,shader->program,8));
    assert(shader->regs.sq_pgm_resources_ps==1 && shader->regs.spi_ps_input_cntls[0]==0x100);
}
void GX2SetAttribBuffer(unsigned index,unsigned size,unsigned stride,const void *bytes) {
    assert(!index && stride==256 && size==expected_count*256u);
    for(unsigned vertex=0;vertex<expected_count;++vertex)
        assert(!memcmp((const uint8_t *)bytes+vertex*256u,expected_vertices[vertex].input,256));
}
void GX2SetVertexUniformReg(unsigned offset,unsigned count,const void *bytes) {
    assert(offset==8 && count==4 && !memcmp(bytes,expected_shader->uniforms[2],16));++uniform_uploads;
}
void GX2SetCullOnlyControl(unsigned front,bool a,bool b) { assert(front==GX2_FRONT_FACE_CCW);cull_front=a;cull_back=b; }
bool ctr_shader_f24_decode(uint32_t raw,uint32_t *value) {
    if(raw!=0x420000)return false;
    *value=0x41000000;return true;
}
ctr_gx2_shader_result ctr_gx2_shader_compile(const ctr_shader_program *program,uint16_t mask,
                                           const uint32_t *maps,unsigned total,ctr_gx2_shader *out) {
    assert(program==expected_shader && mask==3 && total==2 && maps[0]==0x03020100 && maps[1]==0x0b0a0908);
    if(!compile_ok)return CTR_GX2_SHADER_UNSUPPORTED;
    memset(out,0,sizeof(*out));out->program[0]=0x12;out->size=256;out->gprs=53;
    out->input_mask=3;out->uniform_used[2]=15;out->pica_instructions=3;return CTR_GX2_SHADER_OK;
}
static void reset(void) {
    assert(!live_allocations);alloc_attempts=0;fail_allocation=0;fetch_bytes=512;
    wait_ok=true;compile_ok=true;layout_ok=true;active_context=&native_context;bound_target=NULL;
    gpu_draws=0;read_invalidates=0;uniform_uploads=0;rectangle_count=0;context_switches=0;
    wait_calls=0;fail_wait_call=0;
}
static void setup(ctr_scene_state *state,ctr_vertex vertices[3]) {
    memset(state,0,sizeof(*state));memset(vertices,0,3*sizeof(*vertices));
    state->registers[0x041]=state->registers[0x043]=0x420000;
    state->registers[0x04f]=2;state->registers[0x050]=0x03020100;
    state->registers[0x051]=0x0b0a0908;state->registers[0x2bd]=3;
    for(unsigned i=0;i<3;++i) {
        vertices[i].enabled=3;vertices[i].input[0][0]=(float)i*0.25f;
        vertices[i].input[0][1]=-0.5f;vertices[i].input[0][2]=-1.0f;
        vertices[i].input[0][3]=1.0f;vertices[i].input[1][3]=1.0f;
    }
    for(unsigned i=0;i<4;++i)state->shader.uniforms[2][i]=0x3f800000;
    expected_shader=&state->shader;expected_vertices=vertices;expected_count=3;
}
static void clean(const ctr_wiiu_gx2_scene *s) {
    assert(!s->private_state && !s->quarantined && !live_allocations);
    assert(s->allocations==s->releases && active_context==&native_context);
}
static void baseline(void) {
    reset();ctr_wiiu_gx2_scene scene;ctr_wiiu_gx2_scene_init(&scene,&native_context);
    ctr_scene_backend backend=ctr_wiiu_gx2_scene_backend(&scene);
    uint8_t original[1161],output[1161];
    for(unsigned i=0;i<sizeof(original);++i)original[i]=(uint8_t)(i*37u);
    memcpy(output,original,sizeof(output));
    ctr_scene_surface source={original,sizeof(original),16,16,72},target={output,sizeof(output),16,16,72};
    ctr_scene_state state;ctr_vertex vertices[3];setup(&state,vertices);
    assert(backend.begin(backend.context,&source));assert(active_context==&native_context);
    assert(backend.draw(backend.context,&state,vertices,3));assert(active_context==&native_context);
    assert(half_x==8 && half_y==8 && offset_x==8 && offset_y==8);
    assert(!cull_front && !cull_back && gpu_draws==1 && uniform_uploads==1);
    assert(backend.end(backend.context,&target));clean(&scene);
    assert(output[0]==17 && output[1]==33 && output[2]==49 && output[3]==255);
    assert(!memcmp(output+4,original+4,sizeof(output)-4));
    assert(scene.begins==1 && scene.draws==1 && scene.submissions==1 && scene.readbacks==1);
    assert(scene.shader_instructions==3 && read_invalidates==1);
    unsigned switches=context_switches;backend.abort(backend.context);clean(&scene);
    assert(context_switches==switches);
}
static void allocation_failures(void) {
    uint8_t bytes[1024]={0};ctr_scene_surface surface={bytes,sizeof(bytes),16,16,64};
    for(unsigned failure=1;failure<=9;++failure) {
        reset();fail_allocation=failure;ctr_wiiu_gx2_scene scene;ctr_wiiu_gx2_scene_init(&scene,&native_context);
        ctr_scene_backend backend=ctr_wiiu_gx2_scene_backend(&scene);
        assert(!backend.begin(backend.context,&surface));
        assert(scene.result==CTR_WIIU_GX2_SCENE_ALLOCATION);clean(&scene);
        backend.abort(backend.context);clean(&scene);
    }
    reset();fetch_bytes=4097;ctr_wiiu_gx2_scene scene;ctr_wiiu_gx2_scene_init(&scene,&native_context);
    ctr_scene_backend backend=ctr_wiiu_gx2_scene_backend(&scene);
    assert(!backend.begin(backend.context,&surface) && scene.result==CTR_WIIU_GX2_SCENE_LAYOUT);clean(&scene);
    reset();layout_ok=false;ctr_wiiu_gx2_scene_init(&scene,&native_context);backend=ctr_wiiu_gx2_scene_backend(&scene);
    assert(!backend.begin(backend.context,&surface) && scene.result==CTR_WIIU_GX2_SCENE_LAYOUT);clean(&scene);
}
static void scissor_modes(void) {
    uint8_t bytes[1024]={0};ctr_scene_surface surface={bytes,sizeof(bytes),16,16,64};
    for(unsigned mode=0;mode<4;++mode)if(mode!=2) {
        reset();ctr_wiiu_gx2_scene scene;ctr_wiiu_gx2_scene_init(&scene,&native_context);
        ctr_scene_backend backend=ctr_wiiu_gx2_scene_backend(&scene);
        ctr_scene_state state;ctr_vertex vertices[3];setup(&state,vertices);
        state.registers[0x065]=mode;state.registers[0x066]=4u|(3u<<16);state.registers[0x067]=9u|(11u<<16);
        state.registers[0x040]=mode==3u?2u:1u;
        assert(backend.begin(backend.context,&surface));assert(backend.draw(backend.context,&state,vertices,3));
        assert(rectangle_count==(mode==1u?4u:1u));assert(cull_front==(mode!=3u) && cull_back==(mode==3u));
        /* Inside/exclusion partitions must cover each accepted pixel exactly
         * once and never submit the same pixel twice across rectangles. */
        for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x) {
            unsigned hits=0;
            for(unsigned r=0;r<rectangle_count;++r)
                if(x>=rectangles[r][0] && x<rectangles[r][0]+rectangles[r][2] &&
                   y>=rectangles[r][1] && y<rectangles[r][1]+rectangles[r][3])++hits;
            bool inside=x>=4 && x<=9 && y>=3 && y<=11;
            assert(hits==(unsigned)(mode==0 || (mode==3 && inside) || (mode==1 && !inside)));
        }
        backend.abort(backend.context);clean(&scene);
    }
}
static void failures(void) {
    uint8_t bytes[1024]={0};ctr_scene_surface surface={bytes,sizeof(bytes),16,16,64};
    ctr_scene_state state;ctr_vertex vertices[3];setup(&state,vertices);
    reset();ctr_wiiu_gx2_scene scene;ctr_wiiu_gx2_scene_init(&scene,&native_context);
    ctr_scene_backend backend=ctr_wiiu_gx2_scene_backend(&scene);
    assert(backend.begin(backend.context,&surface));compile_ok=false;
    assert(!backend.draw(backend.context,&state,vertices,3));assert(!gpu_draws && scene.result==CTR_WIIU_GX2_SCENE_SHADER);
    backend.abort(backend.context);clean(&scene);
    reset();ctr_wiiu_gx2_scene_init(&scene,&native_context);backend=ctr_wiiu_gx2_scene_backend(&scene);
    assert(backend.begin(backend.context,&surface));vertices[0].enabled=1;
    assert(!backend.draw(backend.context,&state,vertices,3));assert(!gpu_draws);
    backend.abort(backend.context);clean(&scene);vertices[0].enabled=3;
    reset();wait_ok=false;ctr_wiiu_gx2_scene_init(&scene,&native_context);backend=ctr_wiiu_gx2_scene_backend(&scene);
    assert(!backend.begin(backend.context,&surface));assert(scene.quarantined && scene.private_state && live_allocations);
    assert(scene.result==CTR_WIIU_GX2_SCENE_WAIT && active_context==&native_context);
    backend.abort(backend.context);assert(scene.private_state && scene.quarantined);
    assert(!backend.begin(backend.context,&surface) && scene.result==CTR_WIIU_GX2_SCENE_WAIT);
    unsigned switches=context_switches;ctr_wiiu_gx2_scene_after_shutdown(&scene);clean(&scene);
    assert(!scene.restore_context && context_switches==switches);
    assert(!backend.begin(backend.context,&surface) && context_switches==switches);
    reset();ctr_wiiu_gx2_scene_init(&scene,&native_context);backend=ctr_wiiu_gx2_scene_backend(&scene);
    assert(backend.begin(backend.context,&surface));wait_ok=false;
    assert(!backend.draw(backend.context,&state,vertices,3));assert(scene.quarantined && scene.draws==0 && scene.submissions==1);
    wait_ok=true;backend.abort(backend.context);clean(&scene);
    reset();ctr_wiiu_gx2_scene_init(&scene,&native_context);backend=ctr_wiiu_gx2_scene_backend(&scene);
    assert(backend.begin(backend.context,&surface));wait_ok=false;
    assert(!backend.end(backend.context,&surface) && scene.readbacks==0 && scene.quarantined);
    wait_ok=true;backend.abort(backend.context);clean(&scene);
}
static unsigned observed_readbacks;
static uint8_t *expected_readback;
static void observe(void *context,const ctr_scene_surface *readback) {
    assert(context==&observed_readbacks && readback->bytes==expected_readback);
    assert(readback->width==16 && readback->height==16 && readback->stride==72 && readback->size==1161);
    const uint8_t *pixels=readback->bytes;
    assert(pixels[0]==17 && pixels[1]==33 && pixels[2]==49 && pixels[3]==255);
    assert(pixels[64]==0x55 && pixels[1160]==0x55); /* Borrowed padding is preserved. */
    ++observed_readbacks;
}
static void readback_observer(void) {
    for(unsigned failure=0;failure<3;++failure) {
        reset();observed_readbacks=0;
        uint8_t bytes[1161];memset(bytes,0x55,sizeof(bytes));expected_readback=bytes;
        ctr_scene_surface surface={bytes,sizeof(bytes),16,16,72};
        ctr_scene_state state;ctr_vertex vertices[3];setup(&state,vertices);
        ctr_wiiu_gx2_scene scene;ctr_wiiu_gx2_scene_init(&scene,&native_context);
        scene.observe_readback=observe;scene.readback_context=&observed_readbacks;
        ctr_scene_backend backend=ctr_wiiu_gx2_scene_backend(&scene);
        assert(backend.begin(backend.context,&surface));
        assert(backend.draw(backend.context,&state,vertices,3));
        /* First end wait retires the copy; the next retires context restore. */
        if(failure)fail_wait_call=wait_calls+failure;
        assert(backend.end(backend.context,&surface)==(failure==0));
        assert(observed_readbacks==(failure==1?0u:1u));
        backend.abort(backend.context);clean(&scene);
    }
}
int main(void) {
    baseline();allocation_failures();scissor_modes();failures();readback_observer();
    puts("GX2 native API model: lifetime, failure quarantine, raw input upload, shader binding, scissor partition and readback passed");
    return 0;
}
