/* Original bounded Old-family linear-resource PICA draw adapter. */
#include "ctr_guest_pica.h"
#include "ctr_heap.h"
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
static void diagnostic(ctr_guest *g,const ctr_scene_report *r) {
    g->gsp.last_scene_result=(uint32_t)r->result;
    g->gsp.last_vertex_result=(uint32_t)r->vertex_result;
    g->gsp.last_shader_result=(uint32_t)r->shader_result;
    g->gsp.last_scene_backend_stage=(uint32_t)r->backend_stage;
    g->gsp.last_pica_offset=(uint32_t)r->byte_offset;g->gsp.last_pica_register=r->reg;
    g->gsp.last_pica_result=r->result==CTR_SCENE_OK?CTR_PICA_OK:CTR_PICA_UNSUPPORTED;
}
ctr_gx_result ctr_guest_pica_execute(ctr_guest *g,const void *commands,size_t size,
                                    uint32_t command_address,ctr_gx_completion *c) {
    if(!g || !commands || !c)return CTR_GX_UNSUPPORTED;
    c->signal=false;ctr_scene_report report={0};ctr_pica_report wire={0};
    ctr_gx_result result=CTR_GX_UNSUPPORTED;
    ctr_pica_write *writes=NULL;ctr_scene_state *trial=NULL;
    uint8_t *source=NULL,*raw=NULL,*pixels=NULL;
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
    const unsigned ids[]={0x200,0x11d,0x11e,0x117};uint32_t values[4],bound[4]={0};uint8_t defined[4];
    for(unsigned i=0;i<4;++i) { values[i]=trial->registers[ids[i]];defined[i]=trial->register_defined[ids[i]]; }
    bool draws=false;size_t markers=0,first_draw=0;unsigned draw_register=0;
    for(size_t i=0;i<wire.writes;++i) {
        const ctr_pica_write *w=&writes[i];report.byte_offset=w->byte_offset;report.reg=w->reg;
        for(unsigned k=0;k<4;++k)if(w->reg==ids[k]) {
            values[k]=ctr_pica_merge(values[k],w->value,w->byte_mask);defined[k]|=w->byte_mask;
        }
        if(w->reg==CTR_PICA_FINALIZE)++markers;
        if(w->reg==0x22eu || w->reg==0x22fu) {
            if(w->byte_mask!=15u || w->value!=1u) { report.result=CTR_SCENE_UNSUPPORTED;goto done; }
            for(unsigned k=0;k<4;++k)if(defined[k]!=15u) { report.result=CTR_SCENE_UNSUPPORTED;goto done; }
            if(draws && memcmp(values,bound,sizeof(bound))) { report.result=CTR_SCENE_LAYOUT;goto done; }
            if(!draws) { first_draw=w->byte_offset;draw_register=w->reg; }
            memcpy(bound,values,sizeof(bound));draws=true;
        }
    }
    if(g->gsp.pica.committed_lists==UINT64_MAX || wire.writes>UINT64_MAX-g->gsp.pica.register_writes ||
       markers>UINT64_MAX-g->gsp.pica.finalizations) { report.result=CTR_SCENE_OVERFLOW;goto done; }
    uint32_t resource_address=0,resource_bytes=0,target_address=0,target_bytes=4,color_address=0;
    unsigned width=1,height=1;uint8_t empty[4]={0};
    ctr_scene_resources resources={0,NULL,0};ctr_scene_surface surface={empty,4,1,1,4};
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
        result=linear(g,color_address,target_bytes,CTR_MEM_READ|CTR_MEM_WRITE,&target_address,NULL,c);
        if(result!=CTR_GX_OK) { report.result=CTR_SCENE_LAYOUT;goto done; }
        if(command_alias(g,target_address,target_bytes,command_address,(uint32_t)size)) {
            result=CTR_GX_MEMORY;c->memory=CTR_MEM_OVERLAP;report.result=CTR_SCENE_ALIAS;goto done;
        }
        result=linear(g,(uint32_t)base,1u,CTR_MEM_READ,&resource_address,&resource_bytes,c);
        if(result!=CTR_GX_OK) { report.result=CTR_SCENE_LAYOUT;goto done; }
        /* Exclude the current target from the full stable vertex/index span.
         * Conservative cut also rejects resources beyond an intervening target. */
        if(base<(uint64_t)color_address+target_bytes && color_address<base+resource_bytes) {
            if(color_address<=base) { result=CTR_GX_MEMORY;c->memory=CTR_MEM_OVERLAP;report.result=CTR_SCENE_ALIAS;goto done; }
            resource_bytes=color_address-(uint32_t)base;
        }
        if(resource_bytes>CTR_VERTEX_MAX_SOURCE_BYTES) { result=CTR_GX_UNSUPPORTED;report.result=CTR_SCENE_LIMIT;goto done; }
        result=linear(g,(uint32_t)base,resource_bytes,CTR_MEM_READ,&resource_address,NULL,c);
        if(result!=CTR_GX_OK) { report.result=CTR_SCENE_VERTEX;goto done; }
        source=malloc(resource_bytes);raw=malloc(target_bytes);pixels=malloc(target_bytes);
        if(!source || !raw || !pixels) { result=CTR_GX_NO_MEMORY;report.result=CTR_SCENE_NO_MEMORY;goto done; }
        c->memory=ctr_memory_read_bytes(&g->memory,resource_address,source,resource_bytes);
        if(c->memory==CTR_MEM_OK)c->memory=ctr_memory_read_bytes(&g->memory,target_address,raw,target_bytes);
        if(c->memory!=CTR_MEM_OK) { result=CTR_GX_MEMORY;report.result=CTR_SCENE_LAYOUT;goto done; }
        layout(pixels,raw,width,height,false);
        resources=(ctr_scene_resources){(uint32_t)base,source,resource_bytes};
        surface=(ctr_scene_surface){pixels,target_bytes,width,height,width*4u};
    }
    ctr_scene_result rendered=ctr_scene_execute_target_backend(trial,commands,size,&resources,&surface,
                                                               color_address,g->gsp.pica_backend,&report);
    if(rendered!=CTR_SCENE_OK) { result=rendered==CTR_SCENE_NO_MEMORY?CTR_GX_NO_MEMORY:CTR_GX_UNSUPPORTED;goto done; }
    if(draws) {
        layout(pixels,raw,width,height,true);
        c->memory=ctr_memory_write_bytes(&g->memory,target_address,raw,target_bytes);
        if(c->memory!=CTR_MEM_OK) { result=CTR_GX_MEMORY;report.result=CTR_SCENE_LAYOUT;goto done; }
    }
    /* No fallible operations remain after the one atomic guest byte commit. */
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
    free(writes);free(trial);free(source);free(raw);free(pixels);return result;
}
void ctr_guest_set_scene_backend(ctr_guest *g,const ctr_scene_backend *backend) {
    if(g)g->gsp.pica_backend=backend;
}
