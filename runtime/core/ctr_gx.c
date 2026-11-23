/* Original bounded synchronous GX operations; no emulator or hardware backend. */
#include "ctr_gx.h"
#include "ctr_gsp.h"
#include "ctr_transfer.h"
#include "ctr_guest_pica.h"
#include "ctr_resource_cache.h"
#include <stdlib.h>
#include <string.h>
#define LIMIT (4u*1024u*1024u)
uint8_t *ctr_gx_vram_backing(const ctr_guest *g,uint32_t address,uint32_t size) {
    uint8_t *base=g->storage[CTR_VRAM_STORAGE_INDEX];
    if(!base || !size || address<CTR_VRAM_BASE || (uint64_t)address+size>CTR_VRAM_BASE+CTR_VRAM_BYTES)return NULL;
    return base+(address-CTR_VRAM_BASE);
}
void ctr_gx_write_target(ctr_guest *g,uint32_t address,const uint8_t *bytes,uint32_t size) {
    uint8_t *target=ctr_gx_vram_backing(g,address,size);
    if(target) {
        memcpy(target,bytes,size);
        (void)ctr_memory_backing_changed(&g->memory,target,size);
    } else {
        (void)ctr_memory_write_bytes(&g->memory,address,bytes,size);
    }
}
ctr_gx_result ctr_gx_check_range(ctr_guest *g,uint32_t address,uint32_t size,unsigned permissions,ctr_gx_completion *c) {
    uint8_t *video=ctr_gx_vram_backing(g,address,size);
    unsigned access=video?(permissions&~CTR_MEM_WRITE)|CTR_MEM_READ:permissions;
    c->detail=address;c->memory=ctr_memory_check_range(&g->memory,address,size,access);
    if(c->memory!=CTR_MEM_OK)return CTR_GX_MEMORY;
    if(video) {
        uint64_t end=(uint64_t)address+size;
        for(size_t i=0;i<g->memory.count;++i) {
            const ctr_mapping *m=&g->memory.maps[i];uint64_t stop=(uint64_t)m->base+m->size;
            if(address>=stop || m->base>=end)continue;
            uint32_t first=address>m->base?address:m->base;
            if(m->storage+(first-m->base)!=video+(first-address)) {
                c->memory=CTR_MEM_OVERLAP;return CTR_GX_MEMORY;
            }
        }
    }
    if((permissions&CTR_MEM_WRITE) && g->gsp.registered && size) {
        const ctr_guest_shared *relay=&g->shared[g->gsp.relay_shared];uintptr_t base=(uintptr_t)relay->storage;
        uint64_t end=(uint64_t)address+size;
        for(size_t i=0;i<g->memory.count;++i) {
            const ctr_mapping *m=&g->memory.maps[i];uint64_t stop=(uint64_t)m->base+m->size;
            if(address>=stop || m->base>=end)continue;
            uint64_t first=address>m->base?address:m->base,last=end<stop?end:stop;
            uintptr_t p=(uintptr_t)m->storage+(size_t)(first-m->base);
            if(p<base+relay->size && base<p+(size_t)(last-first)) { c->memory=CTR_MEM_OVERLAP;return CTR_GX_MEMORY; }
        }
    }
    return CTR_GX_OK;
}
static ctr_gx_result current(ctr_guest *g,uint32_t address,uint32_t size,ctr_gx_completion *c) {
    if(ctr_memory_resolve_watches(&g->memory,address,size,false))return CTR_GX_OK;
    c->detail=address;c->memory=CTR_MEM_PENDING;return CTR_GX_MEMORY;
}
static ctr_gx_result checked(ctr_guest *g,uint32_t address,uint32_t size,unsigned permissions,ctr_gx_completion *c) {
    ctr_gx_result result=ctr_gx_check_range(g,address,size,permissions,c);
    return result==CTR_GX_OK?current(g,address,size,c):result;
}
static ctr_gx_result copied(ctr_guest *g,uint32_t source,uint32_t dest,uint32_t size,ctr_gx_completion *c) {
    if(!size || size>LIMIT)return CTR_GX_UNSUPPORTED;
    ctr_gx_result r=checked(g,source,size,CTR_MEM_READ,c);if(r!=CTR_GX_OK)return r;
    r=checked(g,dest,size,CTR_MEM_WRITE,c);if(r!=CTR_GX_OK)return r;
    uint8_t *bytes=malloc(size);if(!bytes)return CTR_GX_NO_MEMORY;
    ctr_memory_read_bytes(&g->memory,source,bytes,size);ctr_gx_write_target(g,dest,bytes,size);
    free(bytes);return CTR_GX_OK;
}
static ctr_gx_result fill(ctr_guest *g,const uint32_t *w,ctr_gx_completion *c) {
    uint8_t *bytes[2]={NULL,NULL};unsigned widths[2]={0,0};uint32_t sizes[2]={0,0};ctr_gx_result result=CTR_GX_OK;
    for(unsigned i=0;i<2;++i) {
        uint32_t begin=w[1+i*3],end=w[3+i*3],control=(w[7]>>(i*16))&65535;
        if(!begin) { if(end) { result=CTR_GX_UNSUPPORTED;break; }continue; }
        unsigned depth=(control>>8)&3;
        if(end<=begin || (begin&7) || (end&7) || !(control&1) || (control&~0x301u) || depth==3 || end-begin>LIMIT) { result=CTR_GX_UNSUPPORTED;break; }
        widths[i]=depth==0?2:depth==1?3:4;sizes[i]=end-begin;
        if(sizes[i]%widths[i]) { result=CTR_GX_UNSUPPORTED;break; }
        result=ctr_gx_check_range(g,begin,sizes[i],CTR_MEM_WRITE,c);if(result!=CTR_GX_OK)break;
        bytes[i]=malloc(sizes[i]);if(!bytes[i]) { result=CTR_GX_NO_MEMORY;break; }
        if(widths[i]==4u) {
            uint8_t b0=(uint8_t)w[2+i*3],b1=(uint8_t)(w[2+i*3]>>8),
                b2=(uint8_t)(w[2+i*3]>>16),b3=(uint8_t)(w[2+i*3]>>24);
            for(uint32_t k=0;k<sizes[i];k+=4u) {
                bytes[i][k]=b0;bytes[i][k+1]=b1;bytes[i][k+2]=b2;bytes[i][k+3]=b3;
            }
        } else if(widths[i]==2u) {
            uint8_t b0=(uint8_t)w[2+i*3],b1=(uint8_t)(w[2+i*3]>>8);
            for(uint32_t k=0;k<sizes[i];k+=2u) { bytes[i][k]=b0;bytes[i][k+1]=b1; }
        } else {
            uint8_t b0=(uint8_t)w[2+i*3],b1=(uint8_t)(w[2+i*3]>>8),b2=(uint8_t)(w[2+i*3]>>16);
            for(uint32_t k=0;k<sizes[i];k+=3u) { bytes[i][k]=b0;bytes[i][k+1]=b1;bytes[i][k+2]=b2; }
        }
    }
    const ctr_scene_scanout_state *scanout=ctr_guest_scanout_state(g);
    bool covers_source=false;
    uint32_t source=g->gsp.pending_color_address;
    uint32_t source_bytes=g->gsp.pending_color_width*g->gsp.pending_color_height*4u;
    if(result==CTR_GX_OK && scanout && scanout->source_slot && source)
        for(unsigned i=0;i<2;++i)if(bytes[i] && w[1+i*3]<=source &&
            (uint64_t)source+source_bytes<=(uint64_t)w[1+i*3]+sizes[i] &&
            ctr_gx_vram_backing(g,w[1+i*3],sizes[i]))covers_source=true;
    for(unsigned i=0;i<2 && result==CTR_GX_OK;++i)if(bytes[i]) {
        /* No scanout destination is backed by VRAM. A complete fill can
         * retire the original source without touching its borrowed image. */
        if(!covers_source || !ctr_gx_vram_backing(g,w[1+i*3],sizes[i]))
            result=current(g,w[1+i*3],sizes[i],c);
    }
    if(result==CTR_GX_OK) {
        if(covers_source && scanout->source_slot)ctr_guest_scanout_discard_source(g);
        for(unsigned i=0;i<2;++i)if(bytes[i])ctr_gx_write_target(g,w[1+i*3],bytes[i],sizes[i]);
    }
    /* GSP dispatches one PSC0 for both banks; bank 1 alone dispatches PSC1. */
    c->signal=w[1]!=0 || w[4]!=0;c->irq=w[1]?0:1;
    free(bytes[0]);free(bytes[1]);return result;
}
static uint8_t *transfer_backing(const ctr_guest *g,uint32_t address,uint32_t size) {
    uint8_t *video=ctr_gx_vram_backing(g,address,size);
    if(video)return video;
    for(size_t i=0;i<g->memory.count;++i) {
        const ctr_mapping *m=&g->memory.maps[i];
        if(address>=m->base && (uint64_t)address+size<=(uint64_t)m->base+m->size)
            return m->storage+(address-m->base);
    }
    return NULL;
}
static const ctr_scene_scanout *scanout_ops(const ctr_guest *g) {
    const ctr_scene_backend *backend=g->gsp.pica_backend;
    return backend && backend->deferred_color?backend->deferred_color->scanout:NULL;
}
ctr_scene_scanout_state *ctr_guest_scanout_state(const ctr_guest *g) {
    const ctr_scene_scanout *ops=scanout_ops(g);
    return ops?ops->state(g->gsp.pica_backend->context,false):NULL;
}
static size_t scanout_bytes(unsigned first_row) {
    return (CTR_GX_SCANOUT_HEIGHT-first_row)*CTR_GX_SCANOUT_WIDTH*2u;
}
static bool scanout_materialize(ctr_scene_scanout_slot *slot) {
    if(!slot->storage)return true;
    ctr_guest *g=slot->guest;
    ctr_scene_scanout_state *state=ctr_guest_scanout_state(g);
    unsigned index=(unsigned)(slot-state->slots);
    uint8_t *pixels=malloc(CTR_GX_SCANOUT_BYTES*2u);
    if(!pixels)return false;
    ctr_scene_surface surface={pixels,CTR_GX_SCANOUT_BYTES*2u,
        CTR_GX_SCANOUT_WIDTH,CTR_GX_SCANOUT_HEIGHT,4u*CTR_GX_SCANOUT_WIDTH};
    bool read=scanout_ops(g)->read(g->gsp.pica_backend->context,index,&surface);
    if(read) {
        const uint8_t *crop=pixels+slot->first_row*CTR_GX_SCANOUT_WIDTH*4u;
        for(unsigned i=0;i<scanout_bytes(slot->first_row)/2u;++i) {
            uint16_t pixel=ctr_rgb565_pack(crop+4u*i);
            slot->storage[2u*i]=(uint8_t)pixel;
            slot->storage[2u*i+1u]=(uint8_t)(pixel>>8);
        }
        (void)ctr_memory_set_watch_slot(&g->memory,index+1u,NULL,0,NULL,NULL);
        *slot=(ctr_scene_scanout_slot){0};
    }
    free(pixels);
    return read;
}
static void scanout_access(void *context,const uint8_t *storage,size_t size,bool write) {
    (void)storage;(void)size;(void)write;
    (void)scanout_materialize(context);
}
bool ctr_guest_scanout_flush(ctr_guest *g) {
    ctr_scene_scanout_state *state=ctr_guest_scanout_state(g);
    if(!state || state->guest!=g)return true;
    for(unsigned i=0;i<CTR_GX_SCANOUT_SLOTS;++i)
        if(!scanout_materialize(&state->slots[i]))return false;
    if(!ctr_memory_detach_watch_group(&g->memory,&state->watches))return false;
    state->guest=NULL;
    return true;
}
static bool scanout_overlap(const uint8_t *a,size_t a_bytes,const uint8_t *b,size_t b_bytes) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x<=y?y-x<a_bytes:x-y<b_bytes;
}
static bool defer_scanout(ctr_guest *g,uint32_t address,uint8_t *storage,unsigned first_row) {
    size_t bytes=scanout_bytes(first_row);
    const ctr_scene_scanout *ops=scanout_ops(g);
    if(!ops || g->memory.resource_clock>=UINT64_MAX-2u ||
       g->memory.resource_mapping_version==UINT64_MAX)return false;
    const uint8_t *vram=ctr_gx_vram_backing(g,CTR_VRAM_BASE,CTR_VRAM_BYTES);
    if(!vram || scanout_overlap(storage,bytes,vram,CTR_VRAM_BYTES))return false;
    const ctr_scene_backend *backend=g->gsp.pica_backend;
    ctr_scene_scanout_state *state=ops->state(backend->context,true);
    if(!state || state->source_slot || (state->guest && state->guest!=g))return false;
    unsigned index=CTR_GX_SCANOUT_SLOTS;
    for(unsigned i=0;i<CTR_GX_SCANOUT_SLOTS;++i) {
        const ctr_scene_scanout_slot *slot=&state->slots[i];
        if(!slot->storage) {
            if(index==CTR_GX_SCANOUT_SLOTS)index=i;
        } else if(slot->storage==storage && slot->address==address && slot->first_row==first_row)index=i;
        else if(scanout_overlap(storage,bytes,slot->storage,scanout_bytes(slot->first_row)))return false;
    }
    if(index==CTR_GX_SCANOUT_SLOTS)return false;
    if(!ctr_memory_attach_watch_group(&g->memory,&state->watches))return false;
    state->guest=g;
    if(!ops->lend(backend->context,index))return false;
    ctr_scene_scanout_slot *slot=&state->slots[index];
    *slot=(ctr_scene_scanout_slot){g,storage,address,first_row};
    (void)ctr_memory_set_watch_slot(&g->memory,index+1u,storage,bytes,scanout_access,slot);
    (void)ctr_memory_backing_changed(&g->memory,storage,bytes);
    return true;
}
static bool convert_bottom(ctr_guest *g,const uint32_t *w,const ctr_transfer *t,
                            const uint8_t *src,uint8_t *dst) {
    if(w[3]!=((CTR_GX_SCANOUT_HEIGHT<<16)|CTR_GX_SCANOUT_WIDTH) ||
       w[4]!=((CTR_GX_BOTTOM_HEIGHT<<16)|CTR_GX_SCANOUT_WIDTH) || w[5]!=0x2004u)
        return false;
    ctr_scene_scanout_state *state=ctr_guest_scanout_state(g);
    if(!state || scanout_overlap(src,t->input_bytes,dst,t->output_bytes))return false;
    /* The crop ends at a tile-row boundary, so only this source prefix is read. */
    if(state->bottom_valid && !memcmp(src,state->bottom_source,sizeof(state->bottom_source))) {
        memcpy(dst,state->bottom_output,sizeof(state->bottom_output));
        return true;
    }
    if(!ctr_transfer_convert(w[3],w[4],w[5],src,t->input_bytes,dst,t->output_bytes))return false;
    memcpy(state->bottom_source,src,sizeof(state->bottom_source));
    memcpy(state->bottom_output,dst,sizeof(state->bottom_output));
    state->bottom_valid=true;
    return true;
}
static ctr_gx_result display(ctr_guest *g,const uint32_t *w,ctr_gx_completion *c) {
    ctr_transfer t;if(w[6] || w[7] || (w[1]&7) || (w[2]&7) || !ctr_transfer_init(&t,w[3],w[4],w[5]))return CTR_GX_UNSUPPORTED;
    ctr_gx_result r=ctr_gx_check_range(g,w[1],(uint32_t)t.input_bytes,CTR_MEM_READ,c);if(r!=CTR_GX_OK)return r;
    r=ctr_gx_check_range(g,w[2],(uint32_t)t.output_bytes,CTR_MEM_WRITE,c);if(r!=CTR_GX_OK)return r;
    uint8_t *dst=transfer_backing(g,w[2],(uint32_t)t.output_bytes);
    unsigned input_height=w[3]>>16;
    if(dst && g->gsp.pending_color_address && t.width==CTR_GX_SCANOUT_WIDTH &&
       t.input_width==CTR_GX_SCANOUT_WIDTH && input_height==CTR_GX_SCANOUT_HEIGHT &&
       g->gsp.pending_color_width==CTR_GX_SCANOUT_WIDTH && g->gsp.pending_color_height==CTR_GX_SCANOUT_HEIGHT) {
        const unsigned bottom_row=CTR_GX_SCANOUT_HEIGHT-CTR_GX_BOTTOM_HEIGHT;
        if(w[1]==g->gsp.pending_color_address && t.height==CTR_GX_SCANOUT_HEIGHT &&
           w[5]==0x2000u && defer_scanout(g,w[2],dst,0))return CTR_GX_OK;
        if(w[1]==g->gsp.pending_color_address+bottom_row*CTR_GX_SCANOUT_WIDTH*4u &&
           t.height==CTR_GX_BOTTOM_HEIGHT && w[5]==0x2004u &&
           defer_scanout(g,w[2],dst,bottom_row))return CTR_GX_OK;
    }
    r=current(g,w[2],(uint32_t)t.output_bytes,c);if(r!=CTR_GX_OK)return r;
    if(dst && w[1]==g->gsp.pending_color_address && !t.input_linear && !t.output_tiled &&
       t.input_format==0 && t.output_format==2 && t.input_width==g->gsp.pending_color_width &&
       input_height==g->gsp.pending_color_height) {
        uint8_t *pixels=malloc(t.input_bytes);
        if(!pixels)return CTR_GX_NO_MEMORY;
        if(!ctr_guest_pica_publish_color(g,pixels,t.input_bytes)) {
            free(pixels);c->detail=w[1];c->memory=CTR_MEM_PENDING;return CTR_GX_MEMORY;
        }
        /* VRAM stores the PICA target in reversed row order with ABGR bytes. */
        for(unsigned y=0;y<t.height;++y) {
            const uint8_t *rgba=pixels+(size_t)(input_height-1u-y)*t.input_width*4u;
            uint8_t *out=dst+(size_t)(t.flip?t.height-1u-y:y)*t.width*2u;
            for(unsigned x=0;x<t.width;++x) {
                uint16_t rgb565=ctr_rgb565_pack(rgba+4u*x);
                out[2u*x]=(uint8_t)rgb565;out[2u*x+1u]=(uint8_t)(rgb565>>8);
            }
        }
        free(pixels);
        (void)ctr_memory_backing_changed(&g->memory,dst,t.output_bytes);
        return CTR_GX_OK;
    }
    r=current(g,w[1],(uint32_t)t.input_bytes,c);if(r!=CTR_GX_OK)return r;
    const uint8_t *src=transfer_backing(g,w[1],(uint32_t)t.input_bytes);
    /* Conversion rejects overlapping host spans before writing anything. */
    if(src && dst && (convert_bottom(g,w,&t,src,dst) ||
       ctr_transfer_convert(w[3],w[4],w[5],src,t.input_bytes,dst,t.output_bytes))) {
        (void)ctr_memory_backing_changed(&g->memory,dst,t.output_bytes);
        return CTR_GX_OK;
    }
    uint8_t *input=malloc(t.input_bytes),*output=malloc(t.output_bytes);
    if(!input || !output) { free(input);free(output);return CTR_GX_NO_MEMORY; }
    ctr_memory_read_bytes(&g->memory,w[1],input,(uint32_t)t.input_bytes);
    ctr_transfer_convert(w[3],w[4],w[5],input,t.input_bytes,output,t.output_bytes);
    ctr_gx_write_target(g,w[2],output,(uint32_t)t.output_bytes);
    free(input);free(output);return CTR_GX_OK;
}
static bool span(uint32_t dim,uint32_t size,uint32_t *extent) {
    uint32_t width=(dim&65535)*16,gap=(dim>>16)*16;
    if(!width && gap)return false;
    uint64_t total=(uint64_t)size+(width?(uint64_t)((size-1)/width)*gap:0);
    if(total>LIMIT)return false;
    *extent=(uint32_t)total;return true;
}
static ctr_gx_result texture_copy(ctr_guest *g,const uint32_t *w,ctr_gx_completion *c) {
    uint32_t size=w[3],in_span,out_span;
    /* Bit 2 controls cropping in display-transfer mode. Raw copies use the
       separate width and gap fields, even when the crop bit is set. */
    if(!size || size>LIMIT || (size&15) || (w[1]&7) || (w[2]&7) || (w[6]&~4u)!=8u || w[7] ||
       !span(w[4],size,&in_span) || !span(w[5],size,&out_span))return CTR_GX_UNSUPPORTED;
    ctr_gx_result r=checked(g,w[1],in_span,CTR_MEM_READ,c);if(r!=CTR_GX_OK)return r;
    r=checked(g,w[2],out_span,CTR_MEM_WRITE,c);if(r!=CTR_GX_OK)return r;
    uint8_t *input=malloc(in_span),*bytes=malloc(size);
    if(!input || !bytes) { free(input);free(bytes);return CTR_GX_NO_MEMORY; }
    ctr_memory_read_bytes(&g->memory,w[1],input,in_span);
    uint32_t in_width=(w[4]&65535)*16,in_gap=(w[4]>>16)*16;
    for(uint32_t k=0;k<size;++k)bytes[k]=input[k+(in_width?(k/in_width)*in_gap:0)];
    uint32_t width=(w[5]&65535)*16,gap=(w[5]>>16)*16;
    for(uint32_t k=0;k<size;) {
        uint32_t count=width?width:size;if(count>size-k)count=size-k;
        ctr_gx_write_target(g,w[2]+k+(width?(k/width)*gap:0),bytes+k,count);k+=count;
    }
    free(input);free(bytes);return CTR_GX_OK;
}
static ctr_gx_result pica_list(ctr_guest *g,const uint32_t *w,ctr_gx_completion *c) {
    /* The GX command-list address is virtual; PICA resource/target addresses
       inside it are physical and pass through the separate owned resolver. */
    c->signal=false;
    uint32_t size=w[2];
    if(!size || size>CTR_PICA_MAX_BYTES || (size&15u) || (w[1]&7u) ||
       w[3] || w[4] || w[5] || w[6] || w[7]>1)return CTR_GX_UNSUPPORTED;
    ctr_gx_result result=checked(g,w[1],size,CTR_MEM_READ,c);
    if(result!=CTR_GX_OK)return result;
    if(!ctr_guest_pica_prepare_caches(g))return CTR_GX_NO_MEMORY;
    ctr_resource_lease commands={0};
    c->memory=ctr_resource_cache_acquire(g->pica_resources,CTR_RESOURCE_COMMAND,w[1],size,CTR_MEM_READ,&commands);
    if(c->memory!=CTR_MEM_OK)return c->memory==CTR_MEM_CAPACITY?CTR_GX_NO_MEMORY:CTR_GX_MEMORY;
    const uint8_t *bytes=commands.bytes;
    if(w[7])ctr_cpu_barrier();
    ctr_pica_report report={0};
    ctr_pica_result parsed=ctr_pica_execute(&g->gsp.pica,bytes,size,CTR_PICA_MAX_WRITES,&report);
    g->gsp.last_pica_result=(uint32_t)parsed;
    g->gsp.last_pica_offset=(uint32_t)report.parameter_offset;
    g->gsp.last_pica_register=report.reg;
    if(parsed==CTR_PICA_UNSUPPORTED) {
        result=ctr_guest_pica_execute(g,bytes,size,w[1],c);
        ctr_resource_cache_release(&commands);
        if(result!=CTR_GX_OK)return result;
        parsed=CTR_PICA_OK;
    } else {
        ctr_resource_cache_release(&commands);
        if(parsed==CTR_PICA_OK) {
            g->gsp.last_scene_result=CTR_SCENE_OK;g->gsp.last_vertex_result=CTR_VERTEX_OK;
            g->gsp.last_shader_result=CTR_SHADER_OK;
        }
        if(parsed==CTR_PICA_OK && g->gsp.pica_scene) {
            memcpy(g->gsp.pica_scene->registers,g->gsp.pica.registers,sizeof(g->gsp.pica.registers));
            g->gsp.pica_scene->lists=g->gsp.pica.committed_lists;
        }
    }
    if(parsed!=CTR_PICA_OK) {
        c->detail=w[1]+(report.parameter_offset<size?(uint32_t)report.parameter_offset:0);
        return CTR_GX_UNSUPPORTED;
    }
    /* One synchronous command-list completion, distinct from the one/two
       canonical tail-marker writes. No asynchronous hardware timing claimed. */
    c->irq=5;c->signal=true;return CTR_GX_OK;
}
ctr_gx_result ctr_gx_execute(ctr_guest *g,const uint32_t w[8],ctr_gx_completion *c) {
    if(!g || !w || !c)return CTR_GX_UNSUPPORTED;
    *c=(ctr_gx_completion){.detail=w[0],.signal=true};unsigned id=w[0]&255;
    uint32_t header=w[0]&~UINT32_C(0x100ff); /* low ID and documented stop byte. */
    if((id==5?header!=0x00000100:header!=0x01000100) || ((w[0]>>16)&255)>1)return CTR_GX_UNSUPPORTED;
    ctr_gx_result result=CTR_GX_UNSUPPORTED;
    if(id==0 && !w[4] && !w[5] && !w[6] && w[7]<=1u) {
        if(w[7])ctr_cpu_barrier();
        c->irq=6;result=copied(g,w[1],w[2],w[3],c);
    }
    else if(id==1)result=pica_list(g,w,c);
    else if(id==2)result=fill(g,w,c);
    else if(id==3) { c->irq=4;result=display(g,w,c); }
    else if(id==4) { c->irq=4;result=texture_copy(g,w,c); }
    else if(id==5 && !w[7]) {
        c->signal=false;result=CTR_GX_OK;
        for(unsigned i=0;i<3;++i) {
            uint32_t size=w[2+i*2];if(size>LIMIT) { result=CTR_GX_UNSUPPORTED;break; }
            if(size) { result=ctr_gx_check_range(g,w[1+i*2],size,CTR_MEM_READ,c);if(result!=CTR_GX_OK)break; }
        }
        if(result==CTR_GX_OK)ctr_cpu_barrier();
    }
    if(result==CTR_GX_OK && id!=5)g->cpu.exclusive_valid=false;
    return result;
}
