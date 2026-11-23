#include "diagnostic_display.h"
#include "diagnostic_font.h"
#include "image_scale.h"
#include <coreinit/cache.h>
#include <coreinit/memdefaultheap.h>
#ifdef CTR_GAME_CEMU_TEST
#include <coreinit/time.h>
#include <gx2/draw.h>
#include <gx2/registers.h>
#include <gx2/sampler.h>
#include <gx2/shaders.h>
#include <gx2/state.h>
#include <gx2/texture.h>
#include <gx2/utils.h>
#endif
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/surface.h>
#include <gx2/swap.h>
#include <whb/gfx.h>
#include <stdio.h>
#include <string.h>

enum { WIDTH=512,HEIGHT=288,ROWS=14,COLUMNS=82 };
static uint32_t pixels[WIDTH*HEIGHT];
static char rows[ROWS][COLUMNS+1];
static GX2Surface surfaces[2];
#ifdef CTR_GAME_CEMU_TEST
typedef struct {
    float x,y,z,w,u,v,s,t;
} cemu_quad_vertex;
static struct {
    uint8_t *vs_prog,*ps_prog,*fetch_prog;
    GX2VertexShader vs;
    GX2PixelShader ps;
    GX2FetchShader fs;
    GX2Sampler sampler;
    cemu_quad_vertex *verts[4];
    GX2Texture tex[3][2];
    OSTime tex_ts[3][2];
    unsigned tex_slot[3];
    bool tex_valid[3];
    bool gpu_scanout;
    GX2ColorBuffer scanout_target[2];
    uint32_t scanout_generation[2];
    GX2Texture scanout_texture[2];
    GX2PixelShader scanout_shader;
    GX2Sampler scanout_sampler;
    cemu_quad_vertex *scanout_quad[2];
} cemu_gfx;
#else
static ctr_scale_plan plans[2];
#endif
static bool initialized,ready,dirty=true,presented;
static bool guest_mode;
static char heading[64]="CTR2WUP INSTALL CAPTURE";
static char footer[84]="MINUS: EXIT / CANCEL AND WAIT FOR MCP";

void ctr_diagnostic_display_labels(const char *title,const char *hint) {
    if(title)snprintf(heading,sizeof(heading),"%s",title);
    if(hint)snprintf(footer,sizeof(footer),"%s",hint);
    dirty=true;
}

bool ctr_diagnostic_display_row(unsigned row,const char *text) {
    if(row>=ROWS || !text)return false;
    snprintf(rows[row],sizeof(rows[row]),"%s",text);
    for(char *c=rows[row];*c;++c)if(*c=='_')*c='-';
    dirty=true;return true;
}

void ctr_diagnostic_display_line(const char *text) {
    if(!text)return;
    do {
        memmove(rows,rows+1,sizeof(rows)-sizeof(rows[0]));
        size_t n=strlen(text);if(n>COLUMNS)n=COLUMNS;
        memcpy(rows[ROWS-1],text,n);rows[ROWS-1][n]=0;text+=n;
        for(size_t i=0;i<n;++i)if(rows[ROWS-1][i]=='_')rows[ROWS-1][i]='-';
    } while(*text);
    dirty=true;
}

#ifdef CTR_GAME_CEMU_TEST
static void cemu_put32(void *p,uint32_t v) {
    uint8_t *b=p;b[0]=(uint8_t)v;b[1]=(uint8_t)(v>>8);b[2]=(uint8_t)(v>>16);b[3]=(uint8_t)(v>>24);
}
static bool cemu_init_quad_verts(cemu_quad_vertex **out,unsigned screen_w,unsigned screen_h,
                                 unsigned src_w,unsigned src_h,bool rotate) {
    CtrViewport view;
    if(!ctr_viewport_fit(&view,screen_w,screen_h,src_w,src_h))return false;
    cemu_quad_vertex *v=MEMAllocFromDefaultHeapEx(6u*sizeof(*v),256);
    if(!v)return false;
    float x0=(float)view.x*2.0f/(float)screen_w-1.0f;
    float x1=(float)(view.x+view.width)*2.0f/(float)screen_w-1.0f;
    float y0=1.0f-(float)view.y*2.0f/(float)screen_h;
    float y1=1.0f-(float)(view.y+view.height)*2.0f/(float)screen_h;
    const cemu_quad_vertex q[6]={
        {x0,y0,0.0f,1.0f, 0.0f,0.0f,0.0f,1.0f},
        {x1,y0,0.0f,1.0f, 1.0f,0.0f,0.0f,1.0f},
        {x0,y1,0.0f,1.0f, 0.0f,1.0f,0.0f,1.0f},
        {x0,y1,0.0f,1.0f, 0.0f,1.0f,0.0f,1.0f},
        {x1,y0,0.0f,1.0f, 1.0f,0.0f,0.0f,1.0f},
        {x1,y1,0.0f,1.0f, 1.0f,1.0f,0.0f,1.0f}
    };
    memcpy(v,q,sizeof(q));
    if(rotate)for(unsigned i=0;i<6u;++i) {
        v[i].u=1.0f-q[i].v;
        v[i].v=src_w==320u?0.2f+0.8f*q[i].u:q[i].u;
    }
    DCFlushRange(v,6u*sizeof(*v));
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER,v,6u*sizeof(*v));
    *out=v;
    return true;
}
static bool cemu_init_tex(GX2Texture *t,unsigned w,unsigned h) {
    *t=(GX2Texture){
        .surface={.dim=GX2_SURFACE_DIM_TEXTURE_2D,.width=w,.height=h,.depth=1,.mipLevels=1,
            .format=GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8,.aa=GX2_AA_MODE1X,
            .use=GX2_SURFACE_USE_TEXTURE,.tileMode=GX2_TILE_MODE_LINEAR_ALIGNED},
        .viewNumMips=1,.viewNumSlices=1,.compMap=GX2_COMP_MAP(GX2_SQ_SEL_X,GX2_SQ_SEL_Y,GX2_SQ_SEL_Z,GX2_SQ_SEL_W)
    };
    GX2CalcSurfaceSizeAndAlignment(&t->surface);
    if(!t->surface.imageSize)return false;
    t->surface.image=MEMAllocFromDefaultHeapEx(t->surface.imageSize,
        t->surface.alignment<256u?256u:t->surface.alignment);
    if(!t->surface.image)return false;
    memset(t->surface.image,0,t->surface.imageSize);
    DCFlushRange(t->surface.image,t->surface.imageSize);
    GX2InitTextureRegs(t);
    return true;
}
static bool cemu_upload_rgba(unsigned idx,const uint32_t *src,unsigned w,unsigned h) {
    unsigned slot=cemu_gfx.tex_slot[idx];
    if(idx && cemu_gfx.tex_valid[idx]) {
        const GX2Surface *previous=&cemu_gfx.tex[idx][slot^1u].surface;
        unsigned y=0;
        while(y<h && !memcmp((const uint32_t *)previous->image+(size_t)y*previous->pitch,
                             src+(size_t)y*w,(size_t)w*4u))++y;
        if(y==h)return false;
    }
    GX2Texture *t=&cemu_gfx.tex[idx][slot];
    if(cemu_gfx.tex_ts[idx][slot]>GX2GetRetiredTimeStamp())
        (void)GX2WaitTimeStamp(cemu_gfx.tex_ts[idx][slot]);
    for(unsigned y=0;y<h;++y)
        memcpy((uint32_t *)t->surface.image+(size_t)y*t->surface.pitch,src+(size_t)y*w,(size_t)w*4u);
    DCFlushRange(t->surface.image,t->surface.imageSize);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE,t->surface.image,t->surface.imageSize);
    cemu_gfx.tex_slot[idx]=slot^1u;
    cemu_gfx.tex_valid[idx]=true;
    return true;
}
static void scanout_alu(uint8_t *program,unsigned *count,unsigned op,
                        unsigned a,unsigned ac,unsigned b,unsigned bc,unsigned dest,unsigned lane) {
    uint8_t *at=program+256u+8u*(*count)++;
    cemu_put32(at,UINT32_C(0x80000000)|a|(ac<<10)|(b<<13)|(bc<<23));
    cemu_put32(at+4u,(op<<7)|(1u<<4)|(dest<<21)|(lane<<29));
}
static bool cemu_init_scanout_target(unsigned screen) {
    GX2ColorBuffer *target=&cemu_gfx.scanout_target[screen];
    unsigned width=screen?320u:400u;
    if(cemu_gfx.scanout_quad[screen])return true;
    if(target->surface.image)return false;
    target->surface=(GX2Surface){
        .dim=GX2_SURFACE_DIM_TEXTURE_2D,.width=width,.height=240,.depth=1,.mipLevels=1,
        .format=GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8,.aa=GX2_AA_MODE1X,
        .use=GX2_SURFACE_USE_TEXTURE|GX2_SURFACE_USE_COLOR_BUFFER,.tileMode=GX2_TILE_MODE_TILED_2D_THIN1
    };
    GX2CalcSurfaceSizeAndAlignment(&target->surface);
    if(!target->surface.imageSize)return false;
    target->surface.image=MEMAllocFromDefaultHeapEx(target->surface.imageSize,target->surface.alignment);
    if(!target->surface.image)return false;
    DCFlushRange(target->surface.image,target->surface.imageSize);
    target->viewNumSlices=1;
    GX2InitColorBufferRegs(target);
    GX2Texture *texture=&cemu_gfx.scanout_texture[screen];
    *texture=(GX2Texture){.surface=target->surface,.viewNumMips=1,.viewNumSlices=1,
        .compMap=GX2_COMP_MAP(GX2_SQ_SEL_X,GX2_SQ_SEL_Y,GX2_SQ_SEL_Z,GX2_SQ_SEL_W)};
    GX2InitTextureRegs(texture);
    return cemu_init_quad_verts(&cemu_gfx.scanout_quad[screen],width,240,width,240,true);
}
static bool cemu_init_scanout_shader(void) {
    if(cemu_gfx.scanout_shader.program)return true;
    GX2InitSampler(&cemu_gfx.scanout_sampler,GX2_TEX_CLAMP_MODE_CLAMP,GX2_TEX_XY_FILTER_MODE_POINT);
    uint8_t *program=MEMAllocFromDefaultHeapEx(512,256);
    if(!program)return false;
    cemu_gfx.scanout_shader=cemu_gfx.ps;
    cemu_gfx.scanout_shader.program=program;
    cemu_gfx.scanout_shader.size=512;
    cemu_gfx.scanout_shader.regs.sq_pgm_resources_ps=3;
    memset(program,0,512);
    memcpy(program,cemu_gfx.ps_prog,256);
    unsigned count=0;
    /* Quantize each nearest texel before the TV pass applies its linear filter.
     * floor(channel * 255 / step) gives the RGB565 bits. Replicate the high bits
     * into the low bits, exactly as the guest framebuffer reader does. */
    for(unsigned lane=0;lane<3u;++lane) {
        unsigned scale=(lane==1u)?1u:0u;
        scanout_alu(program,&count,2,1,lane,256,scale,1,lane);
        scanout_alu(program,&count,20,1,lane,0,0,1,lane);
        scanout_alu(program,&count,2,1,lane,257,scale,2,lane);
        scanout_alu(program,&count,20,2,lane,0,0,2,lane);
        scanout_alu(program,&count,2,1,lane,258,scale,1,lane);
        scanout_alu(program,&count,0,1,lane,2,lane,1,lane);
        scanout_alu(program,&count,2,1,lane,258,2,1,lane);
    }
    scanout_alu(program,&count,25,249,0,0,0,1,3);
    cemu_put32(program+8u,256u/8u);
    cemu_put32(program+12u,UINT32_C(0xa0000000)|((count-1u)<<18));
    cemu_put32(program+16u,1u<<15);
    cemu_put32(program+20u,UINT32_C(0x94200688));
    DCFlushRange(program,512);

    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER,program,512);
    return true;
}
static void cemu_quantize_scanout(const GX2Texture *source,unsigned screen) {
    /* Slightly round up the byte scales so exact step boundaries survive
     * UNORM-to-float conversion. The offset is smaller than a single byte. */
    static const float constants[12]={31.875004f,63.750008f,0,0,
        0.25f,0.0625f,0,0,8.0f,4.0f,1.0f/255.0f,0};
    GX2ColorBuffer *target=&cemu_gfx.scanout_target[screen];
    unsigned width=target->surface.width;
    GX2SetColorBuffer(target,GX2_RENDER_TARGET_0);
    GX2SetViewport(0.0f,0.0f,(float)width,240.0f,0.0f,1.0f);
    GX2SetScissor(0,0,width,240);
    GX2SetPixelShader(&cemu_gfx.scanout_shader);
    GX2SetPixelUniformReg(0,12,constants);
    GX2Invalidate(GX2_INVALIDATE_MODE_COLOR_BUFFER,source->surface.image,source->surface.imageSize);
    GX2SetPixelTexture(source,0);
    GX2SetPixelSampler(&cemu_gfx.scanout_sampler,0);
    GX2SetAttribBuffer(0,6u*sizeof(cemu_quad_vertex),sizeof(cemu_quad_vertex),cemu_gfx.scanout_quad[screen]);
    GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES,6,0,1);
    GX2Invalidate(GX2_INVALIDATE_MODE_COLOR_BUFFER,target->surface.image,target->surface.imageSize);
}
static bool cemu_present(bool guest,const ctr_diagnostic_scanout *top_scanout,
                         const ctr_diagnostic_scanout *bottom_scanout) {
    const GX2Surface *tv=&WHBGfxGetTVColourBuffer()->surface;
    const GX2Surface *drc=&WHBGfxGetDRCColourBuffer()->surface;
    unsigned ti=guest?1u:0u,di=guest?2u:0u;
    unsigned tslot=cemu_gfx.tex_slot[ti]^1u,dslot=cemu_gfx.tex_slot[di]^1u;
    WHBGfxBeginRender();
    WHBGfxBeginRenderTV();
    WHBGfxClearColor(0.0f,0.0f,0.0f,1.0f);
    GX2SetShaderModeEx(GX2_SHADER_MODE_UNIFORM_REGISTER,48,64,0,0,200,192);
    GX2SetViewport(0.0f,0.0f,(float)tv->width,(float)tv->height,0.0f,1.0f);
    GX2SetScissor(0,0,tv->width,tv->height);
    GX2SetDepthStencilControl(false,false,GX2_COMPARE_FUNC_ALWAYS,false,false,0,0,0,0,0,0,0,0);
    GX2SetColorControl(GX2_LOGIC_OP_COPY,0,false,true);
    GX2SetBlendControl(GX2_RENDER_TARGET_0,GX2_BLEND_MODE_ONE,GX2_BLEND_MODE_ZERO,GX2_BLEND_COMBINE_MODE_ADD,
                       true,GX2_BLEND_MODE_ONE,GX2_BLEND_MODE_ZERO,GX2_BLEND_COMBINE_MODE_ADD);
    GX2SetTargetChannelMasks(GX2_CHANNEL_MASK_RGBA,0,0,0,0,0,0,0);
    GX2SetAlphaTest(false,GX2_COMPARE_FUNC_ALWAYS,0.0f);
    GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW,false,false);
    GX2SetFetchShader(&cemu_gfx.fs);
    GX2SetVertexShader(&cemu_gfx.vs);
    if(top_scanout && (!top_scanout->generation ||
       top_scanout->generation!=cemu_gfx.scanout_generation[0])) {
        cemu_quantize_scanout(top_scanout->texture,0);
        cemu_gfx.scanout_generation[0]=top_scanout->generation;
        GX2SetColorBuffer(WHBGfxGetTVColourBuffer(),GX2_RENDER_TARGET_0);
        GX2SetViewport(0.0f,0.0f,(float)tv->width,(float)tv->height,0.0f,1.0f);
        GX2SetScissor(0,0,tv->width,tv->height);
    }
    GX2SetPixelShader(&cemu_gfx.ps);
    GX2SetPixelTexture(top_scanout?&cemu_gfx.scanout_texture[0]:&cemu_gfx.tex[ti][tslot],0);
    GX2SetPixelSampler(&cemu_gfx.sampler,0);
    GX2SetAttribBuffer(0,6u*sizeof(cemu_quad_vertex),sizeof(cemu_quad_vertex),cemu_gfx.verts[guest?2:0]);
    GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES,6,0,1);
    WHBGfxFinishRenderTV();

    WHBGfxBeginRenderDRC();
    WHBGfxClearColor(0.0f,0.0f,0.0f,1.0f);
    GX2SetShaderModeEx(GX2_SHADER_MODE_UNIFORM_REGISTER,48,64,0,0,200,192);
    GX2SetViewport(0.0f,0.0f,(float)drc->width,(float)drc->height,0.0f,1.0f);
    GX2SetScissor(0,0,drc->width,drc->height);
    GX2SetDepthStencilControl(false,false,GX2_COMPARE_FUNC_ALWAYS,false,false,0,0,0,0,0,0,0,0);
    GX2SetColorControl(GX2_LOGIC_OP_COPY,0,false,true);
    GX2SetBlendControl(GX2_RENDER_TARGET_0,GX2_BLEND_MODE_ONE,GX2_BLEND_MODE_ZERO,GX2_BLEND_COMBINE_MODE_ADD,
                       true,GX2_BLEND_MODE_ONE,GX2_BLEND_MODE_ZERO,GX2_BLEND_COMBINE_MODE_ADD);
    GX2SetTargetChannelMasks(GX2_CHANNEL_MASK_RGBA,0,0,0,0,0,0,0);
    GX2SetAlphaTest(false,GX2_COMPARE_FUNC_ALWAYS,0.0f);
    GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW,false,false);
    GX2SetFetchShader(&cemu_gfx.fs);
    GX2SetVertexShader(&cemu_gfx.vs);
    if(bottom_scanout && (!bottom_scanout->generation ||
       bottom_scanout->generation!=cemu_gfx.scanout_generation[1])) {
        cemu_quantize_scanout(bottom_scanout->texture,1);
        cemu_gfx.scanout_generation[1]=bottom_scanout->generation;
        GX2SetColorBuffer(WHBGfxGetDRCColourBuffer(),GX2_RENDER_TARGET_0);
        GX2SetViewport(0.0f,0.0f,(float)drc->width,(float)drc->height,0.0f,1.0f);
        GX2SetScissor(0,0,drc->width,drc->height);
    }
    GX2SetPixelShader(&cemu_gfx.ps);
    GX2SetPixelTexture(bottom_scanout?&cemu_gfx.scanout_texture[1]:&cemu_gfx.tex[di][dslot],0);
    GX2SetPixelSampler(&cemu_gfx.sampler,0);
    GX2SetAttribBuffer(0,6u*sizeof(cemu_quad_vertex),sizeof(cemu_quad_vertex),cemu_gfx.verts[guest?3:1]);
    GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES,6,0,1);
    WHBGfxFinishRenderDRC();
    WHBGfxFinishRender();
    OSTime ts=GX2GetLastSubmittedTimeStamp();
    if(!top_scanout)cemu_gfx.tex_ts[ti][tslot]=ts;
    if(!bottom_scanout)cemu_gfx.tex_ts[di][dslot]=ts;
    presented=true;
    return true;
}
#endif
GX2ContextState *ctr_diagnostic_display_context(void) {
    return initialized?WHBGfxGetTVContextState():NULL;
}
bool ctr_diagnostic_display_init(void) {
    if(initialized)return ready;
#ifdef CTR_GAME_CEMU_TEST
    if(!WHBGfxInit())return false;
    initialized=true;
    GX2SetSwapInterval(0);
    const GX2Surface *tv=&WHBGfxGetTVColourBuffer()->surface;
    const GX2Surface *drc=&WHBGfxGetDRCColourBuffer()->surface;
    surfaces[0].width=tv->width;surfaces[0].height=tv->height;
    surfaces[1].width=drc->width;surfaces[1].height=drc->height;
    cemu_gfx.vs_prog=MEMAllocFromDefaultHeapEx(256,256);
    cemu_gfx.ps_prog=MEMAllocFromDefaultHeapEx(256,256);
    uint32_t fsize=GX2CalcFetchShaderSizeEx(2,GX2_FETCH_SHADER_TESSELLATION_NONE,GX2_TESSELLATION_MODE_DISCRETE);
    cemu_gfx.fetch_prog=MEMAllocFromDefaultHeapEx(fsize<256u?256u:fsize,256);
    if(!cemu_gfx.vs_prog || !cemu_gfx.ps_prog || !cemu_gfx.fetch_prog)return false;
    memset(cemu_gfx.vs_prog,0,256);
    cemu_put32(cemu_gfx.vs_prog+4u,UINT32_C(0x09800000));
    cemu_put32(cemu_gfx.vs_prog+8u,60u|(1u<<13)|(1u<<15));
    cemu_put32(cemu_gfx.vs_prog+12u,UINT32_C(0x94000688));
    cemu_put32(cemu_gfx.vs_prog+16u,0u|(2u<<13)|(2u<<15));
    cemu_put32(cemu_gfx.vs_prog+20u,UINT32_C(0x14200b08));
    DCFlushRange(cemu_gfx.vs_prog,256);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER,cemu_gfx.vs_prog,256);
    cemu_gfx.vs=(GX2VertexShader){.size=256,.program=cemu_gfx.vs_prog,.mode=GX2_SHADER_MODE_UNIFORM_REGISTER};
    cemu_gfx.vs.regs.sq_pgm_resources_vs=17u|0x100u;
    cemu_gfx.vs.regs.spi_vs_out_config=1u<<1;
    cemu_gfx.vs.regs.num_spi_vs_out_id=1;
    for(unsigned i=0;i<10u;++i)cemu_gfx.vs.regs.spi_vs_out_id[i]=UINT32_C(0xffffffff);
    cemu_gfx.vs.regs.spi_vs_out_id[0]&=~UINT32_C(0xff);
    cemu_gfx.vs.regs.sq_vtx_semantic_clear=UINT32_C(0xfffffffc);
    cemu_gfx.vs.regs.num_sq_vtx_semantic=2;
    cemu_gfx.vs.regs.sq_vtx_semantic[0]=0;cemu_gfx.vs.regs.sq_vtx_semantic[1]=1;
    cemu_gfx.vs.regs.vgt_vertex_reuse_block_cntl=14;cemu_gfx.vs.regs.vgt_hos_reuse_depth=16;

    memset(cemu_gfx.ps_prog,0,256);
    cemu_put32(cemu_gfx.ps_prog+0u,16u);
    cemu_put32(cemu_gfx.ps_prog+4u,UINT32_C(0x80800000));
    cemu_put32(cemu_gfx.ps_prog+8u,1u<<15);
    cemu_put32(cemu_gfx.ps_prog+12u,UINT32_C(0x94200688));
    cemu_put32(cemu_gfx.ps_prog+128u,0x13u|(0u<<8)|(0u<<16));
    cemu_put32(cemu_gfx.ps_prog+132u,UINT32_C(0xf00d1000)|1u);
    cemu_put32(cemu_gfx.ps_prog+136u,UINT32_C(0xb0800000));
    DCFlushRange(cemu_gfx.ps_prog,256);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER,cemu_gfx.ps_prog,256);
    cemu_gfx.ps=(GX2PixelShader){.size=256,.program=cemu_gfx.ps_prog,.mode=GX2_SHADER_MODE_UNIFORM_REGISTER};
    cemu_gfx.ps.regs.sq_pgm_resources_ps=2;cemu_gfx.ps.regs.sq_pgm_exports_ps=2;
    cemu_gfx.ps.regs.spi_ps_in_control_0=UINT32_C(0x14000000)|1u;
    cemu_gfx.ps.regs.num_spi_ps_input_cntl=1;cemu_gfx.ps.regs.spi_ps_input_cntls[0]=0x100u;
    cemu_gfx.ps.regs.cb_shader_mask=15;cemu_gfx.ps.regs.cb_shader_control=1;cemu_gfx.ps.regs.db_shader_control=0x10;

    GX2AttribStream attrs[2]={0};
    for(unsigned i=0;i<2u;++i) {
        attrs[i].location=i;attrs[i].buffer=0;attrs[i].offset=i*16u;
        attrs[i].format=GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32;
        attrs[i].type=GX2_ATTRIB_INDEX_PER_VERTEX;
        attrs[i].mask=GX2_SEL_MASK(GX2_SQ_SEL_X,GX2_SQ_SEL_Y,GX2_SQ_SEL_Z,GX2_SQ_SEL_W);
        attrs[i].endianSwap=GX2_ENDIAN_SWAP_8_IN_32;
    }
    GX2InitFetchShaderEx(&cemu_gfx.fs,cemu_gfx.fetch_prog,2,attrs,
                         GX2_FETCH_SHADER_TESSELLATION_NONE,GX2_TESSELLATION_MODE_DISCRETE);
    DCFlushRange(cemu_gfx.fetch_prog,fsize);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER,cemu_gfx.fetch_prog,fsize);
    GX2InitSampler(&cemu_gfx.sampler,GX2_TEX_CLAMP_MODE_CLAMP,GX2_TEX_XY_FILTER_MODE_LINEAR);

    if(!cemu_init_quad_verts(&cemu_gfx.verts[0],tv->width,tv->height,WIDTH,HEIGHT,false) ||
       !cemu_init_quad_verts(&cemu_gfx.verts[1],drc->width,drc->height,WIDTH,HEIGHT,false) ||
       !cemu_init_quad_verts(&cemu_gfx.verts[2],tv->width,tv->height,400u,240u,false) ||
       !cemu_init_quad_verts(&cemu_gfx.verts[3],drc->width,drc->height,320u,240u,false))return false;
    static const unsigned tw[3]={WIDTH,400u,320u},th[3]={HEIGHT,240u,240u};
    for(unsigned i=0;i<3u;++i)
        for(unsigned s=0;s<2u;++s)
            if(!cemu_init_tex(&cemu_gfx.tex[i][s],tw[i],th[i]))return false;
#else
    if(!WHBGfxInit())return false;
    initialized=true;
    const GX2Surface *targets[2]={&WHBGfxGetTVColourBuffer()->surface,&WHBGfxGetDRCColourBuffer()->surface};
    for(unsigned i=0;i<2;++i) {
        GX2Surface *s=&surfaces[i];const GX2Surface *target=targets[i];
        *s=(GX2Surface){.dim=GX2_SURFACE_DIM_TEXTURE_2D,.width=target->width,.height=target->height,
            .depth=1,.mipLevels=1,.format=GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8,
            .aa=GX2_AA_MODE1X,.use=GX2_SURFACE_USE_TEXTURE,.tileMode=GX2_TILE_MODE_LINEAR_ALIGNED};
        GX2CalcSurfaceSizeAndAlignment(s);
        if(target->format!=s->format || !s->imageSize || !s->alignment)return false;
        s->image=MEMAllocFromDefaultHeapEx(s->imageSize,s->alignment);
        CtrViewport view;
        if(!s->image || !ctr_viewport_fit(&view,target->width,target->height,WIDTH,HEIGHT) ||
           !ctr_scale_plan_init(&plans[i],&view))return false;
        memset(s->image,0,s->imageSize);
    }
#endif
    ready=true;dirty=true;return true;
}

#ifndef CTR_GAME_CEMU_TEST
static bool configure_plans(unsigned top_width,unsigned top_height,unsigned bottom_width,unsigned bottom_height) {
    const unsigned widths[]={top_width,bottom_width},heights[]={top_height,bottom_height};
    for(unsigned i=0;i<2;++i) {
        if(plans[i].view.guest_width==widths[i] && plans[i].view.guest_height==heights[i])continue;
        CtrViewport view;ctr_scale_plan next={0};
        if(!ctr_viewport_fit(&view,surfaces[i].width,surfaces[i].height,widths[i],heights[i]) ||
            !ctr_scale_plan_init(&next,&view))return false;
        ctr_scale_plan_close(&plans[i]);plans[i]=next;
    }
    return true;
}
static bool present(void) {
    WHBGfxBeginRender();
    WHBGfxBeginRenderTV();
    GX2CopySurface(&surfaces[0],0,0,&WHBGfxGetTVColourBuffer()->surface,0,0);
    WHBGfxFinishRenderTV();
    WHBGfxBeginRenderDRC();
    GX2CopySurface(&surfaces[1],0,0,&WHBGfxGetDRCColourBuffer()->surface,0,0);
    WHBGfxFinishRenderDRC();
    WHBGfxFinishRender();
    presented=true;return true;
}
#endif
bool ctr_diagnostic_display_draw(void) {
    if(!ready)return false;
    if(guest_mode) {
#ifndef CTR_GAME_CEMU_TEST
        if(!configure_plans(WIDTH,HEIGHT,WIDTH,HEIGHT))return false;
#endif
        guest_mode=false;dirty=true;
    }
#ifdef CTR_GAME_CEMU_TEST
    if(!dirty && presented)return true;
#endif
    if(dirty) {
#ifndef CTR_GAME_CEMU_TEST
        if(presented)GX2DrawDone();
#endif
        for(unsigned i=0;i<WIDTH*HEIGHT;++i)pixels[i]=UINT32_C(0x102238ff);
        char title[84];snprintf(title,sizeof(title),"%s / %.12s",heading,CTR_SOURCE_COMMIT);
        ctr_draw_text(pixels,WIDTH,HEIGHT,8,10,title,UINT32_C(0x74ceffff));
        for(unsigned i=0;i<ROWS;++i)
            ctr_draw_text(pixels,WIDTH,HEIGHT,8,34+i*16,rows[i],UINT32_C(0xffffffff));
        ctr_draw_text(pixels,WIDTH,HEIGHT,8,HEIGHT-14,footer,UINT32_C(0x74ceffff));
#ifdef CTR_GAME_CEMU_TEST
        cemu_upload_rgba(0,pixels,WIDTH,HEIGHT);
#else
        for(unsigned i=0;i<2;++i) {
            GX2Surface *s=&surfaces[i];
            if(!ctr_scale_rgba(&plans[i],pixels,WIDTH*HEIGHT,s->image,s->imageSize,s->pitch))return false;
            DCFlushRange(s->image,s->imageSize);
            GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE,s->image,s->imageSize);
        }
#endif
        dirty=false;
    }
#ifdef CTR_GAME_CEMU_TEST
    cemu_gfx.gpu_scanout=false;
    return cemu_present(false,NULL,NULL);
#else
    return present();
#endif
}
#ifdef CTR_GAME_CEMU_TEST
bool ctr_diagnostic_display_scanout(const ctr_diagnostic_scanout *top_texture,const ctr_diagnostic_scanout *bottom_texture,
                                    const uint32_t *top,const uint32_t *bottom) {
    if(!ready || (!top_texture && !bottom_texture) || !cemu_init_scanout_shader() ||
       (top_texture?!cemu_init_scanout_target(0):!top) ||
       (bottom_texture?!cemu_init_scanout_target(1):!bottom))return false;
    if(!top_texture)cemu_upload_rgba(1,top,400u,240u);
    if(!bottom_texture)cemu_upload_rgba(2,bottom,320u,240u);
    cemu_gfx.gpu_scanout=true;guest_mode=true;
    return cemu_present(true,top_texture,bottom_texture);
}
#endif
bool ctr_diagnostic_display_guest(const uint32_t *top,const uint32_t *bottom) {
#ifdef CTR_GAME_CEMU_TEST
    if(!ready || !top || !bottom)return false;
    bool top_changed=cemu_upload_rgba(1,top,400u,240u);
    bool bottom_changed=cemu_upload_rgba(2,bottom,320u,240u);
    bool redraw=top_changed || bottom_changed || !guest_mode || !presented || cemu_gfx.gpu_scanout;
    cemu_gfx.gpu_scanout=false;guest_mode=true;
    return !redraw || cemu_present(true,NULL,NULL);
#else
    if(!ready || !top || !bottom || !configure_plans(400,240,320,240))return false;
    if(presented)GX2DrawDone();
    const uint32_t *images[]={top,bottom};const size_t counts[]={400u*240u,320u*240u};
    for(unsigned i=0;i<2;++i) {
        GX2Surface *s=&surfaces[i];
        if(!ctr_scale_rgba(&plans[i],images[i],counts[i],s->image,s->imageSize,s->pitch))return false;
        DCFlushRange(s->image,s->imageSize);
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE,s->image,s->imageSize);
    }
    guest_mode=true;return present();
#endif
}
bool ctr_diagnostic_display_touch(uint32_t x,uint32_t y,uint16_t *guest_x,uint16_t *guest_y) {
    CtrViewport view;
    return ready && ctr_viewport_fit(&view,surfaces[1].width,surfaces[1].height,320,240) &&
        ctr_viewport_touch(&view,x,y,1280,720,guest_x,guest_y);
}

void ctr_diagnostic_display_invalidate(void) {
    dirty=true;
#ifdef CTR_GAME_CEMU_TEST
    cemu_gfx.scanout_generation[0]=0;
    cemu_gfx.scanout_generation[1]=0;
    presented=false;
#endif
}

void ctr_diagnostic_display_close(bool foreground) {
    if(initialized && foreground)GX2DrawDone();
#ifdef CTR_GAME_CEMU_TEST
    if(cemu_gfx.vs_prog)MEMFreeToDefaultHeap(cemu_gfx.vs_prog);
    if(cemu_gfx.ps_prog)MEMFreeToDefaultHeap(cemu_gfx.ps_prog);
    if(cemu_gfx.fetch_prog)MEMFreeToDefaultHeap(cemu_gfx.fetch_prog);
    if(cemu_gfx.scanout_shader.program)MEMFreeToDefaultHeap(cemu_gfx.scanout_shader.program);
    for(unsigned i=0;i<2u;++i) {
        if(cemu_gfx.scanout_quad[i])MEMFreeToDefaultHeap(cemu_gfx.scanout_quad[i]);
        if(cemu_gfx.scanout_target[i].surface.image)MEMFreeToDefaultHeap(cemu_gfx.scanout_target[i].surface.image);
    }
    for(unsigned i=0;i<4u;++i)if(cemu_gfx.verts[i])MEMFreeToDefaultHeap(cemu_gfx.verts[i]);
    for(unsigned i=0;i<3u;++i)
        for(unsigned s=0;s<2u;++s)
            if(cemu_gfx.tex[i][s].surface.image)MEMFreeToDefaultHeap(cemu_gfx.tex[i][s].surface.image);
    memset(&cemu_gfx,0,sizeof(cemu_gfx));
    memset(surfaces,0,sizeof(surfaces));
#else
    for(unsigned i=0;i<2;++i) {
        ctr_scale_plan_close(&plans[i]);
        if(surfaces[i].image)MEMFreeToDefaultHeap(surfaces[i].image);
        memset(&surfaces[i],0,sizeof(surfaces[i]));
    }
#endif
    if(initialized)WHBGfxShutdown();
    initialized=ready=presented=guest_mode=false;
}
