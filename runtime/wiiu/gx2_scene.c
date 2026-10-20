/* Original bounded PICA -> GX2 offscreen submission. ISA export facts and shader
 * register metadata were checked against devkitPro/wut commit
 * 937f0d6901f5e2a72672154a674ec47b63e2f417, samples/cmake/content/pos_col_shader;
 * TEX/ALU/CF fields against decaf-emu/decaf-emu
 * e6c528a20a41c34e0f9eb91dd3da40f119db2dee, src/libgpu/latte definitions.
 * No pretransformed reference vertices or software raster pixels are submitted. */
#include "gx2_scene.h"
#include "ctr_fragment.h"
#ifdef CTR_GX2_SCENE_MOCK
#include "gx2_mock.h"
#else
#include <coreinit/cache.h>
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
#endif
#include <malloc.h>
#include <stdlib.h>
#include <string.h>

#define MAX_IMAGE_BYTES (2u*1024u*1024u)
#define MAX_TEXTURE_BYTES (8u*1024u*1024u)
#define VERTEX_STRIDE (16u*4u*sizeof(float))
#define MAX_FETCH_BYTES 4096u
#define PIXEL_BYTES 8192u
#define PIXEL_ALU_OFFSET 256u
#define PIXEL_MAX_ALUS 768u

typedef struct {
    GX2ContextState *context;
    GX2ColorBuffer target;
    GX2Surface linear;
    GX2FetchShader fetch;
    GX2VertexShader vertex;
    GX2PixelShader pixel;
    GX2Texture texture;
    GX2Sampler sampler;
    ctr_gx2_shader *compiled;
    void *vertex_program,*pixel_program,*fetch_program,*vertices;
    unsigned width,height,stride;
    size_t size;
    bool pending;
    uint32_t pixel_constants[8][4];
} scene_private;

static bool retire(ctr_wiiu_gx2_scene *s,scene_private *p);
static bool restore(ctr_wiiu_gx2_scene *s) {
    if(s && s->restore_context) {
        GX2SetContextState(s->restore_context);
        /* Context switching can queue shadow-state memory references. Drain
         * the switch before a context allocation can be reclaimed or reused. */
        scene_private *p=s->private_state;
        if(p && p->context) { p->pending=true;return retire(s,p); }
    }
    return true;
}
static bool fail(ctr_wiiu_gx2_scene *s,ctr_wiiu_gx2_scene_result result) {
    if(s)s->result=result;
    restore(s);return false;
}
static void *allocate(ctr_wiiu_gx2_scene *s,size_t alignment,size_t bytes) {
    if(alignment<256u)alignment=256u;
    void *p=memalign(alignment,bytes);
    if(p) { memset(p,0,bytes);++s->allocations; }
    return p;
}
static void dispose(ctr_wiiu_gx2_scene *s,void *p) {
    if(p) { free(p);++s->releases; }
}
static bool retire(ctr_wiiu_gx2_scene *s,scene_private *p) {
    if(!p->pending)return true;
    ++s->waits;
    if(!GX2DrawDone()) {
        ++s->wait_failures;s->quarantined=true;s->result=CTR_WIIU_GX2_SCENE_WAIT;
        return false;
    }
    p->pending=false;s->quarantined=false;return true;
}
static void release(ctr_wiiu_gx2_scene *s,scene_private *p) {
    dispose(s,p->texture.surface.image);
    dispose(s,p->vertices);dispose(s,p->fetch_program);
    dispose(s,p->pixel_program);dispose(s,p->vertex_program);
    dispose(s,p->compiled);dispose(s,p->linear.image);
    dispose(s,p->target.surface.image);dispose(s,p->context);dispose(s,p);
    s->private_state=NULL;s->quarantined=false;
}
void ctr_wiiu_gx2_scene_abort(ctr_wiiu_gx2_scene *s) {
    if(!s)return;
    scene_private *p=s->private_state;
    /* A completed transaction already restored WHB. Keep idle aborts inert,
     * including application cleanup after ProcUI has released foreground. */
    if(p && restore(s) && retire(s,p))release(s,p);
}
void ctr_wiiu_gx2_scene_after_shutdown(ctr_wiiu_gx2_scene *s) {
    if(!s)return;
    if(s->private_state)release(s,s->private_state);
    s->restore_context=NULL;
}
static bool dimensions(const ctr_scene_surface *surface) {
    return surface && surface->bytes && surface->width && surface->height &&
        surface->width<=512u && surface->height<=512u &&
        surface->stride>=surface->width*4u &&
        (uint64_t)surface->stride*surface->height<=surface->size &&
        surface->size<=CTR_SCENE_MAX_SURFACE_BYTES;
}
static bool layout(GX2Surface *surface,unsigned width,unsigned height,GX2TileMode tile) {
    surface->dim=GX2_SURFACE_DIM_TEXTURE_2D;surface->width=width;surface->height=height;
    surface->depth=1;surface->mipLevels=1;surface->format=GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
    surface->aa=GX2_AA_MODE1X;surface->use=GX2_SURFACE_USE_TEXTURE|GX2_SURFACE_USE_COLOR_BUFFER;
    surface->tileMode=tile;GX2CalcSurfaceSizeAndAlignment(surface);
    return surface->tileMode==tile && surface->pitch>=width &&
        surface->imageSize && surface->imageSize<=MAX_IMAGE_BYTES &&
        surface->alignment && !(surface->alignment&(surface->alignment-1u)) &&
        surface->alignment<=65536u && !surface->mipmapSize;
}
static void put32(uint8_t *p,uint32_t value) {
    for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(value>>(8u*i));
}
/* Original scalar Latte encoder. R0 is primary, R1 original guest UV, R2
 * Texture0. R3/R7 alternate Previous/output; R4..R6 snapshot all operands before
 * any output lane changes. R8 is scratch. Independent LAST groups avoid a VLIW
 * bank scheduler. Constants 0..5 contain stage bytes; 6/7 are numeric helpers. */
static uint32_t bits(float value);
static void pixel_op(scene_private *p,unsigned *count,unsigned opcode,unsigned a,unsigned ac,
                      unsigned b,unsigned bc,bool negate,unsigned dest,unsigned lane) {
    if(*count>=PIXEL_MAX_ALUS) { *count=PIXEL_MAX_ALUS+1u;return; }
    uint8_t *at=(uint8_t *)p->pixel_program+PIXEL_ALU_OFFSET+(*count)++*8u;
    put32(at,a|(ac<<10)|(b<<13)|(bc<<23)|(negate?UINT32_C(1)<<25:0u)|UINT32_C(0x80000000));
    put32(at+4u,(opcode<<7)|(1u<<4)|(dest<<21)|(lane<<29));
}
static void pixel_divide(scene_private *p,unsigned *count,unsigned dest,unsigned lane) {
    pixel_op(p,count,2,dest,lane,262,2,false,dest,lane);
    pixel_op(p,count,20,dest,lane,0,0,false,dest,lane);
}
static void pixel_combine(scene_private *p,unsigned *count,unsigned op,unsigned dest,unsigned lane) {
    pixel_op(p,count,op==1u || op==4u || op==8u?2u:op==0u?25u:0u,
        4,lane,op==4u?6u:5u,lane,op==5u,dest,lane);
    if(op==3u)pixel_op(p,count,0,dest,lane,262,1,true,dest,lane);
    if(op==4u) {
        pixel_op(p,count,0,262,0,6,lane,true,8,lane);
        pixel_op(p,count,2,5,lane,8,lane,false,8,lane);
        pixel_op(p,count,0,dest,lane,8,lane,false,dest,lane);
    }
    if(op==9u) {
        pixel_op(p,count,4,dest,lane,262,0,false,dest,lane);
        pixel_op(p,count,2,dest,lane,6,lane,false,dest,lane);
    }
    if(op==1u || op==4u || op==8u || op==9u)pixel_divide(p,count,dest,lane);
    if(op==8u)pixel_op(p,count,0,dest,lane,6,lane,false,dest,lane);
}
static bool pixel_shader(scene_private *p,const ctr_scene_state *state,bool textured) {
    ctr_tev_program tev;
    if(!ctr_tev_decode(state->registers,state->register_defined,textured,&tev))return false;
    uint8_t *program=p->pixel_program;memset(program,0,PIXEL_BYTES);
    unsigned count=0,previous=3,destination=7;
    for(unsigned stage=0;stage<CTR_TEV_STAGES;++stage)
        for(unsigned lane=0;lane<4;++lane)p->pixel_constants[stage][lane]=bits((float)tev.stages[stage].constant[lane]);
    p->pixel_constants[6][0]=bits(255.0f);p->pixel_constants[6][1]=bits(128.0f);
    /* The upward binary32 reciprocal gives exact floor(N/255), 0<=N<=65025.
     * Integer products/sums stay exactly representable; no fused operation. */
    p->pixel_constants[6][2]=UINT32_C(0x3b808081);p->pixel_constants[6][3]=bits(0.5f);
    for(unsigned lane=0;lane<4;++lane)p->pixel_constants[7][lane]=bits(lane<3u?(float)(1u<<lane):0.0f);
    for(unsigned lane=0;lane<4;++lane) {
        pixel_op(p,&count,2,0,lane,262,0,false,0,lane);
        pixel_op(p,&count,0,0,lane,262,3,false,0,lane);
        pixel_op(p,&count,20,0,lane,0,0,false,0,lane);
        if(textured) {
            pixel_op(p,&count,2,2,lane,262,0,false,2,lane);
            pixel_op(p,&count,0,2,lane,262,3,false,2,lane);
            pixel_op(p,&count,20,2,lane,0,0,false,2,lane);
        }
    }
    for(unsigned stage=0;stage<CTR_TEV_STAGES;++stage) {
        const ctr_tev_stage *v=&tev.stages[stage];
        if(ctr_tev_passthrough(v))continue;
        for(unsigned lane=0;lane<4;++lane) {
            unsigned side=lane==3u;
            for(unsigned arg=0;arg<ctr_tev_arguments(v->operation[side]);++arg) {
                unsigned source=v->source[side][arg],operand=v->operand[side][arg];
                unsigned reg=source==0u?0u:source==3u?2u:source==14u?256u+stage:previous;
                const unsigned alpha_channels[4]={3,0,1,2};
                unsigned channel=side?alpha_channels[operand/2u]:
                    operand<2u?lane:operand<4u?3u:operand<6u?0u:operand<10u?1u:2u;
                pixel_op(p,&count,25,reg,channel,0,0,false,4u+arg,lane);
                if(operand&1u)pixel_op(p,&count,0,262,0,4u+arg,lane,true,4u+arg,lane);
            }
        }
        for(unsigned lane=0;lane<4;++lane) {
            unsigned side=lane==3u;
            pixel_combine(p,&count,v->operation[side],destination,lane);
            pixel_op(p,&count,3,destination,lane,263,3,false,destination,lane);
            pixel_op(p,&count,4,destination,lane,262,0,false,destination,lane);
            if(v->scale[side]) {
                pixel_op(p,&count,2,destination,lane,263,v->scale[side],false,destination,lane);
                pixel_op(p,&count,4,destination,lane,262,0,false,destination,lane);
            }
        }
        unsigned swap=previous;previous=destination;destination=swap;
    }
    for(unsigned lane=0;lane<4;++lane)pixel_op(p,&count,2,previous,lane,262,2,false,previous,lane);
    if(count>PIXEL_MAX_ALUS)return false;
    unsigned cf=0;
    if(textured) {
        put32(program,128u/8u);put32(program+4u,UINT32_C(0x80800000));cf=8;
        put32(program+128u,UINT32_C(0x00010013));
        put32(program+132u,UINT32_C(0xf00d1002));
        put32(program+136u,UINT32_C(0xb0800000));
    }
    for(unsigned first=0;first<count;first+=128u) {
        unsigned length=count-first;if(length>128u)length=128u;
        put32(program+cf,PIXEL_ALU_OFFSET/8u+first);
        put32(program+cf+4u,UINT32_C(0xa0000000)|((length-1u)<<18));cf+=8;
    }
    if(cf>120u)return false;
    put32(program+cf,previous<<15);put32(program+cf+4u,UINT32_C(0x94200688));
    memset(&p->pixel,0,sizeof(p->pixel));
    p->pixel.size=(PIXEL_ALU_OFFSET+count*8u+255u)&~255u;
    p->pixel.program=program;p->pixel.mode=GX2_SHADER_MODE_UNIFORM_REGISTER;
    p->pixel.regs.sq_pgm_resources_ps=9u;p->pixel.regs.sq_pgm_exports_ps=2;
    p->pixel.regs.spi_ps_in_control_0=UINT32_C(0x14000000)|(textured?2u:1u);
    p->pixel.regs.num_spi_ps_input_cntl=textured?2u:1u;
    p->pixel.regs.spi_ps_input_cntls[0]=UINT32_C(0x100);
    if(textured)p->pixel.regs.spi_ps_input_cntls[1]=UINT32_C(0x101);
    p->pixel.regs.cb_shader_mask=15;p->pixel.regs.cb_shader_control=1;
    p->pixel.regs.db_shader_control=UINT32_C(0x10);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER,program,p->pixel.size);return true;
}
static bool upload_texture(ctr_wiiu_gx2_scene *s,scene_private *p,
                           const ctr_scene_texture *source) {
    if(!source)return true;
    unsigned wrap_t=(source->params>>8)&7u,wrap_s=(source->params>>12)&7u;
    unsigned filter=source->params&6u;
    uint32_t expected_params=(wrap_t<<8)|(wrap_s<<12)|(source->format>=12u?0x20u:0u)|filter;
    if(!source->rgba || source->width<8u || source->height<8u ||
       source->width>1024u || source->height>1024u ||
       (source->width&(source->width-1u)) || (source->height&(source->height-1u)) ||
       source->format>=14u || source->params!=expected_params ||
       (filter!=0u && filter!=6u) ||
       (wrap_t!=0u && wrap_t!=2u && wrap_t!=3u) ||
       (wrap_s!=0u && wrap_s!=2u && wrap_s!=3u) ||
       (size_t)source->width*source->height*4u>UINTPTR_MAX-(uintptr_t)source->rgba)
        return fail(s,CTR_WIIU_GX2_SCENE_ARGUMENT);
    /* Every previous submission and context-state reference must retire before
     * an image or its CPU cache lines can be overwritten or released. */
    if(!retire(s,p))return false;
    if(p->texture.surface.width!=source->width || p->texture.surface.height!=source->height) {
        GX2Texture next;memset(&next,0,sizeof(next));
        next.surface.dim=GX2_SURFACE_DIM_TEXTURE_2D;
        next.surface.width=source->width;next.surface.height=source->height;
        next.surface.depth=1;next.surface.mipLevels=1;
        next.surface.format=GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
        next.surface.aa=GX2_AA_MODE1X;next.surface.use=GX2_SURFACE_USE_TEXTURE;
        next.surface.tileMode=GX2_TILE_MODE_LINEAR_ALIGNED;
        GX2CalcSurfaceSizeAndAlignment(&next.surface);
        if(next.surface.tileMode!=GX2_TILE_MODE_LINEAR_ALIGNED || next.surface.pitch<source->width ||
           !next.surface.imageSize || next.surface.imageSize>MAX_TEXTURE_BYTES ||
           (uint64_t)next.surface.pitch*source->height*4u>next.surface.imageSize ||
           !next.surface.alignment || (next.surface.alignment&(next.surface.alignment-1u)) ||
           next.surface.alignment>65536u || next.surface.mipmapSize)
            return fail(s,CTR_WIIU_GX2_SCENE_LAYOUT);
        next.surface.image=allocate(s,next.surface.alignment,next.surface.imageSize);
        if(!next.surface.image)return fail(s,CTR_WIIU_GX2_SCENE_ALLOCATION);
        next.viewNumMips=1;next.viewNumSlices=1;
        next.compMap=GX2_COMP_MAP(GX2_SQ_SEL_X,GX2_SQ_SEL_Y,GX2_SQ_SEL_Z,GX2_SQ_SEL_W);
        GX2InitTextureRegs(&next);
        dispose(s,p->texture.surface.image);p->texture=next;
    }
    /* PICA normalized V=0 addresses the last raw Morton-storage row. Reverse
     * the decoded upload rows while preserving the actual guest UV exports. */
    for(unsigned y=0;y<source->height;++y)
        memcpy((uint8_t *)p->texture.surface.image+(size_t)y*p->texture.surface.pitch*4u,
               source->rgba+(size_t)(source->height-1u-y)*source->width*4u,source->width*4u);
    DCFlushRange(p->texture.surface.image,p->texture.surface.imageSize);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE,p->texture.surface.image,p->texture.surface.imageSize);
    GX2TexClampMode clamps[4]={GX2_TEX_CLAMP_MODE_CLAMP,GX2_TEX_CLAMP_MODE_CLAMP,
                             GX2_TEX_CLAMP_MODE_WRAP,GX2_TEX_CLAMP_MODE_MIRROR};
    GX2InitSampler(&p->sampler,GX2_TEX_CLAMP_MODE_CLAMP,
        filter?GX2_TEX_XY_FILTER_MODE_LINEAR:GX2_TEX_XY_FILTER_MODE_POINT);
    GX2InitSamplerClamping(&p->sampler,clamps[wrap_s],clamps[wrap_t],GX2_TEX_CLAMP_MODE_CLAMP);
    GX2InitSamplerLOD(&p->sampler,0.0f,0.0f,0.0f);
    GX2InitSamplerZMFilter(&p->sampler,GX2_TEX_Z_FILTER_MODE_NONE,GX2_TEX_MIP_FILTER_MODE_NONE);
    ++s->texture_uploads;s->texture_bytes+=(uint64_t)source->width*source->height*4u;
    return true;
}
static bool begin(void *context,const ctr_scene_surface *initial) {
    ctr_wiiu_gx2_scene *s=context;
    if(!s || !s->restore_context || !dimensions(initial))return fail(s,CTR_WIIU_GX2_SCENE_ARGUMENT);
    if(s->private_state)return fail(s,CTR_WIIU_GX2_SCENE_BUSY);
    s->result=CTR_WIIU_GX2_SCENE_OK;
    scene_private *p=allocate(s,256,sizeof(*p));
    if(!p)return fail(s,CTR_WIIU_GX2_SCENE_ALLOCATION);
    s->private_state=p;p->width=initial->width;p->height=initial->height;
    p->stride=initial->stride;p->size=initial->size;
    if(!layout(&p->target.surface,p->width,p->height,GX2_TILE_MODE_TILED_2D_THIN1) ||
       !layout(&p->linear,p->width,p->height,GX2_TILE_MODE_LINEAR_ALIGNED) ||
       (uint64_t)p->linear.pitch*p->height*4u>p->linear.imageSize) {
        s->result=CTR_WIIU_GX2_SCENE_LAYOUT;goto error;
    }
    p->context=allocate(s,256,sizeof(*p->context));
    p->target.surface.image=allocate(s,p->target.surface.alignment,p->target.surface.imageSize);
    p->linear.image=allocate(s,p->linear.alignment,p->linear.imageSize);
    p->compiled=allocate(s,256,sizeof(*p->compiled));
    p->vertex_program=allocate(s,256,sizeof(p->compiled->program));
    p->pixel_program=allocate(s,256,PIXEL_BYTES);
    uint32_t fetch_size=GX2CalcFetchShaderSizeEx(16,GX2_FETCH_SHADER_TESSELLATION_NONE,
                                               GX2_TESSELLATION_MODE_DISCRETE);
    if(!fetch_size || fetch_size>MAX_FETCH_BYTES) {
        s->result=CTR_WIIU_GX2_SCENE_LAYOUT;goto error;
    }
    p->fetch_program=allocate(s,256,fetch_size);
    p->vertices=allocate(s,256,CTR_SCENE_MAX_VERTICES*VERTEX_STRIDE);
    if(!p->context || !p->target.surface.image || !p->linear.image || !p->compiled ||
       !p->vertex_program || !p->pixel_program || !p->fetch_program || !p->vertices) {
        s->result=CTR_WIIU_GX2_SCENE_ALLOCATION;goto error;
    }
    for(unsigned y=0;y<p->height;++y)
        memcpy((uint8_t *)p->linear.image+(size_t)y*p->linear.pitch*4u,
               (const uint8_t *)initial->bytes+(size_t)y*initial->stride,p->width*4u);
    DCFlushRange(p->linear.image,p->linear.imageSize);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE,p->linear.image,p->linear.imageSize);
    /* Allocation clearing dirties PowerPC cache lines. Retire those CPU lines
     * before the GPU first writes this target; a later eviction must not
     * overwrite rendered pixels with the allocator's initial zeros. */
    DCFlushRange(p->target.surface.image,p->target.surface.imageSize);
    GX2SetupContextStateEx(p->context,true);GX2SetContextState(p->context);
    p->target.viewNumSlices=1;GX2InitColorBufferRegs(&p->target);
    GX2CopySurface(&p->linear,0,0,&p->target.surface,0,0);p->pending=true;
    if(!retire(s,p))goto error;
    GX2Invalidate(GX2_INVALIDATE_MODE_COLOR_BUFFER,p->target.surface.image,p->target.surface.imageSize);
    GX2AttribStream attributes[16];memset(attributes,0,sizeof(attributes));
    for(unsigned input=0;input<16;++input) {
        attributes[input].location=input;attributes[input].buffer=0;
        attributes[input].offset=input*4u*sizeof(float);
        attributes[input].format=GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32;
        attributes[input].type=GX2_ATTRIB_INDEX_PER_VERTEX;
        attributes[input].mask=GX2_SEL_MASK(GX2_SQ_SEL_X,GX2_SQ_SEL_Y,GX2_SQ_SEL_Z,GX2_SQ_SEL_W);
        attributes[input].endianSwap=GX2_ENDIAN_SWAP_8_IN_32;
    }
    GX2InitFetchShaderEx(&p->fetch,p->fetch_program,16,attributes,
                        GX2_FETCH_SHADER_TESSELLATION_NONE,GX2_TESSELLATION_MODE_DISCRETE);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER,p->fetch_program,fetch_size);
    if(!restore(s))return false;
    ++s->begins;return true;
error:
    ctr_wiiu_gx2_scene_abort(s);return false;
}
static uint32_t bits(float value) { uint32_t result;memcpy(&result,&value,4);return result; }
static float real(uint32_t value) { float result;memcpy(&result,&value,4);return result; }
static bool viewport(scene_private *p,const ctr_scene_state *state) {
    uint32_t x,y;
    if(!ctr_shader_f24_decode(state->registers[0x041u],&x) ||
       !ctr_shader_f24_decode(state->registers[0x043u],&y))return false;
    float hx=real(x),hy=real(y);uint32_t origin=state->registers[0x068u];
    int ox=(int)(origin&1023u),oy=(int)((origin>>16)&1023u);
    if(ox&512)ox-=1024;
    if(oy&512)oy-=1024;
    if(hx<=0 || hy<=0 || hx>(float)p->width/2 || hy>(float)p->height/2)return false;
    GX2ViewportReg v;
    GX2InitViewportReg(&v,0,0,(float)p->width,(float)p->height,0.0f,1.0f);
    /* GX2's convenience viewport negates Y for a top-left display target. The
     * PICA owned surface instead maps (-1,-1) to row zero: use positive Y scale
     * and retain that row convention through both seed and readback copies. */
    v.pa_cl_vport_xscale=bits(hx);v.pa_cl_vport_xoffset=bits(hx+(float)ox);
    v.pa_cl_vport_yscale=bits(hy);v.pa_cl_vport_yoffset=bits(hy+(float)oy);
    GX2SetViewportReg(&v);
    /* PICA's accepted z interval [-w,0] is inside the full GX2 [-w,+w]
     * interval. Depth testing/writes are disabled; do not rewrite shader z. */
    GX2SetRasterizerClipControlEx(true,true,false);return true;
}
static void rectangle(ctr_wiiu_gx2_scene *s,unsigned x,unsigned y,unsigned w,unsigned h,unsigned count) {
    if(!w || !h)return;
    scene_private *p=s->private_state;GX2SetScissor(x,y,w,h);
    GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES,count,0,1);p->pending=true;++s->submissions;
}
static void fragment_state(const ctr_fragment_state *fragment) {
    /* PICA's enum order is neither GX2's ROP3 truth table nor its comparison
     * order. Use the named SDK values instead of casting the guest fields. */
    static const GX2LogicOp logic[16]={
        GX2_LOGIC_OP_CLEAR,GX2_LOGIC_OP_AND,GX2_LOGIC_OP_REV_AND,GX2_LOGIC_OP_COPY,
        GX2_LOGIC_OP_SET,GX2_LOGIC_OP_INV_COPY,GX2_LOGIC_OP_NOP,GX2_LOGIC_OP_INV,
        GX2_LOGIC_OP_NOT_AND,GX2_LOGIC_OP_OR,GX2_LOGIC_OP_NOR,GX2_LOGIC_OP_XOR,
        GX2_LOGIC_OP_EQUIV,GX2_LOGIC_OP_INV_AND,GX2_LOGIC_OP_REV_OR,GX2_LOGIC_OP_INV_OR
    };
    static const GX2CompareFunction compare[8]={
        GX2_COMPARE_FUNC_NEVER,GX2_COMPARE_FUNC_ALWAYS,GX2_COMPARE_FUNC_EQUAL,
        GX2_COMPARE_FUNC_NOT_EQUAL,GX2_COMPARE_FUNC_LESS,GX2_COMPARE_FUNC_LEQUAL,
        GX2_COMPARE_FUNC_GREATER,GX2_COMPARE_FUNC_GEQUAL
    };
    GX2CompareFunction function=compare[fragment->alpha_function];
    float reference=(float)fragment->alpha_reference*real(UINT32_C(0x3b808081));
    if(fragment->alpha_reference==255u) {
        /* The shader's final binary32 multiply exports byte 255 slightly above
         * one; all other bytes stay below one. A threshold at one and these
         * equivalent comparisons work whether GX2 clamps alpha/reference or
         * retains the shader value. Other references match that same multiply. */
        static const GX2CompareFunction endpoint[8]={
            GX2_COMPARE_FUNC_NEVER,GX2_COMPARE_FUNC_ALWAYS,GX2_COMPARE_FUNC_GEQUAL,
            GX2_COMPARE_FUNC_LESS,GX2_COMPARE_FUNC_LESS,GX2_COMPARE_FUNC_ALWAYS,
            GX2_COMPARE_FUNC_NEVER,GX2_COMPARE_FUNC_GEQUAL
        };
        function=endpoint[fragment->alpha_function];reference=1.0f;
    }
    GX2SetColorControl(logic[fragment->logic_op],0,false,true);
    GX2SetTargetChannelMasks((GX2ChannelMask)fragment->write_mask,0,0,0,0,0,0,0);
    GX2SetAlphaTest(fragment->alpha_enabled,function,reference);
}
static bool draw(void *context,const ctr_scene_state *state,const ctr_vertex *vertices,unsigned count,
                 const ctr_scene_texture *texture) {
    ctr_wiiu_gx2_scene *s=context;scene_private *p=s?s->private_state:NULL;
    if(!p || !state || !vertices || !count || count>CTR_SCENE_MAX_VERTICES || count%3u)
        return fail(s,CTR_WIIU_GX2_SCENE_ARGUMENT);
    if(s->quarantined)return fail(s,CTR_WIIU_GX2_SCENE_WAIT);
    ctr_fragment_state fragment;
    if(!ctr_fragment_decode(state->registers,state->register_defined,true,&fragment))
        return fail(s,CTR_WIIU_GX2_SCENE_ARGUMENT);
    s->shader_result=ctr_gx2_shader_compile(&state->shader,(uint16_t)state->registers[0x2bdu],
        &state->registers[0x050u],state->registers[0x04fu],p->compiled);
    if(s->shader_result!=CTR_GX2_SHADER_OK)return fail(s,CTR_WIIU_GX2_SCENE_SHADER);
    if(!p->compiled->size || p->compiled->size>sizeof(p->compiled->program) ||
       p->compiled->gprs>64u || (texture && !p->compiled->texcoord0))
        return fail(s,CTR_WIIU_GX2_SCENE_SHADER);
    if(!pixel_shader(p,state,texture!=NULL))return fail(s,CTR_WIIU_GX2_SCENE_SHADER);
    if(!upload_texture(s,p,texture))return false;
    for(unsigned vertex=0;vertex<count;++vertex) {
        if((vertices[vertex].enabled&p->compiled->input_mask)!=p->compiled->input_mask)
            return fail(s,CTR_WIIU_GX2_SCENE_ARGUMENT);
        memcpy((uint8_t *)p->vertices+(size_t)vertex*VERTEX_STRIDE,
               vertices[vertex].input,VERTEX_STRIDE);
    }
    memcpy(p->vertex_program,p->compiled->program,p->compiled->size);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER,p->vertices,count*VERTEX_STRIDE);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER,p->vertex_program,p->compiled->size);
    memset(&p->vertex,0,sizeof(p->vertex));p->vertex.size=p->compiled->size;
    p->vertex.program=p->vertex_program;p->vertex.mode=GX2_SHADER_MODE_UNIFORM_REGISTER;
    p->vertex.regs.sq_pgm_resources_vs=p->compiled->gprs|UINT32_C(0x100);
    p->vertex.regs.spi_vs_out_config=p->compiled->texcoord0?2u:0u;
    p->vertex.regs.num_spi_vs_out_id=1;
    p->vertex.regs.spi_vs_out_id[0]=p->compiled->texcoord0?UINT32_C(0xffff0100):UINT32_C(0xffffff00);
    for(unsigned index=1;index<10;++index)p->vertex.regs.spi_vs_out_id[index]=UINT32_MAX;
    p->vertex.regs.sq_vtx_semantic_clear=UINT32_C(0xffff0000);
    p->vertex.regs.num_sq_vtx_semantic=16;
    for(unsigned index=0;index<16;++index)p->vertex.regs.sq_vtx_semantic[index]=index;
    p->vertex.regs.vgt_vertex_reuse_block_cntl=14;p->vertex.regs.vgt_hos_reuse_depth=16;
    GX2SetContextState(p->context);
    if(!viewport(p,state))return fail(s,CTR_WIIU_GX2_SCENE_LAYOUT);
    GX2SetColorBuffer(&p->target,GX2_RENDER_TARGET_0);
    GX2SetShaderModeEx(GX2_SHADER_MODE_UNIFORM_REGISTER,64,64,0,0,184,192);
    GX2SetFetchShader(&p->fetch);GX2SetVertexShader(&p->vertex);GX2SetPixelShader(&p->pixel);
    if(texture) { GX2SetPixelTexture(&p->texture,0);GX2SetPixelSampler(&p->sampler,0); }
    GX2SetPixelUniformReg(0,32,p->pixel_constants);
    GX2SetAttribBuffer(0,count*VERTEX_STRIDE,VERTEX_STRIDE,p->vertices);
    for(unsigned uniform=0;uniform<CTR_SHADER_UNIFORMS;++uniform)if(p->compiled->uniform_used[uniform])
        GX2SetVertexUniformReg(uniform*4u,4,state->shader.uniforms[uniform]);
    fragment_state(&fragment);
    GX2SetDepthStencilControl(false,false,GX2_COMPARE_FUNC_ALWAYS,false,false,
        GX2_COMPARE_FUNC_ALWAYS,GX2_STENCIL_FUNCTION_KEEP,GX2_STENCIL_FUNCTION_KEEP,
        GX2_STENCIL_FUNCTION_KEEP,GX2_COMPARE_FUNC_ALWAYS,GX2_STENCIL_FUNCTION_KEEP,
        GX2_STENCIL_FUNCTION_KEEP,GX2_STENCIL_FUNCTION_KEEP);
    GX2SetAlphaToMask(false,GX2_ALPHA_TO_MASK_MODE_NON_DITHERED);
    GX2SetStreamOutEnable(false);
    unsigned cull=state->registers[0x040u]&3u;
    /* Positive viewport Y preserves PICA signed area: cull1 rejects positive
     * (CCW), cull2 negative (CW). PICA cull3 is treated as disabled by this
     * restricted scene contract, matching the preflight reference. */
    GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW,cull==1u,cull==2u);
    unsigned mode=state->registers[0x065u]&3u;
    unsigned x1=state->registers[0x066u]&1023u,y1=(state->registers[0x066u]>>16)&1023u;
    unsigned x2=state->registers[0x067u]&1023u,y2=(state->registers[0x067u]>>16)&1023u;
    if(x1>p->width)x1=p->width;
    if(y1>p->height)y1=p->height;
    if(x2>=p->width)x2=p->width-1u;
    if(y2>=p->height)y2=p->height-1u;
    bool empty=x1>x2 || y1>y2;
    if(mode==0u || (mode==1u && empty))rectangle(s,0,0,p->width,p->height,count);
    else if(mode==3u) {
        if(!empty)rectangle(s,x1,y1,x2-x1+1u,y2-y1+1u,count);
    } else if(mode==1u) {
        rectangle(s,0,0,p->width,y1,count);
        rectangle(s,0,y2+1u,p->width,p->height-y2-1u,count);
        rectangle(s,0,y1,x1,y2-y1+1u,count);
        rectangle(s,x2+1u,y1,p->width-x2-1u,y2-y1+1u,count);
    } else return fail(s,CTR_WIIU_GX2_SCENE_LAYOUT);
    if(!restore(s))return false;
    ++s->draws;s->shader_instructions+=p->compiled->pica_instructions;
    s->result=CTR_WIIU_GX2_SCENE_OK;return true;
}
static bool end(void *context,const ctr_scene_surface *readback) {
    ctr_wiiu_gx2_scene *s=context;scene_private *p=s?s->private_state:NULL;
    if(!p || !dimensions(readback) || readback->width!=p->width || readback->height!=p->height ||
       readback->stride!=p->stride || readback->size!=p->size)return fail(s,CTR_WIIU_GX2_SCENE_ARGUMENT);
    if(s->quarantined)return fail(s,CTR_WIIU_GX2_SCENE_WAIT);
    GX2SetContextState(p->context);
    GX2Invalidate(GX2_INVALIDATE_MODE_COLOR_BUFFER,p->target.surface.image,p->target.surface.imageSize);
    GX2CopySurface(&p->target.surface,0,0,&p->linear,0,0);p->pending=true;
    if(!retire(s,p))return fail(s,CTR_WIIU_GX2_SCENE_WAIT);
    DCInvalidateRange(p->linear.image,p->linear.imageSize);
    for(unsigned y=0;y<p->height;++y)
        memcpy((uint8_t *)readback->bytes+(size_t)y*readback->stride,
               (const uint8_t *)p->linear.image+(size_t)y*p->linear.pitch*4u,p->width*4u);
    if(s->observe_readback)s->observe_readback(s->readback_context,readback);
    if(!restore(s))return false;
    ++s->readbacks;s->result=CTR_WIIU_GX2_SCENE_OK;release(s,p);return true;
}
void ctr_wiiu_gx2_scene_init(ctr_wiiu_gx2_scene *s,GX2ContextState *restore_context) {
    if(s) { memset(s,0,sizeof(*s));s->restore_context=restore_context; }
}
static void abort_callback(void *context) { ctr_wiiu_gx2_scene_abort(context); }
ctr_scene_backend ctr_wiiu_gx2_scene_backend(ctr_wiiu_gx2_scene *s) {
    return (ctr_scene_backend){s,begin,draw,end,abort_callback};
}
const char *ctr_wiiu_gx2_scene_result_name(ctr_wiiu_gx2_scene_result result) {
    static const char *const names[]={"ok","argument","allocation","layout","shader","wait","busy"};
    return (unsigned)result<sizeof(names)/sizeof(*names)?names[result]:"unknown";
}
