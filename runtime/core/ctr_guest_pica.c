/* Original bounded Old-family linear-resource PICA draw adapter. */
#include "ctr_guest_pica.h"
#include "ctr_heap.h"
#include "ctr_texture.h"
#include <stdlib.h>
#include <string.h>

#define PHYSICAL_BASE UINT32_C(0x20000000)
#define PHYSICAL_END UINT32_C(0x28000000)

static ctr_gx_result linear(ctr_guest *g,uint32_t physical,uint32_t size,unsigned permissions,
                            uint32_t *address,uint32_t *available,ctr_gx_completion *c) {
    c->detail=physical;c->memory=CTR_MEM_UNMAPPED;
    if(physical<PHYSICAL_BASE || physical>=PHYSICAL_END || !size ||
       (uint64_t)physical+size>PHYSICAL_END)return CTR_GX_MEMORY;
    uint32_t virtual_address=CTR_LINEAR_BASE+(physical-PHYSICAL_BASE);
    const ctr_guest_allocation *allocation=NULL;
    for(unsigned i=0;i<CTR_GUEST_MAX_ALLOCATIONS;++i) {
        const ctr_guest_allocation *a=&g->allocations[i];
        if(a->storage && a->linear && virtual_address>=a->base &&
           (uint64_t)virtual_address+size<=(uint64_t)a->base+a->size) { allocation=a;break; }
    }
    if(!allocation)return CTR_GX_MEMORY;
    ctr_gx_result result=ctr_gx_check_range(g,virtual_address,size,permissions,c);
    if(result!=CTR_GX_OK)return result;
    /* A physical span is continuous only when every mapping keeps the owned
     * allocation's storage identity, including after permission splits. */
    uint64_t at=virtual_address,end=at+size;
    while(at<end) {
        const ctr_mapping *mapping=NULL;
        for(size_t i=0;i<g->memory.count;++i) {
            const ctr_mapping *m=&g->memory.maps[i];
            if(at>=m->base && at<(uint64_t)m->base+m->size) { mapping=m;break; }
        }
        if(!mapping || mapping->storage+(size_t)(at-mapping->base)!=
            allocation->storage+(size_t)(at-allocation->base)) {
            c->memory=CTR_MEM_OVERLAP;return CTR_GX_MEMORY;
        }
        uint64_t next=(uint64_t)mapping->base+mapping->size;at=next<end?next:end;
    }
    *address=virtual_address;
    if(available)*available=allocation->size-(virtual_address-allocation->base);
    return CTR_GX_OK;
}
static bool command_alias(const ctr_guest *g,uint32_t target,uint32_t bytes,
                           uint32_t commands,uint32_t command_bytes) {
    /* Target storage is owned/continuous and already validated by linear(). */
    const uint8_t *storage=NULL;
    for(unsigned i=0;i<CTR_GUEST_MAX_ALLOCATIONS;++i) {
        const ctr_guest_allocation *a=&g->allocations[i];
        if(a->storage && target>=a->base && (uint64_t)target+bytes<=(uint64_t)a->base+a->size)
            storage=a->storage+(target-a->base);
    }
    if(!storage)return true;
    uintptr_t first=(uintptr_t)storage,last=first+bytes;
    uint64_t end=(uint64_t)commands+command_bytes;
    for(size_t i=0;i<g->memory.count;++i) {
        const ctr_mapping *m=&g->memory.maps[i];uint64_t stop=(uint64_t)m->base+m->size;
        if(commands>=stop || m->base>=end)continue;
        uint64_t begin=commands>m->base?commands:m->base,finish=end<stop?end:stop;
        uintptr_t p=(uintptr_t)m->storage+(size_t)(begin-m->base);
        if(p<last && first<p+(size_t)(finish-begin))return true;
    }
    return false;
}
static size_t tiled(unsigned x,unsigned y,unsigned width) {
    unsigned morton=0;
    for(unsigned bit=0;bit<3;++bit)morton|=((x>>bit)&1u)<<(bit*2u)|((y>>bit)&1u)<<(bit*2u+1u);
    return (((size_t)(y/8u)*(width/8u)+x/8u)*64u+morton)*4u;
}
static void layout(uint8_t *linear_bytes,uint8_t *raw,unsigned width,unsigned height,bool encode) {
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
        uint8_t *p=linear_bytes+((size_t)y*width+x)*4u,*q=raw+tiled(x,y,width);
        if(encode)for(unsigned lane=0;lane<4;++lane)q[lane]=p[3u-lane];
        else for(unsigned lane=0;lane<4;++lane)p[lane]=q[3u-lane];
    }
}
/* D16 uses the same 8x8 Morton indices as color, with two little-endian
 * bytes per sample. Keep raw bytes explicit on both host endian orders. */
static void depth_layout(uint8_t *pixels,uint8_t *raw,unsigned width,unsigned height,bool encode) {
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
        uint8_t *p=pixels+((size_t)y*width+x)*2u,*q=raw+tiled(x,y,width)/2u;
        if(encode) { q[0]=p[0];q[1]=p[1]; }
        else { p[0]=q[0];p[1]=q[1]; }
    }
}
static void diagnostic(ctr_guest *g,const ctr_scene_report *r) {
    g->gsp.last_scene_result=(uint32_t)r->result;
    g->gsp.last_vertex_result=(uint32_t)r->vertex_result;
    g->gsp.last_shader_result=(uint32_t)r->shader_result;
    g->gsp.last_scene_backend_stage=(uint32_t)r->backend_stage;
    g->gsp.last_pica_offset=(uint32_t)r->byte_offset;g->gsp.last_pica_register=r->reg;
    g->gsp.last_pica_result=r->result==CTR_SCENE_OK?CTR_PICA_OK:CTR_PICA_UNSUPPORTED;
}
static bool texture_range(ctr_scene_resource_span *spans,unsigned *count,uint32_t address,size_t size) {
    uint64_t first=address,last=first+size;
    for(unsigned i=0;i<*count;) {
        uint64_t a=spans[i].base_address,b=a+spans[i].size;
        if(first<b && a<last) {
            if(a<first)first=a;
            if(b>last)last=b;
            spans[i]=spans[--*count];i=0;
        } else ++i;
    }
    if(*count==CTR_SCENE_MAX_TEXTURE_SPANS || last-first>CTR_SCENE_MAX_TEXTURE_SOURCE_BYTES)return false;
    spans[(*count)++]=(ctr_scene_resource_span){(uint32_t)first,NULL,(size_t)(last-first)};
    return true;
}
ctr_gx_result ctr_guest_pica_execute(ctr_guest *g,const void *commands,size_t size,
                                    uint32_t command_address,ctr_gx_completion *c) {
    if(!g || !commands || !c)return CTR_GX_UNSUPPORTED;
    c->signal=false;ctr_scene_report report={0};ctr_pica_report wire={0};
    ctr_gx_result result=CTR_GX_UNSUPPORTED;
    ctr_pica_write *writes=NULL;ctr_scene_state *trial=NULL;
    uint8_t *source=NULL,*raw=NULL,*pixels=NULL,*depth_raw=NULL,*depth_pixels=NULL;
    ctr_scene_resource_span textures[CTR_SCENE_MAX_TEXTURE_SPANS]={0};unsigned texture_count=0;
    ctr_pica_result inspected=ctr_pica_inspect(commands,size,CTR_PICA_MAX_WRITES,NULL,0,&wire);
    if(inspected!=CTR_PICA_OK || !size || (size&15u)) {
        report.result=CTR_SCENE_WIRE;report.byte_offset=wire.parameter_offset;report.reg=wire.reg;goto done;
    }
    writes=malloc(wire.writes*sizeof(*writes));trial=malloc(sizeof(*trial));
    if(!writes || !trial) { result=CTR_GX_NO_MEMORY;report.result=CTR_SCENE_NO_MEMORY;goto done; }
    ctr_pica_inspect(commands,size,CTR_PICA_MAX_WRITES,writes,wire.writes,&wire);
    if(g->gsp.pica_scene)*trial=*g->gsp.pica_scene;
    else ctr_scene_init(trial);
    memcpy(trial->registers,g->gsp.pica.registers,sizeof(trial->registers));
    trial->lists=g->gsp.pica.committed_lists;
    /* Select actual bindings at the first draw, rather than guessing from the
     * final state. Later draws must match; post-draw setup may prepare a next list. */
    const unsigned ids[]={0x200,0x11d,0x11e,0x117,0x116,0x11c};
    uint32_t values[6],bound[6]={0};uint8_t defined[6];
    for(unsigned i=0;i<6;++i) { values[i]=trial->registers[ids[i]];defined[i]=trial->register_defined[ids[i]]; }
    const unsigned texture_ids[]={0x080,0x082,0x085,0x08e};uint32_t texture_values[4];uint8_t texture_defined[4];
    for(unsigned i=0;i<4;++i) { texture_values[i]=trial->registers[texture_ids[i]];texture_defined[i]=trial->register_defined[texture_ids[i]]; }
    bool draws=false;size_t markers=0,first_draw=0;unsigned draw_register=0;
    for(size_t i=0;i<wire.writes;++i) {
        const ctr_pica_write *w=&writes[i];report.byte_offset=w->byte_offset;report.reg=w->reg;
        for(unsigned k=0;k<6;++k)if(w->reg==ids[k]) {
            values[k]=ctr_pica_merge(values[k],w->value,w->byte_mask);defined[k]|=w->byte_mask;
        }
        for(unsigned k=0;k<4;++k)if(w->reg==texture_ids[k]) {
            texture_values[k]=ctr_pica_merge(texture_values[k],w->value,w->byte_mask);texture_defined[k]|=w->byte_mask;
        }
        if(w->reg==CTR_PICA_FINALIZE)++markers;
        if(w->reg==0x22eu || w->reg==0x22fu) {
            if(w->byte_mask!=15u || w->value!=1u) { report.result=CTR_SCENE_UNSUPPORTED;goto done; }
            for(unsigned k=0;k<4;++k)if(defined[k]!=15u) { report.result=CTR_SCENE_UNSUPPORTED;goto done; }
            if(draws && memcmp(values,bound,sizeof(bound))) { report.result=CTR_SCENE_LAYOUT;goto done; }
            if(!draws) { first_draw=w->byte_offset;draw_register=w->reg; }
            memcpy(bound,values,sizeof(bound));draws=true;
            if(texture_values[0]&1u) {
                for(unsigned k=0;k<4;++k)if(texture_defined[k]!=15u) { report.result=CTR_SCENE_LAYOUT;goto done; }
                if(texture_values[0]!=1u || (texture_values[1]&~UINT32_C(0x07ff07ff)) ||
                   (texture_values[2]&UINT32_C(0xf0000000)) || (texture_values[3]&~15u)) {
                    report.result=CTR_SCENE_UNSUPPORTED;goto done;
                }
                size_t texture_bytes;
                if(ctr_texture_size(texture_values[3],(texture_values[1]>>16)&2047u,
                    texture_values[1]&2047u,&texture_bytes)!=CTR_TEXTURE_OK) { report.result=CTR_SCENE_LAYOUT;goto done; }
                if(!texture_range(textures,&texture_count,texture_values[2]*8u,texture_bytes)) {
                    report.result=CTR_SCENE_LIMIT;goto done;
                }
            }
        }
    }
    if(g->gsp.pica.committed_lists==UINT64_MAX || wire.writes>UINT64_MAX-g->gsp.pica.register_writes ||
       markers>UINT64_MAX-g->gsp.pica.finalizations) { report.result=CTR_SCENE_OVERFLOW;goto done; }
    uint32_t resource_address=0,resource_bytes=0,target_address=0,target_bytes=4,color_address=0;
    uint32_t depth_address=0,depth_virtual=0,depth_bytes=0;
    unsigned width=1,height=1;uint8_t empty[4]={0};
    ctr_scene_resources resources={0};ctr_scene_surface surface={empty,4,1,1,4};
    ctr_scene_depth_surface depth={0};
    if(draws) {
        report.byte_offset=first_draw;report.reg=draw_register;
        if((bound[0]&UINT32_C(0xe0000001)) || (bound[1]&UINT32_C(0xf0000000)) || bound[3] ||
           (bound[2]&~UINT32_C(0x013ff7ff)) || !(bound[2]&UINT32_C(0x01000000))) {
            report.result=CTR_SCENE_UNSUPPORTED;goto done;
        }
        uint64_t base=(uint64_t)(bound[0]>>1)*16u;
        if(base>UINT32_MAX) { report.result=CTR_SCENE_OVERFLOW;goto done; }
        color_address=bound[1]*8u;width=bound[2]&2047u;height=((bound[2]>>12)&1023u)+1u;
        if(!width || width>512u || height>512u || (width&7u) || (height&7u)) {
            report.result=CTR_SCENE_LAYOUT;goto done;
        }
        target_bytes=width*height*4u;
        if(bound[4] || (bound[5]&UINT32_C(0xf0000000))) { report.result=CTR_SCENE_UNSUPPORTED;goto done; }
        depth_address=bound[5]*8u;
        if(depth_address)depth_bytes=width*height*2u;
        result=linear(g,color_address,target_bytes,CTR_MEM_READ|CTR_MEM_WRITE,&target_address,NULL,c);
        if(result!=CTR_GX_OK) { report.result=CTR_SCENE_LAYOUT;goto done; }
        if(command_alias(g,target_address,target_bytes,command_address,(uint32_t)size)) {
            result=CTR_GX_MEMORY;c->memory=CTR_MEM_OVERLAP;report.result=CTR_SCENE_ALIAS;goto done;
        }
        if(depth_bytes) {
            result=linear(g,depth_address,depth_bytes,CTR_MEM_READ|CTR_MEM_WRITE,&depth_virtual,NULL,c);
            if(result!=CTR_GX_OK) { report.result=CTR_SCENE_LAYOUT;goto done; }
            if(command_alias(g,depth_virtual,depth_bytes,command_address,(uint32_t)size) ||
               command_alias(g,target_address,target_bytes,depth_virtual,depth_bytes)) {
                result=CTR_GX_MEMORY;c->memory=CTR_MEM_OVERLAP;report.result=CTR_SCENE_ALIAS;goto done;
            }
        }
        result=linear(g,(uint32_t)base,1u,CTR_MEM_READ,&resource_address,&resource_bytes,c);
        if(result!=CTR_GX_OK) { report.result=CTR_SCENE_LAYOUT;goto done; }
        /* Exclude the current target from the full stable vertex/index span.
         * Conservative cut also rejects resources beyond an intervening target. */
        if(base<(uint64_t)color_address+target_bytes && color_address<base+resource_bytes) {
            if(color_address<=base) { result=CTR_GX_MEMORY;c->memory=CTR_MEM_OVERLAP;report.result=CTR_SCENE_ALIAS;goto done; }
            resource_bytes=color_address-(uint32_t)base;
        }
        if(depth_bytes && base<(uint64_t)depth_address+depth_bytes && depth_address<base+resource_bytes) {
            if(depth_address<=base) { result=CTR_GX_MEMORY;c->memory=CTR_MEM_OVERLAP;report.result=CTR_SCENE_ALIAS;goto done; }
            resource_bytes=depth_address-(uint32_t)base;
        }
        if(resource_bytes>CTR_VERTEX_MAX_SOURCE_BYTES) { result=CTR_GX_UNSUPPORTED;report.result=CTR_SCENE_LIMIT;goto done; }
        result=linear(g,(uint32_t)base,resource_bytes,CTR_MEM_READ,&resource_address,NULL,c);
        if(result!=CTR_GX_OK) { report.result=CTR_SCENE_VERTEX;goto done; }
        if(command_alias(g,target_address,target_bytes,resource_address,resource_bytes) ||
           (depth_bytes && command_alias(g,depth_virtual,depth_bytes,resource_address,resource_bytes))) {
            result=CTR_GX_MEMORY;c->memory=CTR_MEM_OVERLAP;report.result=CTR_SCENE_ALIAS;goto done;
        }
        source=malloc(resource_bytes);raw=malloc(target_bytes);pixels=malloc(target_bytes);
        if(!source || !raw || !pixels) { result=CTR_GX_NO_MEMORY;report.result=CTR_SCENE_NO_MEMORY;goto done; }
        c->memory=ctr_memory_read_bytes(&g->memory,resource_address,source,resource_bytes);
        if(c->memory==CTR_MEM_OK)c->memory=ctr_memory_read_bytes(&g->memory,target_address,raw,target_bytes);
        if(c->memory!=CTR_MEM_OK) { result=CTR_GX_MEMORY;report.result=CTR_SCENE_LAYOUT;goto done; }
        layout(pixels,raw,width,height,false);
        if(depth_bytes) {
            depth_raw=malloc(depth_bytes);depth_pixels=malloc(depth_bytes);
            if(!depth_raw || !depth_pixels) { result=CTR_GX_NO_MEMORY;report.result=CTR_SCENE_NO_MEMORY;goto done; }
            c->memory=ctr_memory_read_bytes(&g->memory,depth_virtual,depth_raw,depth_bytes);
            if(c->memory!=CTR_MEM_OK) { result=CTR_GX_MEMORY;report.result=CTR_SCENE_LAYOUT;goto done; }
            depth_layout(depth_pixels,depth_raw,width,height,false);
            depth=(ctr_scene_depth_surface){depth_pixels,depth_bytes,width,height,width*2u,depth_address};
        }
        size_t total_texture_bytes=0;
        for(unsigned i=0;i<texture_count;) {
            uint64_t first=textures[i].base_address,last=first+textures[i].size;
            if(first>=base && last<=base+resource_bytes) { textures[i]=textures[--texture_count];continue; }
            /* A partially overlapping external descriptor is ambiguous to the
             * owned-span API. Contained descriptors use the geometry snapshot. */
            if(first<base+resource_bytes && base<last) {
                result=CTR_GX_UNSUPPORTED;report.result=CTR_SCENE_ALIAS;goto done;
            }
            uint32_t texture_address;
            if(textures[i].size>CTR_SCENE_MAX_TEXTURE_SOURCE_BYTES-total_texture_bytes) {
                result=CTR_GX_UNSUPPORTED;report.result=CTR_SCENE_LIMIT;goto done;
            }
            total_texture_bytes+=textures[i].size;
            result=linear(g,(uint32_t)first,(uint32_t)textures[i].size,CTR_MEM_READ,&texture_address,NULL,c);
            if(result!=CTR_GX_OK) { report.result=CTR_SCENE_LAYOUT;goto done; }
            if(command_alias(g,target_address,target_bytes,texture_address,(uint32_t)textures[i].size) ||
               (depth_bytes && command_alias(g,depth_virtual,depth_bytes,texture_address,(uint32_t)textures[i].size))) {
                result=CTR_GX_MEMORY;c->memory=CTR_MEM_OVERLAP;report.result=CTR_SCENE_ALIAS;goto done;
            }
            uint8_t *snapshot=malloc(textures[i].size);
            if(!snapshot) { result=CTR_GX_NO_MEMORY;report.result=CTR_SCENE_NO_MEMORY;goto done; }
            textures[i].bytes=snapshot;
            c->memory=ctr_memory_read_bytes(&g->memory,texture_address,snapshot,textures[i].size);
            if(c->memory!=CTR_MEM_OK) { result=CTR_GX_MEMORY;report.result=CTR_SCENE_LAYOUT;goto done; }
            ++i;
        }
        resources=(ctr_scene_resources){(uint32_t)base,source,resource_bytes,textures,texture_count};
        surface=(ctr_scene_surface){pixels,target_bytes,width,height,width*4u};
    }
    ctr_scene_result rendered=ctr_scene_execute_targets_backend(trial,commands,size,&resources,&surface,
                                                               color_address,depth_bytes?&depth:NULL,g->gsp.pica_backend,&report);
    if(rendered!=CTR_SCENE_OK) { result=rendered==CTR_SCENE_NO_MEMORY?CTR_GX_NO_MEMORY:CTR_GX_UNSUPPORTED;goto done; }
    if(draws) {
        layout(pixels,raw,width,height,true);
        ctr_memory_write_range targets[2]={{target_address,raw,target_bytes},{depth_virtual,depth_raw,depth_bytes}};
        if(depth_bytes)depth_layout(depth_pixels,depth_raw,width,height,true);
        c->memory=ctr_memory_write_ranges(&g->memory,targets,depth_bytes?2u:1u);
        if(c->memory!=CTR_MEM_OK) { result=CTR_GX_MEMORY;report.result=CTR_SCENE_LAYOUT;goto done; }
    }
    /* No fallible operations remain after the atomic color/depth guest commit. */
    if(g->gsp.pica_scene)*g->gsp.pica_scene=*trial;
    else { g->gsp.pica_scene=trial;trial=NULL; }
    const ctr_scene_state *state=g->gsp.pica_scene;
    memcpy(g->gsp.pica.registers,state->registers,sizeof(g->gsp.pica.registers));
    ++g->gsp.pica.committed_lists;g->gsp.pica.register_writes+=wire.writes;g->gsp.pica.finalizations+=markers;
    g->gsp.pica_draws=state->draws;g->gsp.pica_triangles=state->triangles;
    g->gsp.pica_fragments=state->fragments;g->gsp.pica_shader_instructions=state->shader_instructions;
    result=CTR_GX_OK;
done:
    diagnostic(g,&report);
    if(result==CTR_GX_UNSUPPORTED)c->detail=command_address+(report.byte_offset<size?(uint32_t)report.byte_offset:0u);
    for(unsigned i=0;i<texture_count;++i)free((void *)textures[i].bytes);
    free(writes);free(trial);free(source);free(raw);free(pixels);free(depth_raw);free(depth_pixels);return result;
}
void ctr_guest_set_scene_backend(ctr_guest *g,const ctr_scene_backend *backend) {
    if(g)g->gsp.pica_backend=backend;
}
