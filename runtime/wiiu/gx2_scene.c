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
#define MAX_TEXTURE_TOTAL_BYTES (3u*MAX_TEXTURE_BYTES)
#define VERTEX_STRIDE (16u*4u*sizeof(float))
#define MAX_FETCH_BYTES 4096u
#define PIXEL_BYTES 8192u
#define PIXEL_ALU_OFFSET 256u
#define PIXEL_MAX_ALUS 768u

typedef struct {
    GX2ContextState *context;
    GX2ColorBuffer target;
    GX2Surface linear;
    GX2DepthBuffer depth_target;
    GX2Surface depth_linear;
    GX2FetchShader fetch;
    GX2VertexShader vertex;
    GX2PixelShader pixel;
    GX2Texture texture[3];
    GX2Sampler sampler[3];
    ctr_gx2_shader *compiled;
    void *vertex_program,*pixel_program,*fetch_program,*vertices;
    unsigned width,height,stride;
    size_t size;
    unsigned depth_stride;
    size_t depth_size;
    uint32_t depth_address;
    unsigned depth_format,depth_bytes;
    bool pending,depth_enabled,buffered;
    uint32_t pixel_constants[9][4];
} scene_private;

typedef struct {
    GX2PixelShader metadata;
    uint8_t program[PIXEL_BYTES];
} pixel_cache_record;
_Static_assert(sizeof(pixel_cache_record)<=CTR_GX2_CACHE_PIXEL_PAYLOAD_MAX,
               "pixel program cache record exceeds its fixed CPU bound");

static void cache_add(ctr_wiiu_gx2_scene *s,uint64_t *counter,uint64_t amount) {
    if(amount>=UINT64_MAX-*counter) {
        *counter=UINT64_MAX;s->cache_stats.saturated=true;
    } else *counter+=amount;
}
static void cache_observe(ctr_wiiu_gx2_scene *s,const ctr_gx2_cache_statistics *before) {
    ctr_gx2_cache_statistics after;
    ctr_gx2_cache_statistics_get(s->program_cache,&after);
    for(unsigned kind=0;kind<2u;++kind) {
#define CACHE_DELTA(field) cache_add(s,&s->cache_stats.field[kind],after.field[kind]-before->field[kind])
        CACHE_DELTA(lookups);CACHE_DELTA(hits);CACHE_DELTA(misses);
        CACHE_DELTA(stores);CACHE_DELTA(evictions);CACHE_DELTA(bypasses);
#undef CACHE_DELTA
    }
    s->cache_stats.bytes=after.bytes;
    if(after.peak_bytes>s->cache_stats.peak_bytes)s->cache_stats.peak_bytes=after.peak_bytes;
    s->cache_stats.saturated|=after.saturated;
}
static bool cache_lookup(ctr_wiiu_gx2_scene *s,ctr_gx2_cache_kind kind,
                          const uint8_t *key,size_t key_size,void *out,size_t capacity,
                          size_t *out_size) {
    ctr_gx2_cache_statistics before;
    ctr_gx2_cache_statistics_get(s->program_cache,&before);
    bool hit=ctr_gx2_cache_lookup(s->program_cache,kind,key,key_size,out,capacity,out_size);
    cache_observe(s,&before);return hit;
}
static void cache_store(ctr_wiiu_gx2_scene *s,ctr_gx2_cache_kind kind,
                         const uint8_t *key,size_t key_size,const void *record,size_t bytes) {
    ctr_gx2_cache_statistics before;
    ctr_gx2_cache_statistics_get(s->program_cache,&before);
    (void)ctr_gx2_cache_store(s->program_cache,kind,key,key_size,record,bytes);
    cache_observe(s,&before);
}
static void cache_prepare(ctr_wiiu_gx2_scene *s) {
    if(!s->cache_disabled && !s->program_cache) {
        s->program_cache=ctr_gx2_cache_create();
        if(!s->program_cache)cache_add(s,&s->cache_allocation_failures,1u);
    }
}
void ctr_wiiu_gx2_scene_cache_clear(ctr_wiiu_gx2_scene *s) {
    if(!s)return;
    ctr_gx2_cache_destroy(s->program_cache);s->program_cache=NULL;
    s->cache_stats.bytes=0;
}

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
    for(unsigned unit=0;unit<3u;++unit)dispose(s,p->texture[unit].surface.image);
    dispose(s,p->vertices);dispose(s,p->fetch_program);
    dispose(s,p->pixel_program);dispose(s,p->vertex_program);
    dispose(s,p->compiled);dispose(s,p->linear.image);
    dispose(s,p->depth_linear.image);dispose(s,p->depth_target.surface.image);
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
    ctr_wiiu_gx2_scene_cache_clear(s);
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
static bool depth_dimensions(const ctr_scene_depth_surface *depth,const ctr_scene_surface *color) {
    if(!depth || (depth->format!=0u && depth->format!=3u))return false;
    unsigned bytes=depth->format==3u?4u:2u;
    size_t limit=depth->format==3u?CTR_SCENE_MAX_D24S8_BYTES:CTR_SCENE_MAX_DEPTH_BYTES;
    return depth->bytes && depth->width==color->width && depth->height==color->height &&
        depth->stride>=depth->width*bytes &&
        (uint64_t)depth->stride*depth->height<=depth->size && depth->size<=limit;
}
static bool depth_layout(GX2Surface *surface,unsigned width,unsigned height,GX2TileMode tile,
                          unsigned format) {
    surface->dim=GX2_SURFACE_DIM_TEXTURE_2D;surface->width=width;surface->height=height;
    surface->depth=1;surface->mipLevels=1;
    surface->format=format==3u?GX2_SURFACE_FORMAT_UNORM_R24_X8:GX2_SURFACE_FORMAT_UNORM_R16;
    surface->aa=GX2_AA_MODE1X;
    /* Both copy endpoints carry depth usage: depth microtiling differs from
     * color, even for the same element size (Decaf e6c528a gx2_surface.cpp). */
    surface->use=GX2_SURFACE_USE_TEXTURE|GX2_SURFACE_USE_DEPTH_BUFFER;
    surface->tileMode=tile;GX2CalcSurfaceSizeAndAlignment(surface);
    return surface->tileMode==tile && surface->pitch>=width &&
        surface->imageSize && surface->imageSize<=MAX_IMAGE_BYTES &&
        (uint64_t)surface->pitch*height*(format==3u?4u:2u)<=surface->imageSize &&
        surface->alignment && !(surface->alignment&(surface->alignment-1u)) &&
        surface->alignment<=65536u && !surface->mipmapSize;
}
static void put32(uint8_t *p,uint32_t value) {
    for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(value>>(8u*i));
}
/* Original scalar Latte encoder. R0 is primary, R1 original guest UV, R2
 * Texture0. R3/R7 alternate Previous/output; R4..R6 snapshot all operands before
 * any output lane changes. R8 is scratch; buffered programs additionally use
 * R9 visible/R10 pending combiner bytes. Independent LAST groups avoid a VLIW
 * bank scheduler. Constants 0..5 contain stage bytes; 6/7 are numeric helpers
 * and buffered programs use constant 8 for the initial combiner bytes.
 * Additional-unit programs retain color in R0 and compact imported UVs in
 * R1..R3, snapshot in R4..R6, alternate Previous in R7/R14, keep buffers in
 * R9/R10 and sample units 0/1/2 into R11/R12/R13. */
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
static void pixel_constants(scene_private *p,const ctr_tev_program *tev) {
    p->buffered=ctr_tev_buffered(tev);
    memset(p->pixel_constants,0,sizeof(p->pixel_constants));
    for(unsigned stage=0;stage<CTR_TEV_STAGES;++stage)
        for(unsigned lane=0;lane<4;++lane)p->pixel_constants[stage][lane]=bits((float)tev->stages[stage].constant[lane]);
    p->pixel_constants[6][0]=bits(255.0f);p->pixel_constants[6][1]=bits(128.0f);
    /* The upward binary32 reciprocal gives exact floor(N/255), 0<=N<=65025.
     * Integer products/sums stay exactly representable; no fused operation. */
    p->pixel_constants[6][2]=UINT32_C(0x3b808081);p->pixel_constants[6][3]=bits(0.5f);
    for(unsigned lane=0;lane<4;++lane)p->pixel_constants[7][lane]=bits(lane<3u?(float)(1u<<lane):0.0f);
    if(p->buffered)for(unsigned lane=0;lane<4;++lane)
        p->pixel_constants[8][lane]=bits((float)tev->buffer_color[lane]);
}
static unsigned population(unsigned mask) {
    unsigned count=0;for(;mask;mask>>=1)count+=mask&1u;return count;
}
static unsigned coordinate_mask(unsigned mask,bool texture2_coord1) {
    return texture2_coord1 && (mask&4u)?(mask&~4u)|2u:mask;
}
static unsigned coordinate_gpr(unsigned coordinate,unsigned coordinates) {
    return 1u+population(coordinates&((1u<<coordinate)-1u));
}
static unsigned texture_gpr(unsigned unit,bool multiple) { return multiple?11u+unit:2u; }
static bool pixel_shader(scene_private *p,const ctr_tev_program *tev,unsigned mask,
                          bool texture2_coord1) {
    uint8_t *program=p->pixel_program;memset(program,0,PIXEL_BYTES);
    bool multiple=mask>1u;unsigned coordinates=coordinate_mask(mask,texture2_coord1);
    unsigned count=0,previous=multiple?14u:3u,destination=7;
    for(unsigned lane=0;lane<4;++lane) {
        pixel_op(p,&count,2,0,lane,262,0,false,0,lane);
        pixel_op(p,&count,0,0,lane,262,3,false,0,lane);
        pixel_op(p,&count,20,0,lane,0,0,false,0,lane);
        for(unsigned unit=0;unit<3u;++unit)if(mask&(1u<<unit)) {
            unsigned sample=texture_gpr(unit,multiple);
            pixel_op(p,&count,2,sample,lane,262,0,false,sample,lane);
            pixel_op(p,&count,0,sample,lane,262,3,false,sample,lane);
            pixel_op(p,&count,20,sample,lane,0,0,false,sample,lane);
        }
    }
    if(p->buffered)for(unsigned lane=0;lane<4;++lane) {
        pixel_op(p,&count,25,263,3,0,0,false,9,lane);
        pixel_op(p,&count,25,264,lane,0,0,false,10,lane);
    }
    for(unsigned stage=0;stage<CTR_TEV_STAGES;++stage) {
        const ctr_tev_stage *v=&tev->stages[stage];
        bool passthrough=ctr_tev_passthrough(v);
        if(!passthrough) {
            for(unsigned lane=0;lane<4;++lane) {
                unsigned side=lane==3u;
                for(unsigned arg=0;arg<ctr_tev_arguments(v->operation[side]);++arg) {
                    unsigned source=v->source[side][arg],operand=v->operand[side][arg];
                    unsigned reg=source==0u?0u:source>=3u && source<=5u?texture_gpr(source-3u,multiple):source==13u?9u:
                        source==14u?256u+stage:previous;
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
        if(p->buffered) {
            /* PICA publishes the pending bytes before applying this stage's
             * selected output to pending. This one-stage delay also advances
             * across passthrough stages and stages 4/5 with no update bits. */
            for(unsigned lane=0;lane<4;++lane)
                pixel_op(p,&count,25,10,lane,0,0,false,9,lane);
            if(stage<4u)for(unsigned lane=0;lane<4;++lane)
                if(tev->buffer_update[lane==3u]&(1u<<stage))
                    pixel_op(p,&count,25,previous,lane,0,0,false,10,lane);
        }
    }
    for(unsigned lane=0;lane<4;++lane)pixel_op(p,&count,2,previous,lane,262,2,false,previous,lane);
    if(count>PIXEL_MAX_ALUS)return false;
    unsigned cf=0;
    if(mask) {
        put32(program,128u/8u);
        put32(program+4u,UINT32_C(0x80800000)|((population(mask)-1u)<<10));cf=8;
        unsigned fetch=128u;
        for(unsigned unit=0;unit<3u;++unit)if(mask&(1u<<unit)) {
            unsigned coordinate=unit==2u && texture2_coord1?1u:unit;
            unsigned input=coordinate_gpr(coordinate,coordinates);
            put32(program+fetch,UINT32_C(0x13)|(unit<<8)|(input<<16));
            put32(program+fetch+4u,UINT32_C(0xf00d1000)|texture_gpr(unit,multiple));
            put32(program+fetch+8u,UINT32_C(0xb0800000)|(unit<<15));fetch+=16u;
        }
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
    unsigned interpolators=1u+population(coordinates);
    p->pixel.regs.sq_pgm_resources_ps=multiple?15u:p->buffered?11u:9u;p->pixel.regs.sq_pgm_exports_ps=2;
    p->pixel.regs.spi_ps_in_control_0=UINT32_C(0x14000000)|interpolators;
    p->pixel.regs.num_spi_ps_input_cntl=interpolators;
    p->pixel.regs.spi_ps_input_cntls[0]=UINT32_C(0x100);
    unsigned slot=1u;
    for(unsigned coordinate=0;coordinate<3u;++coordinate)if(coordinates&(1u<<coordinate))
        p->pixel.regs.spi_ps_input_cntls[slot++]=UINT32_C(0x101)+coordinate;
    p->pixel.regs.cb_shader_mask=15;p->pixel.regs.cb_shader_control=1;
    /* DB_Z_ORDER bits4..5:0 is LATE_Z, 1 EARLY_Z_THEN_LATE_Z. Alpha test
     * precedes PICA depth updates; HiZ and early depth writes stay disabled.
     * Source: Decaf e6c528a latte_registers_db.h / latte_enum_db.h. */
    p->pixel.regs.db_shader_control=p->depth_enabled?0u:UINT32_C(0x10);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER,program,p->pixel.size);return true;
}
static bool vertex_program(ctr_wiiu_gx2_scene *s,scene_private *p,
                            const ctr_scene_state *state,unsigned coordinates) {
    uint8_t key[CTR_GX2_CACHE_VERTEX_KEY_MAX];size_t key_size=0,record_size=0;
    bool enabled=!s->cache_disabled && s->program_cache;
    bool keyed=enabled && ctr_gx2_cache_vertex_key(&state->shader,
        (uint16_t)state->registers[0x2bdu],&state->registers[0x050u],
        state->registers[0x04fu],key,&key_size);
    bool hit=keyed && cache_lookup(s,CTR_GX2_CACHE_VERTEX,key,key_size,
        p->compiled,sizeof(*p->compiled),&record_size);
    if(!keyed)cache_add(s,&s->cache_stats.bypasses[CTR_GX2_CACHE_VERTEX],1u);
    if(hit) {
        if(record_size!=sizeof(*p->compiled)) {
            s->shader_result=CTR_GX2_SHADER_ARGUMENT;return false;
        }
        /* Code identity excludes current uniform contents. Recheck used lanes
         * in compiler encounter order before any current uniform is uploaded. */
        s->shader_result=ctr_gx2_shader_validate_uniforms(&state->shader,p->compiled);
    } else {
        cache_add(s,&s->vertex_translations,1u);
        s->shader_result=ctr_gx2_shader_compile(&state->shader,
            (uint16_t)state->registers[0x2bdu],&state->registers[0x050u],
            state->registers[0x04fu],p->compiled);
        if(s->shader_result!=CTR_GX2_SHADER_OK)
            cache_add(s,&s->vertex_translation_failures,1u);
    }
    if(s->shader_result!=CTR_GX2_SHADER_OK)return false;
    if(!p->compiled->size || p->compiled->size>sizeof(p->compiled->program) ||
       p->compiled->gprs>64u || p->compiled->texcoord_mask>7u ||
       p->compiled->texcoord0!=((p->compiled->texcoord_mask&1u)!=0) ||
       (coordinates&p->compiled->texcoord_mask)!=coordinates) {
        if(!hit)cache_add(s,&s->vertex_translation_failures,1u);
        return false;
    }
    if(keyed && !hit)cache_store(s,CTR_GX2_CACHE_VERTEX,key,key_size,
        p->compiled,sizeof(*p->compiled));
    return true;
}
static bool pixel_program(ctr_wiiu_gx2_scene *s,scene_private *p,
                           const ctr_tev_program *tev,unsigned mask,bool texture2_coord1) {
    uint8_t key[CTR_GX2_CACHE_PIXEL_KEY_MAX];size_t key_size=0,record_size=0;
    pixel_cache_record record;
    /* Constants and buffered selection always come from the current decoded
     * registers, even when their immutable instruction topology is cached. */
    pixel_constants(p,tev);
    bool enabled=!s->cache_disabled && s->program_cache;
    bool keyed=enabled && ctr_gx2_cache_pixel_key(tev,mask,texture2_coord1,p->depth_enabled,key,&key_size);
    bool hit=keyed && cache_lookup(s,CTR_GX2_CACHE_PIXEL,key,key_size,
        &record,sizeof(record),&record_size);
    if(!keyed)cache_add(s,&s->cache_stats.bypasses[CTR_GX2_CACHE_PIXEL],1u);
    if(hit) {
        if(record_size!=sizeof(record) || !record.metadata.size ||
           record.metadata.size>PIXEL_BYTES || record.metadata.program)return false;
        memcpy(p->pixel_program,record.program,sizeof(record.program));
        p->pixel=record.metadata;p->pixel.program=p->pixel_program;
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER,p->pixel_program,p->pixel.size);
        return true;
    }
    cache_add(s,&s->pixel_translations,1u);
    if(!pixel_shader(p,tev,mask,texture2_coord1)) {
        cache_add(s,&s->pixel_translation_failures,1u);return false;
    }
    if(keyed) {
        /* Copy only immutable scalar/register metadata into a cleared object.
         * All program/reflection/GX2R pointers stay zero in the CPU record. */
        memset(&record,0,sizeof(record));record.metadata.regs=p->pixel.regs;
        record.metadata.size=p->pixel.size;record.metadata.mode=p->pixel.mode;
        memcpy(record.program,p->pixel_program,sizeof(record.program));
        cache_store(s,CTR_GX2_CACHE_PIXEL,key,key_size,&record,sizeof(record));
    }
    return true;
}
static bool upload_texture(ctr_wiiu_gx2_scene *s,scene_private *p,
                           const ctr_scene_texture *source,unsigned unit) {
    if(!source)return true;
    GX2Texture *texture=&p->texture[unit];GX2Sampler *sampler=&p->sampler[unit];
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
    if(texture->surface.width!=source->width || texture->surface.height!=source->height) {
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
        size_t total=next.surface.imageSize;
        for(unsigned other=0;other<3u;++other)if(other!=unit)total+=p->texture[other].surface.imageSize;
        if(total>MAX_TEXTURE_TOTAL_BYTES)return fail(s,CTR_WIIU_GX2_SCENE_LAYOUT);
        next.surface.image=allocate(s,next.surface.alignment,next.surface.imageSize);
        if(!next.surface.image)return fail(s,CTR_WIIU_GX2_SCENE_ALLOCATION);
        next.viewNumMips=1;next.viewNumSlices=1;
        next.compMap=GX2_COMP_MAP(GX2_SQ_SEL_X,GX2_SQ_SEL_Y,GX2_SQ_SEL_Z,GX2_SQ_SEL_W);
        GX2InitTextureRegs(&next);
        dispose(s,texture->surface.image);*texture=next;
    }
    /* PICA normalized V=0 addresses the last raw Morton-storage row. Reverse
     * the decoded upload rows while preserving the actual guest UV exports. */
    for(unsigned y=0;y<source->height;++y)
        memcpy((uint8_t *)texture->surface.image+(size_t)y*texture->surface.pitch*4u,
               source->rgba+(size_t)(source->height-1u-y)*source->width*4u,source->width*4u);
    DCFlushRange(texture->surface.image,texture->surface.imageSize);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE,texture->surface.image,texture->surface.imageSize);
    GX2TexClampMode clamps[4]={GX2_TEX_CLAMP_MODE_CLAMP,GX2_TEX_CLAMP_MODE_CLAMP,
                             GX2_TEX_CLAMP_MODE_WRAP,GX2_TEX_CLAMP_MODE_MIRROR};
    GX2InitSampler(sampler,GX2_TEX_CLAMP_MODE_CLAMP,
        filter?GX2_TEX_XY_FILTER_MODE_LINEAR:GX2_TEX_XY_FILTER_MODE_POINT);
    GX2InitSamplerClamping(sampler,clamps[wrap_s],clamps[wrap_t],GX2_TEX_CLAMP_MODE_CLAMP);
    GX2InitSamplerLOD(sampler,0.0f,0.0f,0.0f);
    GX2InitSamplerZMFilter(sampler,GX2_TEX_Z_FILTER_MODE_NONE,GX2_TEX_MIP_FILTER_MODE_NONE);
    ++s->texture_uploads;s->texture_bytes+=(uint64_t)source->width*source->height*4u;
    return true;
}
static bool begin_targets(void *context,const ctr_scene_surface *initial,
                          const ctr_scene_depth_surface *depth) {
    ctr_wiiu_gx2_scene *s=context;
    if(!s || !s->restore_context || !dimensions(initial) ||
       (depth && !depth_dimensions(depth,initial)))return fail(s,CTR_WIIU_GX2_SCENE_ARGUMENT);
    if(s->private_state)return fail(s,CTR_WIIU_GX2_SCENE_BUSY);
    s->result=CTR_WIIU_GX2_SCENE_OK;
    scene_private *p=allocate(s,256,sizeof(*p));
    if(!p)return fail(s,CTR_WIIU_GX2_SCENE_ALLOCATION);
    s->private_state=p;p->width=initial->width;p->height=initial->height;
    p->stride=initial->stride;p->size=initial->size;
    p->depth_enabled=depth!=NULL;
    if(depth) {
        p->depth_stride=depth->stride;p->depth_size=depth->size;p->depth_address=depth->address;
        p->depth_format=depth->format;p->depth_bytes=depth->format==3u?4u:2u;
    }
    if(!layout(&p->target.surface,p->width,p->height,GX2_TILE_MODE_TILED_2D_THIN1) ||
       !layout(&p->linear,p->width,p->height,GX2_TILE_MODE_LINEAR_ALIGNED) ||
       (uint64_t)p->linear.pitch*p->height*4u>p->linear.imageSize) {
        s->result=CTR_WIIU_GX2_SCENE_LAYOUT;goto error;
    }
    if(depth && (!depth_layout(&p->depth_target.surface,p->width,p->height,GX2_TILE_MODE_TILED_2D_THIN1,p->depth_format) ||
                  !depth_layout(&p->depth_linear,p->width,p->height,GX2_TILE_MODE_LINEAR_ALIGNED,p->depth_format))) {
        s->result=CTR_WIIU_GX2_SCENE_LAYOUT;goto error;
    }
    p->context=allocate(s,256,sizeof(*p->context));
    p->target.surface.image=allocate(s,p->target.surface.alignment,p->target.surface.imageSize);
    p->linear.image=allocate(s,p->linear.alignment,p->linear.imageSize);
    if(depth) {
        p->depth_target.surface.image=allocate(s,p->depth_target.surface.alignment,p->depth_target.surface.imageSize);
        p->depth_linear.image=allocate(s,p->depth_linear.alignment,p->depth_linear.imageSize);
    }
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
       !p->vertex_program || !p->pixel_program || !p->fetch_program || !p->vertices ||
       (depth && (!p->depth_target.surface.image || !p->depth_linear.image))) {
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
    if(depth) {
        /* Raw LE D16/D24S8 remain bytes on big-endian PowerPC. D24S8 is
         * depth in bits0..23 and stencil in bits24..31; DEPTH_8_24 matches
         * pinned R600 Z24_UNORM_S8_UINT packing (Mesa f1f246c). GX2 performs
         * only the pitched-linear/depth-tile conversion, with no byte rotation. */
        for(unsigned y=0;y<p->height;++y)
            memcpy((uint8_t *)p->depth_linear.image+(size_t)y*p->depth_linear.pitch*p->depth_bytes,
                   (const uint8_t *)depth->bytes+(size_t)y*depth->stride,p->width*p->depth_bytes);
        DCFlushRange(p->depth_linear.image,p->depth_linear.imageSize);
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE,p->depth_linear.image,p->depth_linear.imageSize);
        DCFlushRange(p->depth_target.surface.image,p->depth_target.surface.imageSize);
    }
    GX2SetupContextStateEx(p->context,true);GX2SetContextState(p->context);
    p->target.viewNumSlices=1;GX2InitColorBufferRegs(&p->target);
    GX2CopySurface(&p->linear,0,0,&p->target.surface,0,0);p->pending=true;
    if(depth) {
        p->depth_target.viewNumSlices=1;GX2InitDepthBufferRegs(&p->depth_target);
        GX2InitDepthBufferHiZEnable(&p->depth_target,false);
        GX2CopySurface(&p->depth_linear,0,0,&p->depth_target.surface,0,0);
    }
    if(!retire(s,p))goto error;
    GX2Invalidate(GX2_INVALIDATE_MODE_COLOR_BUFFER,p->target.surface.image,p->target.surface.imageSize);
    if(depth)GX2Invalidate(GX2_INVALIDATE_MODE_DEPTH_BUFFER,p->depth_target.surface.image,p->depth_target.surface.imageSize);
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
static bool begin(void *context,const ctr_scene_surface *initial) {
    return begin_targets(context,initial,NULL);
}
static bool begin_depth(void *context,const ctr_scene_surface *initial,
                        const ctr_scene_depth_surface *depth) {
    if(!depth)return fail(context,CTR_WIIU_GX2_SCENE_ARGUMENT);
    return begin_targets(context,initial,depth);
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
    if(p->depth_enabled) {
        /* Canonical Z buffering is depth=-clipZ/W. GX2's convenience 0..1
         * viewport instead uses scale=.5,offset=.5 and cannot be inherited. */
        v.pa_cl_vport_zscale=bits(-1.0f);v.pa_cl_vport_zoffset=bits(0.0f);
    }
    GX2SetViewportReg(&v);
    /* PICA's accepted z interval [-w,0] is inside the full GX2 [-w,+w]
     * interval; retain the original shader Z. */
    GX2SetRasterizerClipControlEx(true,true,false);return true;
}
static void rectangle(ctr_wiiu_gx2_scene *s,unsigned x,unsigned y,unsigned w,unsigned h,unsigned count) {
    if(!w || !h)return;
    scene_private *p=s->private_state;GX2SetScissor(x,y,w,h);
    GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES,count,0,1);p->pending=true;++s->submissions;
}
static bool native_fragment(const ctr_fragment_state *fragment) {
    if(!fragment->blend_enabled)return true;
    for(unsigned side=0;side<2;++side) {
        /* Native MIN/MAX factor handling and destination-selector SAT are not
         * established. ONE/ONE removes the MIN/MAX factor ambiguity. */
        if(fragment->blend_destination[side]==14u ||
           (fragment->blend_equation[side]>=3u &&
            (fragment->blend_source[side]!=1u || fragment->blend_destination[side]!=1u)))
            return false;
    }
    return true;
}
static bool native_depth(const scene_private *p,const ctr_scene_state *state,
                          ctr_stencil_state *stencil) {
    if(!ctr_stencil_decode(state->registers,state->register_defined,
                           p->depth_enabled && p->depth_format==3u,stencil))return false;
    if(!p->depth_enabled)return true;
    static const unsigned full[]={0x04du,0x04eu,0x06du,0x114u,0x115u,0x116u,0x11cu};
    for(unsigned i=0;i<sizeof(full)/sizeof(*full);++i)
        if(state->register_defined[full[i]]!=15u)return false;
    uint32_t read=state->registers[0x114u],write=state->registers[0x115u];
    unsigned gate=p->depth_format==3u?3u:2u;
    return state->registers[0x04du]==UINT32_C(0xbf0000) &&
        (state->registers[0x04eu]&~UINT32_C(0x800000))==0u &&
        state->registers[0x06du]==1u && state->registers[0x116u]==p->depth_format &&
        !(state->registers[0x11cu]&UINT32_C(0xf0000000)) &&
        (uint64_t)state->registers[0x11cu]*8u==p->depth_address &&
        (read==0u || read==gate) && (write==0u || write==gate) &&
        (!((state->registers[0x107u]&1u) || stencil->enabled) || read==gate);
}
static void depth_state(const scene_private *p,const ctr_scene_state *state,
                         const ctr_stencil_state *stencil) {
    static const GX2CompareFunction compare[8]={
        GX2_COMPARE_FUNC_NEVER,GX2_COMPARE_FUNC_ALWAYS,GX2_COMPARE_FUNC_EQUAL,
        GX2_COMPARE_FUNC_NOT_EQUAL,GX2_COMPARE_FUNC_LESS,GX2_COMPARE_FUNC_LEQUAL,
        GX2_COMPARE_FUNC_GREATER,GX2_COMPARE_FUNC_GEQUAL
    };
    static const GX2StencilFunction actions[8]={
        GX2_STENCIL_FUNCTION_KEEP,GX2_STENCIL_FUNCTION_ZERO,GX2_STENCIL_FUNCTION_REPLACE,
        GX2_STENCIL_FUNCTION_INCR_CLAMP,GX2_STENCIL_FUNCTION_DECR_CLAMP,
        GX2_STENCIL_FUNCTION_INV,GX2_STENCIL_FUNCTION_INCR_WRAP,GX2_STENCIL_FUNCTION_DECR_WRAP
    };
    bool test=p->depth_enabled && (state->registers[0x107u]&1u)!=0u;
    bool gate=p->depth_enabled && state->registers[0x115u]==(p->depth_format==3u?3u:2u);
    bool write=gate && (state->registers[0x107u]&0x1000u)!=0u;
    bool enabled=p->depth_enabled && p->depth_format==3u && stencil->enabled;
    GX2CompareFunction function=enabled?compare[stencil->function]:GX2_COMPARE_FUNC_ALWAYS;
    GX2StencilFunction pass=enabled?actions[stencil->action[2]]:GX2_STENCIL_FUNCTION_KEEP;
    GX2StencilFunction zfail=enabled?actions[stencil->action[1]]:GX2_STENCIL_FUNCTION_KEEP;
    GX2StencilFunction fail_action=enabled?actions[stencil->action[0]]:GX2_STENCIL_FUNCTION_KEEP;
    /* PICA writes with its depth test disabled. ALWAYS gives that behavior
     * without relying on disabled-Z writes. Stencil's ZPASS action still runs
     * with depth disabled, as in the standard depth/stencil pipeline. */
    GX2SetDepthStencilControl(test || write,write,
        test?compare[(state->registers[0x107u]>>4)&7u]:GX2_COMPARE_FUNC_ALWAYS,enabled,enabled,
        function,pass,zfail,fail_action,function,pass,zfail,fail_action);
    /* WUT order is input mask, write mask, reference, for each face. Rebind
     * both faces every draw; a closed write gate only suppresses writes. */
    uint8_t mask=enabled?stencil->input_mask:0u;
    uint8_t write_mask=enabled && gate?stencil->write_mask:0u;
    uint8_t reference=enabled?stencil->reference:0u;
    GX2SetStencilMask(mask,write_mask,reference,mask,write_mask,reference);
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
    static const GX2BlendMode factors[15]={
        GX2_BLEND_MODE_ZERO,GX2_BLEND_MODE_ONE,GX2_BLEND_MODE_SRC_COLOR,
        GX2_BLEND_MODE_INV_SRC_COLOR,GX2_BLEND_MODE_DST_COLOR,GX2_BLEND_MODE_INV_DST_COLOR,
        GX2_BLEND_MODE_SRC_ALPHA,GX2_BLEND_MODE_INV_SRC_ALPHA,GX2_BLEND_MODE_DST_ALPHA,
        GX2_BLEND_MODE_INV_DST_ALPHA,GX2_BLEND_MODE_BLEND_FACTOR,GX2_BLEND_MODE_INV_BLEND_FACTOR,
        GX2_BLEND_MODE_CONSTANT_ALPHA,GX2_BLEND_MODE_INV_CONSTANT_ALPHA,GX2_BLEND_MODE_SRC_ALPHA_SAT
    };
    static const GX2BlendCombineMode equations[5]={
        GX2_BLEND_COMBINE_MODE_ADD,GX2_BLEND_COMBINE_MODE_SUB,GX2_BLEND_COMBINE_MODE_REV_SUB,
        GX2_BLEND_COMBINE_MODE_MIN,GX2_BLEND_COMBINE_MODE_MAX
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
    GX2BlendMode source[2]={GX2_BLEND_MODE_ONE,GX2_BLEND_MODE_ONE};
    GX2BlendMode destination[2]={GX2_BLEND_MODE_ZERO,GX2_BLEND_MODE_ZERO};
    GX2BlendCombineMode equation[2]={GX2_BLEND_COMBINE_MODE_ADD,GX2_BLEND_COMBINE_MODE_ADD};
    float constant[4]={0,0,0,0};
    if(fragment->blend_enabled) {
        for(unsigned side=0;side<2;++side) {
            /* PICA's saturate factor is ONE in the alpha equation. */
            source[side]=side && fragment->blend_source[side]==14u?
                GX2_BLEND_MODE_ONE:factors[fragment->blend_source[side]];
            destination[side]=factors[fragment->blend_destination[side]];
            equation[side]=equations[fragment->blend_equation[side]];
        }
        for(unsigned lane=0;lane<4;++lane)constant[lane]=(float)fragment->blend_constant[lane]/255.0f;
    }
    /* These calls translate admitted state, not a hardware rounding proof.
     * Floating/UNORM blending may differ from byte-domain truncation; the
     * unchanged complete-target comparison rejects any difference. */
    GX2SetBlendControl(GX2_RENDER_TARGET_0,source[0],destination[0],equation[0],true,
        source[1],destination[1],equation[1]);
    GX2SetBlendConstantColor(constant[0],constant[1],constant[2],constant[3]);
    GX2SetColorControl(fragment->blend_enabled?GX2_LOGIC_OP_COPY:logic[fragment->logic_op],
        fragment->blend_enabled?1u:0u,false,true);
    GX2SetTargetChannelMasks((GX2ChannelMask)fragment->write_mask,0,0,0,0,0,0,0);
    GX2SetAlphaTest(fragment->alpha_enabled,function,reference);
}
static bool draw_textures(void *context,const ctr_scene_state *state,const ctr_vertex *vertices,unsigned count,
                           const ctr_scene_texture textures[3],unsigned mask) {
    ctr_wiiu_gx2_scene *s=context;scene_private *p=s?s->private_state:NULL;
    if(!p || !state || !vertices || !count || count>CTR_SCENE_MAX_VERTICES || count%3u ||
       mask>7u || (mask && !textures))
        return fail(s,CTR_WIIU_GX2_SCENE_ARGUMENT);
    if(s->quarantined)return fail(s,CTR_WIIU_GX2_SCENE_WAIT);
    ctr_fragment_state fragment;ctr_stencil_state stencil;
    if(!ctr_fragment_decode_depth(state->registers,state->register_defined,true,p->depth_enabled,&fragment) ||
       !native_fragment(&fragment) || !native_depth(p,state,&stencil))
        return fail(s,CTR_WIIU_GX2_SCENE_ARGUMENT);
    /* No hit or translation may overwrite a program or vertex upload while a
     * prior draw/context switch still references the transaction's buffers. */
    if(!retire(s,p))return false;
    cache_prepare(s);
    bool texture2_coord1=(mask&4u) && (state->registers[0x080u]&(1u<<13));
    if(!vertex_program(s,p,state,coordinate_mask(mask,texture2_coord1)))return fail(s,CTR_WIIU_GX2_SCENE_SHADER);
    ctr_tev_program tev;
    if(!ctr_tev_decode(state->registers,state->register_defined,mask,&tev) ||
       !pixel_program(s,p,&tev,mask,texture2_coord1))
        return fail(s,CTR_WIIU_GX2_SCENE_SHADER);
    for(unsigned unit=0;unit<3u;++unit)if(mask&(1u<<unit))
        if(!upload_texture(s,p,&textures[unit],unit))return false;
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
    p->vertex.regs.spi_vs_out_config=population(p->compiled->texcoord_mask)<<1;
    p->vertex.regs.num_spi_vs_out_id=1;
    p->vertex.regs.spi_vs_out_id[0]=UINT32_C(0xffffff00);
    unsigned output=1u;
    for(unsigned unit=0;unit<3u;++unit)if(p->compiled->texcoord_mask&(1u<<unit)) {
        unsigned shift=output++*8u;
        p->vertex.regs.spi_vs_out_id[0]=(p->vertex.regs.spi_vs_out_id[0]&~(UINT32_C(255)<<shift))|
            ((unit+1u)<<shift);
    }
    for(unsigned index=1;index<10;++index)p->vertex.regs.spi_vs_out_id[index]=UINT32_MAX;
    p->vertex.regs.sq_vtx_semantic_clear=UINT32_C(0xffff0000);
    p->vertex.regs.num_sq_vtx_semantic=16;
    for(unsigned index=0;index<16;++index)p->vertex.regs.sq_vtx_semantic[index]=index;
    p->vertex.regs.vgt_vertex_reuse_block_cntl=14;p->vertex.regs.vgt_hos_reuse_depth=16;
    GX2SetContextState(p->context);
    if(!viewport(p,state))return fail(s,CTR_WIIU_GX2_SCENE_LAYOUT);
    GX2SetColorBuffer(&p->target,GX2_RENDER_TARGET_0);
    if(p->depth_enabled)GX2SetDepthBuffer(&p->depth_target);
    GX2SetShaderModeEx(GX2_SHADER_MODE_UNIFORM_REGISTER,64,64,0,0,184,192);
    GX2SetFetchShader(&p->fetch);GX2SetVertexShader(&p->vertex);GX2SetPixelShader(&p->pixel);
    /* Inactive descriptors may remain in shadow state, but only the explicit
     * mask's unit IDs can occur in this fully rebound program. No inactive
     * texture image is sampled or freed before the common retirement gate. */
    for(unsigned unit=0;unit<3u;++unit)if(mask&(1u<<unit)) {
        GX2SetPixelTexture(&p->texture[unit],unit);GX2SetPixelSampler(&p->sampler[unit],unit);
    }
    GX2SetPixelUniformReg(0,p->buffered?36u:32u,p->pixel_constants);
    GX2SetAttribBuffer(0,count*VERTEX_STRIDE,VERTEX_STRIDE,p->vertices);
    for(unsigned uniform=0;uniform<CTR_SHADER_UNIFORMS;++uniform)if(p->compiled->uniform_used[uniform])
        GX2SetVertexUniformReg(uniform*4u,4,state->shader.uniforms[uniform]);
    fragment_state(&fragment);
    depth_state(p,state,&stencil);
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
static bool draw(void *context,const ctr_scene_state *state,const ctr_vertex *vertices,unsigned count,
                 const ctr_scene_texture *texture) {
    ctr_scene_texture textures[3]={{0}};
    if(texture)textures[0]=*texture;
    return draw_textures(context,state,vertices,count,textures,texture?1u:0u);
}
static bool end_targets(void *context,const ctr_scene_surface *readback,
                        const ctr_scene_depth_surface *depth) {
    ctr_wiiu_gx2_scene *s=context;scene_private *p=s?s->private_state:NULL;
    if(!p || !dimensions(readback) || readback->width!=p->width || readback->height!=p->height ||
       readback->stride!=p->stride || readback->size!=p->size ||
       p->depth_enabled!=(depth!=NULL) ||
       (depth && (!depth_dimensions(depth,readback) || depth->stride!=p->depth_stride ||
                  depth->size!=p->depth_size || depth->address!=p->depth_address || depth->format!=p->depth_format)))
        return fail(s,CTR_WIIU_GX2_SCENE_ARGUMENT);
    if(s->quarantined)return fail(s,CTR_WIIU_GX2_SCENE_WAIT);
    GX2SetContextState(p->context);
    GX2Invalidate(GX2_INVALIDATE_MODE_COLOR_BUFFER,p->target.surface.image,p->target.surface.imageSize);
    GX2CopySurface(&p->target.surface,0,0,&p->linear,0,0);p->pending=true;
    if(depth) {
        GX2Invalidate(GX2_INVALIDATE_MODE_DEPTH_BUFFER,p->depth_target.surface.image,p->depth_target.surface.imageSize);
        GX2CopySurface(&p->depth_target.surface,0,0,&p->depth_linear,0,0);
    }
    if(!retire(s,p))return fail(s,CTR_WIIU_GX2_SCENE_WAIT);
    DCInvalidateRange(p->linear.image,p->linear.imageSize);
    if(depth)DCInvalidateRange(p->depth_linear.image,p->depth_linear.imageSize);
    for(unsigned y=0;y<p->height;++y)
        memcpy((uint8_t *)readback->bytes+(size_t)y*readback->stride,
               (const uint8_t *)p->linear.image+(size_t)y*p->linear.pitch*4u,p->width*4u);
    if(depth)for(unsigned y=0;y<p->height;++y)
        memcpy((uint8_t *)depth->bytes+(size_t)y*depth->stride,
               (const uint8_t *)p->depth_linear.image+(size_t)y*p->depth_linear.pitch*p->depth_bytes,p->width*p->depth_bytes);
    if(s->observe_readback)s->observe_readback(s->readback_context,readback);
    if(depth && s->observe_depth_readback)s->observe_depth_readback(s->readback_context,depth);
    if(!restore(s))return false;
    ++s->readbacks;s->result=CTR_WIIU_GX2_SCENE_OK;release(s,p);return true;
}
static bool end(void *context,const ctr_scene_surface *readback) {
    return end_targets(context,readback,NULL);
}
static bool end_depth(void *context,const ctr_scene_surface *readback,
                      const ctr_scene_depth_surface *depth) {
    if(!depth)return fail(context,CTR_WIIU_GX2_SCENE_ARGUMENT);
    return end_targets(context,readback,depth);
}
void ctr_wiiu_gx2_scene_init(ctr_wiiu_gx2_scene *s,GX2ContextState *restore_context) {
    if(s) { memset(s,0,sizeof(*s));s->restore_context=restore_context; }
}
static void abort_callback(void *context) { ctr_wiiu_gx2_scene_abort(context); }
ctr_scene_backend ctr_wiiu_gx2_scene_backend(ctr_wiiu_gx2_scene *s) {
    return (ctr_scene_backend){s,begin,draw,end,abort_callback,begin_depth,end_depth,draw_textures};
}
const char *ctr_wiiu_gx2_scene_result_name(ctr_wiiu_gx2_scene_result result) {
    static const char *const names[]={"ok","argument","allocation","layout","shader","wait","busy"};
    return (unsigned)result<sizeof(names)/sizeof(*names)?names[result]:"unknown";
}
