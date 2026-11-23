/* Original bounded PICA -> GX2 offscreen submission. ISA export facts and shader
 * register metadata were checked against devkitPro/wut commit
 * 937f0d6901f5e2a72672154a674ec47b63e2f417, samples/cmake/content/pos_col_shader;
 * TEX/ALU/CF fields against decaf-emu/decaf-emu
 * e6c528a20a41c34e0f9eb91dd3da40f119db2dee, src/libgpu/latte definitions.
 * The rounded game diagnostic can submit CPU-transformed vertices. */
#include "gx2_scene_internal.h"
#include "diagnostic_display.h"
#include "ctr_guest_pica.h"

typedef struct {
    ctr_scene_scanout_state queue;
    GX2Texture images[CTR_GX_SCANOUT_SLOTS];
    ctr_diagnostic_scanout display[CTR_GX_SCANOUT_SLOTS];
    GX2Surface linear;
} gx2_scanout_state;
#define pixel_shader ctr_gx2_pixel_shader
#define transformed_program ctr_gx2_transformed_program
#define upload_vertex_program ctr_gx2_upload_vertex_program
typedef struct {
    GX2PixelShader metadata;
    uint8_t program[PIXEL_BYTES];
} pixel_cache_record;
_Static_assert(sizeof(pixel_cache_record)<=CTR_GX2_CACHE_PIXEL_PAYLOAD_MAX,
               "pixel program cache record exceeds its fixed CPU bound");

void ctr_gx2_cache_add(ctr_wiiu_gx2_scene *s,uint64_t *counter,uint64_t amount) {
    if(amount>=UINT64_MAX-*counter) {
        *counter=UINT64_MAX;s->cache_stats.saturated=true;
    } else *counter+=amount;
}
#define cache_add ctr_gx2_cache_add
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

static scene_private *queue_restore(ctr_wiiu_gx2_scene *s) {
    if(s && s->restore_context) {
        GX2SetContextState(s->restore_context);
        /* Keep shadow-state references alive through the next retirement. */
        scene_private *p=s->private_state;
        if(p && p->context) {
            p->raster_current=false;
            p->merger_current=false;
            p->bound_texture_mask=p->bound_sampler_mask=0;
            p->draw_context_active=false;p->uploaded_pixel_vectors=0;p->pending=true;return p;
        }
    }
    return NULL;
}
bool ctr_gx2_restore(ctr_wiiu_gx2_scene *s) {
    scene_private *p=queue_restore(s);
    return !p || ctr_gx2_retire(s,p);
}
#define restore ctr_gx2_restore
bool ctr_gx2_fail(ctr_wiiu_gx2_scene *s,ctr_wiiu_gx2_scene_result result) {
    if(s)s->result=result;
    restore(s);return false;
}
#define fail ctr_gx2_fail
void *ctr_gx2_allocate(ctr_wiiu_gx2_scene *s,size_t alignment,size_t bytes) {
    if(alignment<256u)alignment=256u;
    void *p=memalign(alignment,bytes);
    if(p) { memset(p,0,bytes);++s->allocations; }
    return p;
}
#define allocate ctr_gx2_allocate
void ctr_gx2_dispose(ctr_wiiu_gx2_scene *s,void *p) {
    if(p) { free(p);++s->releases; }
}
#define dispose ctr_gx2_dispose
static void texture_cache_add(ctr_wiiu_gx2_scene *s,uint64_t *counter,uint64_t amount) {
    if(amount>=UINT64_MAX-*counter) {
        *counter=UINT64_MAX;s->texture_cache_stats.saturated=true;
    } else *counter+=amount;
}
static void texture_leases_release(ctr_wiiu_gx2_scene *s,scene_private *p) {
    for(unsigned unit=0;unit<3u;++unit)if(p->texture_lease[unit]) {
        --p->texture_lease[unit]->pins;
        --s->texture_cache_stats.pins;
        p->texture_lease[unit]=NULL;
    }
}
static void texture_cache_destroy(ctr_wiiu_gx2_scene *s) {
    if(s->texture_cache) {
        for(unsigned i=0;i<s->texture_cache->slots;++i)
            dispose(s,s->texture_cache->entries[i].upload.texture.surface.image);
        dispose(s,s->texture_cache);s->texture_cache=NULL;
    }
    s->texture_cache_stats.bytes=0;s->texture_cache_stats.pins=0;
}
bool ctr_wiiu_gx2_scene_texture_cache_clear(ctr_wiiu_gx2_scene *s) {
    if(!s || s->private_state)return false;
    texture_cache_destroy(s);return true;
}
static bool wait_for_gx2(ctr_wiiu_gx2_scene *s) {
    ++s->waits;
    bool finished;
    if(s->cemu_readback) {
        /* Cemu 2.6 implements DrawDone's full synchronization but exposes
         * it as void. Read the status from its implemented timestamp fence. */
        (void)GX2DrawDone();
        finished=GX2WaitTimeStamp(GX2GetLastSubmittedTimeStamp());
    } else finished=GX2DrawDone();
    if(!finished) {
        ++s->wait_failures;s->quarantined=true;s->result=CTR_WIIU_GX2_SCENE_WAIT;
        return false;
    }
    s->quarantined=false;return true;
}
bool ctr_gx2_retire(ctr_wiiu_gx2_scene *s,scene_private *p) {
    if(!p->pending)return true;
    if(!wait_for_gx2(s))return false;
    if(p->used_uploads && s->texture_cache)
        for(unsigned i=0;i<s->texture_cache->slots;++i)s->texture_cache->entries[i].submitted=false;
    p->used_uploads=0;
    p->pending=false;return true;
}
#define retire ctr_gx2_retire
void ctr_gx2_release(ctr_wiiu_gx2_scene *s,scene_private *p) {
    texture_leases_release(s,p);
    for(unsigned unit=0;unit<3u;++unit)dispose(s,p->texture[unit].texture.surface.image);
    dispose(s,p->vertices);dispose(s,p->draw_uploads);dispose(s,p->fetch_program);
    if(s->direct_pool) {
        direct_pool_state *pool=s->direct_pool;
        if(p->context==pool->context)p->context=NULL;
        if(p->compiled==pool->compiled)p->compiled=NULL;
        if(p->target.surface.image==pool->target.surface.image ||
           p->target.surface.image==pool->color_candidate.surface.image)p->target.surface.image=NULL;
        if(p->linear.image==pool->linear.image)p->linear.image=NULL;
        if(p->depth_target.surface.image==pool->depth_target.surface.image)p->depth_target.surface.image=NULL;
    }
    dispose(s,p->pixel_storage);dispose(s,p->vertex_storage);
    dispose(s,p->compiled);dispose(s,p->linear.image);
    dispose(s,p->depth_linear.image);dispose(s,p->depth_target.surface.image);
    dispose(s,p->depth_resolve.image);dispose(s,p->stencil_linear.image);
    dispose(s,p->stencil_target.surface.image);
    dispose(s,p->blend_snapshot.surface.image);
    dispose(s,p->target.surface.image);dispose(s,p->context);dispose(s,p);
    s->private_state=NULL;s->quarantined=false;
}
#define release ctr_gx2_release
void ctr_wiiu_gx2_scene_abort(ctr_wiiu_gx2_scene *s) {
    if(!s)return;
    scene_private *p=s->private_state;
    /* A completed transaction already restored WHB. Idle cleanup can release
     * its retired cache even after foreground release, without any GX2 call. */
    if(p) {
        if(!restore(s) || !retire(s,p))return;
        release(s,p);
    } else ctr_gx2_direct_pool_destroy(s);
    texture_cache_destroy(s);
}
void ctr_wiiu_gx2_scene_after_shutdown(ctr_wiiu_gx2_scene *s) {
    if(!s)return;
    if(s->private_state)release(s,s->private_state);
    ctr_gx2_direct_pool_destroy(s);
    texture_cache_destroy(s);
    ctr_wiiu_gx2_scene_cache_clear(s);
    gx2_scanout_state *scanout=s->scanout;
    if(scanout) {
        for(unsigned i=0;i<CTR_GX_SCANOUT_SLOTS;++i)
            dispose(s,scanout->images[i].surface.image);
        dispose(s,scanout->linear.image);
        dispose(s,scanout);
        s->scanout=NULL;
    }
    s->restore_context=NULL;
}
static bool dimensions(const ctr_scene_surface *surface) {
    return surface && surface->bytes && surface->width && surface->height &&
        surface->width<=512u && surface->height<=512u &&
        surface->stride>=surface->width*4u &&
        (uint64_t)surface->stride*surface->height<=surface->size &&
        surface->size<=CTR_SCENE_MAX_SURFACE_BYTES;
}
bool ctr_gx2_layout(GX2Surface *surface,unsigned width,unsigned height,GX2TileMode tile,unsigned format) {
    surface->dim=GX2_SURFACE_DIM_TEXTURE_2D;surface->width=width;surface->height=height;
    surface->depth=1;surface->mipLevels=1;surface->format=format;
    surface->aa=GX2_AA_MODE1X;surface->use=GX2_SURFACE_USE_TEXTURE|GX2_SURFACE_USE_COLOR_BUFFER;
    surface->tileMode=tile;GX2CalcSurfaceSizeAndAlignment(surface);
    return surface->tileMode==tile && surface->pitch>=width &&
        surface->imageSize && surface->imageSize<=MAX_IMAGE_BYTES &&
        surface->alignment && !(surface->alignment&(surface->alignment-1u)) &&
        surface->alignment<=65536u && !surface->mipmapSize;
}
#define layout ctr_gx2_layout
static bool depth_dimensions(const ctr_scene_depth_surface *depth,const ctr_scene_surface *color) {
    if(!depth || (depth->format!=0u && depth->format!=3u))return false;
    unsigned bytes=depth->format==3u?4u:2u;
    size_t limit=depth->format==3u?CTR_SCENE_MAX_D24S8_BYTES:CTR_SCENE_MAX_DEPTH_BYTES;
    return depth->bytes && depth->width==color->width && depth->height==color->height &&
        depth->stride>=depth->width*bytes &&
        (uint64_t)depth->stride*depth->height<=depth->size && depth->size<=limit;
}
bool ctr_gx2_depth_layout(GX2Surface *surface,unsigned width,unsigned height,GX2TileMode tile,
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
#define depth_layout ctr_gx2_depth_layout
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
static void pixel_alu(scene_private *p,unsigned *count,uint32_t operands,uint32_t instruction) {
    if(*count>=PIXEL_MAX_ALUS) { *count=PIXEL_MAX_ALUS+1u;return; }
    uint8_t *at=(uint8_t *)p->pixel_program+PIXEL_ALU_OFFSET+(*count)++*8u;
    put32(at,operands|UINT32_C(0x80000000));
    put32(at+4u,instruction);
}
static void pixel_op(scene_private *p,unsigned *count,unsigned opcode,unsigned a,unsigned ac,
                      unsigned b,unsigned bc,bool negate,unsigned dest,unsigned lane) {
    pixel_alu(p,count,a|(ac<<10)|(b<<13)|(bc<<23)|(negate?UINT32_C(1)<<25:0u),
              (opcode<<7)|(1u<<4)|(dest<<21)|(lane<<29));
}
static void pixel_select(scene_private *p,unsigned *count,unsigned opcode,unsigned cond,unsigned channel,
                         unsigned values,unsigned yes,unsigned no,unsigned dest,unsigned lane) {
    pixel_alu(p,count,cond|(channel<<10)|(values<<13)|(yes<<23),
              values|(no<<10)|(opcode<<13)|(dest<<21)|(lane<<29));
}
static void pixel_mov(scene_private *p,unsigned *count,unsigned a,unsigned ac,unsigned dest,unsigned lane) {
    pixel_op(p,count,25,a,ac,0,0,false,dest,lane);
}
static void pixel_clamp_byte(scene_private *p,unsigned *count,unsigned reg,unsigned lane) {
    pixel_op(p,count,3,reg,lane,263,3,false,reg,lane);
    pixel_op(p,count,4,reg,lane,262,0,false,reg,lane);
}
static void pixel_divide(scene_private *p,unsigned *count,unsigned dest,unsigned lane) {
    pixel_op(p,count,2,dest,lane,262,2,false,dest,lane);
    pixel_op(p,count,20,dest,lane,0,0,false,dest,lane);
}
static bool blend_reads_destination(pixel_blend_mode blend) {
    return blend==BLEND_SOURCE_OVER || blend==BLEND_DESTINATION_SCALE;
}
static void pixel_destination_scale(scene_private *p,unsigned *count,unsigned dest) {
    /* Scale destination RGB by the inverse constant and preserve its alpha. */
    for(unsigned lane=0;lane<4u;++lane) {
        pixel_op(p,count,2,BLEND_DESTINATION,lane,262,0,false,dest,lane);
        pixel_op(p,count,0,dest,lane,262,3,false,dest,lane);
        pixel_op(p,count,20,dest,lane,0,0,false,dest,lane);
        if(lane<3u) {
            pixel_op(p,count,2,dest,lane,256u+BLEND_CONSTANT_SCALE,lane,false,dest,lane);
            pixel_divide(p,count,dest,lane);
        }
    }
}
static void pixel_source_over(scene_private *p,unsigned *count,unsigned source) {
    /* RGB and alpha use SRC_ALPHA/INV_SRC_ALPHA. Products and their sum are
     * exact byte-domain integers in [0, 65025], before the single floor. */
    pixel_op(p,count,0,262,0,source,3,true,BLEND_FACTOR,0);
    for(unsigned lane=0;lane<4u;++lane) {
        pixel_op(p,count,2,BLEND_DESTINATION,lane,262,0,false,BLEND_DESTINATION,lane);
        pixel_op(p,count,0,BLEND_DESTINATION,lane,262,3,false,BLEND_DESTINATION,lane);
        pixel_op(p,count,20,BLEND_DESTINATION,lane,0,0,false,BLEND_DESTINATION,lane);
        pixel_op(p,count,2,BLEND_DESTINATION,lane,BLEND_FACTOR,0,false,BLEND_DESTINATION,lane);
        pixel_op(p,count,2,source,lane,source,3,false,source,lane);
        pixel_op(p,count,0,source,lane,BLEND_DESTINATION,lane,false,source,lane);
        pixel_divide(p,count,source,lane);
    }
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
    memset(p->pixel_constants,0,FOG_TABLE_FIRST*sizeof(p->pixel_constants[0]));
    for(unsigned stage=0;stage<CTR_TEV_STAGES;++stage)
        for(unsigned lane=0;lane<4;++lane)p->pixel_constants[stage][lane]=bits((float)tev->stages[stage].constant[lane]);
    p->pixel_constants[6][0]=bits(255.0f);p->pixel_constants[6][1]=bits(128.0f);
    /* The upward binary32 reciprocal gives exact floor(N/255), 0<=N<=65025.
     * Integer products/sums stay exactly representable; no fused operation. */
    p->pixel_constants[6][2]=UINT32_C(0x3b808081);p->pixel_constants[6][3]=bits(0.5f);
    for(unsigned lane=0;lane<4;++lane)p->pixel_constants[7][lane]=bits(lane<3u?(float)(1u<<lane):0.0f);
    if(p->buffered)for(unsigned lane=0;lane<4;++lane)
        p->pixel_constants[8][lane]=bits((float)tev->buffer_color[lane]);
    p->pixel_uniform_vectors=p->buffered?9u:8u;
}
static uint32_t pack_lut(unsigned base,int slope) {
    /* Bias keeps every packed value out of the floating-point NaN encoding.
     * Two integer fields per lane let all fog and lighting LUTs coexist. */
    return base|((uint32_t)(slope+4096)<<16);
}
static void lookup_constants(scene_private *p) {
    uint32_t *masks=p->pixel_constants[LUT_MASKS],*format=p->pixel_constants[LUT_FORMAT];
    masks[0]=1; masks[1]=2; masks[2]=255; masks[3]=65535;
    format[0]=2; format[1]=16; format[2]=bits(4096.0f); format[3]=bits(127.0f);
    if(p->pixel_uniform_vectors<LUT_FORMAT+1u)p->pixel_uniform_vectors=LUT_FORMAT+1u;
}
static void pixel_lookup(scene_private *p,unsigned *count,unsigned index,unsigned channel,unsigned table) {
    /* Read a packed word using AR.x for the vector and two low index bits
     * for the lane. Masking also wraps signed lighting coordinates. */
    unsigned masks=256u+LUT_MASKS,format=256u+LUT_FORMAT;
    pixel_op(p,count,0x6b,index,channel,0,0,false,LUT_VALUE,0);
    pixel_op(p,count,0x30,LUT_VALUE,0,masks,2,false,LUT_VALUE,0);
    pixel_op(p,count,0x30,LUT_VALUE,0,masks,0,false,LUT_VALUE,2);
    pixel_op(p,count,0x30,LUT_VALUE,0,masks,1,false,LUT_VALUE,3);
    pixel_op(p,count,0x71,LUT_VALUE,0,format,0,false,LUT_VALUE,0);
    pixel_alu(p,count,LUT_VALUE,0x18u<<7);
    for(unsigned lane=0;lane<4u;++lane)
        pixel_alu(p,count,(256u+table)|(1u<<9)|(lane<<10),
                  (25u<<7)|(1u<<4)|(LUT_LANES<<21)|(lane<<29));
    pixel_select(p,count,0x1c,LUT_VALUE,2,LUT_LANES,0,1,LUT_LANES,0);
    pixel_select(p,count,0x1c,LUT_VALUE,2,LUT_LANES,2,3,LUT_LANES,2);
    pixel_select(p,count,0x1c,LUT_VALUE,3,LUT_LANES,0,2,LUT_VALUE,0);
    pixel_op(p,count,0x71,LUT_VALUE,0,format,1,false,LUT_VALUE,1);
    pixel_op(p,count,0x30,LUT_VALUE,0,masks,3,false,LUT_VALUE,0);
    for(unsigned lane=0;lane<2u;++lane)
        pixel_op(p,count,0x6d,LUT_VALUE,lane,0,0,false,LUT_VALUE,lane);
    pixel_op(p,count,0,LUT_VALUE,1,format,2,true,LUT_VALUE,1);
}
static bool fog_constants(scene_private *p,const ctr_scene_state *state) {
    uint32_t color=state->registers[0x0e1u];
    if(color>0xffffffu)return false;
    bool flip=(state->registers[0x0e0u]&UINT32_C(0x10000))!=0;
    for(unsigned i=0;i<3u;++i)p->pixel_constants[9][i]=bits((float)((color>>(i*8u))&255u));
    p->pixel_constants[9][3]=bits(flip?1.0f:0.0f);
    p->pixel_constants[10][0]=bits(127.0f);
    p->pixel_constants[10][1]=bits(1.0f/2047.0f);
    p->pixel_constants[10][2]=bits(flip?-1.0f:1.0f);
    p->pixel_constants[10][3]=bits(1.0f);
    lookup_constants(p);
    if(p->fog_table_valid && !memcmp(p->fog_table,state->fog_lut,sizeof(p->fog_table)))return true;
    for(unsigned i=0;i<128u;++i)if(state->fog_lut[i]>0xffffffu)return false;
    for(unsigned i=0;i<128u;++i) {
        uint32_t entry=state->fog_lut[i];
        p->pixel_constants[FOG_TABLE_FIRST+i/4u][i%4u]=
            pack_lut(entry>>13,(int)(entry&4095u)-(int)(entry&4096u));
    }
    memcpy(p->fog_table,state->fog_lut,sizeof(p->fog_table));p->fog_table_valid=true;
    return true;
}
static void pixel_fog_factor(scene_private *p,unsigned *count,unsigned position) {
    /* Convert the viewport Z to the retained LUT's address and fraction.
     * Clamp the address before using AR.x, preserving a fraction of one
     * for the far endpoint of the last entry. */
    pixel_op(p,count,2,position,2,266,2,false,FOG_COORD,0);
    pixel_op(p,count,0,FOG_COORD,0,265,3,false,FOG_COORD,0);
    pixel_op(p,count,3,FOG_COORD,0,263,3,false,FOG_COORD,0);
    pixel_op(p,count,4,FOG_COORD,0,266,3,false,FOG_COORD,0);
    pixel_op(p,count,2,FOG_COORD,0,262,1,false,FOG_COORD,0);
    pixel_op(p,count,20,FOG_COORD,0,0,0,false,FOG_COORD,1);
    pixel_op(p,count,4,FOG_COORD,1,266,0,false,FOG_COORD,1);
    pixel_op(p,count,0,FOG_COORD,0,FOG_COORD,1,true,FOG_COORD,2);
    pixel_lookup(p,count,FOG_COORD,1,FOG_TABLE_FIRST);
    pixel_op(p,count,2,LUT_VALUE,1,FOG_COORD,2,false,FOG_COORD,1);
    pixel_op(p,count,0,LUT_VALUE,0,FOG_COORD,1,false,FOG_COORD,0);
    pixel_op(p,count,2,FOG_COORD,0,266,1,false,FOG_COORD,0);
    pixel_op(p,count,3,FOG_COORD,0,263,3,false,FOG_COORD,0);
    pixel_op(p,count,4,FOG_COORD,0,266,3,false,FOG_COORD,2);
}
static void pixel_fog_mix(scene_private *p,unsigned *count,unsigned output) {
    for(unsigned lane=0;lane<3u;++lane) {
        pixel_op(p,count,0,output,lane,265,lane,true,FOG_COORD,0);
        pixel_op(p,count,2,FOG_COORD,0,FOG_COORD,2,false,FOG_COORD,0);
        pixel_op(p,count,0,265,lane,FOG_COORD,0,false,FOG_COORD,0);
        pixel_op(p,count,20,FOG_COORD,0,0,0,false,output,lane);
    }
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
static bool lighting_constants(scene_private *p,const ctr_scene_light *light,unsigned mask) {
    if(light->flags>15u || light->alpha>3u || light->bump_unit>3u ||
       (light->bump_unit<3u && !(mask&(1u<<light->bump_unit))))return false;
    const unsigned registers[]={LIGHT_DIRECTION,LIGHT_AMBIENT,LIGHT_DIFFUSE,LIGHT_SPECULAR0,LIGHT_SPECULAR1};
    const double *vectors[]={light->direction,light->ambient,light->diffuse,light->specular[0],light->specular[1]};
    for(unsigned i=0;i<sizeof(registers)/sizeof(*registers);++i)
        for(unsigned lane=0;lane<3u;++lane)p->pixel_constants[registers[i]][lane]=bits((float)vectors[i][lane]);
    uint32_t *format=p->pixel_constants[LIGHT_FORMAT];
    format[0]=bits(256.0f);format[1]=bits(1.0f/4095.0f);
    format[2]=bits(1.0f/2047.0f);format[3]=bits(2.0f/255.0f);
    p->pixel_constants[LIGHT_SCALE][3]=bits(FLT_MIN);
    p->pixel_uniform_vectors=LIGHT_TABLE_FIRST;
    for(unsigned table=0;table<3u;++table)if(light->table[table]) {
        if((light->input[table]>3u && light->input[table]!=5u) ||
           !isfinite(light->scale[table]))return false;
        p->pixel_constants[LIGHT_SCALE][table]=bits((float)light->scale[table]);
        unsigned first=LIGHT_TABLE_FIRST+table*64u;
        p->pixel_uniform_vectors=first+64u;
        unsigned table_bit=1u<<table;
        if((p->light_table_mask&table_bit) &&
           !memcmp(p->light_tables[table],light->table[table],sizeof(p->light_tables[table])))continue;
        p->light_table_mask&=~table_bit;
        for(unsigned index=0;index<256u;++index) {
            uint32_t entry=light->table[table][index];
            if(entry>0xffffffu)return false;
            int slope=(int)((entry>>12)&2047u);
            if(entry&UINT32_C(0x800000))slope=-slope;
            p->pixel_constants[first+index/4u][index%4u]=pack_lut(entry&4095u,slope);
        }
        memcpy(p->light_tables[table],light->table[table],sizeof(p->light_tables[table]));
        p->light_table_mask|=table_bit;
    }
    lookup_constants(p);
    return true;
}
static void pixel_dot(scene_private *p,unsigned *count,unsigned a,unsigned b,unsigned lanes,
                      unsigned dest,unsigned output) {
    pixel_op(p,count,2,a,0,b,0,false,dest,output);
    for(unsigned lane=1;lane<lanes;++lane) {
        pixel_op(p,count,2,a,lane,b,lane,false,LIGHT_MATH,3);
        pixel_op(p,count,0,dest,output,LIGHT_MATH,3,false,dest,output);
    }
}
static void pixel_inverse_length(scene_private *p,unsigned *count,unsigned vector,unsigned lanes) {
    pixel_dot(p,count,vector,vector,lanes,LIGHT_MATH,0);
    pixel_op(p,count,3,LIGHT_MATH,0,256u+LIGHT_SCALE,3,false,LIGHT_MATH,1);
    pixel_op(p,count,0x69,LIGHT_MATH,1,0,0,false,LIGHT_MATH,1);
}
static void pixel_normalize(scene_private *p,unsigned *count,unsigned vector,unsigned lanes) {
    pixel_inverse_length(p,count,vector,lanes);
    for(unsigned lane=0;lane<lanes;++lane)
        pixel_op(p,count,2,vector,lane,LIGHT_MATH,1,false,vector,lane);
}
static void pixel_diffuse(scene_private *p,unsigned *count,unsigned a,unsigned channel,bool two_sided,
                          unsigned dest,unsigned lane) {
    pixel_op(p,count,3,a,channel,two_sided?a:263u,two_sided?channel:3u,two_sided,dest,lane);
}
static void pixel_light_factor(scene_private *p,unsigned *count,const ctr_scene_light *light,unsigned table) {
    unsigned input=light->input[table],format=256u+LIGHT_FORMAT,bounds=256u+LUT_FORMAT;
    bool absolute=light->absolute[table];
    if(input==5u)pixel_mov(p,count,263,3,LIGHT_VIEW,0);
    else if(absolute)pixel_diffuse(p,count,LIGHT_COSINES,input,(light->flags&2u)!=0,LIGHT_VIEW,0);
    else pixel_mov(p,count,LIGHT_COSINES,input,LIGHT_VIEW,0);
    pixel_op(p,count,3,LIGHT_VIEW,0,263,0,true,LIGHT_VIEW,0);
    pixel_op(p,count,4,LIGHT_VIEW,0,263,0,false,LIGHT_VIEW,0);
    pixel_op(p,count,2,LIGHT_VIEW,0,absolute?format:262u,absolute?0u:1u,false,LIGHT_VIEW,0);
    pixel_op(p,count,20,LIGHT_VIEW,0,0,0,false,LIGHT_VIEW,1);
    pixel_op(p,count,4,LIGHT_VIEW,1,absolute?262u:bounds,absolute?0u:3u,false,LIGHT_VIEW,1);
    pixel_op(p,count,0,LIGHT_VIEW,0,LIGHT_VIEW,1,true,LIGHT_VIEW,0);
    pixel_lookup(p,count,LIGHT_VIEW,1,LIGHT_TABLE_FIRST+table*64u);
    pixel_op(p,count,2,LUT_VALUE,0,format,1,false,LUT_VALUE,0);
    pixel_op(p,count,2,LUT_VALUE,1,format,2,false,LUT_VALUE,1);
    pixel_op(p,count,2,LUT_VALUE,1,LIGHT_VIEW,0,false,LUT_VALUE,1);
    pixel_op(p,count,0,LUT_VALUE,0,LUT_VALUE,1,false,LUT_VALUE,0);
    pixel_op(p,count,2,LUT_VALUE,0,256u+LIGHT_SCALE,table,false,LIGHT_QUATERNION,table);
}
static void pixel_lighting(scene_private *p,unsigned *count,const ctr_scene_light *light,bool multiple) {
    unsigned q=LIGHT_QUATERNION,n=LIGHT_NORMAL,h=LIGHT_HALF,m=LIGHT_MATH,cs=LIGHT_COSINES,view=LIGHT_VIEW;
    unsigned direction=256u+LIGHT_DIRECTION;
    pixel_normalize(p,count,q,4u);
    if(light->bump_unit<3u) {
        unsigned sample=texture_gpr(light->bump_unit,multiple);
        unsigned lanes=light->reconstruct_normal?2u:3u;
        for(unsigned lane=0;lane<lanes;++lane) {
            pixel_op(p,count,2,sample,lane,256u+LIGHT_FORMAT,3,false,n,lane);
            pixel_op(p,count,0,n,lane,263,0,true,n,lane);
        }
        if(light->reconstruct_normal) {
            pixel_dot(p,count,n,n,2u,m,0);
            pixel_op(p,count,0,263,0,m,0,true,n,2);
            pixel_op(p,count,3,n,2,263,3,false,n,2);
            pixel_op(p,count,0x6a,n,2,0,0,false,n,2);
        }
    } else {
        for(unsigned lane=0;lane<3u;++lane)pixel_mov(p,count,263,lane==2u?0u:3u,n,lane);
    }
    /* Quaternion rotation. Keep the original normal until the axis dot and
     * all three cross products have been formed. */
    pixel_dot(p,count,q,q,3u,m,0);
    pixel_op(p,count,2,m,0,263,1,false,m,0);
    pixel_op(p,count,0,263,0,m,0,true,m,0);
    pixel_dot(p,count,q,n,3u,m,1);
    for(unsigned lane=0;lane<3u;++lane) {
        unsigned a=(lane+1u)%3u,b=(lane+2u)%3u;
        pixel_op(p,count,2,q,a,n,b,false,h,lane);
        pixel_op(p,count,2,q,b,n,a,false,m,3);
        pixel_op(p,count,0,h,lane,m,3,true,h,lane);
    }
    for(unsigned lane=0;lane<3u;++lane) {
        pixel_op(p,count,2,h,lane,q,3,false,h,lane);
        pixel_op(p,count,2,m,1,q,lane,false,m,3);
        pixel_op(p,count,0,h,lane,m,3,false,h,lane);
        pixel_op(p,count,2,h,lane,263,1,false,h,lane);
        pixel_op(p,count,2,m,0,n,lane,false,n,lane);
        pixel_op(p,count,0,n,lane,h,lane,false,n,lane);
    }
    pixel_dot(p,count,direction,n,3u,cs,3);
    pixel_diffuse(p,count,cs,3,(light->flags&2u)!=0,LIGHT_PRIMARY,3);
    if(light->needs_view) {
        pixel_normalize(p,count,view,3u);
        for(unsigned lane=0;lane<3u;++lane)
            pixel_op(p,count,0,view,lane,direction,lane,false,h,lane);
        pixel_inverse_length(p,count,h,3u);
        if(light->flags&12u) {
            /* A zero half vector has a zero geometric factor. */
            pixel_op(p,count,9,m,0,263,3,false,m,2);
            pixel_op(p,count,3,m,0,256u+LIGHT_SCALE,3,false,m,0);
            pixel_op(p,count,0x66,m,0,0,0,false,m,0);
            pixel_op(p,count,2,LIGHT_PRIMARY,3,m,0,false,m,0);
            pixel_op(p,count,4,m,0,263,0,false,m,0);
            pixel_op(p,count,2,m,0,m,2,false,m,2);
        }
        for(unsigned lane=0;lane<3u;++lane)
            pixel_op(p,count,2,h,lane,m,1,false,h,lane);
        pixel_dot(p,count,h,n,3u,cs,0);
        pixel_dot(p,count,h,view,3u,cs,1);
        pixel_dot(p,count,n,view,3u,cs,2);
    }
    for(unsigned table=0;table<3u;++table) {
        if(light->table[table])pixel_light_factor(p,count,light,table);
        else pixel_mov(p,count,263,0,q,table);
    }
    if(light->clamp_highlights)pixel_op(p,count,9,LIGHT_PRIMARY,3,263,3,false,q,3);
    for(unsigned table=0;table<2u;++table) {
        if(light->flags&(4u<<table))pixel_op(p,count,2,q,table,m,2,false,q,table);
        if(light->clamp_highlights)pixel_op(p,count,2,q,table,q,3,false,q,table);
    }
    for(unsigned lane=0;lane<3u;++lane) {
        pixel_op(p,count,2,LIGHT_PRIMARY,3,256u+LIGHT_DIFFUSE,lane,false,LIGHT_PRIMARY,lane);
        pixel_op(p,count,0,LIGHT_PRIMARY,lane,256u+LIGHT_AMBIENT,lane,false,LIGHT_PRIMARY,lane);
        pixel_op(p,count,2,q,0,256u+LIGHT_SPECULAR0,lane,false,LIGHT_SECONDARY,lane);
        pixel_op(p,count,2,q,1,256u+LIGHT_SPECULAR1,lane,false,m,0);
        pixel_op(p,count,0,LIGHT_SECONDARY,lane,m,0,false,LIGHT_SECONDARY,lane);
    }
    for(unsigned i=0;i<2u;++i) {
        unsigned output=LIGHT_PRIMARY+i;
        if(light->alpha&(1u<<i))pixel_op(p,count,2,q,2,262,0,false,output,3);
        else pixel_mov(p,count,262,0,output,3);
        for(unsigned lane=0;lane<4u;++lane) {
            pixel_clamp_byte(p,count,output,lane);
            pixel_op(p,count,20,output,lane,0,0,false,output,lane);
        }
    }
}
static void pixel_dot3_rgb(scene_private *p,unsigned *count,unsigned dest) {
    for(unsigned lane=0;lane<3u;++lane) {
        for(unsigned source=0;source<2u;++source) {
            pixel_op(p,count,2,4u+source,lane,263,1,false,8,source);
            pixel_op(p,count,0,8,source,262,0,true,8,source);
        }
        pixel_op(p,count,2,8,0,8,1,false,8,0);
        if(!lane)pixel_mov(p,count,8,0,8,2);
        else pixel_op(p,count,0,8,2,8,0,false,8,2);
    }
    pixel_divide(p,count,8,2);
    for(unsigned lane=0;lane<3u;++lane)pixel_mov(p,count,8,2,dest,lane);
}
static void pixel_mip_coordinates(scene_private *p,unsigned *count,unsigned input,
                                  unsigned uv_gradient,unsigned w_gradient,unsigned unit,bool mixed) {
    unsigned horizontal=MIP_DX+unit,vertical=MIP_DY+unit,output=MIP_COORDINATE+unit;
    unsigned dimensions=256u+MIP_DIMENSIONS+unit;
    /* The flat gradients come from the snapped triangle. The interpolated
     * input carries U, V and W, so the last multiply completes the quotient rule. */
    for(unsigned axis=0;axis<2u;++axis) {
        pixel_op(p,count,2,w_gradient,0,input,axis,false,output,axis);
        pixel_op(p,count,0,uv_gradient,axis,output,axis,true,horizontal,axis);
        pixel_op(p,count,2,w_gradient,1,input,axis,false,output,axis);
        pixel_op(p,count,0,uv_gradient,axis+2u,output,axis,true,vertical,axis);
        pixel_op(p,count,3,horizontal,axis,horizontal,axis,true,horizontal,axis);
        pixel_op(p,count,3,vertical,axis,vertical,axis,true,vertical,axis);
        pixel_op(p,count,3,horizontal,axis,vertical,axis,false,horizontal,axis);
        pixel_op(p,count,2,horizontal,axis,dimensions,axis,false,horizontal,axis);
        pixel_mov(p,count,input,axis,output,axis);
    }
    pixel_op(p,count,3,horizontal,0,horizontal,1,false,output,3);
    pixel_op(p,count,2,output,3,input,2,false,output,3);
    pixel_op(p,count,3,output,3,dimensions,3,false,output,3);
    pixel_op(p,count,0x63,output,3,0,0,false,output,3);
    pixel_op(p,count,0,output,3,dimensions,2,false,output,3);
    /* The level bias includes 0.5 for nearest mip rounding. Test the
     * original signed LOD before flooring or clamping the selected level. */
    if(mixed)pixel_op(p,count,9,output,3,262,3,false,output,2);
    pixel_op(p,count,20,output,3,0,0,false,output,3);
}
static void pixel_depth_sum(scene_private *p,unsigned *count,
                            unsigned a,unsigned ac,unsigned b,unsigned bc,unsigned sum,unsigned sc) {
    /* TwoSum keeps the rounding residual in DEPTH_SUM.z. */
    pixel_op(p,count,0,a,ac,b,bc,false,sum,sc);
    pixel_op(p,count,0,sum,sc,a,ac,true,DEPTH_SUM,0);
    pixel_op(p,count,0,sum,sc,DEPTH_SUM,0,true,DEPTH_SUM,1);
    pixel_op(p,count,0,a,ac,DEPTH_SUM,1,true,DEPTH_SUM,1);
    pixel_op(p,count,0,b,bc,DEPTH_SUM,0,true,DEPTH_SUM,0);
    pixel_op(p,count,0,DEPTH_SUM,0,DEPTH_SUM,1,false,DEPTH_SUM,2);
}
static void pixel_filter_coordinates(scene_private *p,unsigned *count,unsigned unit,unsigned w_gradient,bool mixed) {
    unsigned uv=MIP_COORDINATE+unit,weights=FILTER_WEIGHT+unit,tap=FILTER_TAP+unit*4u;
    unsigned scale=256u+FILTER_SCALE,levels=256u+FILTER_LEVELS+unit,dim=256u+MIP_DIMENSIONS+unit;
    pixel_op(p,count,3,uv,3,levels,0,false,FILTER_WORK,3);
    pixel_op(p,count,4,FILTER_WORK,3,levels,1,false,FILTER_WORK,3);
    pixel_op(p,count,0,248,0,FILTER_WORK,3,true,FILTER_WORK,2);
    pixel_op(p,count,0x61,FILTER_WORK,2,0,0,false,FILTER_WORK,2);
    if(mixed)pixel_select(p,count,0x1c,uv,2,levels,2,3,uv,2);
    /* PICA quantizes perspective UVs to Q16 before texel interpolation. */
    for(unsigned axis=0;axis<2u;++axis) {
        pixel_op(p,count,2,dim,axis,FILTER_WORK,2,false,weights,axis+2u);
        pixel_mov(p,count,uv,axis,FILTER_WORK,0);
        pixel_op(p,count,2,uv,axis,scale,0,false,FILTER_WORK,1);
        pixel_op(p,count,0x6b,FILTER_WORK,1,0,0,false,FILTER_WORK,1);
        pixel_op(p,count,0x6c,FILTER_WORK,1,0,0,false,FILTER_WORK,1);
        pixel_op(p,count,2,FILTER_WORK,1,scale,1,false,FILTER_WORK,1);
        pixel_select(p,count,0x1c,w_gradient,2,FILTER_WORK,0,1,FILTER_WORK,1);
        pixel_op(p,count,2,FILTER_WORK,1,weights,axis+2u,false,FILTER_WORK,1);
        pixel_op(p,count,0,FILTER_WORK,1,262,3,true,FILTER_WORK,1);
        pixel_op(p,count,20,FILTER_WORK,1,0,0,false,tap,axis);
        pixel_op(p,count,0,FILTER_WORK,1,tap,axis,true,weights,axis);
        if(mixed) {
            pixel_op(p,count,10,weights,axis,262,3,false,FILTER_WORK,0);
            pixel_mov(p,count,weights,axis,FILTER_WORK,1);
            pixel_select(p,count,0x1c,uv,2,FILTER_WORK,0,1,weights,axis);
        }
        pixel_op(p,count,0,tap,axis,262,3,false,tap,axis);
        pixel_op(p,count,0x66,weights,axis+2u,0,0,false,weights,axis+2u);
        pixel_op(p,count,2,tap,axis,weights,axis+2u,false,tap,axis);
    }
    for(unsigned index=1;index<4u;++index)for(unsigned axis=0;axis<2u;++axis) {
        if(index&(1u<<axis))
            pixel_op(p,count,0,tap,axis,weights,axis+2u,false,tap+index,axis);
        else pixel_mov(p,count,tap,axis,tap+index,axis);
    }
    for(unsigned index=0;index<4u;++index)pixel_mov(p,count,FILTER_WORK,3,tap+index,3);
}
static void pixel_filter_sample(scene_private *p,unsigned *count,unsigned unit,unsigned sample) {
    unsigned weights=FILTER_WEIGHT+unit,tap=FILTER_TAP+unit*4u;
    /* Points are normalized bytes. Round once after both linear weights. */
    for(unsigned lane=0;lane<4u;++lane) {
        for(unsigned row=0;row<2u;++row) {
            unsigned lo=tap+row*2u,hi=lo+1u;
            pixel_op(p,count,0,hi,lane,lo,lane,true,hi,lane);
            pixel_op(p,count,2,hi,lane,weights,0,false,hi,lane);
            pixel_op(p,count,0,lo,lane,hi,lane,false,lo,lane);
        }
        pixel_op(p,count,0,tap+2u,lane,tap,lane,true,sample,lane);
        pixel_op(p,count,2,sample,lane,weights,1,false,sample,lane);
        pixel_op(p,count,0,sample,lane,tap,lane,false,sample,lane);
        pixel_op(p,count,2,sample,lane,262,0,false,sample,lane);
        pixel_op(p,count,0,sample,lane,262,3,false,sample,lane);
        pixel_op(p,count,20,sample,lane,0,0,false,sample,lane);
    }
}
bool ctr_gx2_pixel_shader(scene_private *p,const ctr_tev_program *tev,unsigned mask,
                          bool texture2_coord1,unsigned implicit_lod,unsigned explicit_lod,unsigned filter_mask,unsigned mixed_filters,bool fog,bool transformed,
                          const ctr_scene_light *light,bool depth_plane,pixel_blend_mode blend) {
    bool reads_destination=blend_reads_destination(blend);
    uint8_t *program=p->pixel_program;memset(program,0,PIXEL_BYTES);
    bool multiple=mask>1u;unsigned coordinates=coordinate_mask(mask,texture2_coord1);
    unsigned count=0,previous=multiple?14u:3u,destination=7;
    unsigned mip_coordinates=coordinate_mask(explicit_lod,texture2_coord1);
    unsigned quaternion=1u+population(coordinates);
    unsigned depth_coefficients=quaternion+(light?(light->needs_view?2u:1u):0u);
    unsigned gradient_pos=depth_coefficients+(depth_plane?2u:0u);
    unsigned w_gradient=gradient_pos+population(mip_coordinates);
    unsigned position=w_gradient+(explicit_lod!=0u);
    if(light) {
        for(unsigned lane=0;lane<4u;++lane)pixel_mov(p,&count,quaternion,lane,LIGHT_QUATERNION,lane);
        if(light->needs_view)
            for(unsigned lane=0;lane<3u;++lane)pixel_mov(p,&count,quaternion+1u,lane,LIGHT_VIEW,lane);
    }
    if(depth_plane) {
        /* Evaluate in local coordinates. Add the split gradients and depth
         * origin with their rounding residuals before exporting depth. */
        unsigned coefficients=depth_coefficients,origin=coefficients+1u;
        for(unsigned axis=0;axis<2u;++axis) {
            pixel_op(p,&count,0,position,axis,origin,axis,true,DEPTH_PLANE,axis);
            pixel_op(p,&count,2,DEPTH_PLANE,axis,coefficients,axis,false,DEPTH_PRODUCTS,axis);
            pixel_op(p,&count,2,DEPTH_PLANE,axis,origin,axis+2u,false,DEPTH_PRODUCTS,axis+2u);
        }
        pixel_depth_sum(p,&count,DEPTH_PRODUCTS,0,DEPTH_PRODUCTS,1,DEPTH_PLANE,1);
        pixel_op(p,&count,0,DEPTH_PRODUCTS,2,DEPTH_PRODUCTS,3,false,DEPTH_PLANE,2);
        pixel_op(p,&count,0,DEPTH_PLANE,2,coefficients,2,false,DEPTH_PLANE,2);
        pixel_op(p,&count,0,DEPTH_PLANE,2,DEPTH_SUM,2,false,DEPTH_PLANE,2);
        pixel_depth_sum(p,&count,coefficients,3,DEPTH_PLANE,1,DEPTH_PLANE,3);
        pixel_op(p,&count,0,DEPTH_PLANE,2,DEPTH_SUM,2,false,DEPTH_PLANE,2);
        pixel_op(p,&count,0,DEPTH_PLANE,3,DEPTH_PLANE,2,false,DEPTH_PLANE,0);
        pixel_op(p,&count,3,DEPTH_PLANE,0,248,0,false,DEPTH_PLANE,0);
        pixel_op(p,&count,4,DEPTH_PLANE,0,249,0,false,DEPTH_PLANE,0);
    }
    for(unsigned unit=0;unit<3u;++unit)if(explicit_lod&(1u<<unit)) {
        unsigned coordinate=unit==2u && texture2_coord1?1u:unit;
        unsigned uv_gradient=gradient_pos+coordinate_gpr(coordinate,mip_coordinates)-1u;
        pixel_mip_coordinates(p,&count,coordinate_gpr(coordinate,coordinates),uv_gradient,w_gradient,unit,
                              (mixed_filters&(1u<<unit))!=0u);
    }
    if(fog)pixel_fog_factor(p,&count,position);
    if(reads_destination)for(unsigned axis=0;axis<2u;++axis)
        pixel_op(p,&count,2,position,axis,256u+BLEND_UV_SCALE,axis,false,BLEND_UV,axis);
    for(unsigned unit=0;unit<3u;++unit)if(filter_mask&(1u<<unit))
        pixel_filter_coordinates(p,&count,unit,w_gradient,(mixed_filters&(1u<<unit))!=0u);
    unsigned prefix=count;
    for(unsigned unit=0;unit<3u;++unit)if(filter_mask&(1u<<unit))
        pixel_filter_sample(p,&count,unit,texture_gpr(unit,multiple));
    for(unsigned lane=0;lane<4;++lane) {
        if(transformed) {
            pixel_op(p,&count,3,0,lane,263,3,false,0,lane);
            pixel_op(p,&count,4,0,lane,263,0,false,0,lane);
            pixel_mov(p,&count,263,3,previous,lane);
        }
        pixel_op(p,&count,2,0,lane,262,0,false,0,lane);
        pixel_op(p,&count,0,0,lane,262,3,false,0,lane);
        pixel_op(p,&count,20,0,lane,0,0,false,0,lane);
        for(unsigned unit=0;unit<3u;++unit)if((mask&~filter_mask)&(1u<<unit)) {
            unsigned sample=texture_gpr(unit,multiple);
            pixel_op(p,&count,2,sample,lane,262,0,false,sample,lane);
            pixel_op(p,&count,0,sample,lane,262,3,false,sample,lane);
            pixel_op(p,&count,20,sample,lane,0,0,false,sample,lane);
        }
    }
    if(light)pixel_lighting(p,&count,light,multiple);
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
                        source==14u?256u+stage:source==15u?previous:
                        light?LIGHT_PRIMARY+source-1u:263u;
                    const unsigned alpha_channels[4]={3,0,1,2};
                    unsigned channel=side?alpha_channels[operand/2u]:
                        operand<2u?lane:operand<4u?3u:operand<6u?0u:operand<10u?1u:2u;
                    if(source==1u || source==2u)channel=light?channel:3u;
                    pixel_mov(p,&count,reg,channel,4u+arg,lane);
                    if(operand&1u)pixel_op(p,&count,0,262,0,4u+arg,lane,true,4u+arg,lane);
                }
            }
            bool dot=v->operation[0]==6u;
            if(dot)pixel_dot3_rgb(p,&count,destination);
            for(unsigned lane=0;lane<4;++lane) {
                unsigned side=lane==3u;
                if(!dot || side)pixel_combine(p,&count,v->operation[side],destination,lane);
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
    if(fog)pixel_fog_mix(p,&count,previous);
    if(blend==BLEND_SOURCE_OVER)pixel_source_over(p,&count,previous);
    else if(blend==BLEND_DESTINATION_SCALE)pixel_destination_scale(p,&count,previous);
    else if(blend==BLEND_ALPHA_ADD) {
        /* Floor src * alpha / 255 before the GPU adds integer destination bytes. */
        for(unsigned lane=0;lane<4u;++lane) {
            pixel_op(p,&count,2,previous,lane,previous,3,false,previous,lane);
            pixel_divide(p,&count,previous,lane);
        }
    }
    for(unsigned lane=0;lane<4;++lane)pixel_op(p,&count,2,previous,lane,262,2,false,previous,lane);
    if(count>PIXEL_MAX_ALUS)return false;
    unsigned cf=0;
    unsigned program_end=PIXEL_ALU_OFFSET+count*8u;
    for(unsigned first=0;first<prefix;first+=128u) {
        unsigned length=prefix-first;if(length>128u)length=128u;
        put32(program+cf,PIXEL_ALU_OFFSET/8u+first);
        put32(program+cf+4u,UINT32_C(0xa0000000)|((length-1u)<<18));cf+=8;
    }
    unsigned fetches=population(mask)+3u*population(filter_mask)+(unsigned)reads_destination;
    if(fetches) {
        unsigned first_fetch=filter_mask?(program_end+15u)&~15u:128u;
        for(unsigned start=0;start<fetches;start+=8u) {
            unsigned length=fetches-start;if(length>8u)length=8u;
            put32(program+cf,first_fetch/8u+start*2u);
            put32(program+cf+4u,UINT32_C(0x80800000)|((length-1u)<<10));cf+=8;
        }
        unsigned fetch=first_fetch;
        for(unsigned unit=0;unit<3u;++unit)if(mask&(1u<<unit)) {
            if(filter_mask&(1u<<unit)) {
                for(unsigned tap=0;tap<4u;++tap) {
                    unsigned sample=FILTER_TAP+unit*4u+tap;
                    put32(program+fetch,0x11u|(unit<<8)|(sample<<16));
                    put32(program+fetch+4u,UINT32_C(0xf00d1000)|sample);
                    put32(program+fetch+8u,UINT32_C(0x70800000)|(unit<<15));fetch+=16u;
                }
                continue;
            }
            unsigned coordinate=unit==2u && texture2_coord1?1u:unit;
            unsigned input=coordinate_gpr(coordinate,coordinates);
            /* SAMPLE_LZ forces level zero. Mip and mixed-filter samplers
             * use SAMPLE so GX2 derives the LOD from the fragment quad. */
            unsigned op=implicit_lod&(1u<<unit)?0x10u:0x13u;
            bool mip=explicit_lod&(1u<<unit);
            if(mip) { op=0x11u;input=MIP_COORDINATE+unit; }
            put32(program+fetch,op|(unit<<8)|(input<<16));
            put32(program+fetch+4u,UINT32_C(0xf00d1000)|texture_gpr(unit,multiple));
            put32(program+fetch+8u,(mip?UINT32_C(0x70800000):UINT32_C(0xb0800000))|(unit<<15));fetch+=16u;
        }
        if(reads_destination) {
            put32(program+fetch,0x13u|(3u<<8)|(BLEND_UV<<16));
            put32(program+fetch+4u,UINT32_C(0xf00d1000)|BLEND_DESTINATION);
            put32(program+fetch+8u,UINT32_C(0xb0800000)|(3u<<15));fetch+=16u;
        }
        if(filter_mask)program_end=fetch;
    }
    for(unsigned first=prefix;first<count;first+=128u) {
        unsigned length=count-first;if(length>128u)length=128u;
        put32(program+cf,PIXEL_ALU_OFFSET/8u+first);
        put32(program+cf+4u,UINT32_C(0xa0000000)|((length-1u)<<18));cf+=8;
    }
    if(cf>(depth_plane?112u:120u))return false;
    if(depth_plane) {
        /* Pixel export 61 writes depth from X. Mask the other lanes and
         * leave the following color export as the end of the program. */
        put32(program+cf,61u|(DEPTH_PLANE<<15));
        put32(program+cf+4u,UINT32_C(0x93800ff8));cf+=8;
    }
    put32(program+cf,previous<<15);put32(program+cf+4u,UINT32_C(0x94200688));
    memset(&p->pixel,0,sizeof(p->pixel));
    p->pixel.size=(program_end+255u)&~255u;
    p->pixel.program=program;p->pixel.mode=GX2_SHADER_MODE_UNIFORM_REGISTER;
    bool fragment_position=fog || depth_plane || reads_destination;
    unsigned interpolators=position+(unsigned)fragment_position;
    p->pixel.regs.sq_pgm_resources_ps=(fog || light)?LUT_LANES+1u:multiple?15u:p->buffered?11u:9u;
    if(depth_plane)p->pixel.regs.sq_pgm_resources_ps=DEPTH_PLANE+1u;
    if(reads_destination)p->pixel.regs.sq_pgm_resources_ps=BLEND_FACTOR+1u;
    if(explicit_lod)p->pixel.regs.sq_pgm_resources_ps=MIP_DY+3u;
    if(depth_plane)p->pixel.regs.sq_pgm_resources_ps=DEPTH_SUM+1u;
    if(filter_mask)p->pixel.regs.sq_pgm_resources_ps=FILTER_TAP+12u;
    p->pixel.regs.sq_pgm_exports_ps=depth_plane?3u:2u;
    p->pixel.regs.spi_ps_in_control_0=UINT32_C(0x14000000)|interpolators;
    if(fragment_position)p->pixel.regs.spi_ps_in_control_0|=(1u<<8)|(position<<10);
    p->pixel.regs.num_spi_ps_input_cntl=interpolators;
    p->pixel.regs.spi_ps_input_cntls[0]=UINT32_C(0x100);
    unsigned slot=1u;
    for(unsigned coordinate=0;coordinate<3u;++coordinate)if(coordinates&(1u<<coordinate))
        p->pixel.regs.spi_ps_input_cntls[slot++]=UINT32_C(0x101)+coordinate;
    if(light) {
        p->pixel.regs.spi_ps_input_cntls[slot++]=UINT32_C(0x104);
        if(light->needs_view)p->pixel.regs.spi_ps_input_cntls[slot++]=UINT32_C(0x105);
    }
    if(depth_plane) {
        /* The coefficients and their origin are flat semantics 6 and 7. */
        p->pixel.regs.spi_ps_input_cntls[slot++]=UINT32_C(0x506);
        p->pixel.regs.spi_ps_input_cntls[slot++]=UINT32_C(0x507);
    }
    for(unsigned coordinate=0;coordinate<3u;++coordinate)if(mip_coordinates&(1u<<coordinate))
        p->pixel.regs.spi_ps_input_cntls[slot++]=UINT32_C(0x508)+coordinate;
    if(explicit_lod)p->pixel.regs.spi_ps_input_cntls[slot++]=UINT32_C(0x50b);
    if(fragment_position)p->pixel.regs.spi_ps_input_cntls[position]=UINT32_C(0x100)+
        (explicit_lod?12u:depth_plane?8u:light?6u:4u);
    p->pixel.regs.cb_shader_mask=15;p->pixel.regs.cb_shader_control=1;
    /* DB_Z_ORDER bits4..5:0 is LATE_Z, 1 EARLY_Z_THEN_LATE_Z. Alpha test
     * precedes PICA depth updates; HiZ and early depth writes stay disabled.
     * Source: Decaf e6c528a latte_registers_db.h / latte_enum_db.h. */
    p->pixel.regs.db_shader_control=p->depth_enabled?(unsigned)depth_plane:UINT32_C(0x10);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER,program,p->pixel.size);return true;
}
static bool vertex_program(ctr_wiiu_gx2_scene *s,scene_private *p,
                            const ctr_scene_state *state,unsigned coordinates) {
    uint8_t key[CTR_GX2_CACHE_VERTEX_KEY_MAX];size_t key_size=0,record_size=0;
    bool enabled=!s->cache_disabled && (s->program_cache || s->direct_hardware);
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
void ctr_gx2_transformed_program(ctr_gx2_shader *program,unsigned coordinates,const ctr_scene_light *light,
                                bool depth_plane,unsigned mip_coordinates) {
    memset(program->program,0,TRANSFORMED_PROGRAM_BYTES);
    /* CALL_FS, then export its already transformed position, primary color
     * and only the UV pairs that have active consumers. */
    put32(program->program+4u,UINT32_C(0x09800000));
    put32(program->program+8u,60u|(1u<<13)|(1u<<15));
    put32(program->program+12u,UINT32_C(0x94000688));
    unsigned varyings=1u|(coordinates<<1)|(light?(16u|(light->needs_view?32u:0u)):0u)|(depth_plane?192u:0u);
    if(mip_coordinates)varyings|=(mip_coordinates<<8)|(1u<<11);
    unsigned slot=0;
    for(unsigned varying=0;varyings;++varying)if(varyings&(1u<<varying)) {
        varyings&=~(1u<<varying);
        unsigned word=varyings?UINT32_C(0x13800000):UINT32_C(0x14200000);
        word|=!mip_coordinates && varying && varying<4u?UINT32_C(0xb08):UINT32_C(0x688);
        put32(program->program+16u+slot*8u,slot|(2u<<13)|((2u+varying)<<15));
        put32(program->program+20u+slot++*8u,word);
    }
    program->size=TRANSFORMED_PROGRAM_BYTES;program->gprs=17u;program->pica_instructions=0;
    program->texcoord_mask=(uint8_t)coordinates;
}
static bool pixel_program(ctr_wiiu_gx2_scene *s,scene_private *p,
                           const ctr_tev_program *tev,unsigned mask,bool texture2_coord1,unsigned implicit_lod,
                           unsigned explicit_lod,unsigned filter_mask,unsigned mixed_filters,const ctr_scene_sampler sources[3],
                           const ctr_scene_state *state,bool fog,bool transformed,const ctr_scene_light *light,
                           bool depth_plane,pixel_blend_mode blend) {
    bool reads_destination=blend_reads_destination(blend);
    uint8_t key[CTR_GX2_CACHE_PIXEL_KEY_MAX];size_t key_size=0,record_size=0;
    pixel_cache_record record;
    /* Constants and buffered selection always come from the current decoded
     * registers, even when their immutable instruction topology is cached. */
    pixel_constants(p,tev);
    if(reads_destination) {
        p->pixel_constants[BLEND_UV_SCALE][0]=bits(1.0f/(float)p->width);
        p->pixel_constants[BLEND_UV_SCALE][1]=bits(1.0f/(float)p->height);
        p->pixel_uniform_vectors=BLEND_UV_SCALE+1u;
    }
    if(fog && !fog_constants(p,state))return false;
    if(light && !lighting_constants(p,light,mask))return false;
    for(unsigned unit=0;unit<3u;++unit)if(explicit_lod&(1u<<unit)) {
        uint32_t *constants=p->pixel_constants[MIP_DIMENSIONS+unit];
        constants[0]=bits((float)sources[unit].levels[0].width);
        constants[1]=bits((float)sources[unit].levels[0].height);
        constants[2]=bits((float)sources[unit].bias_q8/256.0f+0.5f);
        constants[3]=bits(FLT_MIN);
        p->pixel_uniform_vectors=MIP_DIMENSIONS+unit+1u;
    }
    bool enabled=!s->cache_disabled && (s->program_cache || s->direct_hardware);
    if(filter_mask) {
        p->pixel_constants[FILTER_SCALE][0]=bits(65536.0f);
        p->pixel_constants[FILTER_SCALE][1]=bits(1.0f/65536.0f);
        for(unsigned unit=0;unit<3u;++unit)if(filter_mask&(1u<<unit)) {
            uint32_t *bounds=p->pixel_constants[FILTER_LEVELS+unit];
            bounds[0]=bits((float)sources[unit].min_level);
            bounds[1]=bits((float)sources[unit].max_level);
            unsigned filters=sources[unit].levels[0].params;
            bounds[2]=bits(filters&2u?1.0f:0.0f);
            bounds[3]=bits(filters&4u?1.0f:0.0f);
            p->pixel_uniform_vectors=FILTER_LEVELS+unit+1u;
        }
    }
    if(blend==BLEND_DESTINATION_SCALE) {
        for(unsigned lane=0;lane<3u;++lane)
            p->pixel_constants[BLEND_CONSTANT_SCALE][lane]=
                bits(255.0f-(float)((state->registers[0x103u]>>(lane*8u))&255u));
        p->pixel_uniform_vectors=BLEND_CONSTANT_SCALE+1u;
    }
    bool keyed=enabled && ctr_gx2_cache_pixel_key(tev,mask,texture2_coord1,p->depth_enabled,key,&key_size);
    /* All code-shaping lighting controls are part of identity. Light vectors,
     * colors, LOD bounds and table values remain draw constants. */
    uint8_t variant[8]={(uint8_t)(implicit_lod|(fog?8u:0u)|(transformed?16u:0u)|(depth_plane?32u:0u)|
                                  (reads_destination?64u:0u)),(uint8_t)explicit_lod,(uint8_t)(filter_mask|((unsigned)blend<<3)|(mixed_filters<<5))};
    if(light) {
        variant[3]=(uint8_t)(light->flags|16u|(light->needs_view?32u:0u)|(light->clamp_highlights?64u:0u));
        variant[4]=(uint8_t)(light->bump_unit|(light->alpha<<2)|(light->reconstruct_normal?16u:0u));
        for(unsigned i=0;i<3u;++i)
            variant[5u+i]=light->table[i]?(uint8_t)(1u+light->input[i]+(light->absolute[i]?8u:0u)):0u;
    }
    size_t variant_size=light?sizeof(variant):3u;
    if(keyed && variant_size<=sizeof(key)-key_size) {
        memcpy(key+key_size,variant,variant_size);key_size+=variant_size;
    } else keyed=false;
    if(s->direct_hardware && s->direct_pool && keyed)
        return ctr_gx2_direct_pixel_program(s,p,tev,mask,texture2_coord1,implicit_lod,explicit_lod,
                                            filter_mask,mixed_filters,fog,transformed,light,depth_plane,
                                            blend,key,key_size);
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
    if(!pixel_shader(p,tev,mask,texture2_coord1,implicit_lod,explicit_lod,filter_mask,mixed_filters,fog,transformed,light,depth_plane,blend)) {
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
static bool texture_source_span_valid(const ctr_scene_texture *source) {
    return source->rgba && source->width>=8u && source->height>=8u &&
        source->width<=1024u && source->height<=1024u &&
        !(source->width&(source->width-1u)) && !(source->height&(source->height-1u)) &&
        (size_t)source->width*source->height*4u<=UINTPTR_MAX-(uintptr_t)source->rgba;
}
static bool texture_source_valid(const ctr_scene_texture *source,bool mipmaps) {
    unsigned wrap_t=(source->params>>8)&7u,wrap_s=(source->params>>12)&7u;
    unsigned filter=source->params&6u;
    uint32_t expected_params=(wrap_t<<8)|(wrap_s<<12)|(source->format==12u?0x20u:0u)|filter;
    if(mipmaps)expected_params|=source->params&UINT32_C(0x01000000);
    uint32_t params=source->format==13u?source->params&~UINT32_C(0x20):source->params;
    return texture_source_span_valid(source) &&
        source->format<14u && params==expected_params &&
        (mipmaps || filter==0u || filter==6u) &&
        (wrap_t==0u || wrap_t==2u || wrap_t==3u) &&
        (wrap_s==0u || wrap_s==2u || wrap_s==3u);
}
static bool overlaps(const void *a,size_t a_size,const void *b,size_t b_size) {
    uintptr_t at=(uintptr_t)a,bt=(uintptr_t)b;
    return a_size && b_size && (at<=bt?bt-at<a_size:at-bt<b_size);
}
static bool texture_source_aliases_cache(const ctr_wiiu_gx2_scene *s,const ctr_scene_texture *source) {
    const ctr_wiiu_gx2_texture_cache *cache=s->texture_cache;
    size_t bytes=(size_t)source->width*source->height*4u;
    if(overlaps(source->rgba,bytes,s,sizeof(*s)))return true;
    if(!cache)return false;
    size_t cache_bytes=sizeof(*cache)+cache->slots*sizeof(cache->entries[0]);
    if(overlaps(source->rgba,bytes,cache,cache_bytes))return true;
    for(unsigned i=0;i<cache->slots;++i) {
        const texture_image *image=&cache->entries[i].upload;
        if(image->texture.surface.image && overlaps(source->rgba,bytes,image->texture.surface.image,image->bytes))return true;
    }
    return false;
}
static inline bool texture_owned_overlap(const void *rgba,size_t bytes,const void *owned,size_t size) {
    return owned && overlaps(rgba,bytes,owned,size);
}
static bool texture_source_aliases_upload(const scene_private *p,const ctr_scene_texture *source) {
    size_t bytes=(size_t)source->width*source->height*4u;
    const void *rgba=source->rgba;
    if(texture_owned_overlap(rgba,bytes,p,sizeof(*p)) ||
       texture_owned_overlap(rgba,bytes,p->context,sizeof(*p->context)) ||
       texture_owned_overlap(rgba,bytes,p->compiled,sizeof(*p->compiled)) ||
       texture_owned_overlap(rgba,bytes,p->vertex_storage,sizeof(p->compiled->program)*p->shader_slots) ||
       texture_owned_overlap(rgba,bytes,p->pixel_storage,PIXEL_BYTES*p->shader_slots) ||
       texture_owned_overlap(rgba,bytes,p->fetch_program,p->fetch_size) ||
       texture_owned_overlap(rgba,bytes,p->vertices,VERTEX_UPLOAD_BYTES) ||
       texture_owned_overlap(rgba,bytes,p->draw_uploads,DRAW_UPLOAD_SLOTS*DRAW_UPLOAD_BYTES) ||
       texture_owned_overlap(rgba,bytes,p->target.surface.image,p->target.surface.imageSize) ||
       texture_owned_overlap(rgba,bytes,p->linear.image,p->linear.imageSize) ||
       texture_owned_overlap(rgba,bytes,p->depth_target.surface.image,p->depth_target.surface.imageSize) ||
       texture_owned_overlap(rgba,bytes,p->depth_linear.image,p->depth_linear.imageSize) ||
       texture_owned_overlap(rgba,bytes,p->depth_resolve.image,p->depth_resolve.imageSize) ||
       texture_owned_overlap(rgba,bytes,p->blend_snapshot.surface.image,p->blend_snapshot.surface.imageSize) ||
       texture_owned_overlap(rgba,bytes,p->stencil_target.surface.image,p->stencil_target.surface.imageSize) ||
       texture_owned_overlap(rgba,bytes,p->stencil_linear.image,p->stencil_linear.imageSize))return true;
    for(unsigned unit=0;unit<3u;++unit) {
        const texture_image *image=&p->texture[unit];
        if(image->texture.surface.image && overlaps(source->rgba,bytes,image->texture.surface.image,image->bytes))return true;
    }
    return false;
}
static bool texture_chain_valid(const ctr_wiiu_gx2_scene *s,const scene_private *p,
                                const ctr_scene_sampler *source,bool mipmaps,bool aliases_only) {
    if(source->max_level>=CTR_SCENE_MAX_MIP_LEVELS || source->min_level>source->max_level ||
       source->bias_q8<-4096 || source->bias_q8>4095 ||
       (!mipmaps && (source->max_level || source->bias_q8)))return false;
    const ctr_scene_texture *base=&source->levels[0];
    for(unsigned i=0;i<=source->max_level;++i) {
        const ctr_scene_texture *level=&source->levels[i];
        if(aliases_only && !texture_source_span_valid(level))continue;
        if((!aliases_only && (!texture_source_valid(level,mipmaps) || level->width!=(base->width>>i) ||
            level->height!=(base->height>>i) || level->format!=base->format || level->params!=base->params)) ||
           texture_source_aliases_cache(s,level) || texture_source_aliases_upload(p,level))return false;
    }
    return true;
}
static size_t texture_mip_offset(const GX2Surface *surface,unsigned level) {
    return level>1u?surface->mipLevelOffset[level-1u]:0u;
}
static bool texture_layout(texture_image *image,const ctr_scene_sampler *source) {
    memset(image,0,sizeof(*image));
    GX2Surface *surface=&image->texture.surface;
    surface->dim=GX2_SURFACE_DIM_TEXTURE_2D;
    surface->width=source->levels[0].width;surface->height=source->levels[0].height;
    surface->depth=1;surface->mipLevels=source->max_level+1u;
    surface->format=GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
    surface->aa=GX2_AA_MODE1X;surface->use=GX2_SURFACE_USE_TEXTURE;
    surface->tileMode=GX2_TILE_MODE_LINEAR_ALIGNED;
    GX2CalcSurfaceSizeAndAlignment(surface);
    if(surface->tileMode!=GX2_TILE_MODE_LINEAR_ALIGNED || surface->mipLevels!=source->max_level+1u ||
       !surface->imageSize || surface->imageSize>MAX_TEXTURE_BYTES || !surface->alignment ||
       (surface->alignment&(surface->alignment-1u)) || surface->alignment>65536u ||
       surface->pitch<surface->width || (uint64_t)surface->pitch*surface->height*4u>surface->imageSize)
        return false;
    image->pitch[0]=surface->pitch;image->bytes=surface->imageSize;
    if(!source->max_level)return !surface->mipmapSize;
    size_t tail=surface->mipLevelOffset[0];
    if(tail<surface->imageSize || (tail&(surface->alignment-1u)) || tail>MAX_TEXTURE_BYTES ||
       !surface->mipmapSize || surface->mipmapSize>MAX_TEXTURE_BYTES-tail)return false;
    image->bytes=tail+surface->mipmapSize;
    for(unsigned i=1;i<=source->max_level;++i) {
        size_t start=texture_mip_offset(surface,i);
        size_t end=i==source->max_level?surface->mipmapSize:texture_mip_offset(surface,i+1u);
        unsigned pitch=GX2GetSurfaceMipPitch(surface,i);
        if(end<=start || end>surface->mipmapSize || pitch<source->levels[i].width ||
           (uint64_t)pitch*source->levels[i].height*4u>end-start)return false;
        image->pitch[i]=pitch;
    }
    return true;
}
static void texture_regs(GX2Texture *texture) {
    texture->viewNumMips=texture->surface.mipLevels;texture->viewNumSlices=1;
    texture->compMap=GX2_COMP_MAP(GX2_SQ_SEL_X,GX2_SQ_SEL_Y,GX2_SQ_SEL_Z,GX2_SQ_SEL_W);
    GX2InitTextureRegs(texture);
}
static bool texture_allocate(ctr_wiiu_gx2_scene *s,texture_image *image) {
    GX2Surface *surface=&image->texture.surface;
    surface->image=allocate(s,surface->alignment,image->bytes);
    if(!surface->image)return false;
    if(surface->mipLevels>1u)surface->mipmaps=(uint8_t *)surface->image+surface->mipLevelOffset[0];
    texture_regs(&image->texture);
    return true;
}
static uint8_t *texture_pixels(const texture_image *image,unsigned level) {
    const GX2Surface *surface=&image->texture.surface;
    if(!level)return surface->image;
    return (uint8_t *)surface->mipmaps+texture_mip_offset(surface,level);
}
static void texture_upload(ctr_wiiu_gx2_scene *s,texture_image *image,const ctr_scene_sampler *source) {
    /* PICA normalized V=0 addresses the last raw Morton-storage row. Reverse
     * decoded rows while preserving guest UV. GPU sampling never writes here. */
    for(unsigned i=0;i<=source->max_level;++i) {
        const ctr_scene_texture *level=&source->levels[i];
        uint8_t *destination=texture_pixels(image,i);
        for(unsigned y=0;y<level->height;++y)
            memcpy(destination+(size_t)y*image->pitch[i]*4u,
                   level->rgba+(size_t)(level->height-1u-y)*level->width*4u,level->width*4u);
        s->texture_bytes+=(uint64_t)level->width*level->height*4u;
    }
    DCFlushRange(image->texture.surface.image,(unsigned)image->bytes);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE,image->texture.surface.image,(unsigned)image->bytes);
    ++s->texture_uploads;
}
static void texture_sampler(GX2Sampler *sampler,const ctr_scene_sampler *source) {
    uint32_t params=source->levels[0].params;
    unsigned wrap_t=(params>>8)&7u,wrap_s=(params>>12)&7u;
    GX2TexClampMode clamps[4]={GX2_TEX_CLAMP_MODE_CLAMP,GX2_TEX_CLAMP_MODE_CLAMP,
                             GX2_TEX_CLAMP_MODE_WRAP,GX2_TEX_CLAMP_MODE_MIRROR};
    GX2TexXYFilterMode mag=params&2u?GX2_TEX_XY_FILTER_MODE_LINEAR:GX2_TEX_XY_FILTER_MODE_POINT;
    GX2TexXYFilterMode min=params&4u?GX2_TEX_XY_FILTER_MODE_LINEAR:GX2_TEX_XY_FILTER_MODE_POINT;
    GX2InitSampler(sampler,GX2_TEX_CLAMP_MODE_CLAMP,mag);
    if(min!=mag)GX2InitSamplerXYFilter(sampler,mag,min,GX2_TEX_ANISO_RATIO_NONE);
    GX2InitSamplerClamping(sampler,clamps[wrap_s],clamps[wrap_t],GX2_TEX_CLAMP_MODE_CLAMP);
    GX2InitSamplerLOD(sampler,(float)source->min_level,(float)source->max_level,(float)source->bias_q8/256.0f);
    GX2TexMipFilterMode mip=GX2_TEX_MIP_FILTER_MODE_NONE;
    if(source->max_level)
        mip=params&UINT32_C(0x01000000)?GX2_TEX_MIP_FILTER_MODE_LINEAR:GX2_TEX_MIP_FILTER_MODE_POINT;
    GX2InitSamplerZMFilter(sampler,GX2_TEX_Z_FILTER_MODE_NONE,mip);
}
static bool upload_texture(ctr_wiiu_gx2_scene *s,scene_private *p,
                           const ctr_scene_sampler *source,unsigned unit,bool mipmaps) {
    texture_image *image=&p->texture[unit];
    const GX2Surface *surface=&image->texture.surface;
    /* The disabled/bypass route preserves transaction-owned uploads. */
    if(!texture_chain_valid(s,p,source,mipmaps,false))return fail(s,CTR_WIIU_GX2_SCENE_ARGUMENT);
    if(!retire(s,p))return false;
    if(surface->width!=source->levels[0].width || surface->height!=source->levels[0].height ||
       surface->mipLevels!=source->max_level+1u) {
        texture_image next;
        if(!texture_layout(&next,source))return fail(s,CTR_WIIU_GX2_SCENE_LAYOUT);
        size_t total=next.bytes;
        for(unsigned other=0;other<3u;++other)if(other!=unit)total+=p->texture[other].bytes;
        if(total>MAX_TEXTURE_TOTAL_BYTES)return fail(s,CTR_WIIU_GX2_SCENE_LAYOUT);
        if(!texture_allocate(s,&next))return fail(s,CTR_WIIU_GX2_SCENE_ALLOCATION);
        dispose(s,image->texture.surface.image);*image=next;
    }
    texture_upload(s,image,source);texture_sampler(&p->sampler[unit],source);return true;
}
static bool texture_matches(const texture_image *image,const ctr_scene_sampler *source) {
    const GX2Surface *surface=&image->texture.surface;
    if(!surface->image || surface->width!=source->levels[0].width || surface->height!=source->levels[0].height ||
       surface->mipLevels!=source->max_level+1u)return false;
    /* Identity includes every decoded row of every level in the upload. */
    for(unsigned i=0;i<=source->max_level;++i) {
        const ctr_scene_texture *level=&source->levels[i];
        const uint8_t *pixels=texture_pixels(image,i);
        for(unsigned y=0;y<level->height;++y)
            if(memcmp(pixels+(size_t)y*image->pitch[i]*4u,
                      level->rgba+(size_t)(level->height-1u-y)*level->width*4u,level->width*4u))return false;
    }
    return true;
}
static uint32_t texture_sampler_hash(const ctr_scene_sampler *source) {
    uint32_t h=UINT32_C(2166136261)^source->levels[0].width^
        (source->levels[0].height<<16)^source->max_level;
    /* The hash only filters candidates. A hit compares every mip byte. */
    for(unsigned i=0;i<=source->max_level;++i) {
        const ctr_scene_texture *lv=&source->levels[i];
        size_t pixels=(size_t)lv->width*lv->height;
        size_t step=(pixels+15u)/16u;
        for(size_t k=0;k<pixels;k+=step) {
            uint32_t w;memcpy(&w,lv->rgba+k*4u,sizeof(w));
            h=(h^w)*UINT32_C(16777619);
        }
    }
    return h;
}
static texture_cache_entry *texture_find(ctr_wiiu_gx2_texture_cache *cache,
                                         const ctr_scene_sampler *source,uint32_t h) {
    for(unsigned i=0;i<cache->slots;++i) {
        texture_cache_entry *entry=&cache->entries[i];
        const GX2Surface *surface=&entry->upload.texture.surface;
        if(!surface->image || entry->hash!=h ||
           surface->width!=source->levels[0].width || surface->height!=source->levels[0].height ||
           surface->mipLevels!=source->max_level+1u)continue;
        if(texture_matches(&entry->upload,source))return entry;
    }
    return NULL;
}
static void texture_pin(ctr_wiiu_gx2_scene *s,scene_private *p,unsigned unit,texture_cache_entry *entry) {
    ++entry->pins;
    ++s->texture_cache_stats.pins;
    p->texture_lease[unit]=entry;
}
static texture_cache_entry *texture_slot(ctr_wiiu_gx2_scene *s,size_t bytes) {
    ctr_wiiu_gx2_texture_cache *cache=s->texture_cache;texture_cache_entry *slot=NULL;
    for(unsigned attempt=0;attempt<cache->slots;++attempt) {
        texture_cache_entry *entry=&cache->entries[cache->next];
        cache->next=(cache->next+1u)%cache->slots;
        if(entry->pins || entry->submitted)continue;
        if(entry->upload.texture.surface.image) {
            /* Cemu can reuse the old image of an address within the same
             * video frame, even when its GPU command has retired. */
            if(s->cemu_readback && !s->direct_hardware)continue;
            s->texture_cache_stats.bytes-=entry->upload.bytes;
            dispose(s,entry->upload.texture.surface.image);memset(entry,0,sizeof(*entry));
            texture_cache_add(s,&s->texture_cache_stats.evictions,1u);
        }
        if(!slot)slot=entry;
        if(s->texture_cache_stats.bytes+bytes<=MAX_TEXTURE_TOTAL_BYTES)return slot;
    }
    return NULL;
}
static bool texture_has_identities(const ctr_scene_sampler *source,const ctr_texture_cache_lease *levels) {
    if(!levels)return false;
    for(unsigned i=0;i<=source->max_level;++i)
        if(!levels[i].identity || levels[i].rgba!=source->levels[i].rgba)return false;
    return true;
}
static texture_cache_entry **texture_identity_slot(ctr_wiiu_gx2_texture_cache *cache,uint32_t identity) {
    return &cache->identity_index[identity&(GX2_TEXTURE_IDENTITY_SLOTS-1u)];
}
static texture_cache_entry *texture_find_identity(ctr_wiiu_gx2_texture_cache *cache,
                                                  const ctr_scene_sampler *source,
                                                  const ctr_texture_cache_lease *levels) {
    texture_cache_entry *entry=*texture_identity_slot(cache,levels[0].identity);
    if(!entry)return NULL;
    const GX2Surface *surface=&entry->upload.texture.surface;
    if(surface->width!=source->levels[0].width || surface->height!=source->levels[0].height ||
       surface->mipLevels!=source->max_level+1u)return NULL;
    for(unsigned i=0;i<=source->max_level;++i)
        if(entry->identities[i]!=levels[i].identity)return NULL;
    return entry;
}
static void texture_record_identity(ctr_wiiu_gx2_texture_cache *cache,texture_cache_entry *entry,
                                    const ctr_scene_sampler *source,const ctr_texture_cache_lease *levels) {
    for(unsigned i=0;i<=source->max_level;++i)entry->identities[i]=levels[i].identity;
    *texture_identity_slot(cache,levels[0].identity)=entry;
}
static bool upload_textures(ctr_wiiu_gx2_scene *s,scene_private *p,
                            const ctr_scene_sampler sources[3],unsigned mask,bool mipmaps,
                            const ctr_texture_cache_lease (*leases)[CTR_SCENE_MAX_MIP_LEVELS]) {
    if(!mask)return true;
    if(s->cemu_readback && s->texture_cache_disabled)return fail(s,CTR_WIIU_GX2_SCENE_ARGUMENT);
    if(s->texture_cache_disabled)texture_cache_destroy(s);
    else if(!s->texture_cache) {
        unsigned slots=s->direct_hardware?DIRECT_TEXTURE_SLOTS:
            s->cemu_readback?CEMU_TEXTURE_SLOTS:CTR_WIIU_GX2_TEXTURE_CACHE_SLOTS;
        s->texture_cache=allocate(s,256,sizeof(*s->texture_cache)+slots*sizeof(texture_cache_entry));
        if(s->texture_cache)s->texture_cache->slots=slots;
        else texture_cache_add(s,&s->texture_cache_stats.allocation_failures,1u);
    }
    if(!s->texture_cache) {
        if(s->cemu_readback)return fail(s,CTR_WIIU_GX2_SCENE_ALLOCATION);
        for(unsigned unit=0;unit<3u;++unit)if(mask&(1u<<unit)) {
            texture_cache_add(s,&s->texture_cache_stats.bypasses,1u);
            if(!upload_texture(s,p,&sources[unit],unit,mipmaps))return false;
        }
        return true;
    }
    /* An earlier owner-allocation bypass may have left transaction images.
     * They are retired and no longer needed once the retained owner exists. */
    for(unsigned unit=0;unit<3u;++unit) {
        dispose(s,p->texture[unit].texture.surface.image);memset(&p->texture[unit],0,sizeof(p->texture[unit]));
    }
    uint32_t hashes[3]={0};
    bool known[3]={false};
    /* Protect all current-draw resident hits before any miss can evict one,
     * including hits on units later than the first missing descriptor. */
    for(unsigned unit=0;unit<3u;++unit)if(mask&(1u<<unit)) {
        known[unit]=leases && texture_has_identities(&sources[unit],leases[unit]);
        texture_cache_entry *entry=known[unit]?texture_find_identity(s->texture_cache,&sources[unit],leases[unit]):NULL;
        if(!entry) {
            hashes[unit]=texture_sampler_hash(&sources[unit]);
            entry=texture_find(s->texture_cache,&sources[unit],hashes[unit]);
            if(entry && known[unit])texture_record_identity(s->texture_cache,entry,&sources[unit],leases[unit]);
        }
        if(entry) {
            texture_cache_add(s,&s->texture_cache_stats.lookups,1u);
            texture_cache_add(s,&s->texture_cache_stats.hits,1u);texture_pin(s,p,unit,entry);
        }
    }
    for(unsigned unit=0;unit<3u;++unit)if(mask&(1u<<unit)) {
        if(!p->texture_lease[unit]) {
            /* A preceding new unit can have uploaded the same decoded image. */
            texture_cache_add(s,&s->texture_cache_stats.lookups,1u);
            texture_cache_entry *entry=texture_find(s->texture_cache,&sources[unit],hashes[unit]);
            if(entry)texture_cache_add(s,&s->texture_cache_stats.hits,1u);
            else {
                texture_cache_add(s,&s->texture_cache_stats.misses,1u);
                texture_image next;
                if(!texture_layout(&next,&sources[unit]))return fail(s,CTR_WIIU_GX2_SCENE_LAYOUT);
                entry=texture_slot(s,next.bytes);
                if(!entry && (!s->cemu_readback || s->direct_hardware) && p->used_uploads) {
                    if(!retire(s,p))return false;
                    entry=texture_slot(s,next.bytes);
                }
                if(!entry)return fail(s,CTR_WIIU_GX2_SCENE_LAYOUT);
                if(!texture_allocate(s,&next)) {
                    texture_cache_add(s,&s->texture_cache_stats.allocation_failures,1u);
                    return fail(s,CTR_WIIU_GX2_SCENE_ALLOCATION);
                }
                texture_upload(s,&next,&sources[unit]);
                entry->upload=next;entry->hash=hashes[unit];
                s->texture_cache_stats.bytes+=next.bytes;
                if(s->texture_cache_stats.bytes>s->texture_cache_stats.peak_bytes)
                    s->texture_cache_stats.peak_bytes=s->texture_cache_stats.bytes;
            }
            if(known[unit])texture_record_identity(s->texture_cache,entry,&sources[unit],leases[unit]);
            texture_pin(s,p,unit,entry);
        }
        texture_sampler(&p->sampler[unit],&sources[unit]);
    }
    return true;
}
static void bind_draw_textures(ctr_wiiu_gx2_scene *s,scene_private *p,unsigned mask) {
    bool reuse=s->direct_hardware && s->cemu_readback;
    /* A new upload can replace a cache entry without changing its address. */
    if(p->bound_texture_uploads!=s->texture_uploads) {
        p->bound_texture_mask=0;
        p->bound_texture_uploads=s->texture_uploads;
    }
    for(unsigned unit=0;unit<3u;++unit)if(mask&(1u<<unit)) {
        unsigned bit=1u<<unit;
        const GX2Texture *texture=p->texture_lease[unit]?
            &p->texture_lease[unit]->upload.texture:&p->texture[unit].texture;
        const GX2Sampler *sampler=&p->sampler[unit];
        if(!reuse || !(p->bound_texture_mask&bit) || p->bound_textures[unit]!=texture) {
            GX2SetPixelTexture(texture,unit);
            if(reuse) { p->bound_textures[unit]=texture;p->bound_texture_mask|=bit; }
        }
        if(!reuse || !(p->bound_sampler_mask&bit) || memcmp(&p->bound_samplers[unit],sampler,sizeof(*sampler))) {
            GX2SetPixelSampler(sampler,unit);
            if(reuse) {
                memcpy(&p->bound_samplers[unit],sampler,sizeof(*sampler));
                p->bound_sampler_mask|=bit;
            }
        }
    }
}
bool ctr_gx2_depth_clear_seed(const ctr_scene_depth_surface *depth,float *value,uint8_t *stencil) {
    const uint8_t *seed=depth->bytes;
    unsigned bytes=depth->format==CTR_SCENE_DEPTH_D24S8?4u:2u;
    unsigned components=bytes==4u?3u:2u;
    if(seed[0]!=0u && seed[0]!=255u)return false;
    for(unsigned i=1;i<components;++i)if(seed[i]!=seed[0])return false;
    size_t row_bytes=(size_t)depth->width*bytes;
    for(unsigned y=0;y<depth->height;++y) {
        const uint8_t *row=seed+(size_t)y*depth->stride;
        if(memcmp(row,seed,bytes) || memcmp(row,row+bytes,row_bytes-bytes))return false;
    }
    *value=seed[0]?1.0f:0.0f;
    *stencil=bytes==4u?seed[3]:0u;
    return true;
}
#define depth_clear_seed ctr_gx2_depth_clear_seed
unsigned ctr_gx2_target_row(const scene_private *p,unsigned y) {
    return p->flip_y?p->height-y-1u:y;
}
#define target_row ctr_gx2_target_row
static bool begin_targets(void *context,const ctr_scene_surface *initial,
                          const ctr_scene_depth_surface *depth,bool resident) {
    ctr_wiiu_gx2_scene *s=context;
    if(!s || !s->restore_context || !dimensions(initial) ||
       (depth && !depth_dimensions(depth,initial)))return fail(s,CTR_WIIU_GX2_SCENE_ARGUMENT);
    if(s->private_state)return fail(s,CTR_WIIU_GX2_SCENE_BUSY);
    if(s->direct_hardware && !resident)
        return ctr_gx2_direct_begin_targets(s,initial,depth,false,false);
    bool clear_depth=depth && s->cemu_readback;
    float clear_value=0.0f;uint8_t clear_stencil=0;
    /* Cemu's depth copy does not reliably initialize the renderer's texture
     * from packed CPU storage. Clear validated uniform endpoint surfaces on
     * GX2. Other seeds still require the native paired-copy route. */
    if(clear_depth && ((!resident && depth->format!=CTR_SCENE_DEPTH_D24S8) ||
                      !depth_clear_seed(depth,&clear_value,&clear_stencil)))
        return fail(s,CTR_WIIU_GX2_SCENE_UNSUPPORTED_READBACK);
    s->result=CTR_WIIU_GX2_SCENE_OK;
    scene_private *p=allocate(s,256,sizeof(*p));
    if(!p)return fail(s,CTR_WIIU_GX2_SCENE_ALLOCATION);
    s->private_state=p;p->width=initial->width;p->height=initial->height;
    p->stride=initial->stride;p->size=initial->size;
    p->flip_y=s->cemu_readback;
    p->depth_enabled=depth!=NULL;p->resident=resident;
    if(depth) {
        p->depth_stride=depth->stride;p->depth_size=depth->size;p->depth_address=depth->address;
        p->depth_format=depth->format;p->depth_bytes=depth->format==3u?4u:2u;
        p->seed_far=clear_value!=0.0f;p->seed_stencil=clear_stencil;
    }
    /* Cemu's tiled-to-linear-special copy synchronizes GPU data with guest
     * memory. An ordinary aligned destination can retain its CPU seed. */
    GX2TileMode staging_tile=s->cemu_readback?GX2_TILE_MODE_LINEAR_SPECIAL:GX2_TILE_MODE_LINEAR_ALIGNED;
    unsigned color_format=GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
    if(!layout(&p->target.surface,p->width,p->height,GX2_TILE_MODE_TILED_2D_THIN1,color_format) ||
       !layout(&p->linear,p->width,p->height,staging_tile,color_format) ||
       (uint64_t)p->linear.pitch*p->height*4u>p->linear.imageSize) {
        s->result=CTR_WIIU_GX2_SCENE_LAYOUT;goto error;
    }
    if(depth && (!depth_layout(&p->depth_target.surface,p->width,p->height,GX2_TILE_MODE_TILED_2D_THIN1,p->depth_format) ||
                 (!clear_depth && !depth_layout(&p->depth_linear,p->width,p->height,GX2_TILE_MODE_LINEAR_ALIGNED,p->depth_format)))) {
        s->result=CTR_WIIU_GX2_SCENE_LAYOUT;goto error;
    }
    p->context=allocate(s,256,sizeof(*p->context));
    p->target.surface.image=allocate(s,p->target.surface.alignment,p->target.surface.imageSize);
    p->linear.image=allocate(s,p->linear.alignment,p->linear.imageSize);
    if(depth) {
        p->depth_target.surface.image=allocate(s,p->depth_target.surface.alignment,p->depth_target.surface.imageSize);
        if(!clear_depth)p->depth_linear.image=allocate(s,p->depth_linear.alignment,p->depth_linear.imageSize);
    }
    p->compiled=allocate(s,256,sizeof(*p->compiled));
    p->shader_slots=s->cemu_readback?2u:1u;
    p->vertex_storage=allocate(s,256,sizeof(p->compiled->program)*p->shader_slots);
    p->pixel_storage=allocate(s,256,(size_t)PIXEL_BYTES*p->shader_slots);
    uint32_t fetch_size=GX2CalcFetchShaderSizeEx(16,GX2_FETCH_SHADER_TESSELLATION_NONE,
                                               GX2_TESSELLATION_MODE_DISCRETE);
    if(!fetch_size || fetch_size>MAX_FETCH_BYTES) {
        s->result=CTR_WIIU_GX2_SCENE_LAYOUT;goto error;
    }
    p->fetch_size=fetch_size;
    p->fetch_program=allocate(s,256,fetch_size);
    p->vertices=allocate(s,256,VERTEX_UPLOAD_BYTES);
    if(!p->context || !p->target.surface.image || !p->linear.image || !p->compiled ||
       !p->vertex_storage || !p->pixel_storage || !p->fetch_program || !p->vertices ||
       (depth && (!p->depth_target.surface.image || (!clear_depth && !p->depth_linear.image)))) {
        s->result=CTR_WIIU_GX2_SCENE_ALLOCATION;goto error;
    }
    for(unsigned y=0;y<p->height;++y)
        memcpy((uint8_t *)p->linear.image+(size_t)y*p->linear.pitch*4u,
               (const uint8_t *)initial->bytes+(size_t)target_row(p,y)*initial->stride,p->width*4u);
    DCFlushRange(p->linear.image,p->linear.imageSize);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE,p->linear.image,p->linear.imageSize);
    /* Allocation clearing dirties PowerPC cache lines. Retire those CPU lines
     * before the GPU first writes this target; a later eviction must not
     * overwrite rendered pixels with the allocator's initial zeros. */
    DCFlushRange(p->target.surface.image,p->target.surface.imageSize);
    if(depth && !clear_depth) {
        /* Raw LE D16/D24S8 remain bytes on big-endian PowerPC. D24S8 is
         * depth in bits0..23 and stencil in bits24..31; DEPTH_8_24 matches
         * pinned R600 Z24_UNORM_S8_UINT packing (Mesa f1f246c). GX2 performs
         * only the pitched-linear/depth-tile conversion, with no byte rotation. */
        for(unsigned y=0;y<p->height;++y)
            memcpy((uint8_t *)p->depth_linear.image+(size_t)y*p->depth_linear.pitch*p->depth_bytes,
                   (const uint8_t *)depth->bytes+(size_t)y*depth->stride,(size_t)p->width*p->depth_bytes);
        DCFlushRange(p->depth_linear.image,p->depth_linear.imageSize);
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE,p->depth_linear.image,p->depth_linear.imageSize);
    }
    if(depth)DCFlushRange(p->depth_target.surface.image,p->depth_target.surface.imageSize);
    GX2SetupContextStateEx(p->context,true);GX2SetContextState(p->context);
    p->target.viewNumSlices=1;GX2InitColorBufferRegs(&p->target);
    GX2CopySurface(&p->linear,0,0,&p->target.surface,0,0);p->pending=true;
    if(depth) {
        p->depth_target.viewNumSlices=1;GX2InitDepthBufferRegs(&p->depth_target);
        GX2InitDepthBufferHiZEnable(&p->depth_target,false);
        if(clear_depth)GX2ClearDepthStencilEx(&p->depth_target,clear_value,clear_stencil,
            p->depth_format==CTR_SCENE_DEPTH_D24S8?GX2_CLEAR_FLAGS_BOTH:GX2_CLEAR_FLAGS_DEPTH);
        else GX2CopySurface(&p->depth_linear,0,0,&p->depth_target.surface,0,0);
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
bool ctr_wiiu_gx2_scene_begin_resident(ctr_wiiu_gx2_scene *s,const ctr_scene_surface *initial,
                                      const ctr_scene_depth_surface *depth) {
    if(!depth)return fail(s,CTR_WIIU_GX2_SCENE_ARGUMENT);
    return begin_targets(s,initial,depth,true);
}
static uint32_t bits(float value) { uint32_t result;memcpy(&result,&value,4);return result; }
static float real(uint32_t value) { float result;memcpy(&result,&value,4);return result; }
static bool read_viewport(const scene_private *p,const ctr_scene_state *state,float half[2],int offset[2]) {
    uint32_t x,y;
    if(!ctr_shader_f24_decode(state->registers[0x041u],&x) ||
       !ctr_shader_f24_decode(state->registers[0x043u],&y))return false;
    half[0]=real(x);half[1]=real(y);
    uint32_t origin=state->registers[0x068u];
    offset[0]=(int)(origin&1023u);offset[1]=(int)((origin>>16)&1023u);
    for(unsigned axis=0;axis<2u;++axis)if(offset[axis]&512)offset[axis]-=1024;
    return half[0]>0 && half[1]>0 && half[0]<=(float)p->width/2 && half[1]<=(float)p->height/2;
}
static void viewport(const scene_private *p,const float half[2],const int offset[2],bool fog) {
    GX2ViewportReg v;
    GX2InitViewportReg(&v,0,0,(float)p->width,(float)p->height,0.0f,1.0f);
    /* PICA maps (-1,-1) to guest row zero. Reverse the Cemu backing rows
     * with the viewport to match PICA ownership of horizontal shared edges. */
    v.pa_cl_vport_xscale=bits(half[0]);v.pa_cl_vport_xoffset=bits(half[0]+(float)offset[0]);
    float base_y=half[1]+(float)offset[1];
    v.pa_cl_vport_yscale=bits(p->flip_y?-half[1]:half[1]);
    v.pa_cl_vport_yoffset=bits(p->flip_y?(float)p->height-base_y:base_y);
    if(p->depth_enabled || fog) {
        /* Canonical Z buffering is depth=-clipZ/W. GX2's convenience 0..1
         * viewport instead uses scale=.5,offset=.5 and cannot be inherited. */
        v.pa_cl_vport_zscale=bits(-1.0f);v.pa_cl_vport_zoffset=bits(0.0f);
    }
    GX2SetViewportReg(&v);
    /* PICA's accepted z interval [-w,0] is inside the full GX2 [-w,+w]
     * interval; retain the original shader Z. */
    GX2SetRasterizerClipControlEx(true,true,false);
}
static void rectangle(ctr_wiiu_gx2_scene *s,unsigned x,unsigned y,unsigned w,unsigned h,
                      unsigned first,unsigned count) {
    if(!w || !h)return;
    scene_private *p=s->private_state;GX2SetScissor(x,p->flip_y?p->height-y-h:y,w,h);
    GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES,count,first,1);p->pending=true;++s->submissions;
    if(p->shader_slots>1u) {
        s->cemu_last_vertex_program=(uintptr_t)p->vertex_program;
        s->cemu_last_pixel_program=(uintptr_t)p->pixel_program;
    }
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
                          ctr_stencil_state *stencil,bool independent_gates) {
    if(!ctr_stencil_decode(state->registers,state->register_defined,
                           p->depth_enabled && p->depth_format==3u,stencil))return false;
    if(!p->depth_enabled)return true;
    static const unsigned full[]={0x04du,0x04eu,0x06du,0x114u,0x115u,0x116u,0x11cu};
    for(unsigned i=0;i<sizeof(full)/sizeof(*full);++i)
        if(state->register_defined[full[i]]!=15u)return false;
    uint32_t read=state->registers[0x114u],write=state->registers[0x115u];
    unsigned gate=p->depth_format==3u?3u:2u;
    bool separate=independent_gates && gate==3u;
    unsigned required=((state->registers[0x107u]&1u)?2u:0u)|(stencil->enabled?1u:0u);
    if(required && !separate)required=gate;
    return state->registers[0x04du]==UINT32_C(0xbf0000) &&
        (state->registers[0x04eu]&~UINT32_C(0x800000))==0u &&
        state->registers[0x06du]==1u && state->registers[0x116u]==p->depth_format &&
        !(state->registers[0x11cu]&UINT32_C(0xf0000000)) &&
        (uint64_t)state->registers[0x11cu]*8u==p->depth_address &&
        (separate?(read<=3u && write<=3u):
                  ((read==0u || read==gate) && (write==0u || write==gate))) &&
        (read&required)==required;
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
    unsigned gate=p->depth_enabled?state->registers[0x115u]:0u;
    bool write=(gate&2u) && (state->registers[0x107u]&0x1000u)!=0u;
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
    uint8_t write_mask=enabled && (gate&1u)?stencil->write_mask:0u;
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
static void select_shader_storage(ctr_wiiu_gx2_scene *s,scene_private *p) {
    p->vertex_program=p->vertex_storage;p->pixel_program=p->pixel_storage;
    if(p->shader_slots>1u) {
        /* Cemu 2.6 remembers the last shader hash by address and size even
         * after shader invalidation. Bind a different address so it hashes
         * the newly written bytes. The previous draw is already retired. */
        if((uintptr_t)p->vertex_program==s->cemu_last_vertex_program)
            p->vertex_program=(uint8_t *)p->vertex_storage+sizeof(p->compiled->program);
        if((uintptr_t)p->pixel_program==s->cemu_last_pixel_program)
            p->pixel_program=(uint8_t *)p->pixel_storage+PIXEL_BYTES;
    }
}
static bool prepare_draw_uploads(ctr_wiiu_gx2_scene *s,scene_private *p) {
    if(p->upload_batch_unavailable)return false;
    if(!p->draw_uploads) {
        p->draw_uploads=allocate(s,256,DRAW_UPLOAD_SLOTS*DRAW_UPLOAD_BYTES);
        p->upload_batch_unavailable=!p->draw_uploads;
    }
    return p->draw_uploads!=NULL;
}
static void *select_draw_upload(ctr_wiiu_gx2_scene *s,scene_private *p) {
    uint8_t *base=p->draw_uploads;
    uint8_t *slot=base+p->next_upload*DRAW_UPLOAD_BYTES;
    if(s->cemu_readback && !p->used_uploads &&
       ((uintptr_t)slot==s->cemu_last_vertex_program ||
        (uintptr_t)(slot+TRANSFORMED_PROGRAM_BYTES)==s->cemu_last_pixel_program)) {
        p->next_upload=(p->next_upload+1u)%DRAW_UPLOAD_SLOTS;
        slot=base+p->next_upload*DRAW_UPLOAD_BYTES;
    }
    p->vertex_program=slot;
    p->pixel_program=slot+TRANSFORMED_PROGRAM_BYTES;
    return slot+TRANSFORMED_PROGRAM_BYTES+PIXEL_BYTES;
}
void ctr_gx2_upload_vertex_program(scene_private *p,const ctr_scene_light *light,bool depth_plane,
                                  unsigned mip_coordinates) {
    memcpy(p->vertex_program,p->compiled->program,p->compiled->size);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER,p->vertex_program,p->compiled->size);
    memset(&p->vertex,0,sizeof(p->vertex));p->vertex.size=p->compiled->size;
    p->vertex.program=p->vertex_program;p->vertex.mode=GX2_SHADER_MODE_UNIFORM_REGISTER;
    p->vertex.regs.sq_pgm_resources_vs=p->compiled->gprs|UINT32_C(0x100);
    unsigned varyings=p->compiled->texcoord_mask|(light?(8u|(light->needs_view?16u:0u)):0u)|(depth_plane?96u:0u);
    if(mip_coordinates)varyings|=(mip_coordinates<<7)|(1u<<10);
    unsigned extra_outputs=population(varyings);
    p->vertex.regs.spi_vs_out_config=extra_outputs<<1;
    p->vertex.regs.num_spi_vs_out_id=extra_outputs/4u+1u;
    for(unsigned index=0;index<10;++index)p->vertex.regs.spi_vs_out_id[index]=UINT32_MAX;
    p->vertex.regs.spi_vs_out_id[0]&=~UINT32_C(255);
    unsigned output=1u;
    for(unsigned semantic=1;semantic<=11u;++semantic)if(varyings&(1u<<(semantic-1u))) {
        unsigned shift=(output%4u)*8u;
        uint32_t *ids=&p->vertex.regs.spi_vs_out_id[output++/4u];
        *ids=(*ids&~(UINT32_C(255)<<shift))|(semantic<<shift);
    }
    p->vertex.regs.sq_vtx_semantic_clear=UINT32_C(0xffff0000);
    p->vertex.regs.num_sq_vtx_semantic=16;
    for(unsigned index=0;index<16;++index)p->vertex.regs.sq_vtx_semantic[index]=index;
    p->vertex.regs.vgt_vertex_reuse_block_cntl=14;p->vertex.regs.vgt_hos_reuse_depth=16;
}
static bool triangle_grid(const ctr_scene_transformed_vertex triangle[3],const float half[2],
                          const int offset[2],double x[3],double y[3],double inverse_w[3]) {
    for(unsigned i=0;i<3u;++i) {
        const float *position=triangle[i].position;
        if(!isfinite(position[0]) || !isfinite(position[1]) ||
           !isfinite(position[2]) || !isfinite(position[3]) || position[3]<=0.0f)return false;
        inverse_w[i]=1.0/position[3];
        /* Transformed input is already snapped to the PICA 1/16 pixel grid.
         * Recover the grid after its float clip-space representation. */
        double sx=((position[0]*inverse_w[i]+1.0)*half[0]+offset[0])*16.0;
        double sy=((position[1]*inverse_w[i]+1.0)*half[1]+offset[1])*16.0;
        /* project_vertex bounds the grid to ±1024 pixels. Allow the float
         * clip-space round trip up to the next grid cell's midpoint. */
        const double limit=1024.0*16.0+0.5;
        if(sx<=-limit || sx>=limit || sy<=-limit || sy>=limit)return false;
        x[i]=(int32_t)(sx+(sx<0.0?-0.5:0.5))/16.0;
        y[i]=(int32_t)(sy+(sy<0.0?-0.5:0.5))/16.0;
    }
    return true;
}
static bool depth_plane_coefficients(const ctr_scene_transformed_vertex triangle[3],
                                    const double x[3],const double y[3],const double inverse_w[3],float plane[8]) {
    double z[3];
    for(unsigned i=0;i<3u;++i)z[i]=-triangle[i].position[2]*inverse_w[i];
    double x1=x[1]-x[0],x2=x[2]-x[0],y1=y[1]-y[0],y2=y[2]-y[0];
    double area=x1*y2-x2*y1,dx=0.0,dy=0.0;
    if(area) {
        dx=((z[1]-z[0])*y2-(z[2]-z[0])*y1)/area;
        dy=(x1*(z[2]-z[0])-x2*(z[1]-z[0]))/area;
    }
    double min_x=x[0],max_x=x[0],min_y=y[0],max_y=y[0];
    for(unsigned i=1;i<3u;++i) {
        if(x[i]<min_x)min_x=x[i];
        if(x[i]>max_x)max_x=x[i];
        if(y[i]<min_y)min_y=y[i];
        if(y[i]>max_y)max_y=y[i];
    }
    double origin_x=(min_x+max_x)*0.5,origin_y=(min_y+max_y)*0.5;
    double depth=z[0]+dx*(origin_x-x[0])+dy*(origin_y-y[0]);
    /* Keep nine significant bits in each coarse gradient. The other
     * parts correct their contributions at the local screen position. */
    plane[0]=real(bits((float)dx)&UINT32_C(0xffff8000));
    plane[1]=real(bits((float)dy)&UINT32_C(0xffff8000));
    plane[3]=(float)depth;
    plane[2]=(float)(depth-plane[3]);
    plane[4]=(float)origin_x;plane[5]=(float)origin_y;
    plane[6]=(float)(dx-plane[0]);plane[7]=(float)(dy-plane[1]);
    return isfinite(plane[0]) && isfinite(plane[1]) && isfinite(plane[2]) && isfinite(plane[3]);
}
static bool mip_plane_gradients(const ctr_scene_transformed_vertex triangle[3],const double x[3],
                                const double y[3],const double inverse_w[3],unsigned coordinates,
                                float gradients[4][4]) {
    double area=(x[1]-x[0])*(y[2]-y[0])-(x[2]-x[0])*(y[1]-y[0]);
    memset(gradients,0,4u*sizeof(gradients[0]));
    if(!area)return true;
    double dx[3],dy[3];
    for(unsigned i=0;i<3u;++i) {
        unsigned j=(i+1u)%3u,k=(i+2u)%3u;
        dx[i]=(y[j]-y[k])*inverse_w[i]/area;
        dy[i]=(x[k]-x[j])*inverse_w[i]/area;
    }
    gradients[3][0]=(float)(dx[0]+dx[1]+dx[2]);
    gradients[3][1]=(float)(dy[0]+dy[1]+dy[2]);
    gradients[3][2]=(float)(inverse_w[0]!=inverse_w[1] || inverse_w[0]!=inverse_w[2]);
    if(!isfinite(gradients[3][0]) || !isfinite(gradients[3][1]))return false;
    for(unsigned coordinate=0;coordinate<3u;++coordinate)if(coordinates&(1u<<coordinate))
        for(unsigned axis=0;axis<2u;++axis) {
            double a=triangle[0].uv[coordinate][axis],b=triangle[1].uv[coordinate][axis],
                c=triangle[2].uv[coordinate][axis];
            gradients[coordinate][axis]=(float)(dx[0]*a+dx[1]*b+dx[2]*c);
            gradients[coordinate][axis+2u]=(float)(dy[0]*a+dy[1]*b+dy[2]*c);
            if(!isfinite(gradients[coordinate][axis]) || !isfinite(gradients[coordinate][axis+2u]))return false;
        }
    return true;
}
static bool prepare_blend_snapshot(ctr_wiiu_gx2_scene *s,scene_private *p) {
    GX2Surface *surface=&p->blend_snapshot.surface;
    if(surface->image)return true;
    if(!layout(surface,p->width,p->height,GX2_TILE_MODE_TILED_2D_THIN1,GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8))
        return fail(s,CTR_WIIU_GX2_SCENE_LAYOUT);
    surface->image=allocate(s,surface->alignment,surface->imageSize);
    if(!surface->image)return fail(s,CTR_WIIU_GX2_SCENE_ALLOCATION);
    DCFlushRange(surface->image,surface->imageSize);
    p->blend_snapshot.viewNumMips=p->blend_snapshot.viewNumSlices=1u;
    p->blend_snapshot.compMap=UINT32_C(0x00010203);
    GX2InitTextureRegs(&p->blend_snapshot);
    GX2InitSampler(&p->blend_sampler,GX2_TEX_CLAMP_MODE_CLAMP,GX2_TEX_XY_FILTER_MODE_POINT);
    return true;
}
static void upload_pixel_uniforms(ctr_wiiu_gx2_scene *s,scene_private *p) {
    direct_pool_state *pool=s->direct_hardware && s->cemu_readback?s->direct_pool:NULL;
    unsigned vectors=p->pixel_uniform_vectors;
    if(pool) {
        if(p->uploaded_pixel_vectors==vectors && pool->uploaded_pixel_program==p->pixel.program) {
            unsigned table=vectors>LIGHT_TABLE_FIRST?LIGHT_TABLE_FIRST:FOG_TABLE_FIRST;
            if(vectors>table &&
               !memcmp(pool->uploaded_pixel_constants[table],p->pixel_constants[table],
                       (vectors-table)*sizeof(p->pixel_constants[0])))vectors=table;
            if(!memcmp(pool->uploaded_pixel_constants,p->pixel_constants,
                       vectors*sizeof(p->pixel_constants[0])))return;
        }
        memcpy(pool->uploaded_pixel_constants,p->pixel_constants,vectors*sizeof(p->pixel_constants[0]));
        pool->uploaded_pixel_program=p->pixel.program;
        p->uploaded_pixel_vectors=p->pixel_uniform_vectors;
    }
    GX2SetPixelUniformReg(0,vectors*4u,p->pixel_constants);
}
typedef struct {
    int32_t xy[3][2];
    int64_t area;
} blend_triangle;
static int64_t blend_edge(const int32_t a[2],const int32_t b[2],const int32_t point[2]) {
    return (int64_t)(b[0]-a[0])*(point[1]-a[1])-(int64_t)(b[1]-a[1])*(point[0]-a[0]);
}
static void project_blend_triangle(blend_triangle *out,const ctr_scene_transformed_vertex triangle[3],
                                   const float half[2],const int offset[2]) {
    out->area=0;
    for(unsigned vertex=0;vertex<3u;++vertex)for(unsigned axis=0;axis<2u;++axis) {
        const float *position=triangle[vertex].position;
        double pixel=((double)position[axis]/position[3]+1.0)*half[axis]+offset[axis];
        double scaled=pixel*16.0;
        const double limit=1024.0*16.0+0.5;
        if(scaled<=-limit || scaled>=limit)return;
        int32_t snapped=(int32_t)(scaled+(scaled<0.0?-0.5:0.5));
        /* Group only grid-aligned triangles. Tolerate the clip-space round
         * trip up to 1/1024 pixel, inside one GPU subpixel's rounding range. */
        if(fabs(scaled-snapped)>0.015625)return;
        out->xy[vertex][axis]=snapped;
    }
    out->area=blend_edge(out->xy[0],out->xy[1],out->xy[2]);
}
static bool blend_separates(const blend_triangle *a,const blend_triangle *b) {
    if(!a->area || !b->area)return false;
    for(unsigned edge=0;edge<3u;++edge) {
        const int32_t *first=a->xy[edge],*last=a->xy[(edge+1u)%3u];
        bool separated=true;
        for(unsigned i=0;i<3u;++i) {
            int64_t side=blend_edge(first,last,b->xy[i]);
            if(a->area>0?side>0:side<0)separated=false;
        }
        if(separated)return true;
    }
    return false;
}
static unsigned blend_group_size(const blend_triangle *triangles,unsigned first,unsigned total) {
    /* Triangles in a group share one destination image. Check separating
     * edges in both directions. The rasterizer owns shared boundary samples
     * through its edge rule. Uncertain coverage starts a new group. */
    for(unsigned i=first+1u;i<total;++i)
        for(unsigned j=first;j<i;++j)
            if(!blend_separates(&triangles[i],&triangles[j]) &&
               !blend_separates(&triangles[j],&triangles[i]))return i-first;
    return total-first;
}
static bool draw_samples(void *context,const ctr_scene_state *state,const ctr_vertex *vertices,
                         const ctr_scene_transformed_vertex *transformed,unsigned count,
                         const ctr_scene_sampler sources[3],unsigned mask,bool mipmaps,const ctr_scene_light *light) {
    ctr_wiiu_gx2_scene *s=context;scene_private *p=s?s->private_state:NULL;
    bool direct=s && s->direct_hardware && s->direct_pool!=NULL;
    unsigned max_vertices=direct?CTR_SCENE_MAX_GAME_DRAW_VERTICES:CTR_SCENE_MAX_VERTICES;
    if(!p || !state || (!vertices && !transformed) || (direct && !transformed) ||
       !count || count>max_vertices || count%3u || mask>7u || (mask && !sources))
        return fail(s,CTR_WIIU_GX2_SCENE_ARGUMENT);
    if(s->quarantined)return fail(s,CTR_WIIU_GX2_SCENE_WAIT);
    ctr_fragment_state fragment;ctr_stencil_state stencil;
    if(!ctr_fragment_decode_depth(state->registers,state->register_defined,true,p->depth_enabled,&fragment) ||
       !native_fragment(&fragment) || !native_depth(p,state,&stencil,transformed!=NULL))
        return fail(s,CTR_WIIU_GX2_SCENE_ARGUMENT);
    float half[2];int offset[2];
    if(!read_viewport(p,state,half,offset))return fail(s,CTR_WIIU_GX2_SCENE_LAYOUT);
    /* Validate current descriptors and independent source storage before any
     * lookup, unpin, replacement or eviction. The disabled/no-owner path keeps
     * legacy per-unit descriptor validation, but rejects aliases up front. */
    bool aliases_only=s->texture_cache_disabled && !s->texture_cache;
    for(unsigned unit=0;unit<3u;++unit)if((mask&(1u<<unit)) &&
        !texture_chain_valid(s,p,&sources[unit],mipmaps,aliases_only))
        return fail(s,CTR_WIIU_GX2_SCENE_ARGUMENT);
    direct_pool_state *pool=direct?(direct_pool_state *)s->direct_pool:NULL;
    bool batched=!direct && s->batch_transformed && transformed && !s->texture_cache_disabled && prepare_draw_uploads(s,p);
    /* A fresh slot has never been written by a draw since the last fence.
     * The ordinary path continues to retire before writing its shared slot. */
    if(!direct && (!batched || p->used_uploads==DRAW_UPLOAD_SLOTS) && !retire(s,p))return false;
    texture_leases_release(s,p);
    size_t vbytes=direct?(size_t)count*DIRECT_VERTEX_STRIDE:(size_t)count*VERTEX_STRIDE;
    if(direct && pool->vertex_cursor+vbytes>DIRECT_VERTEX_RING_BYTES) {
        if(!retire(s,p))return false;
        pool->vertex_cursor=0;
        pool->vertex_upload_streams=0;
    }
    void *vertex_upload=direct?(void *)((uint8_t *)pool->vertex_ring+pool->vertex_cursor):p->vertices;
    if(batched)vertex_upload=select_draw_upload(s,p);
    else if(!direct)select_shader_storage(s,p);
    cache_prepare(s);
    bool texture2_coord1=(mask&4u) && (state->registers[0x080u]&(1u<<13));
    unsigned implicit_lod=0,explicit_lod=0,filter_mask=0,mixed_filters=0;
    for(unsigned unit=0;unit<3u;++unit)if(mask&(1u<<unit)) {
        unsigned filters=sources[unit].levels[0].params&6u;
        if(sources[unit].max_level || sources[unit].bias_q8 || (filters!=0u && filters!=6u))
            implicit_lod|=1u<<unit;
        if(!direct && transformed && s->cemu_readback && sources[unit].max_level>sources[unit].min_level &&
           (filters==0u || filters==6u) && !(sources[unit].levels[0].params&UINT32_C(0x01000000)))
            explicit_lod|=1u<<unit;
        if(!direct && transformed && s->cemu_readback && filters &&
           !(sources[unit].levels[0].params&UINT32_C(0x01000000))) {
            filter_mask|=1u<<unit;
            explicit_lod|=1u<<unit;
            if(filters!=6u)mixed_filters|=1u<<unit;
        }
    }
    unsigned coordinates=coordinate_mask(mask,texture2_coord1);
    unsigned mip_coordinates=coordinate_mask(explicit_lod,texture2_coord1);
    bool depth_plane=!direct && transformed && s->cemu_readback && p->depth_enabled &&
                     p->depth_format==CTR_SCENE_DEPTH_D24S8;
    unsigned direct_vs_id=coordinates|(light?(8u|(light->needs_view?16u:0u)):0u);
    if(direct)ctr_gx2_direct_prepare_draw(s,p,direct_vs_id,coordinates,light);
    else if(transformed)ctr_gx2_transformed_program(p->compiled,coordinates,light,depth_plane,mip_coordinates);
    else if(!vertex_program(s,p,state,coordinates))return fail(s,CTR_WIIU_GX2_SCENE_SHADER);
    ctr_tev_program tev;
    bool fog=(state->registers[0x0e0u]&7u)==5u;
    pixel_blend_mode blend=BLEND_FIXED;
    if(!direct && transformed && s->cemu_readback && fragment.blend_enabled &&
       (!fragment.alpha_enabled || fragment.alpha_function==1u) &&
       fragment.blend_equation[0]==0u && fragment.blend_equation[1]==0u) {
        if(fragment.blend_source[0]==6u && fragment.blend_source[1]==6u &&
           fragment.blend_destination[0]==7u && fragment.blend_destination[1]==7u)
            blend=BLEND_SOURCE_OVER;
        else if(fragment.blend_source[0]==0u && fragment.blend_source[1]==0u &&
                fragment.blend_destination[0]==11u && fragment.blend_destination[1]==1u)
            blend=BLEND_DESTINATION_SCALE;
        else if(fragment.blend_source[0]==6u && fragment.blend_source[1]==6u &&
                fragment.blend_destination[0]==1u && fragment.blend_destination[1]==1u)
            blend=BLEND_ALPHA_ADD;
    }
    bool reads_destination=blend_reads_destination(blend);
    bool tev_valid=transformed?ctr_tev_decode_game(state->registers,state->register_defined,mask,&tev):
        ctr_tev_decode_fog(state->registers,state->register_defined,mask,&tev);
    if(!tev_valid ||
       !pixel_program(s,p,&tev,mask,texture2_coord1,implicit_lod,explicit_lod,filter_mask,mixed_filters,sources,state,fog,transformed!=NULL,
                      light,depth_plane,blend))
        return fail(s,CTR_WIIU_GX2_SCENE_SHADER);
    if(!upload_textures(s,p,sources,mask,mipmaps,NULL))return false;
    for(unsigned unit=0;unit<3u;++unit)if(explicit_lod&(1u<<unit))
        GX2InitSamplerLOD(&p->sampler[unit],(float)sources[unit].min_level,(float)sources[unit].max_level,0.0f);
    for(unsigned unit=0;unit<3u;++unit)if(filter_mask&(1u<<unit))
        GX2InitSamplerXYFilter(&p->sampler[unit],GX2_TEX_XY_FILTER_MODE_POINT,
                              GX2_TEX_XY_FILTER_MODE_POINT,GX2_TEX_ANISO_RATIO_NONE);
    /* Transaction-owned fallback textures share allocations between draws. */
    if(mask && !s->texture_cache)batched=false;
    float coefficients[8],gradients[4][4];
    unsigned stride=direct?DIRECT_VERTEX_STRIDE:VERTEX_STRIDE;
    for(unsigned vertex=0;vertex<count;++vertex) {
        uint8_t *target=(uint8_t *)vertex_upload+(size_t)vertex*stride;
        if(transformed) {
            const ctr_scene_transformed_vertex *source=&transformed[vertex];
            for(unsigned lane=0;lane<4u;++lane)
                if(!isfinite(source->position[lane]) || !isfinite(source->color[lane]))
                    return fail(s,CTR_WIIU_GX2_SCENE_ARGUMENT);
            if(source->position[3]<=0.0f)return fail(s,CTR_WIIU_GX2_SCENE_ARGUMENT);
            if(!direct)memset(target,0,stride);
            memcpy(target,source->position,sizeof(source->position));
            if(!direct && vertex%3u==0u && (depth_plane || mip_coordinates)) {
                const ctr_scene_transformed_vertex *triangle=&transformed[vertex];
                double x[3],y[3],inverse_w[3];
                if(!triangle_grid(triangle,half,offset,x,y,inverse_w))
                    return fail(s,CTR_WIIU_GX2_SCENE_ARGUMENT);
                if(p->flip_y)for(unsigned j=0;j<3u;++j)y[j]=p->height-y[j];
                if((depth_plane && !depth_plane_coefficients(triangle,x,y,inverse_w,coefficients)) ||
                   (mip_coordinates && !mip_plane_gradients(triangle,x,y,inverse_w,mip_coordinates,gradients)))
                    return fail(s,CTR_WIIU_GX2_SCENE_ARGUMENT);
            }
            if(depth_plane)
                memcpy(target+7u*4u*sizeof(float),coefficients,sizeof(coefficients));
            if(mip_coordinates)
                memcpy(target+9u*4u*sizeof(float),gradients,sizeof(gradients));
            memcpy(target+sizeof(source->position),source->color,sizeof(source->color));
            for(unsigned unit=0;unit<3u;++unit)if(coordinates&(1u<<unit)) {
                if(!isfinite(source->uv[unit][0]) || !isfinite(source->uv[unit][1]))
                    return fail(s,CTR_WIIU_GX2_SCENE_ARGUMENT);
                uint8_t *attribute=target+(unit+2u)*4u*sizeof(float);
                if(direct) {
                    float uv4[4]={source->uv[unit][0],source->uv[unit][1],0.0f,1.0f};
                    memcpy(attribute,uv4,sizeof(uv4));
                } else {
                    memcpy(attribute,source->uv[unit],sizeof(source->uv[unit]));
                    if(explicit_lod)memcpy(attribute+2u*sizeof(float),&source->position[3],sizeof(float));
                }
            }
            if(light) {
                for(unsigned lane=0;lane<4u;++lane)
                    if(!isfinite(source->orientation[lane]))return fail(s,CTR_WIIU_GX2_SCENE_ARGUMENT);
                memcpy(target+5u*4u*sizeof(float),source->orientation,sizeof(source->orientation));
                if(light->needs_view) {
                    for(unsigned lane=0;lane<3u;++lane)
                        if(!isfinite(source->view[lane]))return fail(s,CTR_WIIU_GX2_SCENE_ARGUMENT);
                    if(direct) {
                        float vw4[4]={source->view[0],source->view[1],source->view[2],0.0f};
                        memcpy(target+6u*4u*sizeof(float),vw4,sizeof(vw4));
                    } else memcpy(target+6u*4u*sizeof(float),source->view,sizeof(source->view));
                }
            }
            continue;
        }
        if((vertices[vertex].enabled&p->compiled->input_mask)!=p->compiled->input_mask)
            return fail(s,CTR_WIIU_GX2_SCENE_ARGUMENT);
        memcpy(target,vertices[vertex].input,VERTEX_STRIDE);
    }
    if(direct) {
        pool->vertex_cursor+=vbytes;
        DCFlushRange(vertex_upload,(unsigned)vbytes);
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER,vertex_upload,(unsigned)vbytes);
    } else {
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER,vertex_upload,count*VERTEX_STRIDE);
        ctr_gx2_upload_vertex_program(p,light,depth_plane,mip_coordinates);
    }
    if(!p->draw_context_active) {
        GX2SetContextState(p->context);p->draw_context_active=true;
    }
    p->raster_current=false;
    p->merger_current=false;
    if(reads_destination && !prepare_blend_snapshot(s,p))return false;
    viewport(p,half,offset,fog);
    if(direct) {
        ctr_gx2_direct_bind_pipeline(p,pool,direct_vs_id);
    } else {
        GX2SetColorBuffer(&p->target,GX2_RENDER_TARGET_0);
        if(p->depth_enabled)GX2SetDepthBuffer(&p->depth_target);
        GX2SetShaderModeEx(GX2_SHADER_MODE_UNIFORM_REGISTER,64,64,0,0,184,192);
        GX2SetFetchShader(&p->fetch);GX2SetVertexShader(&p->vertex);GX2SetPixelShader(&p->pixel);
    }
    /* Inactive descriptors may remain in shadow state, but only the explicit
     * mask's unit IDs can occur in this fully rebound program. No inactive
     * texture image is sampled or freed before the common retirement gate. */
    bind_draw_textures(s,p,mask);
    if(reads_destination) {
        GX2SetPixelTexture(&p->blend_snapshot,3u);GX2SetPixelSampler(&p->blend_sampler,3u);
        fragment.blend_enabled=false;fragment.logic_op=3u;
    } else if(blend==BLEND_ALPHA_ADD) {
        fragment.blend_source[0]=fragment.blend_source[1]=1u;
    }
    upload_pixel_uniforms(s,p);
    GX2SetAttribBuffer(0,(unsigned)vbytes,stride,vertex_upload);
    if(!transformed)for(unsigned uniform=0;uniform<CTR_SHADER_UNIFORMS;++uniform)if(p->compiled->uniform_used[uniform])
        GX2SetVertexUniformReg(uniform*4u,4,state->shader.uniforms[uniform]);
    if(s->cemu_readback && !fragment.blend_enabled && fragment.logic_op==6u) {
        /* Cemu 2.6 Vulkan falls back to COPY for ROP NOP. Mask color
         * writes so the draw still runs its alpha, depth and stencil tests. */
        fragment.logic_op=3u;
        fragment.write_mask=0u;
    }
    fragment_state(&fragment);
    depth_state(p,state,&stencil);
    if(!direct && s->cemu_readback && p->depth_enabled) {
        unsigned gate=state->registers[0x115u];
        p->depth_written|=(gate&2u) && (state->registers[0x107u]&0x1000u);
        if((gate&1u) && stencil.enabled && (stencil.action[0] || stencil.action[1] || stencil.action[2]))
            p->stencil_written|=stencil.write_mask;
    }
    if(!direct) {
        GX2SetAlphaToMask(false,GX2_ALPHA_TO_MASK_MODE_NON_DITHERED);
        GX2SetStreamOutEnable(false);
        /* Transformed triangles have already been culled using the PICA screen
         * grid. Only original-vertex submission delegates culling to GX2. */
        unsigned cull=transformed?0u:state->registers[0x040u]&3u;
        GX2SetCullOnlyControl(p->flip_y?GX2_FRONT_FACE_CW:GX2_FRONT_FACE_CCW,cull==1u,cull==2u);
    }
    unsigned mode=state->registers[0x065u]&3u;
    unsigned x1=state->registers[0x066u]&1023u,y1=(state->registers[0x066u]>>16)&1023u;
    unsigned x2=state->registers[0x067u]&1023u,y2=(state->registers[0x067u]>>16)&1023u;
    if(x1>p->width)x1=p->width;
    if(y1>p->height)y1=p->height;
    if(x2>=p->width)x2=p->width-1u;
    if(y2>=p->height)y2=p->height-1u;
    bool empty=x1>x2 || y1>y2;
    if(mode!=0u && mode!=1u && mode!=3u)return fail(s,CTR_WIIU_GX2_SCENE_LAYOUT);
    if(mode!=3u || !empty) {
        blend_triangle triangles[CTR_SCENE_MAX_VERTICES/3u];
        unsigned total=count/3u;
        if(reads_destination)for(unsigned i=0;i<total;++i)
            project_blend_triangle(&triangles[i],transformed+i*3u,half,offset);
        for(unsigned first=0;first<total;) {
            unsigned run=reads_destination?blend_group_size(triangles,first,total):total;
            if(reads_destination) {
                GX2Invalidate(GX2_INVALIDATE_MODE_COLOR_BUFFER,p->target.surface.image,p->target.surface.imageSize);
                GX2CopySurface(&p->target.surface,0,0,&p->blend_snapshot.surface,0,0);p->pending=true;
            }
            unsigned base=first*3u,vertices_in_run=run*3u;
            if(mode==0u || (mode==1u && empty))
                rectangle(s,0,0,p->width,p->height,base,vertices_in_run);
            else if(mode==3u)
                rectangle(s,x1,y1,x2-x1+1u,y2-y1+1u,base,vertices_in_run);
            else {
                rectangle(s,0,0,p->width,y1,base,vertices_in_run);
                rectangle(s,0,y2+1u,p->width,p->height-y2-1u,base,vertices_in_run);
                rectangle(s,0,y1,x1,y2-y1+1u,base,vertices_in_run);
                rectangle(s,x2+1u,y1,p->width-x2-1u,y2-y1+1u,base,vertices_in_run);
            }
            first+=run;
        }
    }
    if(batched || direct) {
        for(unsigned unit=0;unit<3u;++unit)
            if(p->texture_lease[unit])p->texture_lease[unit]->submitted=true;
        if(batched) { ++p->used_uploads;p->next_upload=(p->next_upload+1u)%DRAW_UPLOAD_SLOTS; }
        /* Keep the draw context active until the caller resolves or aborts.
         * All queued uploads remain owned across intermediate fences. */
        p->pending=true;
    } else if(!restore(s))return false;
    ++s->draws;s->shader_instructions+=p->compiled->pica_instructions;
    s->result=CTR_WIIU_GX2_SCENE_OK;return true;
}
bool ctr_gx2_direct_setup_state(ctr_wiiu_gx2_scene *s,scene_private *p,const ctr_scene_state *state,
                                const ctr_scene_sampler sources[3],unsigned mask,const ctr_scene_light *light,
                                const ctr_texture_cache_lease (*leases)[CTR_SCENE_MAX_MIP_LEVELS],
                                const ctr_tev_program *validated_tev,bool *skip_draw) {
    *skip_draw=false;
    if(!p || !state || mask>7u || (mask && !sources) || s->quarantined)return false;
    ctr_fragment_state fragment;ctr_stencil_state stencil;
    if(!ctr_fragment_decode_depth(state->registers,state->register_defined,true,p->depth_enabled,&fragment) ||
       !native_fragment(&fragment) || !native_depth(p,state,&stencil,true))
        return false;
    float half[2];int offset[2];
    if(!read_viewport(p,state,half,offset))return false;
    bool aliases_only=s->texture_cache_disabled && !s->texture_cache;
    for(unsigned unit=0;unit<3u;++unit)if((mask&(1u<<unit)) &&
        !texture_chain_valid(s,p,&sources[unit],true,aliases_only))
        return false;
    unsigned mode=state->registers[0x065u]&3u;
    unsigned x1=state->registers[0x066u]&1023u,y1=(state->registers[0x066u]>>16)&1023u;
    unsigned x2=state->registers[0x067u]&1023u,y2=(state->registers[0x067u]>>16)&1023u;
    if(x1>p->width)x1=p->width;
    if(y1>p->height)y1=p->height;
    if(x2>=p->width)x2=p->width-1u;
    if(y2>=p->height)y2=p->height-1u;
    bool empty=x1>x2 || y1>y2;
    if(mode!=0u && mode!=1u && mode!=3u)return false;
    if(mode==1u && !empty)return false;
    if(mode==3u && empty) { *skip_draw=true;return true; }
    texture_leases_release(s,p);
    cache_prepare(s);
    bool texture2_coord1=(mask&4u) && (state->registers[0x080u]&(1u<<13));
    unsigned implicit_lod=0;
    for(unsigned unit=0;unit<3u;++unit)if(mask&(1u<<unit)) {
        unsigned filters=sources[unit].levels[0].params&6u;
        if(sources[unit].max_level || sources[unit].bias_q8 || (filters!=0u && filters!=6u))
            implicit_lod|=1u<<unit;
    }
    ctr_tev_program decoded;
    const ctr_tev_program *tev=s->cemu_readback?validated_tev:NULL;
    if(!tev) {
        if(!ctr_tev_decode_game(state->registers,state->register_defined,mask,&decoded))return false;
        tev=&decoded;
    }
    bool fog=(state->registers[0x0e0u]&7u)==5u;
    if(!pixel_program(s,p,tev,mask,texture2_coord1,implicit_lod,0u,0u,0u,sources,state,fog,true,
                      light && light->active?light:NULL,false,BLEND_FIXED))
        return false;
    if(!upload_textures(s,p,sources,mask,true,leases))return false;
    if(!p->draw_context_active) {
        GX2SetContextState(p->context);p->draw_context_active=true;
    }
    unsigned x=0,y=0,w=p->width,h=p->height;
    if(mode==3u) {
        x=x1;w=x2-x1+1u;h=y2-y1+1u;y=p->flip_y?p->height-y1-h:y1;
    }
    const uint32_t raster[]={bits(half[0]),bits(half[1]),(uint32_t)offset[0],(uint32_t)offset[1],fog,x,y,w,h};
    bool update_raster=!s->cemu_readback || !p->raster_current ||
        memcmp(p->raster_registers,raster,sizeof(raster));
    if(update_raster)viewport(p,half,offset,fog);
    bind_draw_textures(s,p,mask);
    upload_pixel_uniforms(s,p);
    if(s->cemu_readback && !fragment.blend_enabled && fragment.logic_op==6u) {
        fragment.logic_op=3u;
        fragment.write_mask=0u;
    }
    const uint32_t *regs=state->registers;
    const uint32_t merger[]={regs[0x100u],regs[0x101u],regs[0x102u],
        state->register_defined[0x103u]==15u?regs[0x103u]:0u,regs[0x104u],
        regs[0x105u],regs[0x106u],regs[0x107u],regs[0x113u],regs[0x115u]};
    bool update_merger=!s->cemu_readback || !p->merger_current ||
        memcmp(p->merger_registers,merger,sizeof(merger));
    if(update_merger) {
        fragment_state(&fragment);
        depth_state(p,state,&stencil);
        if(s->cemu_readback) {
            memcpy(p->merger_registers,merger,sizeof(merger));
            p->merger_current=true;
        }
    }
    if(update_raster) {
        GX2SetScissor(x,y,w,h);
        if(s->cemu_readback) {
            memcpy(p->raster_registers,raster,sizeof(raster));
            p->raster_current=true;
        }
    }
    return true;
}
bool ctr_gx2_draw_mips_legacy(void *context,const ctr_scene_state *state,const ctr_vertex *vertices,unsigned count,
                              const ctr_scene_sampler samplers[3],unsigned mask) {
    return draw_samples(context,state,vertices,NULL,count,samplers,mask,true,NULL);
}
bool ctr_gx2_draw_transformed_legacy(void *context,const ctr_scene_state *state,
                                     const ctr_scene_transformed_vertex *vertices,unsigned count,
                                     const ctr_scene_sampler samplers[3],unsigned mask,const ctr_scene_light *light) {
    return draw_samples(context,state,NULL,vertices,count,samplers,mask,true,light && light->active?light:NULL);
}
bool ctr_gx2_draw_textures_legacy(void *context,const ctr_scene_state *state,const ctr_vertex *vertices,unsigned count,
                                  const ctr_scene_texture textures[3],unsigned mask) {
    if(mask>7u || (mask && !textures))return fail(context,CTR_WIIU_GX2_SCENE_ARGUMENT);
    ctr_scene_sampler samplers[3]={0};
    for(unsigned unit=0;unit<3u;++unit)if(mask&(1u<<unit))samplers[unit].levels[0]=textures[unit];
    return draw_samples(context,state,vertices,NULL,count,samplers,mask,false,NULL);
}
bool ctr_gx2_draw_legacy(void *context,const ctr_scene_state *state,const ctr_vertex *vertices,unsigned count,
                         const ctr_scene_texture *texture) {
    ctr_scene_texture textures[3]={{0}};
    if(texture)textures[0]=*texture;
    return ctr_gx2_draw_textures_legacy(context,state,vertices,count,textures,texture?1u:0u);
}
static bool prepare_resolve(ctr_wiiu_gx2_scene *s,const scene_private *p,GX2Surface *surface,
                            GX2TileMode tile,unsigned format) {
    if(surface->image)return true;
    if(!layout(surface,p->width,p->height,tile,format) ||
       (uint64_t)surface->pitch*p->height*4u>surface->imageSize)
        return fail(s,CTR_WIIU_GX2_SCENE_LAYOUT);
    surface->image=allocate(s,surface->alignment,surface->imageSize);
    if(!surface->image)return fail(s,CTR_WIIU_GX2_SCENE_ALLOCATION);
    DCFlushRange(surface->image,surface->imageSize);
    return true;
}
static bool resolve_stencil(ctr_wiiu_gx2_scene *s,scene_private *p) {
    const unsigned rgba=GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
    if(!prepare_resolve(s,p,&p->stencil_target.surface,GX2_TILE_MODE_TILED_2D_THIN1,rgba) ||
       !prepare_resolve(s,p,&p->stencil_linear,GX2_TILE_MODE_LINEAR_SPECIAL,rgba) || !retire(s,p))return false;
    /* Sampling a depth texture on Cemu cannot read stencil. Project only
     * bits that guest draws may have changed into a separate RGBA8 target
     * using fixed-function stencil tests. Neither gameplay target is written. */
    static const float quad[6][4]={
        {-1,-1,0,1}, {1,-1,0,1}, {-1,1,0,1},
        {-1,1,0,1}, {1,-1,0,1}, {1,1,0,1}
    };
    select_shader_storage(s,p);
    transformed_program(p->compiled,0u,NULL,false,0u);
    memset(p->vertices,0,6u*VERTEX_STRIDE);
    for(unsigned i=0;i<6u;++i)memcpy((uint8_t *)p->vertices+i*VERTEX_STRIDE,quad[i],sizeof(quad[i]));
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER,p->vertices,6u*VERTEX_STRIDE);
    upload_vertex_program(p,NULL,false,0u);
    ctr_tev_program tev={0};
    for(unsigned i=0;i<CTR_TEV_STAGES;++i)
        tev.stages[i].source[0][0]=tev.stages[i].source[1][0]=i?15u:14u;
    pixel_constants(p,&tev);
    if(!pixel_shader(p,&tev,0u,false,0u,0u,0u,0u,false,false,NULL,false,BLEND_FIXED))return fail(s,CTR_WIIU_GX2_SCENE_SHADER);

    p->stencil_target.viewNumSlices=1;
    GX2SetContextState(p->context);
    GX2InitColorBufferRegs(&p->stencil_target);
    GX2ClearColor(&p->stencil_target,0,0,0,0);p->pending=true;
    GX2SetColorBuffer(&p->stencil_target,GX2_RENDER_TARGET_0);
    GX2SetDepthBuffer(&p->depth_target);
    GX2SetShaderModeEx(GX2_SHADER_MODE_UNIFORM_REGISTER,64,64,0,0,184,192);
    GX2SetFetchShader(&p->fetch);GX2SetVertexShader(&p->vertex);GX2SetPixelShader(&p->pixel);
    GX2SetAttribBuffer(0,6u*VERTEX_STRIDE,VERTEX_STRIDE,p->vertices);
    GX2SetPixelUniformReg(0,p->pixel_uniform_vectors*4u,p->pixel_constants);
    GX2ViewportReg viewport;
    GX2InitViewportReg(&viewport,0,0,(float)p->width,(float)p->height,0,1);
    viewport.pa_cl_vport_yscale=bits((float)p->height/2.0f);
    GX2SetViewportReg(&viewport);GX2SetRasterizerClipControlEx(true,true,false);
    GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW,false,false);
    GX2SetAlphaToMask(false,GX2_ALPHA_TO_MASK_MODE_NON_DITHERED);GX2SetStreamOutEnable(false);
    GX2SetAlphaTest(false,GX2_COMPARE_FUNC_ALWAYS,0);
    GX2SetColorControl(GX2_LOGIC_OP_OR,0,false,true);
    GX2SetTargetChannelMasks(1,0,0,0,0,0,0,0);
    GX2SetDepthStencilControl(false,false,GX2_COMPARE_FUNC_ALWAYS,true,true,
        GX2_COMPARE_FUNC_EQUAL,GX2_STENCIL_FUNCTION_KEEP,GX2_STENCIL_FUNCTION_KEEP,GX2_STENCIL_FUNCTION_KEEP,
        GX2_COMPARE_FUNC_EQUAL,GX2_STENCIL_FUNCTION_KEEP,GX2_STENCIL_FUNCTION_KEEP,GX2_STENCIL_FUNCTION_KEEP);
    for(unsigned bit=1u;bit<=128u;bit<<=1u)if(p->stencil_written&bit) {
        uint32_t color[4]={bits((float)bit),0,0,0};
        GX2SetPixelUniformReg(0,4,color);
        GX2SetStencilMask(bit,0,bit,bit,0,bit);
        rectangle(s,0,0,p->width,p->height,0u,6u);
    }
    GX2Invalidate(GX2_INVALIDATE_MODE_COLOR_BUFFER,p->stencil_target.surface.image,p->stencil_target.surface.imageSize);
    GX2CopySurface(&p->stencil_target.surface,0,0,&p->stencil_linear,0,0);p->pending=true;
    return true;
}
static bool queue_cemu_depth(ctr_wiiu_gx2_scene *s,scene_private *p) {
    if(p->depth_written) {
        const unsigned format=GX2_SURFACE_FORMAT_FLOAT_R32;
        if(!prepare_resolve(s,p,&p->depth_resolve,GX2_TILE_MODE_TILED_2D_THIN1,format) ||
           !prepare_resolve(s,p,&p->depth_linear,GX2_TILE_MODE_LINEAR_SPECIAL,format))return false;
        GX2ConvertDepthBufferToTextureSurface(&p->depth_target,&p->depth_resolve,0,0);
        /* The Cemu linear-special copy reads the converted image on the
         * host. Retire the conversion before requesting that readback. */
        p->pending=true;
        if(!retire(s,p))return false;
        GX2Invalidate(GX2_INVALIDATE_MODE_COLOR_BUFFER,p->depth_resolve.image,p->depth_resolve.imageSize);
        GX2CopySurface(&p->depth_resolve,0,0,&p->depth_linear,0,0);p->pending=true;
    }
    return !p->stencil_written || resolve_stencil(s,p);
}
static bool publish_cemu_depth(const scene_private *p,const ctr_scene_depth_surface *output) {
    if(p->depth_written) {
        DCInvalidateRange(p->depth_linear.image,p->depth_linear.imageSize);
        for(unsigned y=0;y<p->height;++y)for(unsigned x=0;x<p->width;++x) {
            uint8_t *sample=(uint8_t *)p->depth_linear.image+((size_t)y*p->depth_linear.pitch+x)*4u;
            uint32_t raw=(uint32_t)sample[0]|(uint32_t)sample[1]<<8|(uint32_t)sample[2]<<16|(uint32_t)sample[3]<<24;
            float value=real(raw);
            if(!isfinite(value) || value<0.0f || value>1.0f)return false;
            /* Normalize back to the nearest representable packed depth.
             * The paired reference still rejects any numeric disparity. */
            put32(sample,(uint32_t)((double)value*16777215.0+0.5));
        }
    }
    if(p->stencil_written)DCInvalidateRange(p->stencil_linear.image,p->stencil_linear.imageSize);
    for(unsigned y=0;y<p->height;++y) {
        uint8_t *row=(uint8_t *)output->bytes+(size_t)target_row(p,y)*output->stride;
        for(unsigned x=0;x<p->width;++x) {
            if(p->depth_written)
                memcpy(row+x*4u,(const uint8_t *)p->depth_linear.image+((size_t)y*p->depth_linear.pitch+x)*4u,3u);
            else memset(row+x*4u,p->seed_far?255:0,3u);
            uint8_t value=p->stencil_written?
                ((const uint8_t *)p->stencil_linear.image)[((size_t)y*p->stencil_linear.pitch+x)*4u]:0;
            row[x*4u+3u]=(p->seed_stencil&~p->stencil_written)|(value&p->stencil_written);
        }
    }
    return true;
}
static bool resolve_targets(void *context,const ctr_scene_surface *readback,
                            const ctr_scene_depth_surface *depth,bool resident) {
    ctr_wiiu_gx2_scene *s=context;scene_private *p=s?s->private_state:NULL;
    if(!p || !dimensions(readback) || readback->width!=p->width || readback->height!=p->height ||
       readback->stride!=p->stride || readback->size!=p->size ||
       p->resident!=resident || (!resident && p->depth_enabled!=(depth!=NULL)) ||
       (depth && (!depth_dimensions(depth,readback) || depth->stride!=p->depth_stride ||
                  depth->size!=p->depth_size || depth->address!=p->depth_address ||
                  depth->format!=p->depth_format ||
                  overlaps(readback->bytes,readback->size,depth->bytes,depth->size))))
        return fail(s,CTR_WIIU_GX2_SCENE_ARGUMENT);
    if(s->quarantined)return fail(s,CTR_WIIU_GX2_SCENE_WAIT);
    bool direct_skip=s->direct_hardware && !resident;
    bool cemu_depth=depth && s->cemu_readback && !direct_skip;
    /* Cemu's tested conversion route can return packed D24S8. */
    if(cemu_depth && p->depth_format!=CTR_SCENE_DEPTH_D24S8)
        return fail(s,CTR_WIIU_GX2_SCENE_UNSUPPORTED_READBACK);
    GX2SetContextState(p->context);
    GX2Invalidate(GX2_INVALIDATE_MODE_COLOR_BUFFER,p->target.surface.image,p->target.surface.imageSize);
    GX2CopySurface(&p->target.surface,0,0,&p->linear,0,0);p->pending=true;
    if(direct_skip) {
        if(depth)++s->direct_depth_skips;
    } else if(cemu_depth) {
        if(!queue_cemu_depth(s,p))return false;
    } else if(depth) {
        GX2Invalidate(GX2_INVALIDATE_MODE_DEPTH_BUFFER,p->depth_target.surface.image,p->depth_target.surface.imageSize);
        GX2CopySurface(&p->depth_target.surface,0,0,&p->depth_linear,0,0);
    }
    bool batch_restore=direct_skip && s->cemu_readback;
    if(batch_restore)queue_restore(s);
    if(!retire(s,p))return fail(s,CTR_WIIU_GX2_SCENE_WAIT);
    /* A resident caller can publish this surface directly. Restore the
     * presentation context successfully before modifying its output. */
    if(!batch_restore && (resident || cemu_depth || direct_skip) && !restore(s))return false;
    if(cemu_depth && !publish_cemu_depth(p,depth))return fail(s,CTR_WIIU_GX2_SCENE_LAYOUT);
    DCInvalidateRange(p->linear.image,p->linear.imageSize);
    if(depth && !cemu_depth && !direct_skip)DCInvalidateRange(p->depth_linear.image,p->depth_linear.imageSize);
    for(unsigned y=0;y<p->height;++y)
        memcpy((uint8_t *)readback->bytes+(size_t)y*readback->stride,
               (const uint8_t *)p->linear.image+(size_t)target_row(p,y)*p->linear.pitch*4u,p->width*4u);
    if(depth && !cemu_depth && !direct_skip)for(unsigned y=0;y<p->height;++y)
        memcpy((uint8_t *)depth->bytes+(size_t)y*depth->stride,
               (const uint8_t *)p->depth_linear.image+(size_t)y*p->depth_linear.pitch*p->depth_bytes,(size_t)p->width*p->depth_bytes);
    if(s->observe_readback)s->observe_readback(s->readback_context,readback);
    if(depth && !direct_skip && s->observe_depth_readback)s->observe_depth_readback(s->readback_context,depth);
    if(!resident && !cemu_depth && !direct_skip && !restore(s))return false;
    if(direct_skip && s->direct_pool) {
        direct_pool_state *pool=s->direct_pool;
        pool->color_resolved=true;pool->color_flipped=p->flip_y;
    }
    ++s->readbacks;s->result=CTR_WIIU_GX2_SCENE_OK;
    if(!resident)release(s,p);
    return true;
}
bool ctr_gx2_begin_deferred_color(void *context,const ctr_scene_surface *initial,
                                  const ctr_scene_depth_surface *depth,bool reuse_pending) {
    ctr_wiiu_gx2_scene *s=context;
    if(!s || !s->cemu_readback || !s->direct_hardware || !s->restore_context || !dimensions(initial) ||
       (depth && !depth_dimensions(depth,initial)))return fail(s,CTR_WIIU_GX2_SCENE_ARGUMENT);
    if(s->private_state)return fail(s,CTR_WIIU_GX2_SCENE_BUSY);
    return ctr_gx2_direct_begin_targets(s,initial,depth,reuse_pending,true);
}
bool ctr_gx2_end_deferred_color(void *context) {
    ctr_wiiu_gx2_scene *s=context;
    scene_private *p=s->private_state;
    direct_pool_state *pool=s->direct_pool;
    if(!p || !pool || s->quarantined)return false;
    queue_restore(s);
    if(!retire(s,p))return false;
    if(p->target.surface.image==pool->color_candidate.surface.image) {
        pool->color_candidate=pool->target;
        pool->target=p->target;
    }
    pool->color_pending=true;pool->color_resolved=false;pool->color_flipped=p->flip_y;
    if(p->depth_enabled)++s->direct_depth_skips;
    s->result=CTR_WIIU_GX2_SCENE_OK;
    release(s,p);
    return true;
}
static ctr_scene_scanout_state *scanout_state(void *context,bool create) {
    ctr_wiiu_gx2_scene *s=context;
    if(!s->cemu_readback)return NULL;
    gx2_scanout_state *scanout=s->scanout;
    if(!scanout && create) {
        scanout=allocate(s,256,sizeof(*scanout));
        if(!scanout)return NULL;
        memset(scanout,0,sizeof(*scanout));
        s->scanout=scanout;
    }
    return scanout?&scanout->queue:NULL;
}
const void *ctr_wiiu_gx2_scene_scanout_texture(const ctr_wiiu_gx2_scene *s,unsigned slot) {
    const gx2_scanout_state *scanout=s->scanout;
    if(!scanout || !slot || slot>CTR_GX_SCANOUT_SLOTS || s->quarantined)return NULL;
    return scanout->images[slot-1u].surface.image?&scanout->display[slot-1u]:NULL;
}
static uint32_t scanout_generation;
static bool scanout_lend(void *context,unsigned slot) {
    ctr_wiiu_gx2_scene *s=context;
    gx2_scanout_state *scanout=s->scanout;
    direct_pool_state *pool=s->direct_pool;
    if(!scanout || slot>=CTR_GX_SCANOUT_SLOTS || scanout->queue.source_slot ||
       !pool || !pool->color_pending || !pool->color_flipped || s->private_state ||
       s->quarantined || pool->width!=CTR_GX_SCANOUT_WIDTH || pool->height!=CTR_GX_SCANOUT_HEIGHT)
        return false;
    GX2Texture *image=&scanout->images[slot];
    GX2Surface surface=pool->target.surface;
    void *spare=image->surface.image;
    if(spare && (image->surface.format!=surface.format ||
                 image->surface.pitch!=surface.pitch || image->surface.tileMode!=surface.tileMode ||
                 image->surface.imageSize!=surface.imageSize))return false;
    if(!spare) {
        spare=allocate(s,surface.alignment,surface.imageSize);
        if(!spare)return false;
        DCFlushRange(spare,surface.imageSize);
    }
    image->surface=surface;
    image->viewNumMips=1;image->viewNumSlices=1;
    image->compMap=GX2_COMP_MAP(GX2_SQ_SEL_X,GX2_SQ_SEL_Y,GX2_SQ_SEL_Z,GX2_SQ_SEL_W);
    GX2InitTextureRegs(image);
    pool->target.surface.image=spare;
    GX2InitColorBufferRegs(&pool->target);
    scanout->queue.source_slot=slot+1u;
    /* A surface address can be reused between presentations. Each successful
     * lend gets a new identity for the pixels in that surface. */
    uint32_t generation=scanout_generation!=UINT32_MAX?++scanout_generation:0u;
    scanout->display[slot]=(ctr_diagnostic_scanout){image,generation};
    return true;
}
static void scanout_discard_source(void *context) {
    ctr_wiiu_gx2_scene *s=context;
    gx2_scanout_state *scanout=s->scanout;
    direct_pool_state *pool=s->direct_pool;
    scanout->queue.source_slot=0;
    pool->color_pending=false;pool->color_resolved=false;
}
static bool scanout_read(void *context,unsigned slot,const ctr_scene_surface *readback) {
    ctr_wiiu_gx2_scene *s=context;
    gx2_scanout_state *scanout=s->scanout;
    if(!scanout || slot>=CTR_GX_SCANOUT_SLOTS || s->private_state ||
       s->quarantined || !dimensions(readback))return false;
    GX2Surface *image=&scanout->images[slot].surface;
    if(!image->image || image->width!=readback->width || image->height!=readback->height)return false;
    GX2Surface *linear=&scanout->linear;
    if(!linear->image) {
        if(!layout(linear,image->width,image->height,GX2_TILE_MODE_LINEAR_SPECIAL,image->format))return false;
        linear->image=allocate(s,linear->alignment,linear->imageSize);
        if(!linear->image)return false;
        DCFlushRange(linear->image,linear->imageSize);
    }
    direct_pool_state *pool=s->direct_pool;
    if(pool)GX2SetContextState(pool->context);
    GX2Invalidate(GX2_INVALIDATE_MODE_COLOR_BUFFER,image->image,image->imageSize);
    GX2CopySurface(image,0,0,linear,0,0);
    queue_restore(s);
    if(!wait_for_gx2(s))return false;
    DCInvalidateRange(linear->image,linear->imageSize);
    for(unsigned y=0;y<image->height;++y)
        memcpy((uint8_t *)readback->bytes+(size_t)y*readback->stride,
               (const uint8_t *)linear->image+(size_t)y*linear->pitch*4u,image->width*4u);
    ++s->readbacks;
    return true;
}
const ctr_scene_scanout ctr_gx2_scanout={
    scanout_state,scanout_lend,scanout_read,scanout_discard_source
};
bool ctr_gx2_resolve_deferred_color(void *context,const ctr_scene_surface *readback) {
    ctr_wiiu_gx2_scene *s=context;
    direct_pool_state *pool=s->direct_pool;
    if(s->private_state || !pool || !dimensions(readback) ||
       readback->width!=pool->width || readback->height!=pool->height)return false;
    gx2_scanout_state *scanout=s->scanout;
    unsigned source_slot=scanout?scanout->queue.source_slot:0u;
    if(pool->color_pending) {
        GX2Surface *source=source_slot?&scanout->images[source_slot-1u].surface:&pool->target.surface;
        GX2SetContextState(pool->context);
        GX2Invalidate(GX2_INVALIDATE_MODE_COLOR_BUFFER,source->image,source->imageSize);
        GX2CopySurface(source,0,0,&pool->linear,0,0);
        queue_restore(s);
        if(!wait_for_gx2(s))return false;
        DCInvalidateRange(pool->linear.image,pool->linear.imageSize);
        pool->color_pending=false;pool->color_resolved=true;
        ++s->readbacks;
    }
    if(!pool->color_resolved)return false;
    for(unsigned y=0;y<pool->height;++y) {
        unsigned row=pool->color_flipped?pool->height-y-1u:y;
        memcpy((uint8_t *)readback->bytes+(size_t)y*readback->stride,
               (const uint8_t *)pool->linear.image+(size_t)row*pool->linear.pitch*4u,(size_t)pool->width*4u);
    }
    if(source_slot)scanout_discard_source(s);
    if(s->observe_readback)s->observe_readback(s->readback_context,readback);
    s->result=CTR_WIIU_GX2_SCENE_OK;
    return true;
}
bool ctr_gx2_begin_legacy(void *context,const ctr_scene_surface *initial) {
    return begin_targets(context,initial,NULL,false);
}
bool ctr_gx2_begin_depth_legacy(void *context,const ctr_scene_surface *initial,const ctr_scene_depth_surface *depth) {
    if(!depth)return fail(context,CTR_WIIU_GX2_SCENE_ARGUMENT);
    return begin_targets(context,initial,depth,false);
}
bool ctr_gx2_end_legacy(void *context,const ctr_scene_surface *readback) {
    return resolve_targets(context,readback,NULL,false);
}
bool ctr_gx2_end_depth_legacy(void *context,const ctr_scene_surface *readback,
                              const ctr_scene_depth_surface *depth) {
    if(!depth)return fail(context,CTR_WIIU_GX2_SCENE_ARGUMENT);
    return resolve_targets(context,readback,depth,false);
}
bool ctr_wiiu_gx2_scene_resolve_color(ctr_wiiu_gx2_scene *s,const ctr_scene_surface *readback) {
    return resolve_targets(s,readback,NULL,true);
}
bool ctr_wiiu_gx2_scene_resolve_targets(ctr_wiiu_gx2_scene *s,const ctr_scene_surface *readback,
                                        const ctr_scene_depth_surface *depth) {
    if(!depth)return fail(s,CTR_WIIU_GX2_SCENE_ARGUMENT);
    return resolve_targets(s,readback,depth,true);
}
void ctr_wiiu_gx2_scene_init(ctr_wiiu_gx2_scene *s,GX2ContextState *restore_context) {
    if(s) { memset(s,0,sizeof(*s));s->restore_context=restore_context; }
}
void ctr_gx2_abort_cb(void *context) {
    ctr_wiiu_gx2_scene *s=context;
    direct_pool_state *pool=s?s->direct_pool:NULL;
    if(pool && pool->color_pending && !s->private_state)return;
    ctr_wiiu_gx2_scene_abort(s);
}
ctr_scene_backend ctr_wiiu_gx2_scene_backend(ctr_wiiu_gx2_scene *s) {
    return (ctr_scene_backend){s,ctr_gx2_begin_legacy,ctr_gx2_draw_legacy,ctr_gx2_end_legacy,
                               ctr_gx2_abort_cb,ctr_gx2_begin_depth_legacy,ctr_gx2_end_depth_legacy,
                               ctr_gx2_draw_textures_legacy,ctr_gx2_draw_mips_legacy,
                               CTR_SCENE_BACKEND_DEPTH_FOG,ctr_gx2_draw_transformed_legacy,NULL,NULL};
}
const char *ctr_wiiu_gx2_scene_result_name(ctr_wiiu_gx2_scene_result result) {
    static const char *const names[]={"ok","argument","allocation","layout","shader","wait","busy","unsupported-readback"};
    return (unsigned)result<sizeof(names)/sizeof(*names)?names[result]:"unknown";
}
