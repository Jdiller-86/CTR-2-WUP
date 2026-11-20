#include "gx2_scene_internal.h"

void ctr_gx2_direct_pool_destroy(ctr_wiiu_gx2_scene *s) {
    direct_pool_state *pool=s?s->direct_pool:NULL;
    if(!pool)return;
    ctr_gx2_dispose(s,pool->ps_arena);
    ctr_gx2_dispose(s,pool->vs_arena);
    ctr_gx2_dispose(s,pool->depth_target.surface.image);
    ctr_gx2_dispose(s,pool->linear.image);
    ctr_gx2_dispose(s,pool->target.surface.image);
    ctr_gx2_dispose(s,pool->vertex_ring);
    ctr_gx2_dispose(s,pool->fetch_program);
    ctr_gx2_dispose(s,pool->compiled);
    ctr_gx2_dispose(s,pool->context);
    ctr_gx2_dispose(s,pool);
    s->direct_pool=NULL;
}

static bool color_clear_seed(const ctr_scene_surface *initial,float rgba[4]) {
    const uint8_t *first=initial->bytes;
    uint32_t word;memcpy(&word,first,4u);
    size_t row_bytes=(size_t)initial->width*4u;
    for(size_t x=4u;x<row_bytes;x+=4u) {
        uint32_t w;memcpy(&w,first+x,4u);
        if(w!=word)return false;
    }
    for(unsigned y=1u;y<initial->height;++y)
        if(memcmp(first,first+(size_t)y*initial->stride,row_bytes))return false;
    rgba[0]=(float)first[0]*(1.0f/255.0f);
    rgba[1]=(float)first[1]*(1.0f/255.0f);
    rgba[2]=(float)first[2]*(1.0f/255.0f);
    rgba[3]=(float)first[3]*(1.0f/255.0f);
    return true;
}

static direct_pool_state *direct_pool_ensure(ctr_wiiu_gx2_scene *s,unsigned width,unsigned height,
                                             bool has_depth,unsigned depth_format) {
    direct_pool_state *pool=s->direct_pool;
    if(!pool) {
        pool=ctr_gx2_allocate(s,256,sizeof(*pool));
        if(!pool) { ctr_gx2_fail(s,CTR_WIIU_GX2_SCENE_ALLOCATION);return NULL; }
        pool->context=ctr_gx2_allocate(s,256,sizeof(*pool->context));
        pool->compiled=ctr_gx2_allocate(s,256,sizeof(*pool->compiled));
        uint32_t fsize=GX2CalcFetchShaderSizeEx(7,GX2_FETCH_SHADER_TESSELLATION_NONE,GX2_TESSELLATION_MODE_DISCRETE);
        if(!fsize || fsize>MAX_FETCH_BYTES) {
            s->direct_pool=pool;ctr_gx2_direct_pool_destroy(s);
            ctr_gx2_fail(s,CTR_WIIU_GX2_SCENE_LAYOUT);return NULL;
        }
        pool->fetch_size=fsize;
        pool->fetch_program=ctr_gx2_allocate(s,256,fsize);
        pool->vertex_ring=ctr_gx2_allocate(s,256,DIRECT_VERTEX_RING_BYTES);
        pool->vs_arena=ctr_gx2_allocate(s,256,DIRECT_VS_SLOTS*TRANSFORMED_PROGRAM_BYTES);
        pool->ps_arena=ctr_gx2_allocate(s,256,DIRECT_PS_ARENA_BYTES);
        if(!pool->context || !pool->compiled || !pool->fetch_program ||
           !pool->vertex_ring || !pool->vs_arena || !pool->ps_arena) {
            s->direct_pool=pool;ctr_gx2_direct_pool_destroy(s);
            ctr_gx2_fail(s,CTR_WIIU_GX2_SCENE_ALLOCATION);return NULL;
        }
        GX2AttribStream attributes[7];memset(attributes,0,sizeof(attributes));
        for(unsigned input=0;input<7u;++input) {
            attributes[input].location=input;attributes[input].buffer=0;
            attributes[input].offset=input*4u*sizeof(float);
            attributes[input].format=GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32;
            attributes[input].type=GX2_ATTRIB_INDEX_PER_VERTEX;
            attributes[input].mask=GX2_SEL_MASK(GX2_SQ_SEL_X,GX2_SQ_SEL_Y,GX2_SQ_SEL_Z,GX2_SQ_SEL_W);
            attributes[input].endianSwap=GX2_ENDIAN_SWAP_8_IN_32;
        }
        GX2InitFetchShaderEx(&pool->fetch,pool->fetch_program,7,attributes,
                            GX2_FETCH_SHADER_TESSELLATION_NONE,GX2_TESSELLATION_MODE_DISCRETE);
        DCFlushRange(pool->fetch_program,fsize);
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER,pool->fetch_program,fsize);
        GX2SetupContextStateEx(pool->context,true);
        s->direct_pool=pool;
    }
    unsigned color_format=GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
    if(!pool->target.surface.image || pool->width!=width || pool->height!=height) {
        ctr_gx2_dispose(s,pool->target.surface.image);pool->target.surface.image=NULL;
        ctr_gx2_dispose(s,pool->linear.image);pool->linear.image=NULL;
        ctr_gx2_dispose(s,pool->depth_target.surface.image);pool->depth_target.surface.image=NULL;
        pool->has_depth=false;pool->resident_depth_valid=false;
        memset(&pool->target,0,sizeof(pool->target));
        memset(&pool->linear,0,sizeof(pool->linear));
        memset(&pool->depth_target,0,sizeof(pool->depth_target));
        if(!ctr_gx2_layout(&pool->target.surface,width,height,GX2_TILE_MODE_TILED_2D_THIN1,color_format) ||
           !ctr_gx2_layout(&pool->linear,width,height,GX2_TILE_MODE_LINEAR_SPECIAL,color_format) ||
           (uint64_t)pool->linear.pitch*height*4u>pool->linear.imageSize) {
            ctr_gx2_fail(s,CTR_WIIU_GX2_SCENE_LAYOUT);return NULL;
        }
        pool->target.surface.image=ctr_gx2_allocate(s,pool->target.surface.alignment,pool->target.surface.imageSize);
        pool->linear.image=ctr_gx2_allocate(s,pool->linear.alignment,pool->linear.imageSize);
        if(!pool->target.surface.image || !pool->linear.image) {
            ctr_gx2_fail(s,CTR_WIIU_GX2_SCENE_ALLOCATION);return NULL;
        }
        DCFlushRange(pool->target.surface.image,pool->target.surface.imageSize);
        DCFlushRange(pool->linear.image,pool->linear.imageSize);
        pool->target.viewNumSlices=1;GX2InitColorBufferRegs(&pool->target);
        pool->width=width;pool->height=height;
    }
    if(has_depth && (!pool->depth_target.surface.image || !pool->has_depth || pool->depth_format!=depth_format)) {
        ctr_gx2_dispose(s,pool->depth_target.surface.image);pool->depth_target.surface.image=NULL;
        pool->resident_depth_valid=false;
        memset(&pool->depth_target,0,sizeof(pool->depth_target));
        if(!ctr_gx2_depth_layout(&pool->depth_target.surface,width,height,GX2_TILE_MODE_TILED_2D_THIN1,depth_format)) {
            ctr_gx2_fail(s,CTR_WIIU_GX2_SCENE_LAYOUT);return NULL;
        }
        pool->depth_target.surface.image=ctr_gx2_allocate(s,pool->depth_target.surface.alignment,pool->depth_target.surface.imageSize);
        if(!pool->depth_target.surface.image) { ctr_gx2_fail(s,CTR_WIIU_GX2_SCENE_ALLOCATION);return NULL; }
        DCFlushRange(pool->depth_target.surface.image,pool->depth_target.surface.imageSize);
        pool->depth_target.viewNumSlices=1;GX2InitDepthBufferRegs(&pool->depth_target);
        GX2InitDepthBufferHiZEnable(&pool->depth_target,false);
        pool->depth_format=depth_format;pool->has_depth=true;
    }
    return pool;
}

bool ctr_gx2_direct_begin_targets(ctr_wiiu_gx2_scene *s,const ctr_scene_surface *initial,
                                  const ctr_scene_depth_surface *depth) {
    direct_pool_state *pool=direct_pool_ensure(s,initial->width,initial->height,
                                               depth!=NULL,depth?depth->format:0u);
    if(!pool)return false;
    bool skip_depth_clear=depth && s->direct_keep_depth && pool->resident_depth_valid &&
        pool->has_depth && pool->resident_depth_address==depth->address &&
        pool->resident_depth_width==initial->width && pool->resident_depth_height==initial->height &&
        pool->resident_depth_format==depth->format;
    float clear_value=0.0f;uint8_t clear_stencil=0;
    if(depth && !skip_depth_clear && !ctr_gx2_depth_clear_seed(depth,&clear_value,&clear_stencil))
        return ctr_gx2_fail(s,CTR_WIIU_GX2_SCENE_UNSUPPORTED_READBACK);
    s->result=CTR_WIIU_GX2_SCENE_OK;
    scene_private *p=ctr_gx2_allocate(s,256,sizeof(*p));
    if(!p)return ctr_gx2_fail(s,CTR_WIIU_GX2_SCENE_ALLOCATION);
    s->private_state=p;p->width=initial->width;p->height=initial->height;
    p->stride=initial->stride;p->size=initial->size;
    p->flip_y=s->cemu_readback;p->depth_enabled=depth!=NULL;p->resident=false;
    p->context=pool->context;p->target=pool->target;p->linear=pool->linear;
    p->compiled=pool->compiled;p->shader_slots=1u;
    if(depth) {
        p->depth_stride=depth->stride;p->depth_size=depth->size;p->depth_address=depth->address;
        p->depth_format=depth->format;p->depth_bytes=depth->format==3u?4u:2u;
        p->seed_far=clear_value!=0.0f;p->seed_stencil=clear_stencil;
        p->depth_target=pool->depth_target;
    }
    pool->bound_vs=NULL;pool->bound_ps_valid=false;pool->list_bound=false;
    GX2SetContextState(p->context);
    float clear_rgba[4];
    if(color_clear_seed(initial,clear_rgba)) {
        GX2ClearColor(&p->target,clear_rgba[0],clear_rgba[1],clear_rgba[2],clear_rgba[3]);
    } else {
        for(unsigned y=0;y<p->height;++y)
            memcpy((uint8_t *)p->linear.image+(size_t)y*p->linear.pitch*4u,
                   (const uint8_t *)initial->bytes+(size_t)ctr_gx2_target_row(p,y)*initial->stride,p->width*4u);
        DCFlushRange(p->linear.image,p->linear.imageSize);
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE,p->linear.image,p->linear.imageSize);
        GX2CopySurface(&p->linear,0,0,&p->target.surface,0,0);
    }
    p->pending=true;
    if(depth && !skip_depth_clear) {
        GX2ClearDepthStencilEx(&p->depth_target,clear_value,clear_stencil,
            p->depth_format==CTR_SCENE_DEPTH_D24S8?GX2_CLEAR_FLAGS_BOTH:GX2_CLEAR_FLAGS_DEPTH);
        pool->resident_depth_valid=true;
        pool->resident_depth_address=depth->address;
        pool->resident_depth_width=p->width;pool->resident_depth_height=p->height;
        pool->resident_depth_format=p->depth_format;
    } else if(skip_depth_clear) {
        ++s->direct_depth_skips;
    }
    GX2Invalidate(GX2_INVALIDATE_MODE_COLOR_BUFFER,p->target.surface.image,p->target.surface.imageSize);
    if(depth)GX2Invalidate(GX2_INVALIDATE_MODE_DEPTH_BUFFER,p->depth_target.surface.image,p->depth_target.surface.imageSize);
    if(!ctr_gx2_restore(s)) { ctr_gx2_release(s,p);return false; }
    ++s->begins;return true;
}

bool ctr_gx2_direct_pixel_program(ctr_wiiu_gx2_scene *s,scene_private *p,const ctr_tev_program *tev,
                                  unsigned mask,bool texture2_coord1,unsigned implicit_lod,
                                  unsigned explicit_lod,unsigned filter_mask,unsigned mixed_filters,
                                  bool fog,bool transformed,const ctr_scene_light *light,
                                  bool depth_plane,pixel_blend_mode blend,const uint8_t *key,size_t key_size) {
    direct_pool_state *pool=s->direct_pool;
    uint64_t h=UINT64_C(14695981039346656037);
    for(size_t i=0;i<key_size;++i)h=(h^key[i])*UINT64_C(1099511628211);
    for(unsigned i=0;i<pool->ps_count;++i) {
        direct_ps_entry *e=&pool->ps_entries[i];
        if(e->hash==h && e->key_size==key_size && !memcmp(e->key,key,key_size)) {
            p->pixel=e->shader;++s->direct_shader_hits;return true;
        }
    }
    p->pixel_program=pool->scratch_ps;
    if(!ctr_gx2_pixel_shader(p,tev,mask,texture2_coord1,implicit_lod,explicit_lod,filter_mask,mixed_filters,fog,transformed,light,depth_plane,blend)) {
        ctr_gx2_cache_add(s,&s->pixel_translation_failures,1u);return false;
    }
    size_t sz=p->pixel.size,aligned_sz=(sz+255u)&~(size_t)255u;
    if(pool->ps_count>=DIRECT_PS_SLOTS || aligned_sz>DIRECT_PS_ARENA_BYTES-pool->ps_arena_used) {
        ctr_gx2_cache_add(s,&s->cache_allocation_failures,1u);
        return false;
    }
    void *dest=(uint8_t *)pool->ps_arena+pool->ps_arena_used;
    memcpy(dest,pool->scratch_ps,sz);
    DCFlushRange(dest,(unsigned)sz);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER,dest,(unsigned)sz);
    direct_ps_entry *e=&pool->ps_entries[pool->ps_count++];
    e->hash=h;e->key_size=key_size;memcpy(e->key,key,key_size);
    e->shader=p->pixel;e->shader.program=dest;
    p->pixel=e->shader;pool->ps_arena_used+=aligned_sz;
    ++s->direct_shader_compiles;
    return true;
}

void ctr_gx2_direct_prepare_draw(ctr_wiiu_gx2_scene *s,scene_private *p,unsigned direct_vs_id,
                                 unsigned coordinates,const ctr_scene_light *light) {
    direct_pool_state *pool=s->direct_pool;
    if(!pool->vs_valid[direct_vs_id]) {
        p->compiled=pool->compiled;
        p->vertex_program=(uint8_t *)pool->vs_arena+(size_t)direct_vs_id*TRANSFORMED_PROGRAM_BYTES;
        ctr_gx2_transformed_program(p->compiled,coordinates,light,false,0u);
        ctr_gx2_upload_vertex_program(p,light,false,0u);
        p->vertex.regs.sq_vtx_semantic_clear=UINT32_C(0xffffff80);
        p->vertex.regs.num_sq_vtx_semantic=7u;
        DCFlushRange(p->vertex_program,p->vertex.size);
        pool->vs[direct_vs_id]=p->vertex;
        pool->vs_valid[direct_vs_id]=true;
    }
}

void ctr_gx2_direct_bind_pipeline(scene_private *p,direct_pool_state *pool,unsigned direct_vs_id) {
    if(!pool->list_bound) {
        GX2SetColorBuffer(&p->target,GX2_RENDER_TARGET_0);
        if(p->depth_enabled)GX2SetDepthBuffer(&p->depth_target);
        GX2SetShaderModeEx(GX2_SHADER_MODE_UNIFORM_REGISTER,64,64,0,0,184,192);
        GX2SetFetchShader(&pool->fetch);
        GX2SetAlphaToMask(false,GX2_ALPHA_TO_MASK_MODE_NON_DITHERED);
        GX2SetStreamOutEnable(false);
        GX2SetCullOnlyControl(p->flip_y?GX2_FRONT_FACE_CW:GX2_FRONT_FACE_CCW,false,false);
        pool->list_bound=true;pool->bound_vs=NULL;pool->bound_ps_valid=false;
    }
    const GX2VertexShader *vs_ptr=&pool->vs[direct_vs_id];
    if(pool->bound_vs!=vs_ptr) { GX2SetVertexShader(vs_ptr);pool->bound_vs=vs_ptr; }
    if(!pool->bound_ps_valid || pool->bound_ps.program!=p->pixel.program) {
        GX2SetPixelShader(&p->pixel);
        pool->bound_ps=p->pixel;
        pool->bound_ps_valid=true;
    }
}

ctr_scene_backend ctr_wiiu_gx2_scene_backend_direct(ctr_wiiu_gx2_scene *s) {
    if(s)s->direct_hardware=true;
    return (ctr_scene_backend){s,ctr_gx2_begin_legacy,ctr_gx2_draw_legacy,ctr_gx2_end_legacy,
                               ctr_gx2_abort_cb,ctr_gx2_begin_depth_legacy,ctr_gx2_end_depth_legacy,
                               ctr_gx2_draw_textures_legacy,ctr_gx2_draw_mips_legacy,
                               CTR_SCENE_BACKEND_DEPTH_FOG|CTR_SCENE_BACKEND_DIRECT,
                               ctr_gx2_draw_transformed_legacy};
}

void ctr_wiiu_gx2_scene_set_depth_keep(void *context,bool keep) {
    ctr_wiiu_gx2_scene *s=context;
    if(s)s->direct_keep_depth=keep;
}
