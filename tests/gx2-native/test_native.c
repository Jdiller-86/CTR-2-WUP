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
static bool gpu_pending,gpu_stopped,expected_uv;
static GX2ContextState native_context,*active_context;
static const GX2ColorBuffer *bound_target;
static const ctr_shader_program *expected_shader;
static const ctr_vertex *expected_vertices;
static const ctr_scene_texture *expected_texture;
static const ctr_scene_state *expected_state;
static uint32_t bound_pixel_uniforms[32];
static const GX2Texture *bound_texture;
static const GX2Sampler *bound_sampler;
static const uint8_t *bound_pixel_program;
static GX2PixelShader bound_pixel_metadata;
static void *texture_image;
static unsigned texture_inits,texture_binds,sampler_binds,texture_flushes,texture_invalidates;
static unsigned expected_count,gpu_draws,read_invalidates,uniform_uploads;
static unsigned context_switches;
static unsigned wait_calls,fail_wait_call;
static unsigned scissor[4],rectangles[4][4],rectangle_count;
static float half_x,half_y,offset_x,offset_y;
static bool cull_front,cull_back;
static unsigned color_control_sets,channel_mask_sets,alpha_test_sets;
static unsigned blend_control_sets,blend_constant_sets,compile_attempts;
static unsigned captured_logic,captured_write_mask,captured_alpha_function;
static unsigned captured_blend_enable,captured_blend_source[2],captured_blend_destination[2],captured_blend_equation[2];
static float captured_blend_constant[4];
static bool captured_alpha_enabled;
static float captured_alpha_reference;

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
        assert(!gpu_pending || gpu_stopped);
        if(p==texture_image)texture_image=NULL;
        assert(live_allocations);--live_allocations;
#ifdef _WIN32
        _aligned_free(p);
#else
        free(p);
#endif
    }
}
void GX2SetContextState(GX2ContextState *context) {
    assert(context && !gpu_stopped);
    if(context!=&native_context || active_context!=&native_context)gpu_pending=true;
    active_context=context;++context_switches;
}
bool GX2DrawDone(void) {
    assert(!gpu_stopped);++wait_calls;
    bool ok=wait_ok && wait_calls!=fail_wait_call;
    if(ok)gpu_pending=false;
    return ok;
}
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
void DCFlushRange(void *bytes,unsigned size) {
    assert(bytes && size);
    if(bytes==texture_image) { assert(!gpu_pending);++texture_flushes; }
}
void DCInvalidateRange(void *bytes,unsigned size) { assert(bytes && size);++read_invalidates; }
void GX2Invalidate(unsigned mode,void *bytes,unsigned size) {
    assert(mode && bytes && size);
    if(bytes==texture_image) {
        assert(mode==GX2_INVALIDATE_MODE_CPU_TEXTURE && !gpu_pending);
        assert(texture_flushes>texture_invalidates);++texture_invalidates;
    }
}
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
    assert(bound_target && active_context!=&native_context);++gpu_draws;gpu_pending=true;
    assert(!expected_texture || (bound_texture && bound_sampler && bound_pixel_program));
    assert(color_control_sets==uniform_uploads && channel_mask_sets==uniform_uploads &&
        alpha_test_sets==uniform_uploads && blend_control_sets==uniform_uploads &&
        blend_constant_sets==uniform_uploads);
    /* A visible nonrendering marker distinguishes target readback from a stale
     * or substituted reference buffer. It deliberately ignores fragment state;
     * fixed-state calls and numerical comparisons are checked separately. */
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
    assert(shader->regs.sq_pgm_resources_vs==(expected_uv?0x136u:0x135u) && shader->regs.num_sq_vtx_semantic==16);
    assert(shader->regs.sq_vtx_semantic_clear==0xffff0000);
    assert(shader->regs.spi_vs_out_id[0]==(expected_uv?UINT32_C(0xffff0100):UINT32_C(0xffffff00)));
    assert(shader->regs.spi_vs_out_config==(expected_uv?2u:0u));
    for(unsigned i=0;i<16;++i)assert(shader->regs.sq_vtx_semantic[i]==i);
    assert(((uint8_t *)shader->program)[0]==0x12);
}
static uint32_t little32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
/* Independently decode the emitted instruction fields. This is an API model,
 * not a GPU emulator: it checks the exact sampler/TEV program bound to GX2. */
static void check_pixel_program(const uint8_t *program,unsigned size) {
    bool used[8192]={false};unsigned at=0,samples=0;
    for(;at<128u;at+=8u) {
        for(unsigned i=at;i<at+8u;++i) { assert(!used[i]);used[i]=true; }
        uint32_t a=little32(program+at),b=little32(program+at+4u);
        if(b==UINT32_C(0x80800000)) {
            assert(expected_texture && !samples++ && a==16u);
            assert(little32(program+128u)==0x00010013u);
            assert(little32(program+132u)==0xf00d1002u);
            assert(little32(program+136u)==0xb0800000u && !little32(program+140u));
            for(unsigned i=128;i<144;++i)used[i]=true;
        } else if((b&UINT32_C(0xfe03ffff))==UINT32_C(0xa0000000)) {
            unsigned count=((b>>18)&127u)+1u,start=a*8u;
            assert(start>=256u && start+count*8u<=size);
            for(unsigned i=0;i<count;++i) {
                uint32_t x=little32(program+start+i*8u),y=little32(program+start+i*8u+4u);
                unsigned op=(y>>7)&2047u,src=x&511u,other=(x>>13)&511u;
                assert(op==0u || op==2u || op==3u || op==4u || op==20u || op==25u);
                assert((x>>31)==1u && (y&16u) && ((y>>21)&127u)<9u);
                assert(src<9u || (src>=256u && src<264u));
                if(op!=20u && op!=25u)assert(other<9u || (other>=256u && other<264u));
            }
            for(unsigned i=start;i<start+count*8u;++i) { assert(!used[i]);used[i]=true; }
        } else {
            assert(b==0x94200688u && (a==3u<<15 || a==7u<<15));
            assert(samples==(expected_texture?1u:0u));
            for(unsigned i=0;i<size;++i)if(!used[i])assert(!program[i]);
            return;
        }
    }
    assert(!"Missing pixel program end");
}
void GX2SetPixelShader(const GX2PixelShader *shader) {
    assert(shader->size>=256 && shader->size<=8192 && !(shader->size&255u) && ((uintptr_t)shader->program&255u)==0);
    assert(shader->regs.sq_pgm_resources_ps==9u);
    assert(shader->regs.sq_pgm_exports_ps==2u && shader->regs.spi_ps_input_cntls[0]==0x100u);
    assert(shader->regs.spi_ps_in_control_0==(expected_texture?0x14000002u:0x14000001u));
    assert(shader->regs.num_spi_ps_input_cntl==(expected_texture?2u:1u));
    if(expected_texture)assert(shader->regs.spi_ps_input_cntls[1]==0x101u);
    assert(shader->regs.cb_shader_mask==15u && shader->regs.cb_shader_control==1u && shader->regs.db_shader_control==16u);
    bound_pixel_program=shader->program;bound_pixel_metadata=*shader;check_pixel_program(shader->program,shader->size);
}
void GX2InitTextureRegs(GX2Texture *texture) {
    assert(texture && !gpu_pending && texture->surface.image);
    assert(texture->surface.tileMode==GX2_TILE_MODE_LINEAR_ALIGNED && texture->surface.use==GX2_SURFACE_USE_TEXTURE);
    assert(texture->surface.format==GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8);
    assert(!texture->viewFirstMip && texture->viewNumMips==1u && !texture->viewFirstSlice && texture->viewNumSlices==1u);
    assert(texture->compMap==0x00010203u && ((uintptr_t)texture->surface.image&255u)==0u);
    texture_image=texture->surface.image;++texture_inits;
}
void GX2SetPixelTexture(const GX2Texture *texture,unsigned unit) {
    assert(expected_texture && !unit && texture->surface.image==texture_image);
    assert(texture_invalidates==texture_flushes && texture_flushes>texture_binds);
    assert(texture->surface.width==expected_texture->width && texture->surface.height==expected_texture->height);
    for(unsigned y=0;y<texture->surface.height;++y) {
        const uint8_t *row=(const uint8_t *)texture->surface.image+(size_t)y*texture->surface.pitch*4u;
        const uint8_t *expected=expected_texture->rgba+(size_t)(expected_texture->height-1u-y)*expected_texture->width*4u;
        assert(!memcmp(row,expected,texture->surface.width*4u));
        for(unsigned at=texture->surface.width*4u;at<texture->surface.pitch*4u;++at)assert(row[at]==0u);
    }
    bound_texture=texture;++texture_binds;
}
void GX2InitSampler(GX2Sampler *sampler,GX2TexClampMode clamp,unsigned filter) {
    assert(clamp==GX2_TEX_CLAMP_MODE_CLAMP &&
        (filter==GX2_TEX_XY_FILTER_MODE_POINT || filter==GX2_TEX_XY_FILTER_MODE_LINEAR));
    memset(sampler,0,sizeof(*sampler));sampler->filter=filter;
}
void GX2InitSamplerClamping(GX2Sampler *sampler,GX2TexClampMode x,GX2TexClampMode y,GX2TexClampMode z) {
    sampler->clamp[0]=x;sampler->clamp[1]=y;sampler->clamp[2]=z;
}
void GX2InitSamplerLOD(GX2Sampler *sampler,float min,float max,float bias) {
    sampler->lod[0]=min;sampler->lod[1]=max;sampler->lod[2]=bias;
}
void GX2InitSamplerZMFilter(GX2Sampler *sampler,unsigned z,unsigned mip) { sampler->z_filter=z;sampler->mip_filter=mip; }
void GX2SetPixelSampler(const GX2Sampler *sampler,unsigned unit) {
    unsigned wraps[4]={GX2_TEX_CLAMP_MODE_CLAMP,99u,GX2_TEX_CLAMP_MODE_WRAP,GX2_TEX_CLAMP_MODE_MIRROR};
    assert(expected_texture && !unit);
    assert(sampler->clamp[0]==wraps[(expected_texture->params>>12)&7u]);
    assert(sampler->clamp[1]==wraps[(expected_texture->params>>8)&7u] && sampler->clamp[2]==GX2_TEX_CLAMP_MODE_CLAMP);
    assert(sampler->filter==((expected_texture->params&6u)?GX2_TEX_XY_FILTER_MODE_LINEAR:GX2_TEX_XY_FILTER_MODE_POINT));
    assert(!sampler->mip_filter && !sampler->z_filter);
    assert(sampler->lod[0]==0.0f && sampler->lod[1]==0.0f && sampler->lod[2]==0.0f);
    bound_sampler=sampler;++sampler_binds;
}
void GX2SetAttribBuffer(unsigned index,unsigned size,unsigned stride,const void *bytes) {
    assert(!index && stride==256 && size==expected_count*256u);
    for(unsigned vertex=0;vertex<expected_count;++vertex)
        assert(!memcmp((const uint8_t *)bytes+vertex*256u,expected_vertices[vertex].input,256));
}
void GX2SetVertexUniformReg(unsigned offset,unsigned count,const void *bytes) {
    assert(offset==8 && count==4 && !memcmp(bytes,expected_shader->uniforms[2],16));++uniform_uploads;
}
void GX2SetPixelUniformReg(unsigned offset,unsigned count,const void *bytes) {
    assert(!offset && count==32u && bytes && bound_pixel_program);
    memcpy(bound_pixel_uniforms,bytes,sizeof(bound_pixel_uniforms));
    assert(bound_pixel_uniforms[26]==0x3b808081u);
    for(unsigned stage=0;stage<6;++stage)for(unsigned lane=0;lane<4;++lane) {
        float value=(float)((expected_state->registers[ctr_tev_registers[stage]+3u]>>(lane*8u))&255u);
        uint32_t raw;memcpy(&raw,&value,4);assert(bound_pixel_uniforms[stage*4u+lane]==raw);
    }
}
void GX2SetCullOnlyControl(unsigned front,bool a,bool b) { assert(front==GX2_FRONT_FACE_CCW);cull_front=a;cull_back=b; }
void GX2SetColorControl(GX2LogicOp logic,uint8_t blend,bool multi_write,bool color_write) {
    static const unsigned expected[16]={0x00,0x88,0x44,0xcc,0xff,0x33,0xaa,0x55,
        0x77,0xee,0x11,0x66,0x99,0x22,0xdd,0xbb};
    assert(expected_state->registers[0x102]<16u);
    bool blending=(expected_state->registers[0x100]&0x100u)!=0;
    assert(logic==(blending?0xccu:expected[expected_state->registers[0x102]]));
    assert(blend==(blending?1u:0u) && !multi_write && color_write);
    captured_logic=logic;captured_blend_enable=blend;++color_control_sets;
}
void GX2SetBlendControl(GX2RenderTarget target,GX2BlendMode color_source,GX2BlendMode color_destination,
                        GX2BlendCombineMode color_equation,bool separate_alpha,GX2BlendMode alpha_source,
                        GX2BlendMode alpha_destination,GX2BlendCombineMode alpha_equation) {
    static const unsigned factor[15]={0,1,2,3,8,9,4,5,6,7,13,14,19,20,10};
    static const unsigned equation[5]={0,1,4,2,3};
    assert(!target && separate_alpha);
    captured_blend_source[0]=color_source;captured_blend_source[1]=alpha_source;
    captured_blend_destination[0]=color_destination;captured_blend_destination[1]=alpha_destination;
    captured_blend_equation[0]=color_equation;captured_blend_equation[1]=alpha_equation;
    bool blending=(expected_state->registers[0x100]&0x100u)!=0;
    uint32_t config=blending?expected_state->registers[0x101]:0x01010000u;
    for(unsigned side=0;side<2;++side) {
        unsigned source=(config>>(16u+side*8u))&15u,destination=(config>>(20u+side*8u))&15u;
        assert(source<15u && destination<14u && ((config>>(side*8u))&7u)<5u);
        unsigned mapped_source=side && source==14u?1u:factor[source];
        assert(captured_blend_source[side]==mapped_source && captured_blend_destination[side]==factor[destination]);
        assert(captured_blend_equation[side]==equation[(config>>(side*8u))&7u]);
    }
    ++blend_control_sets;
}
void GX2SetBlendConstantColor(float red,float green,float blue,float alpha) {
    const float values[4]={red,green,blue,alpha};
    uint32_t constant=(expected_state->registers[0x100]&0x100u) &&
        expected_state->register_defined[0x103]==15u?expected_state->registers[0x103]:0u;
    for(unsigned lane=0;lane<4;++lane) {
        float expected=(float)((double)((constant>>(lane*8u))&255u)/255.0);
        assert(values[lane]==expected && values[lane]>=0.0f && values[lane]<=1.0f);
        captured_blend_constant[lane]=values[lane];
    }
    ++blend_constant_sets;
}
void GX2SetTargetChannelMasks(GX2ChannelMask a,GX2ChannelMask b,GX2ChannelMask c,GX2ChannelMask d,
                              GX2ChannelMask e,GX2ChannelMask f,GX2ChannelMask g,GX2ChannelMask h) {
    unsigned expected=expected_state->registers[0x113]==15u?(expected_state->registers[0x107]>>8)&15u:0u;
    assert(a==expected && !b && !c && !d && !e && !f && !g && !h);
    captured_write_mask=a;++channel_mask_sets;
}
void GX2SetAlphaTest(bool enabled,GX2CompareFunction function,float reference) {
    static const unsigned compare[8]={0,7,2,5,1,3,4,6},endpoint[8]={0,7,6,1,1,7,0,6};
    unsigned raw=expected_state->registers[0x104],fn=(raw>>4)&7u,ref=(raw>>8)&255u;
    assert(enabled==((raw&1u)!=0) && function==(ref==255u?endpoint[fn]:compare[fn]));
    assert(reference>=0.0f && reference<=1.0f);
    captured_alpha_enabled=enabled;captured_alpha_function=function;
    captured_alpha_reference=reference;++alpha_test_sets;
}
bool ctr_shader_f24_decode(uint32_t raw,uint32_t *value) {
    if(raw!=0x420000)return false;
    *value=0x41000000;return true;
}
ctr_gx2_shader_result ctr_gx2_shader_compile(const ctr_shader_program *program,uint16_t mask,
                                           const uint32_t *maps,unsigned total,ctr_gx2_shader *out) {
    ++compile_attempts;
    assert(program==expected_shader && mask==(expected_uv?7u:3u) && total==(expected_uv?3u:2u));
    assert(maps[0]==0x03020100 && maps[1]==0x0b0a0908);
    if(expected_uv)assert(maps[2]==0x1f1f0d0c);
    if(!compile_ok)return CTR_GX2_SHADER_UNSUPPORTED;
    memset(out,0,sizeof(*out));out->program[0]=0x12;out->size=256;out->gprs=expected_uv?54u:53u;
    out->texcoord0=expected_uv;
    out->input_mask=expected_uv?7u:3u;out->uniform_used[2]=15;out->pica_instructions=3;return CTR_GX2_SHADER_OK;
}
static void reset(void) {
    assert(!live_allocations);alloc_attempts=0;fail_allocation=0;fetch_bytes=512;
    wait_ok=true;compile_ok=true;layout_ok=true;active_context=&native_context;bound_target=NULL;
    gpu_draws=0;read_invalidates=0;uniform_uploads=0;rectangle_count=0;context_switches=0;
    wait_calls=0;fail_wait_call=0;
    gpu_pending=false;gpu_stopped=false;expected_uv=false;expected_texture=NULL;
    bound_texture=NULL;bound_sampler=NULL;bound_pixel_program=NULL;texture_image=NULL;
    texture_inits=0;texture_binds=0;sampler_binds=0;texture_flushes=0;texture_invalidates=0;
    color_control_sets=channel_mask_sets=alpha_test_sets=0;
    blend_control_sets=blend_constant_sets=compile_attempts=0;
    captured_blend_enable=0;
    memset(captured_blend_source,0,sizeof(captured_blend_source));
    memset(captured_blend_destination,0,sizeof(captured_blend_destination));
    memset(captured_blend_equation,0,sizeof(captured_blend_equation));
    memset(captured_blend_constant,0,sizeof(captured_blend_constant));
    captured_logic=captured_write_mask=captured_alpha_function=0;
    captured_alpha_enabled=false;captured_alpha_reference=0.0f;
}
static void setup(ctr_scene_state *state,ctr_vertex vertices[3]) {
    memset(state,0,sizeof(*state));memset(vertices,0,3*sizeof(*vertices));
    expected_state=state;
    const unsigned fragment_regs[6]={0x100,0x102,0x104,0x107,0x112,0x113};
    for(unsigned i=0;i<6;++i)state->register_defined[fragment_regs[i]]=15u;
    state->registers[0x102]=3u;state->registers[0x107]=0xf00u;
    state->registers[0x112]=state->registers[0x113]=15u;
    for(unsigned stage=0;stage<6;++stage) {
        unsigned base=ctr_tev_registers[stage];
        for(unsigned i=0;i<5;++i)state->register_defined[base+i]=15u;
        state->registers[base]=stage?0x000f000fu:0u;
    }
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
    assert(backend.draw(backend.context,&state,vertices,3,NULL));assert(active_context==&native_context);
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
        assert(backend.begin(backend.context,&surface));assert(backend.draw(backend.context,&state,vertices,3,NULL));
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
    assert(!backend.draw(backend.context,&state,vertices,3,NULL));assert(!gpu_draws && scene.result==CTR_WIIU_GX2_SCENE_SHADER);
    backend.abort(backend.context);clean(&scene);
    reset();ctr_wiiu_gx2_scene_init(&scene,&native_context);backend=ctr_wiiu_gx2_scene_backend(&scene);
    assert(backend.begin(backend.context,&surface));vertices[0].enabled=1;
    assert(!backend.draw(backend.context,&state,vertices,3,NULL));assert(!gpu_draws);
    backend.abort(backend.context);clean(&scene);vertices[0].enabled=3;
    reset();wait_ok=false;ctr_wiiu_gx2_scene_init(&scene,&native_context);backend=ctr_wiiu_gx2_scene_backend(&scene);
    assert(!backend.begin(backend.context,&surface));assert(scene.quarantined && scene.private_state && live_allocations);
    assert(scene.result==CTR_WIIU_GX2_SCENE_WAIT && active_context==&native_context);
    backend.abort(backend.context);assert(scene.private_state && scene.quarantined);
    assert(!backend.begin(backend.context,&surface) && scene.result==CTR_WIIU_GX2_SCENE_WAIT);
    unsigned switches=context_switches;gpu_stopped=true;ctr_wiiu_gx2_scene_after_shutdown(&scene);clean(&scene);
    assert(!scene.restore_context && context_switches==switches);
    assert(!backend.begin(backend.context,&surface) && context_switches==switches);
    reset();ctr_wiiu_gx2_scene_init(&scene,&native_context);backend=ctr_wiiu_gx2_scene_backend(&scene);
    assert(backend.begin(backend.context,&surface));wait_ok=false;
    assert(!backend.draw(backend.context,&state,vertices,3,NULL));assert(scene.quarantined && scene.draws==0 && scene.submissions==1);
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
        assert(backend.draw(backend.context,&state,vertices,3,NULL));
        /* First end wait retires the copy; the next retires context restore. */
        if(failure)fail_wait_call=wait_calls+failure;
        assert(backend.end(backend.context,&surface)==(failure==0));
        assert(observed_readbacks==(failure==1?0u:1u));
        backend.abort(backend.context);clean(&scene);
    }
}
static void textured_setup(ctr_scene_state *state,ctr_vertex vertices[3]) {
    setup(state,vertices);expected_uv=true;
    state->registers[0xc0]=0x00030003u;
    state->registers[0x04f]=3;state->registers[0x052]=0x1f1f0d0c;state->registers[0x2bd]=7;
    for(unsigned i=0;i<3;++i) {
        vertices[i].enabled=7;
        /* Original input UV vary per vertex, including wrapping coordinates.
         * The complete attribute upload is checked without CPU UV rewriting. */
        vertices[i].input[2][0]=(float)i*1.5f-0.25f;
        vertices[i].input[2][1]=(float)i*-0.5f+1.25f;
    }
}
static bool pica_alpha(unsigned function,unsigned value,unsigned reference) {
    switch(function) {
        case 0:return false;
        case 1:return true;
        case 2:return value==reference;
        case 3:return value!=reference;
        case 4:return value<reference;
        case 5:return value<=reference;
        case 6:return value>reference;
        case 7:return value>=reference;
    }
    assert(!"Invalid PICA comparison");return false;
}
static bool gx2_alpha(unsigned function,float value,float reference) {
    switch(function) {
        case 0:return false;
        case 1:return value<reference;
        case 2:return value==reference;
        case 3:return value<=reference;
        case 4:return value>reference;
        case 5:return value!=reference;
        case 6:return value>=reference;
        case 7:return true;
    }
    assert(!"Invalid GX2 comparison");return false;
}
static unsigned pica_logic_bit(unsigned function,unsigned source,unsigned dest) {
    switch(function) {
        case 0:return 0;
        case 1:return source&dest;
        case 2:return source&(dest^1u);
        case 3:return source;
        case 4:return 1;
        case 5:return source^1u;
        case 6:return dest;
        case 7:return dest^1u;
        case 8:return (source&dest)^1u;
        case 9:return source|dest;
        case 10:return (source|dest)^1u;
        case 11:return source^dest;
        case 12:return (source^dest)^1u;
        case 13:return (source^1u)&dest;
        case 14:return source|(dest^1u);
        case 15:return (source^1u)|dest;
    }
    assert(!"Invalid PICA logic operation");return 0;
}
static void fragment_bindings(void) {
    reset();uint8_t bytes[1024]={0};ctr_scene_surface surface={bytes,sizeof(bytes),16,16,64};
    ctr_scene_state state;ctr_vertex vertices[3];setup(&state,vertices);
    ctr_wiiu_gx2_scene scene;ctr_wiiu_gx2_scene_init(&scene,&native_context);
    ctr_scene_backend backend=ctr_wiiu_gx2_scene_backend(&scene);
    assert(backend.begin(backend.context,&surface));
    for(unsigned operation=0;operation<16;++operation)for(unsigned mask=0;mask<16;++mask) {
        state.registers[0x102]=operation;state.registers[0x107]=mask<<8;
        state.registers[0x104]=1u|((operation&7u)<<4)|(93u<<8);rectangle_count=0;
        assert(backend.draw(backend.context,&state,vertices,3,NULL));
        assert(captured_write_mask==mask && captured_alpha_enabled);
        /* ROP3's pattern input is irrelevant for a two-input logic operation:
         * both nibbles must implement the independently stated PICA truth table. */
        for(unsigned source=0;source<2;++source)for(unsigned dest=0;dest<2;++dest) {
            unsigned bit=pica_logic_bit(operation,source,dest),index=source*2u+dest;
            assert(((captured_logic>>index)&1u)==bit && ((captured_logic>>(index+4u))&1u)==bit);
        }
    }
    /* Writes disabled by the gate cannot consume a destination, even when a
     * destination-dependent operation is retained and color reads are off. */
    state.registers[0x112]=state.registers[0x113]=0;
    state.registers[0x102]=11;state.registers[0x107]=0xf00u;rectangle_count=0;
    assert(backend.draw(backend.context,&state,vertices,3,NULL));assert(!captured_write_mask);
    /* Changing back to default state must reset every fixed-state binding. */
    state.registers[0x112]=state.registers[0x113]=15;state.registers[0x102]=3;
    state.registers[0x104]=0;rectangle_count=0;
    assert(backend.draw(backend.context,&state,vertices,3,NULL));
    assert(captured_logic==0xccu && captured_write_mask==15u && !captured_alpha_enabled &&
        captured_alpha_reference==0.0f && captured_alpha_function==0u);
    const unsigned source_only[4]={0,3,4,5};state.registers[0x112]=0;
    for(unsigned i=0;i<4;++i) {
        state.registers[0x102]=source_only[i];rectangle_count=0;
        assert(backend.draw(backend.context,&state,vertices,3,NULL));assert(captured_write_mask==15u);
    }
    assert(backend.end(backend.context,&surface));clean(&scene);
}
static void alpha_comparison_vectors(void) {
    reset();uint8_t bytes[1024]={0};ctr_scene_surface surface={bytes,sizeof(bytes),16,16,64};
    ctr_scene_state state;ctr_vertex vertices[3];setup(&state,vertices);
    ctr_wiiu_gx2_scene scene;ctr_wiiu_gx2_scene_init(&scene,&native_context);
    ctr_scene_backend backend=ctr_wiiu_gx2_scene_backend(&scene);
    assert(backend.begin(backend.context,&surface));
    uint32_t reciprocal_bits=UINT32_C(0x3b808081);float reciprocal;
    memcpy(&reciprocal,&reciprocal_bits,4);
    for(unsigned reference=0;reference<256;++reference)for(unsigned function=0;function<8;++function) {
        state.registers[0x104]=1u|(function<<4)|(reference<<8);rectangle_count=0;
        assert(backend.draw(backend.context,&state,vertices,3,NULL));
        assert(captured_alpha_enabled);
        for(unsigned alpha=0;alpha<256;++alpha) {
            /* Binary64 can represent the exact product of an eight-bit integer
             * and binary32 reciprocal. The cast models only the shader's final
             * multiply, independently of the native reference calculation. */
            float exported=(float)((double)alpha*(double)reciprocal);
            float clamped=exported>1.0f?1.0f:exported;
            bool expected=pica_alpha(function,alpha,reference);
            assert(gx2_alpha(captured_alpha_function,exported,captured_alpha_reference)==expected);
            assert(gx2_alpha(captured_alpha_function,clamped,captured_alpha_reference)==expected);
        }
    }
    assert(scene.draws==2048u);
    assert(backend.end(backend.context,&surface));clean(&scene);
}
static void fragment_failures(void) {
    const unsigned regs[6]={0x100,0x102,0x104,0x107,0x112,0x113};
    uint8_t pixels[8u*8u*4u]={0},bytes[1024]={0};
    ctr_scene_surface surface={bytes,sizeof(bytes),16,16,64};
    for(unsigned retained=0;retained<2;++retained)for(unsigned failure=0;failure<19;++failure) {
        reset();ctr_scene_state state;ctr_vertex vertices[3];textured_setup(&state,vertices);
        ctr_scene_texture texture={pixels,8,8,0x18010000u,0,0};expected_texture=&texture;
        ctr_wiiu_gx2_scene scene;ctr_wiiu_gx2_scene_init(&scene,&native_context);
        ctr_scene_backend backend=ctr_wiiu_gx2_scene_backend(&scene);
        assert(backend.begin(backend.context,&surface));
        if(retained)assert(backend.draw(backend.context,&state,vertices,3,&texture));
        unsigned allocations=alloc_attempts;
        if(failure<6u)state.register_defined[regs[failure]]=0;
        else if(failure<12u)state.register_defined[regs[failure-6u]]=14;
        else switch(failure) {
            case 12:state.registers[0x100]=0x100;break; /* Blending unsupported. */
            case 13:state.registers[0x102]=16;break;
            case 14:state.registers[0x104]=2;break;
            case 15:state.registers[0x107]|=1u;break; /* Depth unsupported. */
            case 16:state.registers[0x112]=1;break;
            case 17:state.registers[0x113]=1;break;
            case 18:state.registers[0x112]=0;state.registers[0x102]=1;break;
        }
        assert(!backend.draw(backend.context,&state,vertices,3,&texture));
        assert(scene.result==CTR_WIIU_GX2_SCENE_ARGUMENT && scene.draws==retained && gpu_draws==retained &&
            scene.texture_uploads==retained && texture_inits==retained && texture_binds==retained &&
            color_control_sets==retained && channel_mask_sets==retained && alpha_test_sets==retained &&
            blend_control_sets==retained && blend_constant_sets==retained && compile_attempts==retained &&
            alloc_attempts==allocations);
        backend.abort(backend.context);clean(&scene);
    }
}
static void blend_config(ctr_scene_state *state,unsigned rgb_equation,unsigned rgb_source,unsigned rgb_destination,
                          unsigned alpha_equation,unsigned alpha_source,unsigned alpha_destination) {
    state->registers[0x100]=0x100u;
    state->register_defined[0x101]=state->register_defined[0x103]=15u;
    state->registers[0x101]=rgb_equation|(alpha_equation<<8)|(rgb_source<<16)|
        (rgb_destination<<20)|(alpha_source<<24)|(alpha_destination<<28);
    state->registers[0x103]=UINT32_C(0x0953a7ed);
}
static void blend_bindings(void) {
    reset();uint8_t bytes[1024]={0};ctr_scene_surface surface={bytes,sizeof(bytes),16,16,64};
    ctr_scene_state state;ctr_vertex vertices[3];setup(&state,vertices);
    ctr_wiiu_gx2_scene scene;ctr_wiiu_gx2_scene_init(&scene,&native_context);
    ctr_scene_backend backend=ctr_wiiu_gx2_scene_backend(&scene);
    assert(backend.begin(backend.context,&surface));
    state.registers[0x102]=11; /* The blend route must bind COPY regardless. */
    for(unsigned side=0;side<2;++side)for(unsigned equation=0;equation<3;++equation)
        for(unsigned source=0;source<15;++source)for(unsigned destination=0;destination<14;++destination) {
            blend_config(&state,side?0u:equation,side?1u:source,side?0u:destination,
                side?equation:0u,side?source:1u,side?destination:0u);rectangle_count=0;
            assert(backend.draw(backend.context,&state,vertices,3,NULL));
            assert(captured_logic==0xccu && captured_blend_enable==1u);
            if(side && source==14u)assert(captured_blend_source[1]==1u);
        }
    /* ONE/ONE makes factor-aware and factor-ignoring MIN/MAX equivalent. Both
     * equations remain independently selectable for RGB and alpha. */
    for(unsigned rgb=0;rgb<5;++rgb)for(unsigned alpha=0;alpha<5;++alpha) {
        blend_config(&state,rgb,1,1,alpha,1,1);rectangle_count=0;
        assert(backend.draw(backend.context,&state,vertices,3,NULL));
    }
    /* Every byte is observed in every constant lane; these check normalized
     * API arguments only, without claiming a hardware blend numeric model. */
    for(unsigned value=0;value<256;++value) {
        blend_config(&state,0,10,11,2,12,13);
        state.registers[0x103]=value|((255u-value)<<8)|((value^0xa5u)<<16)|(((value*3u+17u)&255u)<<24);
        rectangle_count=0;assert(backend.draw(backend.context,&state,vertices,3,NULL));
    }
    for(unsigned mask=0;mask<16;++mask) {
        blend_config(&state,0,1,1,0,1,1);state.registers[0x107]=mask<<8;rectangle_count=0;
        assert(backend.draw(backend.context,&state,vertices,3,NULL));assert(captured_write_mask==mask);
    }
    blend_config(&state,0,1,0,2,1,0);state.registers[0x112]=0;state.registers[0x107]=0xf00u;
    rectangle_count=0;assert(backend.draw(backend.context,&state,vertices,3,NULL));
    /* Reset complete controls and constants on a blend -> logic -> blend
     * sequence, including retained valid inactive blend configuration. */
    state.registers[0x100]=0;state.registers[0x102]=3;rectangle_count=0;
    assert(backend.draw(backend.context,&state,vertices,3,NULL));
    assert(!captured_blend_enable && captured_blend_source[0]==1u && captured_blend_source[1]==1u &&
        !captured_blend_destination[0] && !captured_blend_destination[1] &&
        !captured_blend_equation[0] && !captured_blend_equation[1]);
    for(unsigned lane=0;lane<4;++lane)assert(captured_blend_constant[lane]==0.0f);
    blend_config(&state,1,10,11,0,12,13);state.registers[0x112]=15;rectangle_count=0;
    assert(backend.draw(backend.context,&state,vertices,3,NULL));assert(captured_blend_enable==1u);
    assert(backend.end(backend.context,&surface));clean(&scene);
}
static void blend_constant_lists(void) {
    reset();uint8_t bytes[1024]={0};ctr_scene_surface surface={bytes,sizeof(bytes),16,16,64};
    ctr_wiiu_gx2_scene scene;ctr_wiiu_gx2_scene_init(&scene,&native_context);
    ctr_scene_backend backend=ctr_wiiu_gx2_scene_backend(&scene);
    for(unsigned list=0;list<3;++list) {
        ctr_scene_state state;ctr_vertex vertices[3];setup(&state,vertices);
        blend_config(&state,0,10,11,2,12,13);
        state.registers[0x103]=list?UINT32_C(0x71e5932b):UINT32_C(0x2d9b63e7);
        if(list==1u) {
            blend_config(&state,0,1,0,0,1,0);
            state.registers[0x103]=UINT32_MAX;state.register_defined[0x103]=1u;
        }
        rectangle_count=0;assert(backend.begin(backend.context,&surface));
        assert(backend.draw(backend.context,&state,vertices,3,NULL));
        if(list==1u)for(unsigned lane=0;lane<4;++lane)assert(captured_blend_constant[lane]==0.0f);
        assert(backend.end(backend.context,&surface));clean(&scene);
    }
    assert(blend_control_sets==3u && blend_constant_sets==3u && scene.draws==3u);
}
static void blend_failures(void) {
    uint8_t pixels[8u*8u*4u]={0},bytes[1024]={0};
    ctr_scene_surface surface={bytes,sizeof(bytes),16,16,64};
    for(unsigned retained=0;retained<2;++retained)for(unsigned failure=0;failure<24;++failure) {
        reset();ctr_scene_state state;ctr_vertex vertices[3];textured_setup(&state,vertices);
        blend_config(&state,0,1,0,0,1,0);
        ctr_scene_texture texture={pixels,8,8,0x18010000u,0,0};expected_texture=&texture;
        ctr_wiiu_gx2_scene scene;ctr_wiiu_gx2_scene_init(&scene,&native_context);
        ctr_scene_backend backend=ctr_wiiu_gx2_scene_backend(&scene);
        assert(backend.begin(backend.context,&surface));
        if(retained)assert(backend.draw(backend.context,&state,vertices,3,&texture));
        unsigned allocations=alloc_attempts;
        if(failure<2u) {
            if(failure)blend_config(&state,0,1,0,0,1,14);
            else blend_config(&state,0,1,14,0,1,0);
        } else if(failure<14u) {
            unsigned index=failure-2u,side=index/6u,equation=3u+(index%6u)/3u,variation=index%3u;
            unsigned source=variation==0u?0u:variation==1u?1u:2u,destination=variation==1u?0u:1u;
            blend_config(&state,side?0u:equation,side?1u:source,side?0u:destination,
                side?equation:0u,side?source:1u,side?destination:0u);
        } else switch(failure) {
            case 14:state.register_defined[0x101]=0;break;
            case 15:state.register_defined[0x101]=14;break;
            case 16:blend_config(&state,0,10,0,0,1,0);state.register_defined[0x103]=0;break;
            case 17:blend_config(&state,0,1,0,0,12,0);state.register_defined[0x103]=14;break;
            case 18:blend_config(&state,5,1,0,0,1,0);break;
            case 19:blend_config(&state,0,15,0,0,1,0);break;
            case 20:state.registers[0x101]|=8u;break;
            case 21:state.registers[0x100]|=1u;break;
            case 22:blend_config(&state,0,1,1,0,1,0);state.registers[0x112]=0;break;
            case 23:blend_config(&state,0,14,0,0,1,0);state.registers[0x112]=0;break;
        }
        assert(!backend.draw(backend.context,&state,vertices,3,&texture));
        assert(scene.result==CTR_WIIU_GX2_SCENE_ARGUMENT && scene.draws==retained && gpu_draws==retained &&
            scene.texture_uploads==retained && texture_inits==retained && texture_binds==retained &&
            color_control_sets==retained && channel_mask_sets==retained && alpha_test_sets==retained &&
            blend_control_sets==retained && blend_constant_sets==retained && compile_attempts==retained &&
            alloc_attempts==allocations);
        backend.abort(backend.context);clean(&scene);
    }
}
static void texture_pixels(uint8_t *bytes,unsigned width,unsigned height) {
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)
        for(unsigned c=0;c<4;++c)bytes[((size_t)y*width+x)*4u+c]=(uint8_t)(x*13u+y*37u+c*59u);
}
static void dump_pixel(const char *mode,const ctr_scene_state *state,bool comma) {
    if(comma)fputc(',',stdout);
    printf("{\"mode\":\"%s\",\"textured\":%s,\"hex\":\"",mode,expected_texture?"true":"false");
    for(unsigned i=0;i<bound_pixel_metadata.size;++i)printf("%02x",bound_pixel_program[i]);
    printf("\",\"gprs\":%u,\"interpolators\":%u,\"spi_ps_in_control_0\":%u,\"inputs\":[%u",
        bound_pixel_metadata.regs.sq_pgm_resources_ps,bound_pixel_metadata.regs.num_spi_ps_input_cntl,
        bound_pixel_metadata.regs.spi_ps_in_control_0,bound_pixel_metadata.regs.spi_ps_input_cntls[0]);
    if(expected_texture)printf(",%u",bound_pixel_metadata.regs.spi_ps_input_cntls[1]);
    fputs("],\"uniforms\":[",stdout);
    for(unsigned i=0;i<32;++i)printf("%s%u",i?",":"",bound_pixel_uniforms[i]);
    fputs("],\"stages\":[",stdout);
    for(unsigned stage=0;stage<6;++stage) {
        printf("%s[",stage?",":"");
        for(unsigned offset=0;offset<5;++offset)printf("%s%u",offset?",":"",state->registers[ctr_tev_registers[stage]+offset]);
        fputc(']',stdout);
    }
    fputs("]}",stdout);
}
static void combiner_programs(bool dump);
static void textured_bindings(bool dump_programs) {
    uint8_t pixels[16u*16u*4u],bytes[1024]={0};texture_pixels(pixels,16,16);
    ctr_scene_surface surface={bytes,sizeof(bytes),16,16,64};
    const unsigned wraps[3]={0,2,3};
    if(dump_programs)fputs("{\"programs\":[",stdout);
    for(unsigned filter=0;filter<(dump_programs?1u:2u);++filter)
    for(unsigned format=0;format<(dump_programs?1u:14u);++format)
        for(unsigned t=0;t<(dump_programs?1u:3u);++t)
            for(unsigned s=0;s<(dump_programs?1u:3u);++s)for(unsigned tev=0;tev<2;++tev) {
                reset();ctr_scene_state state;ctr_vertex vertices[3];textured_setup(&state,vertices);
                ctr_scene_texture texture={pixels,16,16,0x18010000u,format,
                    (wraps[t]<<8)|(wraps[s]<<12)|(format>=12u?0x20u:0u)|(filter?6u:0u)};
                state.registers[0xc2]=tev?0x00010001u:0u;
                expected_texture=&texture;
                ctr_wiiu_gx2_scene scene;ctr_wiiu_gx2_scene_init(&scene,&native_context);
                ctr_scene_backend backend=ctr_wiiu_gx2_scene_backend(&scene);
                assert(backend.begin(backend.context,&surface));
                assert(backend.draw(backend.context,&state,vertices,3,&texture));
                assert(!gpu_pending && active_context==&native_context);
                assert(texture_inits==1 && texture_binds==1 && sampler_binds==1 && gpu_draws==1);
                assert(scene.texture_uploads==1 && scene.texture_bytes==sizeof(pixels));
                if(dump_programs)dump_pixel(tev?"modulate":"replace",&state,tev!=0u);
                assert(backend.end(backend.context,&surface));clean(&scene);
            }
    if(dump_programs) { combiner_programs(true);fputs("]}\n",stdout); }
}
static void tev_row(ctr_scene_state *state,unsigned stage,uint32_t sources,uint32_t operands,
                    uint32_t operations,uint32_t constant,uint32_t scale) {
    unsigned base=ctr_tev_registers[stage];
    state->registers[base]=sources;state->registers[base+1u]=operands;
    state->registers[base+2u]=operations;state->registers[base+3u]=constant;
    state->registers[base+4u]=scale;
}
static void combiner_programs(bool dump) {
    const unsigned operations[8]={0,1,2,3,4,5,8,9},operands[10]={0,1,2,3,4,5,8,9,12,13};
    uint8_t pixels[16u*16u*4u],bytes[1024]={0};texture_pixels(pixels,16,16);
    ctr_scene_surface surface={bytes,sizeof(bytes),16,16,64};
    for(unsigned sample=0;sample<33u;++sample) {
        reset();ctr_scene_state state;ctr_vertex vertices[3];textured_setup(&state,vertices);
        ctr_scene_texture texture={pixels,16,16,0x18010000u,0,0};
        expected_texture=sample==28u || sample==29u?NULL:&texture;
        if(sample<8u) {
            tev_row(&state,0,0x0ee30ee3,0,operations[sample]|operations[7u-sample]<<16,0x953dc571u,0);
        } else if(sample<18u) {
            unsigned n=sample-8u;
            uint32_t mods=operands[n]|operands[(n+3u)%10u]<<4|operands[(n+6u)%10u]<<8|
                (n%8u)<<12|((n+3u)%8u)<<16|((n+5u)%8u)<<20;
            tev_row(&state,0,0x0ee30ee3,mods,0x00040004u,0x9d53c725u,0);
        } else if(sample<27u) {
            unsigned n=sample-18u;
            tev_row(&state,0,0x0ee30ee3,0,0x00030001u,0x958bb547u,n/3u|(n%3u)<<16);
        } else {
            tev_row(&state,0,0x0e3e00e3u,0,0x00040001u,0x65e949b3u,0);
            tev_row(&state,1,0x00ef00efu,0,0x00050002u,0x21139725u,0);
            tev_row(&state,2,0x0e3f0e3fu,0x00120210u,0x00080004u,0x532fd35bu,0);
            tev_row(&state,3,0x00ef00efu,0,0x00010003u,0xb1f14395u,1);
            tev_row(&state,4,0x03ef03efu,0,0x00040009u,0x77b52f53u,0);
            tev_row(&state,5,0x03ef03efu,0,0x00020008u,0x113bc771u,0);
            if(sample==28u || sample==29u) {
                for(unsigned stage=0;stage<6;++stage) {
                    unsigned reg=ctr_tev_registers[stage];
                    uint32_t source=state.registers[reg];
                    /* Replace every texture source with primary for this
                     * untextured pipeline; no sampler or UV fetch is bound. */
                    for(unsigned shift=0;shift<32;shift+=4u)
                        if(((source>>shift)&15u)==3u)source&=~(15u<<shift);
                    state.registers[reg]=source;
                }
                if(sample==28u) {
                    tev_row(&state,0,0x000e000eu,0,0,0x2fab5313u,0);
                    for(unsigned stage=1;stage<6;++stage)tev_row(&state,stage,0x000f000fu,0,0,0,0);
                }
            }
            if(sample==30u)tev_row(&state,3,0x000f000fu,0,0,0,0);
            if(sample==31u) {
                tev_row(&state,2,0x000f000fu,0,0,0,0);
                tev_row(&state,5,0x000f000fu,0,0,0,0);
            }
        }
        if(sample==32u)for(unsigned stage=0;stage<6;++stage)
            tev_row(&state,stage,stage?0x03ef03efu:0x00e300e3u,0x0035759du,0x00040004u,
                0x953dc571u^(stage*0x11111111u),0x00020002u);
        ctr_wiiu_gx2_scene scene;ctr_wiiu_gx2_scene_init(&scene,&native_context);
        ctr_scene_backend backend=ctr_wiiu_gx2_scene_backend(&scene);
        assert(backend.begin(backend.context,&surface));
        assert(backend.draw(backend.context,&state,vertices,3,expected_texture));
        if(dump) { char mode[32];snprintf(mode,sizeof(mode),"combiner-%02u",sample);dump_pixel(mode,&state,true); }
        assert(backend.end(backend.context,&surface));clean(&scene);
    }
}
static void texture_reuse(void) {
    reset();uint8_t pixels[16u*16u*4u],saved[sizeof(pixels)],bytes[1024]={0};
    texture_pixels(pixels,16,16);memcpy(saved,pixels,sizeof(saved));
    ctr_scene_surface surface={bytes,sizeof(bytes),16,16,64};
    ctr_scene_state state;ctr_vertex vertices[3];textured_setup(&state,vertices);
    ctr_scene_texture texture={pixels,8,16,0x18010000u,0,0};expected_texture=&texture;
    ctr_wiiu_gx2_scene scene;ctr_wiiu_gx2_scene_init(&scene,&native_context);
    ctr_scene_backend backend=ctr_wiiu_gx2_scene_backend(&scene);
    assert(backend.begin(backend.context,&surface));
    assert(backend.draw(backend.context,&state,vertices,3,&texture));
    unsigned allocations=alloc_attempts;void *first_image=texture_image;
    /* Same dimensions and changed content/TEV reuse a retired owned allocation. */
    for(unsigned i=0;i<sizeof(pixels);++i)pixels[i]^=0xb5u;
    state.registers[0xc2]=0x00010001u;texture.params=0x3206;rectangle_count=0;
    assert(backend.draw(backend.context,&state,vertices,3,&texture));
    assert(alloc_attempts==allocations && texture_image==first_image && texture_inits==1u);
    /* A larger texture replaces the old image only after successful allocation. */
    texture.width=16;rectangle_count=0;
    assert(backend.draw(backend.context,&state,vertices,3,&texture));
    assert(alloc_attempts==allocations+1u && texture_inits==2u);
    assert(scene.texture_uploads==3u && scene.texture_bytes==2048u);
    /* Return to the original untextured PS while harmless UV exports remain. */
    expected_texture=NULL;state.registers[0xc0]=0;state.registers[0xc2]=0;rectangle_count=0;
    assert(backend.draw(backend.context,&state,vertices,3,NULL));
    assert(scene.texture_uploads==3u && texture_binds==3u && sampler_binds==3u);
    assert(backend.end(backend.context,&surface));clean(&scene);
    for(unsigned i=0;i<sizeof(pixels);++i)assert(pixels[i]==(uint8_t)(saved[i]^0xb5u));
}
static void texture_dimensions(void) {
    uint8_t *pixels=malloc(1024u*1024u*4u);assert(pixels);
    const unsigned sizes[3][2]={{8,1024},{1024,8},{1024,1024}};
    for(unsigned sample=0;sample<3;++sample) {
        reset();unsigned width=sizes[sample][0],height=sizes[sample][1];
        texture_pixels(pixels,width,height);
        uint8_t bytes[1024]={0};ctr_scene_surface surface={bytes,sizeof(bytes),16,16,64};
        ctr_scene_state state;ctr_vertex vertices[3];textured_setup(&state,vertices);
        ctr_scene_texture texture={pixels,width,height,0x18010000u,13u,0x20u};expected_texture=&texture;
        ctr_wiiu_gx2_scene scene;ctr_wiiu_gx2_scene_init(&scene,&native_context);
        ctr_scene_backend backend=ctr_wiiu_gx2_scene_backend(&scene);
        assert(backend.begin(backend.context,&surface));
        assert(backend.draw(backend.context,&state,vertices,3,&texture));
        assert(scene.texture_bytes==(uint64_t)width*height*4u);
        assert(backend.end(backend.context,&surface));clean(&scene);
    }
    free(pixels);
}
static void texture_failures(void) {
    uint8_t pixels[16u*16u*4u],bytes[1024]={0};texture_pixels(pixels,16,16);
    ctr_scene_surface surface={bytes,sizeof(bytes),16,16,64};
    for(unsigned failure=0;failure<21;++failure) {
        reset();ctr_scene_state state;ctr_vertex vertices[3];textured_setup(&state,vertices);
        ctr_scene_texture texture={pixels,8,16,0x18010000u,0,0};expected_texture=&texture;
        ctr_wiiu_gx2_scene scene;ctr_wiiu_gx2_scene_init(&scene,&native_context);
        ctr_scene_backend backend=ctr_wiiu_gx2_scene_backend(&scene);
        assert(backend.begin(backend.context,&surface));
        switch(failure) {
            case 0:texture.rgba=NULL;break;
            case 1:texture.width=4;break;
            case 2:texture.height=2048;break;
            case 3:texture.width=12;break;
            case 4:texture.format=14;break;
            case 5:state.registers[0xc2]=6;break;
            case 6:texture.params=0x0100;break;
            case 7:texture.params=0x4000;break;
            case 8:texture.params=1;break;
            case 9:texture.params=0x20;break;
            case 10:texture.format=12;break;
            case 11:texture.rgba=(const uint8_t *)(UINTPTR_MAX-15u);break;
            case 12:layout_ok=false;break;
            case 13:fail_allocation=alloc_attempts+1u;break;
            case 14:expected_uv=false;setup(&state,vertices);break;
            case 15:texture.height=0;break;
            case 16:texture.params=0x7000;break;
            case 17:texture.params=2;break; /* Mixed nearest/linear unsupported. */
            case 18:texture.params=4;break;
            case 19:texture.params=6u|(1u<<24);break; /* Mip filter remains unsupported. */
            case 20:texture.params=6u|(1u<<28);break;
        }
        assert(!backend.draw(backend.context,&state,vertices,3,&texture));
        assert(!gpu_draws && !scene.texture_uploads && !texture_binds);
        assert(scene.result==(failure==12?CTR_WIIU_GX2_SCENE_LAYOUT:
            failure==13?CTR_WIIU_GX2_SCENE_ALLOCATION:
            (failure==14 || failure==5)?CTR_WIIU_GX2_SCENE_SHADER:CTR_WIIU_GX2_SCENE_ARGUMENT));
        backend.abort(backend.context);clean(&scene);
    }
    /* Failed replacement allocation leaves the retired previous image owned
     * until abort. A failed submission wait retains it through driver shutdown. */
    for(unsigned failure=0;failure<3;++failure) {
        reset();ctr_scene_state state;ctr_vertex vertices[3];textured_setup(&state,vertices);
        ctr_scene_texture texture={pixels,8,16,0x18010000u,0,0};expected_texture=&texture;
        ctr_wiiu_gx2_scene scene;ctr_wiiu_gx2_scene_init(&scene,&native_context);
        ctr_scene_backend backend=ctr_wiiu_gx2_scene_backend(&scene);
        assert(backend.begin(backend.context,&surface));
        assert(backend.draw(backend.context,&state,vertices,3,&texture));
        void *first_image=texture_image;unsigned live=live_allocations;rectangle_count=0;
        if(failure==0) { texture.width=16;fail_allocation=alloc_attempts+1u; }
        else wait_ok=false;
        assert(!backend.draw(backend.context,&state,vertices,3,&texture));
        assert(texture_image==first_image && live_allocations==live && scene.private_state);
        if(failure) {
            assert(scene.quarantined && scene.result==CTR_WIIU_GX2_SCENE_WAIT);
            unsigned uploads=(unsigned)scene.texture_uploads;
            assert(!backend.draw(backend.context,&state,vertices,3,&texture));
            assert(scene.texture_uploads==uploads && texture_image==first_image);
            backend.abort(backend.context);assert(live_allocations==live);
            if(failure==1)wait_ok=true;
            else { gpu_stopped=true;ctr_wiiu_gx2_scene_after_shutdown(&scene); }
        } else assert(scene.result==CTR_WIIU_GX2_SCENE_ALLOCATION);
        if(failure!=2)backend.abort(backend.context);
        clean(&scene);
    }
}
int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"--fragment-programs")) { textured_bindings(true);return 0; }
    assert(argc==1);
    baseline();allocation_failures();scissor_modes();failures();readback_observer();
    fragment_bindings();alpha_comparison_vectors();fragment_failures();
    blend_bindings();blend_constant_lists();blend_failures();
    textured_bindings(false);combiner_programs(false);texture_reuse();texture_dimensions();texture_failures();
    puts("GX2 native API model: lifetime, texture retirement/quarantine, original inputs, sampler/TEV bytecode, fragment/blend mappings, 1048576 alpha comparisons, pitched upload, scissor and readback passed");
    return 0;
}
