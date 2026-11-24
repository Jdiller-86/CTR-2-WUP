/* Original bounded Old-family PICA draw adapter. */
#include "ctr_guest_pica.h"
#include "ctr_gsp.h"
#include "ctr_heap.h"
#include "ctr_resource_cache.h"
#include "ctr_texture_cache.h"
#include "ctr_transfer.h"
#include <stdlib.h>
#include <string.h>

#define PHYSICAL_BASE UINT32_C(0x20000000)
#define PHYSICAL_END UINT32_C(0x28000000)
#define COMMAND_SEGMENTS 128u

typedef struct { uint32_t address,size;size_t offset; } command_span;
typedef struct {
    command_span *spans;
    unsigned count;
    uint8_t *bytes;
    size_t length;
} command_chain;
typedef struct {
    size_t offset;
    unsigned preceding;
    uint32_t physical,size;
} command_link;

static uint32_t command_word(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}

static bool ensure_buf(void **ptr,size_t *cap,size_t need) {
    if(need<=*cap)return true;
    size_t next=*cap?*cap*2u:need;
    if(next<need)next=need;
    void *grown=realloc(*ptr,next);
    if(!grown)return false;
    *ptr=grown;*cap=next;
    return true;
}

bool ctr_guest_pica_prepare_caches(ctr_guest *g) {
    if(!g)return false;
    if(!g->pica_resources)g->pica_resources=ctr_resource_cache_create(&g->memory);
    if(!g->pica_textures)g->pica_textures=ctr_texture_cache_create();
    return g->pica_resources && g->pica_textures;
}
static ctr_gx_result snapshot(ctr_guest *g,ctr_resource_role role,uint32_t address,
                             uint32_t size,unsigned permissions,ctr_resource_lease *lease,
                             ctr_gx_completion *c) {
    if(ctr_gx_vram_backing(g,address,size))permissions=CTR_MEM_READ;
    c->memory=ctr_resource_cache_acquire(g->pica_resources,role,address,size,permissions,lease);
    return c->memory==CTR_MEM_OK?CTR_GX_OK:
        c->memory==CTR_MEM_CAPACITY?CTR_GX_NO_MEMORY:CTR_GX_MEMORY;
}

static ctr_gx_result linear(ctr_guest *g,uint32_t physical,uint32_t size,unsigned permissions,
                            uint32_t *address,uint32_t *available,const uint8_t **backing,ctr_gx_completion *c) {
    c->detail=physical;c->memory=CTR_MEM_UNMAPPED;
    if(size && physical>=CTR_VRAM_PHYSICAL_BASE &&
       (uint64_t)physical+size<=CTR_VRAM_PHYSICAL_BASE+CTR_VRAM_BYTES) {
        uint32_t offset=physical-CTR_VRAM_PHYSICAL_BASE,virtual_address=CTR_VRAM_BASE+offset;
        const uint8_t *video=ctr_gx_vram_backing(g,virtual_address,size);
        if(!video)return CTR_GX_MEMORY;
        ctr_gx_result result=ctr_gx_check_range(g,virtual_address,size,permissions,c);
        if(result!=CTR_GX_OK)return result;
        *address=virtual_address;
        if(available)*available=CTR_VRAM_BYTES-offset;
        if(backing)*backing=video;
        return CTR_GX_OK;
    }
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
    if(backing && !ctr_memory_resolve_watches(&g->memory,virtual_address,size,false)) {
        c->memory=CTR_MEM_PENDING;return CTR_GX_MEMORY;
    }
    *address=virtual_address;
    if(available)*available=allocation->size-(virtual_address-allocation->base);
    if(backing)*backing=allocation->storage+(virtual_address-allocation->base);
    return CTR_GX_OK;
}
static bool command_alias(const ctr_guest *g,uint32_t target,uint32_t bytes,
                           uint32_t commands,uint32_t command_bytes) {
    /* Target storage is owned/continuous and already validated by linear(). */
    const uint8_t *storage=ctr_gx_vram_backing(g,target,bytes);
    for(unsigned i=0;!storage && i<CTR_GUEST_MAX_ALLOCATIONS;++i) {
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
static bool chain_alias(const ctr_guest *g,uint32_t target,uint32_t bytes,const command_chain *chain) {
    for(unsigned i=0;i<chain->count;++i)
        if(command_alias(g,target,bytes,chain->spans[i].address,chain->spans[i].size))return true;
    return false;
}
static bool append_segment(ctr_guest *g,command_chain *chain,const uint8_t *bytes,size_t size,const command_link *link) {
    size_t prefix=link->size?link->offset:size;
    size_t rest=link->preceding?((link->preceding+2u)/2u)*8u:0;
    if(!prefix && !rest)return true;
    if(!ensure_buf((void **)&g->pica_chain_bytes,&g->pica_chain_cap,chain->length+prefix+rest))return false;
    uint8_t *grown=g->pica_chain_bytes;
    chain->bytes=grown;
    if(prefix)memcpy(grown+chain->length,bytes,prefix);
    chain->length+=prefix;
    if(rest) {
        /* Preserve configuration in a grouped record ending in a jump. */
        const size_t used=(link->preceding+1u)*4u;
        uint8_t *record=grown+chain->length;
        memcpy(record,bytes+prefix,used);memset(record+used,0,rest-used);
        unsigned extra=link->preceding-1u;
        record[6]=(uint8_t)((record[6]&15u)|(extra<<4));
        record[7]=(uint8_t)((record[7]&0xf0u)|(extra>>4));
        chain->length+=rest;
    }
    return true;
}
static ctr_gx_result expand_commands(ctr_guest *g,const uint8_t *root,size_t size,uint32_t address,
                                     command_chain *chain,size_t *out_writes,
                                     ctr_scene_report *report,ctr_gx_completion *c) {
    chain->spans[chain->count++]=(command_span){address,(uint32_t)size,0};
    uint32_t slots[4];memcpy(slots,g->gsp.pica.registers+0x238u,sizeof(slots));
    const uint8_t *bytes=root;
    size_t fetched=size,writes=0;
    ctr_resource_lease lease={0};
    ctr_gx_result result=CTR_GX_UNSUPPORTED;
    for(;;) {
        if(size>CTR_PICA_MAX_BYTES || (size&7u)) {
            report->result=CTR_SCENE_WIRE;report->reg=0;report->byte_offset=chain->length;break;
        }
        command_link link={0};
        size_t at=0;
        bool wire_ok=true,link_ok=true;
        while(at<size) {
            uint32_t first_val=command_word(bytes+at),header=command_word(bytes+at+4u);
            unsigned reg=header&65535u;
            size_t count=1u+((header>>20)&255u);
            bool increment=(header>>31)!=0;
            uint8_t mask=(uint8_t)((header>>16)&15u);
            if((header&UINT32_C(0x70000000)) || reg>=CTR_PICA_REGISTER_COUNT ||
               (increment && count>CTR_PICA_REGISTER_COUNT-reg)) {
                report->result=CTR_SCENE_WIRE;report->reg=(uint16_t)reg;
                report->byte_offset=chain->length+at;wire_ok=false;break;
            }
            size_t record_size=(((count+1u)*4u)+7u)&~(size_t)7u;
            if(record_size>size-at || count>CTR_PICA_MAX_CHAIN_WRITES-writes) {
                report->result=CTR_SCENE_WIRE;report->reg=(uint16_t)reg;
                report->byte_offset=chain->length+at;wire_ok=false;break;
            }
            if(writes+count>g->pica_writes_cap/sizeof(ctr_pica_write) &&
               !ensure_buf((void **)&g->pica_writes,&g->pica_writes_cap,(writes+count)*sizeof(ctr_pica_write))) {
                report->result=CTR_SCENE_NO_MEMORY;result=CTR_GX_NO_MEMORY;wire_ok=false;break;
            }
            ctr_pica_write *dst=g->pica_writes+writes;
            for(size_t i=0;i<count;++i) {
                uint32_t val=i?command_word(bytes+at+4u+i*4u):first_val;
                uint16_t cur=(uint16_t)(reg+(increment?i:0u));
                dst[i]=(ctr_pica_write){.byte_offset=chain->length+at,.value=val,.reg=cur,.byte_mask=mask};
                if(cur>=0x238u && cur<=0x23du) {
                    report->reg=cur;report->byte_offset=chain->length+at;
                    if(cur<0x23cu) {
                        unsigned slot=cur-0x238u;
                        uint32_t merged=ctr_pica_merge(slots[slot],val,mask);
                        if(merged>(slot<2u?0xfffffu:0xfffffffu)) { link_ok=false;break; }
                        slots[slot]=merged;
                    } else {
                        unsigned slot=cur-0x23cu;
                        if(mask!=15u || val!=1u || i+1u!=count || !slots[slot]) { link_ok=false;break; }
                        link=(command_link){at,(unsigned)i,slots[slot+2u]*8u,slots[slot]*8u};
                        writes+=i;
                        goto link_found;
                    }
                }
            }
            if(!link_ok)break;
            writes+=count;
            at+=record_size;
        }
    link_found:
        if(!wire_ok)break;
        if(!link_ok) { report->result=CTR_SCENE_UNSUPPORTED;break; }
        if(!writes) { report->result=CTR_SCENE_WIRE;break; }
        if((link.size || chain->count>1u) && !append_segment(g,chain,bytes,size,&link)) {
            report->result=CTR_SCENE_NO_MEMORY;result=CTR_GX_NO_MEMORY;break;
        }
        if(!link.size) { result=CTR_GX_OK;break; }
        if(chain->count==COMMAND_SEGMENTS || link.size>CTR_PICA_MAX_BYTES-fetched) {
            report->result=CTR_SCENE_LIMIT;break;
        }
        uint32_t next_address;
        ctr_gx_result resolved=linear(g,link.physical,link.size,CTR_MEM_READ,&next_address,NULL,NULL,c);
        if(resolved!=CTR_GX_OK) { report->result=CTR_SCENE_LAYOUT;result=resolved;break; }
        bool visited=false;
        for(unsigned i=0;i<chain->count;++i)if(chain->spans[i].address==next_address)visited=true;
        if(visited) { report->result=CTR_SCENE_WIRE;break; }
        ctr_resource_cache_release(&lease);
        resolved=snapshot(g,CTR_RESOURCE_COMMAND,next_address,link.size,CTR_MEM_READ,&lease,c);
        if(resolved!=CTR_GX_OK) { report->result=CTR_SCENE_LAYOUT;result=resolved;break; }
        chain->spans[chain->count++]=(command_span){next_address,link.size,chain->length};
        fetched+=link.size;bytes=lease.bytes;size=link.size;
    }
    ctr_resource_cache_release(&lease);
    *out_writes=writes;
    return result;
}
/* Three bits spread over the even positions of one 8x8 Morton tile. */
static const uint8_t morton_axis[8]={0,1,4,5,16,17,20,21};
static void layout_rgba8_aligned(uint8_t *output,const uint8_t *input,unsigned width,unsigned height,
                                 bool top_down,bool encode) {
    for(unsigned y=0;y<height;++y) {
        unsigned row=top_down?height-1u-y:y;
        size_t linear_row=(size_t)y*width*4u;
        size_t tiled_row=((size_t)(row/8u)*width*8u+2u*morton_axis[row%8u])*4u;
        for(unsigned x=0;x<width;x+=8u,tiled_row+=256u) {
            size_t linear_tile=linear_row+x*4u;
#pragma GCC unroll 8
            for(unsigned lane=0;lane<8u;++lane) {
                size_t linear=linear_tile+4u*lane,tiled=tiled_row+4u*morton_axis[lane];
                const void *src=__builtin_assume_aligned(input+(encode?linear:tiled),4);
                void *dst=__builtin_assume_aligned(output+(encode?tiled:linear),4);
                uint32_t pixel;
                memcpy(&pixel,src,sizeof(pixel));
                pixel=__builtin_bswap32(pixel);
                memcpy(dst,&pixel,sizeof(pixel));
            }
        }
    }
}
static void layout(uint8_t *output,const uint8_t *input,unsigned width,unsigned height,unsigned format,
                   bool top_down,bool encode) {
    if(format==CTR_SCENE_COLOR_RGBA8 && !(((uintptr_t)input|(uintptr_t)output)&3u)) {
        layout_rgba8_aligned(output,input,width,height,top_down,encode);
        return;
    }
    unsigned element=format==CTR_SCENE_COLOR_RGBA8?4u:2u;
    for(unsigned y=0;y<height;++y) {
        unsigned row=top_down?height-1u-y:y;
        size_t row_offset=(size_t)y*width*4u;
        size_t tile=((size_t)(row/8u)*width*8u+2u*morton_axis[row%8u])*element;
        for(unsigned x=0;x<width;x+=8u,tile+=64u*element) {
            for(unsigned column=0;column<8u;++column) {
                size_t pixel=row_offset+(x+column)*4u,tiled=tile+morton_axis[column]*element;
                uint8_t *dst=output+(encode?tiled:pixel);
                const uint8_t *src=input+(encode?pixel:tiled);
                if(element==2u) {
                    if(encode) {
                        uint16_t word=format==CTR_SCENE_COLOR_RGB5A1?ctr_rgb5a1_pack(src):
                            format==CTR_SCENE_COLOR_RGB565?ctr_rgb565_pack(src):ctr_rgba4_pack(src);
                        dst[0]=(uint8_t)word;dst[1]=(uint8_t)(word>>8);
                    } else {
                        uint16_t word=(uint16_t)((uint16_t)src[0]|(uint16_t)src[1]<<8);
                        if(format==CTR_SCENE_COLOR_RGB5A1)ctr_rgb5a1_unpack(word,dst);
                        else if(format==CTR_SCENE_COLOR_RGB565)ctr_rgb565_unpack(word,dst);
                        else ctr_rgba4_unpack(word,dst);
                    }
                } else {
                    uint32_t word;memcpy(&word,src,sizeof(word));
                    word=__builtin_bswap32(word);
                    memcpy(dst,&word,sizeof(word));
                }
            }
        }
    }
}
static void clear_pending_color(ctr_guest *g) {
    g->gsp.pending_color_address=0;
    ctr_memory_set_watch(&g->memory,NULL,0,NULL,NULL);
}
void ctr_guest_scanout_discard_source(ctr_guest *g) {
    const ctr_scene_backend *backend=g->gsp.pica_backend;
    backend->deferred_color->scanout->discard_source(backend->context);
    clear_pending_color(g);
}
bool ctr_guest_pica_publish_color(ctr_guest *g,uint8_t *pixels,size_t size) {
    ctr_guest_gsp *s=&g->gsp;
    if(!s->pending_color_address)return false;
    unsigned width=s->pending_color_width,height=s->pending_color_height;
    uint32_t bytes=width*height*4u;
    uint8_t *target=ctr_gx_vram_backing(g,s->pending_color_address,bytes);
    if(!target || !pixels || size<bytes)return false;
    ctr_scene_surface readback={pixels,bytes,width,height,width*4u};
    const ctr_scene_backend *backend=s->pica_backend;
    bool resolved=backend->deferred_color->resolve(backend->context,&readback);
    if(resolved) {
        layout(target,pixels,width,height,CTR_SCENE_COLOR_RGBA8,true,true);
        clear_pending_color(g);
    }
    return resolved;
}
bool ctr_guest_pica_flush_color(ctr_guest *g) {
    if(!g->gsp.pending_color_address)return true;
    size_t size=(size_t)g->gsp.pending_color_width*g->gsp.pending_color_height*4u;
    uint8_t *pixels=malloc(size);
    bool resolved=ctr_guest_pica_publish_color(g,pixels,size);
    free(pixels);
    return resolved;
}
static void access_pending_color(void *context,const uint8_t *storage,size_t size,bool write) {
    (void)storage;(void)size;(void)write;
    (void)ctr_guest_pica_flush_color(context);
}
/* Depth uses the same 8x8 Morton indices as color: D16 has two bytes;
 * D24S8 has little-endian depth bytes 0..2 and stencil byte 3. */
static void depth_layout(uint8_t *output,const uint8_t *input,unsigned width,unsigned height,unsigned element,
                         bool top_down,bool encode) {
    for(unsigned y=0;y<height;++y) {
        unsigned row=top_down?height-1u-y:y;
        size_t row_offset=(size_t)y*width*element;
        size_t tile=((size_t)(row/8u)*width*8u+2u*morton_axis[row%8u])*element;
        if(element==4u) {
            for(unsigned x=0;x<width;x+=8u,tile+=256u)for(unsigned column=0;column<8u;++column) {
                size_t pixel=row_offset+(x+column)*4u,tiled=tile+morton_axis[column]*4u;
                memcpy(output+(encode?tiled:pixel),input+(encode?pixel:tiled),4u);
            }
        } else {
            for(unsigned x=0;x<width;x+=8u,tile+=128u)for(unsigned column=0;column<8u;++column) {
                size_t pixel=row_offset+(x+column)*2u,tiled=tile+morton_axis[column]*2u;
                memcpy(output+(encode?tiled:pixel),input+(encode?pixel:tiled),2u);
            }
        }
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
static bool vram_geometry(uint64_t address) {
    return address>=CTR_VRAM_PHYSICAL_BASE && address<CTR_VRAM_PHYSICAL_BASE+CTR_VRAM_BYTES;
}
static bool resource_range(ctr_scene_resource_span *spans,unsigned *count,unsigned limit,uint32_t address,size_t size) {
    uint64_t first=address,last=first+size;
    if(*count) {
        const ctr_scene_resource_span *back=&spans[*count-1u];
        uint64_t a=back->base_address,b=a+back->size;
        if(a<=first && last<=b)return true;
    }
    for(unsigned i=0;i<*count;) {
        uint64_t a=spans[i].base_address,b=a+spans[i].size;
        if(first<b && a<last) {
            if(a<=first && last<=b) {
                ctr_scene_resource_span span=spans[i];
                spans[i]=spans[*count-1u];
                spans[*count-1u]=span;
                return true;
            }
            bool widened=a<first || b>last;
            if(a<first)first=a;
            if(b>last)last=b;
            spans[i]=spans[--*count];
            if(widened)i=0;
        } else ++i;
    }
    if(*count==limit || last-first>CTR_SCENE_MAX_TEXTURE_SOURCE_BYTES)return false;
    spans[(*count)++]=(ctr_scene_resource_span){(uint32_t)first,NULL,(size_t)(last-first)};
    return true;
}
enum { GEOMETRY_FIRST=0x203u, GEOMETRY_LAST=0x22au };
static bool external_geometry_source(uint64_t address,uint64_t vram_anchor) {
    if(vram_geometry(address))return address<vram_anchor || address-vram_anchor>=CTR_VERTEX_MAX_SOURCE_BYTES;
    return address>=PHYSICAL_BASE && address<PHYSICAL_END;
}
static bool geometry_range(ctr_scene_resource_span *spans,unsigned *count,uint64_t vram_anchor,
                           uint32_t address,uint64_t size) {
    if(!size || size>CTR_VERTEX_MAX_SOURCE_BYTES || address+size>PHYSICAL_END)return false;
    uint64_t last=address+size,first=address&~(CTR_GUEST_PAGE-1u);
    uint64_t end=(last+CTR_GUEST_PAGE-1u)&~(uint64_t)(CTR_GUEST_PAGE-1u);
    if(vram_geometry(address)) {
        if(!vram_geometry(last-1u))return false;
        uint64_t anchor_end=vram_anchor+CTR_VERTEX_MAX_SOURCE_BYTES;
        if(address>=anchor_end && first<anchor_end)first=anchor_end;
        if(last<=vram_anchor && end>vram_anchor)end=vram_anchor;
    }
    return resource_range(spans,count,CTR_SCENE_MAX_GAME_TEXTURE_SPANS,(uint32_t)first,(size_t)(end-first));
}
/* The base can remain in VRAM while an offset reaches beyond the geometry
 * snapshot. Collect the owned source extents required by the actual indices. */
static ctr_scene_result external_geometry(ctr_guest *g,uint32_t base,uint64_t vram_anchor,const uint32_t *regs,bool indexed,
                                          bool stable_inputs,ctr_scene_resource_span *spans,unsigned *count,ctr_gx_completion *c) {
    uint32_t addresses[CTR_VERTEX_MAX_STREAMS];unsigned strides[CTR_VERTEX_MAX_STREAMS],streams=0;
    for(unsigned bank=0;bank<CTR_VERTEX_MAX_STREAMS;++bank) {
        uint32_t config=regs[bank*3u+2u],offset=regs[bank*3u];
        uint64_t address=(uint64_t)base+offset;
        if(!(config>>28) || !external_geometry_source(address,vram_anchor))continue;
        addresses[streams]=(uint32_t)address;
        strides[streams++]=(config>>16)&255u;
    }
    uint32_t index=regs[0x227u-GEOMETRY_FIRST];
    uint64_t index_address=(uint64_t)base+(index&UINT32_C(0x0fffffff));
    bool external_index=indexed && external_geometry_source(index_address,vram_anchor);
    if(!streams && !external_index)return CTR_SCENE_OK;
    unsigned vertices=regs[0x228u-GEOMETRY_FIRST];
    if(!vertices || vertices>CTR_SCENE_MAX_GAME_DRAW_VERTICES)return CTR_SCENE_LIMIT;
    uint64_t last=(uint64_t)regs[0x22au-GEOMETRY_FIRST]+vertices-1u;
    if(indexed) {
        unsigned element=(index>>31)+1u,index_bytes=vertices*element;
        uint32_t address;
        const uint8_t *index_backing=NULL;
        if((index&UINT32_C(0x70000000)) || index_address>UINT32_MAX ||
           linear(g,(uint32_t)index_address,index_bytes,CTR_MEM_READ,&address,NULL,stable_inputs?&index_backing:NULL,c)!=CTR_GX_OK)
            return CTR_SCENE_LAYOUT;
        if(external_index && !geometry_range(spans,count,vram_anchor,(uint32_t)index_address,index_bytes))
            return CTR_SCENE_LIMIT;
        if(streams) {
            ctr_resource_lease lease={0};
            const uint8_t *bytes=index_backing;
            if(!bytes) {
                ctr_gx_result result=snapshot(g,CTR_RESOURCE_VERTEX,address,index_bytes,CTR_MEM_READ,&lease,c);
                if(result!=CTR_GX_OK)return result==CTR_GX_NO_MEMORY?CTR_SCENE_NO_MEMORY:CTR_SCENE_LAYOUT;
                bytes=lease.bytes;
            }
            last=0;
            for(unsigned i=0;i<vertices;++i) {
                unsigned value=bytes[i*element];
                if(element==2u)value|=(unsigned)bytes[i*element+1u]<<8;
                if(value>last)last=value;
            }
            ctr_resource_cache_release(&lease);
        }
    }
    for(unsigned i=0;i<streams;++i)
        if(!geometry_range(spans,count,vram_anchor,addresses[i],(last+1u)*strides[i]))return CTR_SCENE_LIMIT;
    return CTR_SCENE_OK;
}
ctr_gx_result ctr_guest_pica_execute(ctr_guest *g,const void *commands,size_t size,
                                    uint32_t command_address,ctr_gx_completion *c) {
    if(!g || !commands || !c)return CTR_GX_UNSUPPORTED;
    c->signal=false;ctr_scene_report report={0};
    ctr_gx_result result=CTR_GX_UNSUPPORTED;
    ctr_pica_write *writes=NULL;ctr_scene_state *trial=NULL;
    size_t write_count=0;
    command_span spans[COMMAND_SEGMENTS];
    command_chain chain={.spans=spans};
    uint8_t *raw=NULL,*pixels=NULL,*depth_raw=NULL,*depth_pixels=NULL;
    ctr_resource_lease geometry={0},color_initial={0},depth_initial={0};
    ctr_resource_lease texture_sources[CTR_SCENE_MAX_GAME_TEXTURE_SPANS]={{0}};
    ctr_scene_resource_span textures[CTR_SCENE_MAX_GAME_TEXTURE_SPANS]={0};unsigned texture_count=0;
    bool software_game=g->gsp.pica_round_f24 &&
        (!g->gsp.pica_backend || g->gsp.pica_backend->draw_transformed);
    bool stable_inputs=g->gsp.pica_backend && (g->gsp.pica_backend->features&CTR_SCENE_BACKEND_STABLE_INPUTS);
    bool native_mips=g->gsp.pica_backend && g->gsp.pica_backend->draw_mips;
    unsigned texture_limit=software_game?CTR_SCENE_MAX_GAME_TEXTURE_SPANS:CTR_SCENE_MAX_TEXTURE_SPANS;
    if(!ctr_guest_pica_prepare_caches(g)) {
        result=CTR_GX_NO_MEMORY;report.result=CTR_SCENE_NO_MEMORY;goto done;
    }
    if(!size || (size&15u)) { report.result=CTR_SCENE_WIRE;goto done; }
    result=expand_commands(g,commands,size,command_address,&chain,&write_count,&report,c);
    if(result!=CTR_GX_OK)goto done;
    if(chain.bytes) { commands=chain.bytes;size=chain.length; }
    report=(ctr_scene_report){0};result=CTR_GX_UNSUPPORTED;
    if(!write_count) { report.result=CTR_SCENE_WIRE;goto done; }
    if(!g->pica_trial)g->pica_trial=malloc(sizeof(*trial));
    trial=g->pica_trial;
    writes=g->pica_writes;
    if(!writes || !trial) { result=CTR_GX_NO_MEMORY;report.result=CTR_SCENE_NO_MEMORY;goto done; }
    if(g->gsp.pica_scene)*trial=*g->gsp.pica_scene;
    else ctr_scene_init(trial);
    trial->shader.round_f24=g->gsp.pica_round_f24;
    memcpy(trial->registers,g->gsp.pica.registers,sizeof(trial->registers));
    trial->lists=g->gsp.pica.committed_lists;
    /* Select actual bindings at the first draw, rather than guessing from the
     * final state. Later draws must match; post-draw setup may prepare a next list. */
    const unsigned ids[]={0x200,0x11d,0x11e,0x117,0x116,0x11c};
    uint32_t values[6],bound[6]={0};uint8_t defined[6];
    for(unsigned i=0;i<6;++i) { values[i]=trial->registers[ids[i]];defined[i]=trial->register_defined[ids[i]]; }
    uint32_t texture_config=trial->registers[0x080u];
    uint8_t texture_config_defined=trial->register_defined[0x080u];
    uint32_t texture_values[CTR_SCENE_TEXTURE_UNITS][5];
    uint8_t texture_defined[CTR_SCENE_TEXTURE_UNITS][5];
    for(unsigned unit=0;unit<CTR_SCENE_TEXTURE_UNITS;++unit)for(unsigned field=0;field<5u;++field) {
        unsigned reg=ctr_scene_texture_registers[unit][field];
        texture_values[unit][field]=trial->registers[reg];
        texture_defined[unit][field]=trial->register_defined[reg];
    }
    uint32_t geometry_registers[GEOMETRY_LAST-GEOMETRY_FIRST+1u];
    memcpy(geometry_registers,trial->registers+GEOMETRY_FIRST,sizeof(geometry_registers));
    bool draws=false,array_draws=false,inline_data=false,geometry_anchor_vram=false;
    uint32_t fixed_index=trial->registers[0x232u];
    uint32_t geometry_anchor=0;
    size_t markers=0,first_draw=0;unsigned draw_register=0;
    uint8_t texture_dirty=0x7u;
    uint32_t cached_tex_addr[CTR_SCENE_TEXTURE_UNITS]={0},cached_tex_bytes[CTR_SCENE_TEXTURE_UNITS]={0};
    for(size_t i=0;i<write_count;++i) {
        const ctr_pica_write *w=&writes[i];report.byte_offset=w->byte_offset;report.reg=w->reg;
        if(w->reg==0x200u || (w->reg>=0x116u && w->reg<=0x11eu)) {
            for(unsigned k=0;k<6;++k)if(w->reg==ids[k]) {
                values[k]=ctr_pica_merge(values[k],w->value,w->byte_mask);defined[k]|=w->byte_mask;
            }
        }
        if(software_game && w->reg>=GEOMETRY_FIRST && w->reg<=GEOMETRY_LAST) {
            uint32_t *reg=&geometry_registers[w->reg-GEOMETRY_FIRST];
            *reg=ctr_pica_merge(*reg,w->value,w->byte_mask);
        }
        if(w->reg==0x080u) {
            texture_config=ctr_pica_merge(texture_config,w->value,w->byte_mask)&~CTR_PICA_CLEAR_TEXTURE_CACHE;
            texture_config_defined|=w->byte_mask;
        }
        if(w->reg>=ctr_scene_texture_registers[0][0] &&
           w->reg<=ctr_scene_texture_registers[CTR_SCENE_TEXTURE_UNITS-1u][4]) {
            for(unsigned unit=0;unit<CTR_SCENE_TEXTURE_UNITS;++unit)for(unsigned field=0;field<5u;++field)
                if(w->reg==ctr_scene_texture_registers[unit][field]) {
                    texture_values[unit][field]=ctr_pica_merge(texture_values[unit][field],w->value,w->byte_mask);
                    texture_defined[unit][field]|=w->byte_mask;
                    texture_dirty|=(uint8_t)(1u<<unit);
                }
        }
        if(w->reg==CTR_PICA_FINALIZE)++markers;
        if(software_game && w->byte_mask && w->reg==0x232u)
            fixed_index=ctr_pica_merge(fixed_index,w->value,w->byte_mask);
        if(software_game && fixed_index==CTR_SCENE_IMMEDIATE_INDEX &&
           w->reg>=0x233u && w->reg<=0x235u && w->byte_mask)inline_data=true;
        bool inline_draw=inline_data && w->reg==0x245u && (w->byte_mask&1u) && w->value==1u;
        bool array_draw=(w->reg==0x22eu || w->reg==0x22fu) &&
                        !(software_game && w->byte_mask==15u && !w->value);
        if(array_draw || inline_draw) {
            if(array_draw && (w->byte_mask!=15u || w->value!=1u || (values[0]&UINT32_C(0xe0000001)))) {
                report.result=CTR_SCENE_UNSUPPORTED;goto done;
            }
            uint64_t geometry_base=(uint64_t)(values[0]>>1)*16u;
            bool vram_base=vram_geometry(geometry_base);
            if(array_draw && !array_draws) {
                geometry_anchor=values[0];geometry_anchor_vram=vram_base;
            }
            array_draws|=array_draw;
            if(inline_draw)inline_data=false;
            for(unsigned k=0;k<4;++k)if(defined[k]!=15u) { report.result=CTR_SCENE_UNSUPPORTED;goto done; }
            if(draws && ((memcmp(values+1,bound+1,sizeof(bound)-sizeof(bound[0]))) ||
                         (!software_game && values[0]!=bound[0]))) { report.result=CTR_SCENE_LAYOUT;goto done; }
            if(!draws) {
                first_draw=w->byte_offset;draw_register=w->reg;
                memcpy(bound,values,sizeof(bound));draws=true;
            }
            if(array_draw && software_game && (vram_base || geometry_anchor_vram)) {
                uint64_t vram_anchor=geometry_anchor_vram?(uint64_t)(geometry_anchor>>1)*16u:0u;
                report.result=external_geometry(g,(uint32_t)geometry_base,vram_anchor,geometry_registers,w->reg==0x22fu,
                                                stable_inputs,textures,&texture_count,c);
                if(report.result!=CTR_SCENE_OK) {
                    if(report.result==CTR_SCENE_NO_MEMORY)result=CTR_GX_NO_MEMORY;
                    goto done;
                }
            }
            if(texture_config&~CTR_PICA_TEXTURE_CONFIG_MASK) { report.result=CTR_SCENE_UNSUPPORTED;goto done; }
            if(texture_config && texture_config_defined!=15u) { report.result=CTR_SCENE_LAYOUT;goto done; }
            for(unsigned unit=0;unit<CTR_SCENE_TEXTURE_UNITS;++unit)if(texture_config&(1u<<unit)) {
                if(texture_dirty&(1u<<unit)) {
                    for(unsigned field=0;field<5u;++field)
                        if(texture_defined[unit][field]!=15u) { report.result=CTR_SCENE_LAYOUT;goto done; }
                    const uint32_t *binding=texture_values[unit];
                    uint32_t dimensions=binding[CTR_SCENE_TEXTURE_DIMENSIONS];
                    uint32_t parameters=binding[CTR_SCENE_TEXTURE_PARAMETERS];
                    uint32_t address=binding[CTR_SCENE_TEXTURE_ADDRESS],format=binding[CTR_SCENE_TEXTURE_FORMAT];
                    if(address&UINT32_C(0xf0000000)) {
                        report.result=CTR_SCENE_UNSUPPORTED;goto done;
                    }
                    uint32_t lod=binding[CTR_SCENE_TEXTURE_LOD];
                    unsigned filters=parameters&6u;
                    bool extended=software_game || (native_mips &&
                        (lod || (parameters&UINT32_C(0x01000000)) || (filters!=0u && filters!=6u)));
                    ctr_scene_texture_layout layout;
                    report.result=ctr_scene_texture_layout_read(dimensions,parameters,lod,format,extended,&layout);
                    if(report.result!=CTR_SCENE_OK)goto done;
                    cached_tex_addr[unit]=address*8u;
                    cached_tex_bytes[unit]=layout.byte_count;
                    texture_dirty&=(uint8_t)~(1u<<unit);
                }
                if(!resource_range(textures,&texture_count,texture_limit,cached_tex_addr[unit],cached_tex_bytes[unit])) {
                    report.result=CTR_SCENE_LIMIT;goto done;
                }
            }
        }
    }
    size_t register_writes=write_count+chain.count-1u;
    if(g->gsp.pica.committed_lists==UINT64_MAX || register_writes>UINT64_MAX-g->gsp.pica.register_writes ||
       markers>UINT64_MAX-g->gsp.pica.finalizations) { report.result=CTR_SCENE_OVERFLOW;goto done; }
    uint32_t resource_address=0,resource_bytes=0,target_address=0,target_bytes=4,color_address=0;
    uint32_t depth_address=0,depth_virtual=0,depth_bytes=0;
    unsigned width=1,height=1,color_format=CTR_SCENE_COLOR_RGBA8,color_element=4,depth_element=2;
    /* The game path stores PICA framebuffers top-down. The scene's staging
     * surfaces and original diagnostic fixtures use bottom-up rows. */
    bool top_down=g->gsp.pica_round_f24;
    uint8_t empty[4]={0};
    ctr_scene_resources resources={0};ctr_scene_surface surface={empty,4,1,1,4};
    const uint8_t *geometry_bytes=NULL;
    ctr_scene_depth_surface depth={0};
    bool direct_backend=false,keep_depth=false,reuse_color=false;
    const ctr_scene_backend *backend=g->gsp.pica_backend;
    ctr_scene_backend deferred_backend;
    const uint8_t *color_backing=NULL;
    if(draws) {
        report.byte_offset=first_draw;report.reg=draw_register;
        uint32_t geometry_base=array_draws?geometry_anchor:bound[0];
        if((geometry_base&UINT32_C(0xe0000001)) || (bound[1]&UINT32_C(0xf0000000)) ||
           !ctr_scene_color_format(bound[3],&color_format) ||
           (bound[2]&~UINT32_C(0x013ff7ff)) || !(bound[2]&UINT32_C(0x01000000))) {
            report.result=CTR_SCENE_UNSUPPORTED;goto done;
        }
        uint64_t base=(uint64_t)(geometry_base>>1)*16u;
        if(base>UINT32_MAX) { report.result=CTR_SCENE_OVERFLOW;goto done; }
        color_address=bound[1]*8u;width=bound[2]&2047u;height=((bound[2]>>12)&1023u)+1u;
        if(!width || width>512u || height>512u || (width&7u) || (height&7u)) {
            report.result=CTR_SCENE_LAYOUT;goto done;
        }
        color_element=color_format==CTR_SCENE_COLOR_RGBA8?4u:2u;
        target_bytes=width*height*color_element;
        if((bound[4]!=0u && bound[4]!=3u) || (bound[5]&UINT32_C(0xf0000000))) { report.result=CTR_SCENE_UNSUPPORTED;goto done; }
        depth_address=bound[5]*8u;
        depth_element=bound[4]==3u?4u:2u;
        if(depth_address)depth_bytes=width*height*depth_element;
        result=linear(g,color_address,target_bytes,CTR_MEM_READ|CTR_MEM_WRITE,&target_address,NULL,NULL,c);
        if(result!=CTR_GX_OK) { report.result=CTR_SCENE_LAYOUT;goto done; }
        if(chain_alias(g,target_address,target_bytes,&chain)) {
            result=CTR_GX_MEMORY;c->memory=CTR_MEM_OVERLAP;report.result=CTR_SCENE_ALIAS;goto done;
        }
        color_backing=ctr_gx_vram_backing(g,target_address,target_bytes);
        bool defer_color=software_game && backend && backend->deferred_color && color_backing &&
            color_format==CTR_SCENE_COLOR_RGBA8 && g->memory.resource_mapping_version!=UINT64_MAX;
        const ctr_scene_scanout_state *scanout=ctr_guest_scanout_state(g);
        reuse_color=defer_color && g->gsp.pending_color_address==target_address &&
            g->gsp.pending_color_width==width && g->gsp.pending_color_height==height &&
            (!scanout || !scanout->source_slot);
        if(!reuse_color && !ctr_guest_pica_flush_color(g)) {
            result=CTR_GX_MEMORY;c->memory=CTR_MEM_PENDING;report.result=CTR_SCENE_BACKEND;goto done;
        }
        if(depth_bytes) {
            result=linear(g,depth_address,depth_bytes,CTR_MEM_READ|CTR_MEM_WRITE,&depth_virtual,NULL,NULL,c);
            if(result!=CTR_GX_OK) { report.result=CTR_SCENE_LAYOUT;goto done; }
            if(chain_alias(g,depth_virtual,depth_bytes,&chain) ||
               command_alias(g,target_address,target_bytes,depth_virtual,depth_bytes)) {
                result=CTR_GX_MEMORY;c->memory=CTR_MEM_OVERLAP;report.result=CTR_SCENE_ALIAS;goto done;
            }
        }
        if(array_draws) {
            result=linear(g,(uint32_t)base,1u,CTR_MEM_READ,&resource_address,&resource_bytes,NULL,c);
            if(result!=CTR_GX_OK) { report.result=CTR_SCENE_LAYOUT;goto done; }
            /* The game VRAM aperture has one stable identity. Its array offsets
             * can point past the framebuffer, so the scene bounds each actual
             * vertex, index and texture source against both target ranges. */
            bool shared_vram=software_game && ctr_gx_vram_backing(g,resource_address,1u);
            if(!shared_vram && base<(uint64_t)color_address+target_bytes && color_address<base+resource_bytes) {
                if(color_address<=base) { result=CTR_GX_MEMORY;c->memory=CTR_MEM_OVERLAP;report.result=CTR_SCENE_ALIAS;goto done; }
                resource_bytes=color_address-(uint32_t)base;
            }
            if(!shared_vram && depth_bytes && base<(uint64_t)depth_address+depth_bytes && depth_address<base+resource_bytes) {
                if(depth_address<=base) { result=CTR_GX_MEMORY;c->memory=CTR_MEM_OVERLAP;report.result=CTR_SCENE_ALIAS;goto done; }
                resource_bytes=depth_address-(uint32_t)base;
            }
            /* The vertex decoder checks each access within the bounded window. */
            if(resource_bytes>CTR_VERTEX_MAX_SOURCE_BYTES)resource_bytes=CTR_VERTEX_MAX_SOURCE_BYTES;
            result=linear(g,(uint32_t)base,resource_bytes,CTR_MEM_READ,&resource_address,NULL,
                          stable_inputs?&geometry_bytes:NULL,c);
            if(result!=CTR_GX_OK) { report.result=CTR_SCENE_VERTEX;goto done; }
            if(!shared_vram && (command_alias(g,target_address,target_bytes,resource_address,resource_bytes) ||
               (depth_bytes && command_alias(g,depth_virtual,depth_bytes,resource_address,resource_bytes)))) {
                result=CTR_GX_MEMORY;c->memory=CTR_MEM_OVERLAP;report.result=CTR_SCENE_ALIAS;goto done;
            }
        }
        uint32_t pixel_bytes=width*height*4u;
        if(!ensure_buf((void **)&g->pica_color_raw,&g->pica_color_raw_cap,target_bytes) ||
           !ensure_buf((void **)&g->pica_color_pixels,&g->pica_color_pixels_cap,pixel_bytes)) {
            result=CTR_GX_NO_MEMORY;report.result=CTR_SCENE_NO_MEMORY;goto done;
        }
        raw=g->pica_color_raw;pixels=g->pica_color_pixels;
        if(resource_bytes && !geometry_bytes) {
            result=snapshot(g,CTR_RESOURCE_VERTEX,resource_address,resource_bytes,CTR_MEM_READ,&geometry,c);
            if(result!=CTR_GX_OK) { report.result=CTR_SCENE_VERTEX;goto done; }
            geometry_bytes=geometry.bytes;
        }
        direct_backend=g->gsp.pica_backend &&
            (g->gsp.pica_backend->features&CTR_SCENE_BACKEND_DIRECT)!=0u &&
            color_format==CTR_SCENE_COLOR_RGBA8;
        if(direct_backend) {
            keep_depth=depth_bytes && g->gsp.gpu_depth_valid &&
                depth_address==g->gsp.gpu_depth_address && width==g->gsp.gpu_depth_width &&
                height==g->gsp.gpu_depth_height && bound[4]==g->gsp.gpu_depth_format &&
                ctr_memory_resource_key_current(&g->memory,&g->gsp.gpu_depth_key);
            if(g->gsp.pica_depth_hint)g->gsp.pica_depth_hint(g->gsp.pica_backend->context,keep_depth);
        }
        if(depth_bytes) {
            if(!ensure_buf((void **)&g->pica_depth_pixels,&g->pica_depth_pixels_cap,depth_bytes) ||
               (!keep_depth && !ensure_buf((void **)&g->pica_depth_raw,&g->pica_depth_raw_cap,depth_bytes))) {
                result=CTR_GX_NO_MEMORY;report.result=CTR_SCENE_NO_MEMORY;goto done;
            }
            depth_pixels=g->pica_depth_pixels;
            if(!keep_depth)depth_raw=g->pica_depth_raw;
            if(!keep_depth) {
                result=snapshot(g,CTR_RESOURCE_DEPTH,depth_virtual,depth_bytes,CTR_MEM_READ|CTR_MEM_WRITE,&depth_initial,c);
                if(result!=CTR_GX_OK) { report.result=CTR_SCENE_LAYOUT;goto done; }
                const uint8_t *seed=depth_initial.bytes;
                bool uniform=true;
                if(depth_element==4u) {
                    uint32_t w0;memcpy(&w0,seed,4u);
                    for(uint32_t k=4u;k<depth_bytes;k+=4u) {
                        uint32_t wk;memcpy(&wk,seed+k,4u);
                        if(wk!=w0) { uniform=false;break; }
                    }
                    if(uniform)for(uint32_t k=0;k<depth_bytes;k+=4u)memcpy(depth_pixels+k,&w0,4u);
                } else {
                    uint16_t w0;memcpy(&w0,seed,2u);
                    for(uint32_t k=2u;k<depth_bytes;k+=2u) {
                        uint16_t wk;memcpy(&wk,seed+k,2u);
                        if(wk!=w0) { uniform=false;break; }
                    }
                    if(uniform)for(uint32_t k=0;k<depth_bytes;k+=2u)memcpy(depth_pixels+k,&w0,2u);
                }
                if(!uniform)depth_layout(depth_pixels,seed,width,height,depth_element,top_down,false);
                ctr_resource_cache_release(&depth_initial);
            }
            depth=(ctr_scene_depth_surface){depth_pixels,depth_bytes,width,height,width*depth_element,depth_address,bound[4]};
        }
        size_t total_texture_bytes=0;
        for(unsigned i=0;i<texture_count;) {
            uint64_t first=textures[i].base_address,last=first+textures[i].size;
            if(first>=base && last<=base+resource_bytes) { textures[i]=textures[--texture_count];continue; }
            /* A partially overlapping external span is ambiguous during replay.
             * Contained spans use the geometry buffer. */
            if(resource_bytes && first<base+resource_bytes && base<last) {
                result=CTR_GX_UNSUPPORTED;report.result=CTR_SCENE_ALIAS;goto done;
            }
            uint32_t texture_address;
            if(textures[i].size>CTR_SCENE_MAX_TEXTURE_SOURCE_BYTES-total_texture_bytes) {
                result=CTR_GX_UNSUPPORTED;report.result=CTR_SCENE_LIMIT;goto done;
            }
            total_texture_bytes+=textures[i].size;
            const uint8_t *texture_bytes=NULL;
            result=linear(g,(uint32_t)first,(uint32_t)textures[i].size,CTR_MEM_READ,&texture_address,NULL,
                          stable_inputs?&texture_bytes:NULL,c);
            if(result!=CTR_GX_OK) { report.result=CTR_SCENE_LAYOUT;goto done; }
            if(command_alias(g,target_address,target_bytes,texture_address,(uint32_t)textures[i].size) ||
               (depth_bytes && command_alias(g,depth_virtual,depth_bytes,texture_address,(uint32_t)textures[i].size))) {
                result=CTR_GX_MEMORY;c->memory=CTR_MEM_OVERLAP;report.result=CTR_SCENE_ALIAS;goto done;
            }
            if(!texture_bytes) {
                result=snapshot(g,CTR_RESOURCE_TEXTURE,texture_address,(uint32_t)textures[i].size,
                                CTR_MEM_READ,&texture_sources[i],c);
                if(result!=CTR_GX_OK) { report.result=CTR_SCENE_LAYOUT;goto done; }
                texture_bytes=texture_sources[i].bytes;
            }
            textures[i].bytes=texture_bytes;
            ++i;
        }
        resources=(ctr_scene_resources){(uint32_t)base,geometry_bytes,resource_bytes,textures,texture_count};
        surface=(ctr_scene_surface){pixels,pixel_bytes,width,height,width*4u};
        reuse_color=reuse_color && g->gsp.pending_color_address!=0;
        if(!reuse_color) {
            result=snapshot(g,CTR_RESOURCE_COLOR,target_address,target_bytes,CTR_MEM_READ|CTR_MEM_WRITE,&color_initial,c);
            if(result!=CTR_GX_OK) { report.result=CTR_SCENE_LAYOUT;goto done; }
            layout(pixels,color_initial.bytes,width,height,color_format,top_down,false);
        }
        if(defer_color && g->memory.resource_mapping_version!=UINT64_MAX) {
            deferred_backend=*backend;
            deferred_backend.features|=CTR_SCENE_BACKEND_DEFER_COLOR;
            if(reuse_color)deferred_backend.features|=CTR_SCENE_BACKEND_REUSE_COLOR;
            backend=&deferred_backend;
        }
    }
    ctr_scene_result rendered=draws?
        ctr_scene_execute_guest_writes(trial,commands,size,writes,write_count,&resources,&surface,color_address,color_format,
                                       depth_bytes?&depth:NULL,backend,g->pica_textures,&report):
        ctr_scene_apply_setup(trial,commands,size,writes,write_count,&report);
    if(draws && keep_depth && rendered==CTR_SCENE_OK && report.backend_fallback) {
        g->gsp.gpu_depth_valid=false;
        if(!depth_raw) {
            if(!ensure_buf((void **)&g->pica_depth_raw,&g->pica_depth_raw_cap,depth_bytes)) {
                result=CTR_GX_NO_MEMORY;report.result=CTR_SCENE_NO_MEMORY;goto done;
            }
            depth_raw=g->pica_depth_raw;
        }
        result=snapshot(g,CTR_RESOURCE_DEPTH,depth_virtual,depth_bytes,CTR_MEM_READ|CTR_MEM_WRITE,&depth_initial,c);
        if(result!=CTR_GX_OK) { report.result=CTR_SCENE_LAYOUT;goto done; }
        depth_layout(depth_pixels,depth_initial.bytes,width,height,depth_element,top_down,false);
        ctr_resource_cache_release(&depth_initial);
        if(g->gsp.pica_scene)*trial=*g->gsp.pica_scene; else ctr_scene_init(trial);
        trial->shader.round_f24=g->gsp.pica_round_f24;
        memcpy(trial->registers,g->gsp.pica.registers,sizeof(trial->registers));
        trial->lists=g->gsp.pica.committed_lists;
        if(reuse_color) {
            if(!ctr_guest_pica_flush_color(g)) {
                result=CTR_GX_MEMORY;c->memory=CTR_MEM_PENDING;report.result=CTR_SCENE_BACKEND;goto done;
            }
            result=snapshot(g,CTR_RESOURCE_COLOR,target_address,target_bytes,CTR_MEM_READ,&color_initial,c);
            if(result!=CTR_GX_OK) { report.result=CTR_SCENE_LAYOUT;goto done; }
        }
        layout(pixels,color_initial.bytes,width,height,color_format,top_down,false);
        report=(ctr_scene_report){0};
        rendered=ctr_scene_execute_guest_writes(trial,commands,size,writes,write_count,&resources,&surface,color_address,color_format,
                                               &depth,NULL,g->pica_textures,&report);
        report.backend_fallback=true;
    }
    ctr_resource_cache_release(&color_initial);
    if(rendered!=CTR_SCENE_OK) { result=rendered==CTR_SCENE_NO_MEMORY?CTR_GX_NO_MEMORY:CTR_GX_UNSUPPORTED;goto done; }
    if(draws) {
        bool skip_depth_write=depth_bytes && direct_backend && !report.backend_fallback;
        bool skip_color_write=report.color_deferred || (reuse_color && !report.backend_fallback);
        if(report.backend_fallback)++g->gsp.pica_fallback_lists;
        ctr_memory_write_range targets[2];unsigned target_count=0;
        if(!skip_color_write) {
            layout(raw,pixels,width,height,color_format,top_down,true);
            targets[target_count++]=(ctr_memory_write_range){target_address,raw,target_bytes};
        }
        if(depth_bytes && !skip_depth_write) {
            depth_layout(depth_raw,depth_pixels,width,height,depth_element,top_down,true);
            targets[target_count++]=(ctr_memory_write_range){depth_virtual,depth_raw,depth_bytes};
        }
        /* Commit ordinary mappings atomically before the already validated
         * VRAM writes. The staged targets have distinct backing spans. */
        ctr_memory_write_range ordinary[2];unsigned ordinary_count=0;
        bool video[2];
        for(unsigned i=0;i<target_count;++i) {
            video[i]=ctr_gx_vram_backing(g,targets[i].address,targets[i].size)!=NULL;
            if(!video[i])ordinary[ordinary_count++]=targets[i];
        }
        c->memory=ctr_memory_write_ranges(&g->memory,ordinary,ordinary_count);
        if(c->memory!=CTR_MEM_OK) { result=CTR_GX_MEMORY;report.result=CTR_SCENE_LAYOUT;goto done; }
        if(reuse_color && !skip_color_write)clear_pending_color(g);
        for(unsigned i=0;i<target_count;++i)if(video[i])
            ctr_gx_write_target(g,targets[i].address,targets[i].input,targets[i].size);
        if(report.color_deferred) {
            if(!reuse_color) {
                g->gsp.pending_color_address=target_address;
                g->gsp.pending_color_width=width;g->gsp.pending_color_height=height;
                ctr_memory_set_watch(&g->memory,color_backing,target_bytes,access_pending_color,g);
            }
            /* This is the guest-visible write. The later readback only
             * materializes the already committed bytes. */
            (void)ctr_memory_backing_changed(&g->memory,color_backing,target_bytes);
        }
        if(depth_bytes) {
            if(skip_depth_write) {
                g->gsp.gpu_depth_valid=ctr_memory_resource_key_capture(&g->memory,depth_virtual,depth_bytes,CTR_MEM_READ,&g->gsp.gpu_depth_key);
                g->gsp.gpu_depth_address=depth_address;g->gsp.gpu_depth_width=width;
                g->gsp.gpu_depth_height=height;g->gsp.gpu_depth_format=bound[4];
            } else g->gsp.gpu_depth_valid=false;
        }
    }
    /* No fallible operations remain after the atomic color/depth guest commit. */
    if(g->gsp.pica_scene)*g->gsp.pica_scene=*trial;
    else { g->gsp.pica_scene=trial;g->pica_trial=NULL; }
    const ctr_scene_state *state=g->gsp.pica_scene;
    memcpy(g->gsp.pica.registers,state->registers,sizeof(g->gsp.pica.registers));
    ++g->gsp.pica.committed_lists;g->gsp.pica.register_writes+=register_writes;g->gsp.pica.finalizations+=markers;
    g->gsp.pica_draws=state->draws;g->gsp.pica_triangles=state->triangles;
    g->gsp.pica_fragments=state->fragments;g->gsp.pica_shader_instructions=state->shader_instructions;
    result=CTR_GX_OK;
done:
    diagnostic(g,&report);
    if(result==CTR_GX_UNSUPPORTED) {
        c->detail=command_address;
        for(unsigned i=chain.count;i;--i) {
            const command_span *span=&chain.spans[i-1u];
            if(report.byte_offset>=span->offset) {
                size_t offset=report.byte_offset-span->offset;
                c->detail=span->address+(offset<span->size?(uint32_t)offset:0u);
                break;
            }
        }
    }
    for(unsigned i=0;i<texture_count;++i)ctr_resource_cache_release(&texture_sources[i]);
    ctr_resource_cache_release(&geometry);ctr_resource_cache_release(&color_initial);
    ctr_resource_cache_release(&depth_initial);
    return result;
}
void ctr_guest_set_scene_backend(ctr_guest *g,const ctr_scene_backend *backend) {
    if(g && ctr_guest_pica_flush_color(g) && ctr_guest_scanout_flush(g))g->gsp.pica_backend=backend;
}
