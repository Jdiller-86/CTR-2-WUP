#include "gx2_scene_internal.h"

void ctr_gx2_direct_pool_destroy(ctr_wiiu_gx2_scene *s) {
    direct_pool_state *pool=s?s->direct_pool:NULL;
    if(!pool)return;
    ctr_gx2_dispose(s,pool->gpu_fetch_arena);
    ctr_gx2_dispose(s,pool->gpu_fetch_entries);
    ctr_gx2_dispose(s,pool->gpu_vs_arena);
    ctr_gx2_dispose(s,pool->gpu_vs_entries);
    ctr_gx2_dispose(s,pool->ps_arena);
    ctr_gx2_dispose(s,pool->vs_arena);
    ctr_gx2_dispose(s,pool->depth_target.surface.image);
    ctr_gx2_dispose(s,pool->linear.image);
    ctr_gx2_dispose(s,pool->target.surface.image);
    ctr_gx2_dispose(s,pool->color_candidate.surface.image);
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
        unsigned vs_slots=s->cemu_readback?CEMU_GPU_SHADER_SLOTS:DIRECT_GPU_VS_SLOTS;
        unsigned fetch_slots=s->cemu_readback?CEMU_GPU_SHADER_SLOTS:DIRECT_GPU_FETCH_SLOTS;
        pool->gpu_vs_entries=ctr_gx2_allocate(s,256,vs_slots*sizeof(*pool->gpu_vs_entries));
        pool->gpu_fetch_entries=ctr_gx2_allocate(s,256,fetch_slots*sizeof(*pool->gpu_fetch_entries));
        pool->gpu_vs_arena=ctr_gx2_allocate(s,256,s->cemu_readback?CEMU_GPU_VS_ARENA_BYTES:DIRECT_GPU_VS_ARENA_BYTES);
        pool->gpu_fetch_arena=ctr_gx2_allocate(s,256,fetch_slots*DIRECT_GPU_FETCH_BYTES);
        if(!pool->context || !pool->compiled || !pool->fetch_program ||
           !pool->vertex_ring || !pool->vs_arena || !pool->ps_arena ||
           !pool->gpu_vs_entries || !pool->gpu_fetch_entries || !pool->gpu_vs_arena || !pool->gpu_fetch_arena) {
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
        pool->color_resolved=false;
        ctr_gx2_dispose(s,pool->target.surface.image);pool->target.surface.image=NULL;
        ctr_gx2_dispose(s,pool->color_candidate.surface.image);pool->color_candidate.surface.image=NULL;
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
        /* CPU consumers read the separate linear resolve buffer. */
        if(s->cemu_readback)
            pool->target.surface.resourceFlags|=GX2R_RESOURCE_USAGE_GPU_READ|GX2R_RESOURCE_USAGE_GPU_WRITE;
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
                                  const ctr_scene_depth_surface *depth,bool reuse_pending,bool deferred) {
    direct_pool_state *previous=s->direct_pool;
    if((previous && previous->color_pending)!=reuse_pending ||
       (reuse_pending && (previous->width!=initial->width || previous->height!=initial->height)))
        return ctr_gx2_fail(s,CTR_WIIU_GX2_SCENE_ARGUMENT);
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
    if(reuse_pending && !pool->color_candidate.surface.image) {
        GX2ColorBuffer candidate=pool->target;
        candidate.surface.image=ctr_gx2_allocate(s,candidate.surface.alignment,candidate.surface.imageSize);
        if(!candidate.surface.image)return ctr_gx2_fail(s,CTR_WIIU_GX2_SCENE_ALLOCATION);
        DCFlushRange(candidate.surface.image,candidate.surface.imageSize);
        GX2InitColorBufferRegs(&candidate);
        pool->color_candidate=candidate;
    }
    s->result=CTR_WIIU_GX2_SCENE_OK;
    scene_private *p=ctr_gx2_allocate(s,256,sizeof(*p));
    if(!p)return ctr_gx2_fail(s,CTR_WIIU_GX2_SCENE_ALLOCATION);
    s->private_state=p;p->width=initial->width;p->height=initial->height;
    p->stride=initial->stride;p->size=initial->size;
    p->flip_y=s->cemu_readback;p->depth_enabled=depth!=NULL;p->resident=false;
    p->context=pool->context;p->target=reuse_pending?pool->color_candidate:pool->target;p->linear=pool->linear;
    p->compiled=pool->compiled;p->shader_slots=1u;
    if(depth) {
        p->depth_stride=depth->stride;p->depth_size=depth->size;p->depth_address=depth->address;
        p->depth_format=depth->format;p->depth_bytes=depth->format==3u?4u:2u;
        p->seed_far=clear_value!=0.0f;p->seed_stencil=clear_stencil;
        p->depth_target=pool->depth_target;
    }
    pool->bound_vs=NULL;pool->bound_ps_valid=false;pool->list_bound=false;
    GX2SetContextState(p->context);
    bool reuse_color=pool->color_resolved && pool->color_flipped==p->flip_y;
    pool->color_resolved=false;
    for(unsigned y=0;reuse_color && y<p->height;++y)
        reuse_color=!memcmp((const uint8_t *)p->linear.image+(size_t)y*p->linear.pitch*4u,
                            (const uint8_t *)initial->bytes+(size_t)ctr_gx2_target_row(p,y)*initial->stride,p->width*4u);
    float clear_rgba[4];
    if(reuse_pending) {
        /* A rejected list must leave the last committed GPU target intact. */
        GX2Invalidate(GX2_INVALIDATE_MODE_COLOR_BUFFER,pool->target.surface.image,pool->target.surface.imageSize);
        GX2CopySurface(&pool->target.surface,0,0,&p->target.surface,0,0);
    } else if(!reuse_color && color_clear_seed(initial,clear_rgba)) {
        GX2ClearColor(&p->target,clear_rgba[0],clear_rgba[1],clear_rgba[2],clear_rgba[3]);
    } else if(!reuse_color) {
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
    /* The deferred caller draws next in this context, then restores on end or abort. */
    if(deferred)p->draw_context_active=true;
    else if(!ctr_gx2_restore(s)) { ctr_gx2_release(s,p);return false; }
    ++s->begins;return true;
}

bool ctr_gx2_direct_pixel_program(ctr_wiiu_gx2_scene *s,scene_private *p,const ctr_tev_program *tev,
                                  unsigned mask,bool texture2_coord1,unsigned implicit_lod,
                                  unsigned explicit_lod,unsigned filter_mask,unsigned mixed_filters,
                                  bool fog,bool transformed,const ctr_scene_light *light,
                                  bool depth_plane,pixel_blend_mode blend,const uint8_t *key,size_t key_size) {
    direct_pool_state *pool=s->direct_pool;
    if(pool->last_ps) {
        const direct_ps_entry *last=&pool->ps_entries[pool->last_ps-1u];
        if(last->key_size==key_size && !memcmp(last->key,key,key_size)) {
            p->pixel=last->shader;++s->direct_shader_hits;return true;
        }
    }
    uint32_t h=UINT32_C(2166136261);
    for(size_t i=0;i<key_size;++i)h=(h^key[i])*UINT32_C(16777619);
    const unsigned index_mask=DIRECT_PS_INDEX_SLOTS-1u;
    unsigned slot=(h^(h>>16u))&index_mask;
    /* Entries live until pool destruction, and at least half the index stays empty. */
    for(;pool->ps_index[slot];slot=(slot+1u)&index_mask) {
        const direct_ps_entry *e=&pool->ps_entries[pool->ps_index[slot]-1u];
        if(e->hash==h && e->key_size==key_size && !memcmp(e->key,key,key_size)) {
            pool->last_ps=pool->ps_index[slot];
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
    pool->ps_index[slot]=(uint16_t)pool->ps_count;
    pool->last_ps=pool->ps_count;
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

static void direct_list_bind_init(scene_private *p,direct_pool_state *pool) {
    GX2SetColorBuffer(&p->target,GX2_RENDER_TARGET_0);
    if(p->depth_enabled)GX2SetDepthBuffer(&p->depth_target);
    GX2SetShaderModeEx(GX2_SHADER_MODE_UNIFORM_REGISTER,64,64,0,0,184,192);
    p->uploaded_pixel_vectors=0;
    p->raster_current=false;
    p->merger_current=false;
    p->bound_texture_mask=p->bound_sampler_mask=0;
    GX2SetAlphaToMask(false,GX2_ALPHA_TO_MASK_MODE_NON_DITHERED);
    GX2SetStreamOutEnable(false);
    pool->list_bound=true;pool->bound_vs=NULL;pool->bound_fetch=NULL;pool->bound_ps_valid=false;
}

void ctr_gx2_direct_bind_pipeline(scene_private *p,direct_pool_state *pool,unsigned direct_vs_id) {
    if(!pool->list_bound) {
        direct_list_bind_init(p,pool);
        GX2SetCullOnlyControl(p->flip_y?GX2_FRONT_FACE_CW:GX2_FRONT_FACE_CCW,false,false);
    }
    const GX2VertexShader *vs_ptr=&pool->vs[direct_vs_id];
    if(pool->bound_vs!=vs_ptr) {
        GX2SetVertexShader(vs_ptr);pool->bound_vs=vs_ptr;
        GX2SetCullOnlyControl(p->flip_y?GX2_FRONT_FACE_CW:GX2_FRONT_FACE_CCW,false,false);
    }
    if(pool->bound_fetch!=&pool->fetch) { GX2SetFetchShader(&pool->fetch);pool->bound_fetch=&pool->fetch; }
    if(!pool->bound_ps_valid || pool->bound_ps.program!=p->pixel.program) {
        GX2SetPixelShader(&p->pixel);
        pool->bound_ps=p->pixel;
        pool->bound_ps_valid=true;
    }
}
static uint64_t fnv1a_bytes(uint64_t h,const void *data,size_t bytes) {
    const uint8_t *b=data;
    for(size_t i=0;i<bytes;++i)h=(h^b[i])*UINT64_C(1099511628211);
    return h;
}

static bool gpu_vs_options_match(const direct_gpu_vs_entry *entry,const ctr_shader_program *program,
                                 const ctr_gx2_shader_game_options *options) {
    return entry->entry==program->entry && entry->boolean_defined==program->boolean_defined &&
           entry->boolean_uniforms==program->boolean_uniforms && entry->integer_defined==program->integer_defined &&
           !memcmp(entry->integer_uniforms,program->integer_uniforms,sizeof(entry->integer_uniforms)) &&
           !memcmp(entry->uniform_defined,program->uniform_defined,sizeof(entry->uniform_defined)) &&
           !memcmp(&entry->opt,options,sizeof(*options));
}

static const direct_gpu_vs_entry *remember_gpu_vs(direct_pool_state *pool,const direct_gpu_vs_entry *entry,
                                                   const ctr_shader_program *program) {
    memcpy(pool->last_vs_code,program->code,sizeof(pool->last_vs_code));
    memcpy(pool->last_vs_descriptors,program->descriptors,sizeof(pool->last_vs_descriptors));
    memcpy(pool->last_vs_code_defined,program->code_defined,sizeof(pool->last_vs_code_defined));
    memcpy(pool->last_vs_descriptors_defined,program->descriptor_defined,sizeof(pool->last_vs_descriptors_defined));
    pool->last_gpu_vs=entry;
    return entry;
}

static const direct_gpu_vs_entry *direct_gpu_vs_ensure(ctr_wiiu_gx2_scene *s,scene_private *p,
                                                       const ctr_scene_gpu_draw *draw,
                                                       uint16_t fixed_input_mask) {
    direct_pool_state *pool=s->direct_pool;
    const ctr_shader_program *prog=&draw->state->shader;
    const ctr_scene_light *light=draw->light && draw->light->active?draw->light:NULL;
    uint16_t out_mask=(uint16_t)draw->state->registers[0x2bdu];
    unsigned out_count=draw->state->registers[0x04fu];
    if(out_count>7u)return NULL;
    ctr_gx2_shader_game_options opt={0};
    opt.output_mask=out_mask;opt.fixed_mask=fixed_input_mask;
    opt.output_count=out_count;
    for(unsigned i=0;i<out_count;++i)opt.output_maps[i]=draw->state->registers[0x050u+i];
    if(light) { opt.light_active=true;opt.light_needs_view=light->needs_view; }
    for(size_t i=0;i<draw->attribute_count;++i) {
        const ctr_vertex_attribute *a=&draw->attributes[i];
        if(a->input>=16u)return NULL;
        if(a->format==0u || (a->format==2u && !s->cemu_readback)) {
            opt.int_signed_mask|=(uint16_t)(1u<<a->input);
            opt.int_components[a->input]=(uint8_t)a->components;
        } else if(a->format==1u) {
            opt.int_unsigned_mask|=(uint16_t)(1u<<a->input);
            opt.int_components[a->input]=(uint8_t)a->components;
        }
    }
    const direct_gpu_vs_entry *last=pool->last_gpu_vs;
    /* A new upload can reuse the same guest state object with different code. */
    if(last && gpu_vs_options_match(last,prog,&opt) &&
       ((prog->code_generation && last->code_generation==prog->code_generation) ||
        (!memcmp(pool->last_vs_code,prog->code,sizeof(pool->last_vs_code)) &&
         !memcmp(pool->last_vs_descriptors,prog->descriptors,sizeof(pool->last_vs_descriptors)) &&
         !memcmp(pool->last_vs_code_defined,prog->code_defined,sizeof(pool->last_vs_code_defined)) &&
         !memcmp(pool->last_vs_descriptors_defined,prog->descriptor_defined,sizeof(pool->last_vs_descriptors_defined)))))
        return last;
    uint64_t h=UINT64_C(14695981039346656037);
    h=fnv1a_bytes(h,&prog->entry,sizeof(prog->entry));
    h=fnv1a_bytes(h,&prog->boolean_defined,sizeof(prog->boolean_defined));
    h=fnv1a_bytes(h,&prog->boolean_uniforms,sizeof(prog->boolean_uniforms));
    h=fnv1a_bytes(h,&prog->integer_defined,sizeof(prog->integer_defined));
    h=fnv1a_bytes(h,prog->integer_uniforms,sizeof(prog->integer_uniforms));
    h=fnv1a_bytes(h,prog->uniform_defined,sizeof(prog->uniform_defined));
    h=fnv1a_bytes(h,&opt,sizeof(opt));
    uint64_t code_hash2=UINT64_C(0x9e3779b97f4a7c15);
    for(unsigned i=0;i<CTR_SHADER_MAX_CODE;++i)if(prog->code_defined[i]) {
        uint64_t v=((uint64_t)i<<32)|prog->code[i];
        h=fnv1a_bytes(h,&v,sizeof(v));
        code_hash2=(code_hash2^v)*UINT64_C(0xbf58476d1ce4e5b9);
    }
    for(unsigned i=0;i<CTR_SHADER_MAX_DESCRIPTORS;++i)if(prog->descriptor_defined[i]) {
        uint64_t v=((uint64_t)i<<32)|prog->descriptors[i];
        h=fnv1a_bytes(h,&v,sizeof(v));
        code_hash2=(code_hash2^v)*UINT64_C(0x94d049bb133111eb);
    }
    for(unsigned i=0;i<pool->gpu_vs_count;++i) {
        const direct_gpu_vs_entry *e=&pool->gpu_vs_entries[i];
        if(e->hash==h && e->code_hash2==code_hash2 && gpu_vs_options_match(e,prog,&opt))
            return remember_gpu_vs(pool,e,prog);
    }
    /* Cemu caches shader addresses, so keep compiled programs at stable addresses. */
    if(pool->gpu_vs_count>=(s->cemu_readback?CEMU_GPU_SHADER_SLOTS:DIRECT_GPU_VS_SLOTS))return NULL;
    p->compiled=pool->compiled;
    if(ctr_gx2_shader_compile_game(prog,&opt,p->compiled)!=CTR_GX2_SHADER_OK)return NULL;
    size_t aligned=(p->compiled->size+255u)&~(size_t)255u;
    size_t arena_bytes=s->cemu_readback?CEMU_GPU_VS_ARENA_BYTES:DIRECT_GPU_VS_ARENA_BYTES;
    if(aligned>arena_bytes-pool->gpu_vs_arena_used)return NULL;
    p->vertex_program=(uint8_t *)pool->gpu_vs_arena+pool->gpu_vs_arena_used;
    ctr_gx2_upload_vertex_program(p,light,false,0u);
    DCFlushRange(p->vertex_program,p->vertex.size);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER,p->vertex_program,p->vertex.size);
    direct_gpu_vs_entry *e=&pool->gpu_vs_entries[pool->gpu_vs_count++];
    e->hash=h;e->code_hash2=code_hash2;e->code_generation=prog->code_generation;e->entry=prog->entry;
    e->boolean_defined=prog->boolean_defined;e->boolean_uniforms=prog->boolean_uniforms;
    e->integer_defined=prog->integer_defined;
    memcpy(e->integer_uniforms,prog->integer_uniforms,sizeof(e->integer_uniforms));
    memcpy(e->uniform_defined,prog->uniform_defined,sizeof(e->uniform_defined));
    e->opt=opt;
    e->shader=p->vertex;e->input_mask=p->compiled->input_mask;
    memcpy(e->uniform_used,p->compiled->uniform_used,sizeof(e->uniform_used));
    e->pica_instructions=p->compiled->pica_instructions;
    pool->gpu_vs_arena_used+=aligned;
    ++s->direct_shader_compiles;
    return remember_gpu_vs(pool,e,prog);
}

static bool gpu_attrib_format(unsigned type,unsigned components,bool cemu,unsigned *fmt,unsigned *swap) {
    static const unsigned t8[4]={GX2_ATTRIB_TYPE_8,GX2_ATTRIB_TYPE_8_8,GX2_ATTRIB_TYPE_8_8_8_8,GX2_ATTRIB_TYPE_8_8_8_8};
    static const unsigned t16[4]={GX2_ATTRIB_TYPE_16,GX2_ATTRIB_TYPE_16_16,GX2_ATTRIB_TYPE_16_16_16_16,GX2_ATTRIB_TYPE_16_16_16_16};
    static const unsigned t32[4]={GX2_ATTRIB_TYPE_32_FLOAT,GX2_ATTRIB_TYPE_32_32_FLOAT,GX2_ATTRIB_TYPE_32_32_32_FLOAT,GX2_ATTRIB_TYPE_32_32_32_32_FLOAT};
    if(!components || components>4u)return false;
    if(cemu) {
        /* Cemu decodes these wider formats. The fetch selector masks the extra lanes. */
        if(type<=1u)components=4u;
        else if(type==2u) {
            *fmt=GX2_ATTRIB_FLAG_SCALED|GX2_ATTRIB_FLAG_SIGNED|GX2_ATTRIB_TYPE_16_16_16_16;
            *swap=GX2_ENDIAN_SWAP_NONE;return true;
        } else if(components==1u)components=2u;
    }
    if(type==0u) { *fmt=GX2_ATTRIB_FLAG_INTEGER|GX2_ATTRIB_FLAG_SIGNED|t8[components-1u];*swap=GX2_ENDIAN_SWAP_NONE; }
    else if(type==1u) { *fmt=GX2_ATTRIB_FLAG_INTEGER|t8[components-1u];*swap=GX2_ENDIAN_SWAP_NONE; }
    /* The raw ring copies guest bytes unchanged. Only host-native vertices need a swap. */
    else if(type==2u) { *fmt=GX2_ATTRIB_FLAG_INTEGER|GX2_ATTRIB_FLAG_SIGNED|t16[components-1u];*swap=GX2_ENDIAN_SWAP_NONE; }
    else if(type==3u) { *fmt=GX2_ATTRIB_FLAG_SCALED|t32[components-1u];*swap=GX2_ENDIAN_SWAP_NONE; }
    else return false;
    return true;
}

static const GX2FetchShader *direct_gpu_fetch_ensure(ctr_wiiu_gx2_scene *s,const ctr_scene_gpu_draw *draw) {
    direct_pool_state *pool=s->direct_pool;
    if(!draw->attribute_count || draw->attribute_count>16u)return NULL;
    uint64_t h=UINT64_C(14695981039346656037);
    h=fnv1a_bytes(h,draw->attributes,draw->attribute_count*sizeof(*draw->attributes));
    for(unsigned i=0;i<pool->gpu_fetch_count;++i) {
        const direct_gpu_fetch_entry *e=&pool->gpu_fetch_entries[i];
        if(e->hash==h && e->attribute_count==(unsigned)draw->attribute_count &&
           !memcmp(e->attributes,draw->attributes,draw->attribute_count*sizeof(*draw->attributes)))
            return &e->shader;
    }
    if(pool->gpu_fetch_count>=(s->cemu_readback?CEMU_GPU_SHADER_SLOTS:DIRECT_GPU_FETCH_SLOTS))return NULL;
    uint32_t fsize=GX2CalcFetchShaderSizeEx((uint32_t)draw->attribute_count,
                                            GX2_FETCH_SHADER_TESSELLATION_NONE,GX2_TESSELLATION_MODE_DISCRETE);
    if(!fsize || fsize>DIRECT_GPU_FETCH_BYTES)return NULL;
    GX2AttribStream streams[16];memset(streams,0,sizeof(streams));
    for(size_t i=0;i<draw->attribute_count;++i) {
        const ctr_vertex_attribute *a=&draw->attributes[i];
        unsigned fmt=0,swap=0;
        if(!gpu_attrib_format(a->format,a->components,s->cemu_readback,&fmt,&swap))return NULL;
        streams[i].location=a->input;
        streams[i].buffer=a->stream;
        streams[i].offset=a->offset;
        streams[i].format=fmt;
        streams[i].type=GX2_ATTRIB_INDEX_PER_VERTEX;
        streams[i].mask=GX2_SEL_MASK(GX2_SQ_SEL_X,
            a->components>=2u?GX2_SQ_SEL_Y:GX2_SQ_SEL_0,
            a->components>=3u?GX2_SQ_SEL_Z:GX2_SQ_SEL_0,
            a->components>=4u?GX2_SQ_SEL_W:GX2_SQ_SEL_1);
        streams[i].endianSwap=swap;
    }
    void *prog=(uint8_t *)pool->gpu_fetch_arena+(size_t)pool->gpu_fetch_count*DIRECT_GPU_FETCH_BYTES;
    direct_gpu_fetch_entry *e=&pool->gpu_fetch_entries[pool->gpu_fetch_count++];
    e->hash=h;e->attribute_count=(unsigned)draw->attribute_count;
    memcpy(e->attributes,draw->attributes,draw->attribute_count*sizeof(*draw->attributes));
    GX2InitFetchShaderEx(&e->shader,prog,(uint32_t)draw->attribute_count,streams,
                         GX2_FETCH_SHADER_TESSELLATION_NONE,GX2_TESSELLATION_MODE_DISCRETE);
    DCFlushRange(prog,fsize);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER,prog,fsize);
    return &e->shader;
}

bool ctr_gx2_draw_gpu_legacy(void *context,const ctr_scene_gpu_draw *draw) {
    ctr_wiiu_gx2_scene *s=context;scene_private *p=s?s->private_state:NULL;
    direct_pool_state *pool=s?s->direct_pool:NULL;
    if(!p || !pool || !draw || !draw->vertex_count || draw->vertex_count%3u ||
       !draw->stream_count || draw->stream_count>12u || draw->total_inputs>16u)return false;
    unsigned fetches=pool->gpu_fetch_count;
    uint64_t programs=s->direct_shader_compiles;
    uint16_t fixed_input_mask=0;
    uint32_t fixed_regs[16][4]={{0}};
    for(unsigned a=0;a<draw->total_inputs;++a)if(draw->fixed_mask&(1u<<a)) {
        unsigned slot=draw->fixed_slots[a];
        if(slot>=16u)return false;
        fixed_input_mask|=(uint16_t)(1u<<slot);
        for(unsigned lane=0;lane<4u;++lane) {
            float v=draw->fixed_values[a][lane];
            memcpy(&fixed_regs[slot][lane],&v,4u);
        }
    }
    const direct_gpu_vs_entry *vs=direct_gpu_vs_ensure(s,p,draw,fixed_input_mask);
    if(!vs)return false;
    const GX2FetchShader *fs=direct_gpu_fetch_ensure(s,draw);
    if(!fs)return false;

    unsigned min_idx=draw->vertex_offset,max_idx=draw->vertex_offset+draw->vertex_count-1u;
    if(draw->indexed) {
        if(!draw->indices)return false;
        unsigned fmt=draw->indices->format,width=fmt+1u;
        if((uint64_t)draw->indices->offset+(uint64_t)draw->vertex_count*width>draw->indices->size)return false;
        const uint8_t *ib=(const uint8_t *)draw->indices->bytes+draw->indices->offset;
        min_idx=UINT32_MAX;max_idx=0;
        if(fmt==0u) {
            for(unsigned i=0;i<draw->vertex_count;++i) {
                unsigned v=ib[i];
                if(v<min_idx)min_idx=v;
                if(v>max_idx)max_idx=v;
            }
        } else {
            for(unsigned i=0;i<draw->vertex_count;++i) {
                unsigned v=(unsigned)ib[i*2u]|((unsigned)ib[i*2u+1u]<<8);
                if(v<min_idx)min_idx=v;
                if(v>max_idx)max_idx=v;
            }
        }
    }
    unsigned span_verts=max_idx-min_idx+1u;
    size_t stream_bytes[12]={0},copy_bytes[12]={0},vertex_bytes=0;
    const uint8_t *source_bytes[12]={NULL};
    size_t fetch_padding=s->cemu_readback?8u:4u;
    uint8_t *ring=pool->vertex_ring;
    for(size_t si=0;si<draw->stream_count;++si) {
        const ctr_vertex_stream *st=&draw->streams[si];
        if((uint64_t)st->offset+(uint64_t)(max_idx+1u)*st->stride>st->size)return false;
        copy_bytes[si]=(size_t)span_verts*st->stride;
        source_bytes[si]=(const uint8_t *)st->bytes+st->offset+(size_t)min_idx*st->stride;
        stream_bytes[si]=(copy_bytes[si]+fetch_padding+255u)&~(size_t)255u;
        vertex_bytes+=stream_bytes[si];
    }
    size_t reuse_offset=0;
    bool reuse=false;
    for(unsigned ci=0;ci<pool->vertex_upload_count;++ci) {
        if(pool->vertex_upload_cache[ci].streams!=draw->stream_count)continue;
        bool match=true;
        size_t off=0;
        for(size_t si=0;si<draw->stream_count;++si) {
            if(pool->vertex_upload_cache[ci].sources[si]!=source_bytes[si] ||
               pool->vertex_upload_cache[ci].sizes[si]!=copy_bytes[si] ||
               memcmp(ring+pool->vertex_upload_cache[ci].offset+off,source_bytes[si],copy_bytes[si])) {
                match=false;break;
            }
            off+=stream_bytes[si];
        }
        if(match) { reuse=true;reuse_offset=pool->vertex_upload_cache[ci].offset;break; }
    }
    size_t idx_bytes=draw->indexed?(((size_t)draw->vertex_count*2u+255u)&~(size_t)255u):0u;
    if(vertex_bytes+idx_bytes>DIRECT_VERTEX_RING_BYTES)return false;
    size_t total_ring=(reuse?0u:vertex_bytes)+idx_bytes;
    if(pool->vertex_cursor+total_ring>DIRECT_VERTEX_RING_BYTES) {
        if(!ctr_gx2_retire(s,p))return false;
        pool->vertex_cursor=0;
        pool->vertex_upload_count=0;
        pool->vertex_upload_cursor=0;
        reuse=false;
        total_ring=vertex_bytes+idx_bytes;
    }

    uint8_t *ring_base=ring+pool->vertex_cursor;
    uint8_t *stream_base=reuse?ring+reuse_offset:ring_base;
    size_t ring_off=0;
    void *stream_ptrs[12]={NULL};
    for(size_t si=0;si<draw->stream_count;++si) {
        stream_ptrs[si]=stream_base+ring_off;
        if(!reuse) {
            memcpy(stream_ptrs[si],source_bytes[si],copy_bytes[si]);
            memset((uint8_t *)stream_ptrs[si]+copy_bytes[si],0,fetch_padding);
        }
        ring_off+=stream_bytes[si];
    }
    uint8_t *idx_ptr=NULL;
    if(draw->indexed) {
        idx_ptr=ring_base+(reuse?0u:vertex_bytes);
        const uint8_t *ib=(const uint8_t *)draw->indices->bytes+draw->indices->offset;
        uint16_t *dst_idx=(uint16_t *)idx_ptr;
        if(draw->indices->format==0u) {
            for(unsigned i=0;i<draw->vertex_count;++i)
                dst_idx[i]=(uint16_t)(ib[i]-min_idx);
        } else {
            for(unsigned i=0;i<draw->vertex_count;++i)
                dst_idx[i]=(uint16_t)(((unsigned)ib[i*2u]|((unsigned)ib[i*2u+1u]<<8))-min_idx);
        }
    }
    if(total_ring) {
        DCFlushRange(ring_base,(unsigned)total_ring);
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER,ring_base,(unsigned)total_ring);
    }

    bool context_active=p->draw_context_active;
    bool skip_draw=false;
    if(!ctr_gx2_direct_setup_state(s,p,draw->state,draw->samplers,draw->texture_mask,draw->light,
                                   draw->texture_leases,draw->validated_tev,&skip_draw))
        return false;
    if(skip_draw) { ++s->draws;s->result=CTR_WIIU_GX2_SCENE_OK;return true; }
    if(!reuse) {
        unsigned slot=pool->vertex_upload_cursor;
        pool->vertex_upload_cache[slot].offset=pool->vertex_cursor;
        pool->vertex_upload_cache[slot].streams=(unsigned)draw->stream_count;
        memcpy(pool->vertex_upload_cache[slot].sizes,copy_bytes,draw->stream_count*sizeof(copy_bytes[0]));
        memcpy((void *)pool->vertex_upload_cache[slot].sources,source_bytes,draw->stream_count*sizeof(source_bytes[0]));
        pool->vertex_upload_cursor=(slot+1u)&15u;
        if(pool->vertex_upload_count<16u)++pool->vertex_upload_count;
    }
    pool->vertex_cursor+=total_ring;

    if(!pool->list_bound)direct_list_bind_init(p,pool);
    bool reuse_uniforms=context_active && pool->bound_vs==&vs->shader;
    if(pool->bound_fetch!=fs) { GX2SetFetchShader(fs);pool->bound_fetch=fs; }
    if(pool->bound_vs!=&vs->shader) { GX2SetVertexShader(&vs->shader);pool->bound_vs=&vs->shader; }
    if(!pool->bound_ps_valid || pool->bound_ps.program!=p->pixel.program) {
        GX2SetPixelShader(&p->pixel);
        pool->bound_ps=p->pixel;pool->bound_ps_valid=true;
    }
    unsigned cull=draw->state->registers[0x040u]&3u;
    /* Cemu's mirrored viewport already accounts for the winding change. */
    GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW,cull==1u,cull==2u);
    for(size_t si=0;si<draw->stream_count;++si)
        GX2SetAttribBuffer((uint32_t)si,(uint32_t)stream_bytes[si],(uint32_t)draw->streams[si].stride,stream_ptrs[si]);
    /* The compiler marks every defined vector when relative addressing can read it. */
    for(unsigned u=0;u<CTR_SHADER_UNIFORMS;) {
        unsigned first=u++;
        if(!vs->uniform_used[first])continue;
        while(u<CTR_SHADER_UNIFORMS && vs->uniform_used[u])++u;
        const uint32_t *values=draw->state->shader.uniforms[first];
        size_t bytes=(u-first)*sizeof(draw->state->shader.uniforms[0]);
        if(reuse_uniforms && !memcmp(pool->uploaded_vertex_uniforms[first],values,bytes))continue;
        GX2SetVertexUniformReg(first*4u,(u-first)*4u,draw->state->shader.uniforms[first]);
        memcpy(pool->uploaded_vertex_uniforms[first],values,bytes);
    }
    if(fixed_input_mask)
        for(unsigned slot=0;slot<16u;++slot)if(fixed_input_mask&(1u<<slot))
            GX2SetVertexUniformReg((96u+slot)*4u,4u,fixed_regs[slot]);

    if(draw->indexed)GX2DrawIndexedEx(GX2_PRIMITIVE_MODE_TRIANGLES,draw->vertex_count,GX2_INDEX_TYPE_U16,idx_ptr,0u,1u);
    else GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES,draw->vertex_count,0u,1u);

    for(unsigned unit=0;unit<3u;++unit)
        if(p->texture_lease[unit])p->texture_lease[unit]->submitted=true;
    p->pending=true;
    /* Cemu's first draw with newly uploaded shader code needs a retirement fence. */
    if(s->cemu_readback && (fetches!=pool->gpu_fetch_count || programs!=s->direct_shader_compiles) &&
       !ctr_gx2_retire(s,p))return false;
    ++s->draws;s->shader_instructions+=vs->pica_instructions;
    s->result=CTR_WIIU_GX2_SCENE_OK;
    return true;
}

ctr_scene_backend ctr_wiiu_gx2_scene_backend_direct(ctr_wiiu_gx2_scene *s) {
    static const ctr_scene_deferred_color deferred_color={
        ctr_gx2_begin_deferred_color,ctr_gx2_end_deferred_color,ctr_gx2_resolve_deferred_color,&ctr_gx2_scanout
    };
    if(s)s->direct_hardware=true;
    unsigned features=CTR_SCENE_BACKEND_DEPTH_FOG|CTR_SCENE_BACKEND_DIRECT|CTR_SCENE_BACKEND_STABLE_INPUTS;
    if(s && s->cemu_readback)features|=CTR_SCENE_BACKEND_IN_PLACE_END;
    return (ctr_scene_backend){s,ctr_gx2_begin_legacy,ctr_gx2_draw_legacy,ctr_gx2_end_legacy,
                               ctr_gx2_abort_cb,ctr_gx2_begin_depth_legacy,ctr_gx2_end_depth_legacy,
                               ctr_gx2_draw_textures_legacy,ctr_gx2_draw_mips_legacy,
                               features,
                               ctr_gx2_draw_transformed_legacy,ctr_gx2_draw_gpu_legacy,
                               s && s->cemu_readback && !s->observe_readback?&deferred_color:NULL};
}

void ctr_wiiu_gx2_scene_set_depth_keep(void *context,bool keep) {
    ctr_wiiu_gx2_scene *s=context;
    if(s)s->direct_keep_depth=keep;
}
