/* Original experimental owned-resource PICA scene boundary; no emulator linked. */
#include "ctr_scene.h"
#include "ctr_texture.h"
#include "ctr_transfer.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define GAME_LIST_TRIANGLES 65536u
#define GAME_LIST_RASTER_SAMPLES (16u*1024u*1024u)
enum { NATIVE_MIPS=2u };

const unsigned ctr_scene_texture_registers[CTR_SCENE_TEXTURE_UNITS][5]={
    {0x082u,0x083u,0x084u,0x085u,0x08eu},
    {0x092u,0x093u,0x094u,0x095u,0x096u},
    {0x09au,0x09bu,0x09cu,0x09du,0x09eu}
};

static bool texture_parameters(uint32_t params,bool mipmaps) {
    uint32_t allowed=UINT32_C(0x3326)|(mipmaps?UINT32_C(0x01000000):0u);
    return !(params&~allowed) && (mipmaps || (params&6u)==0u || (params&6u)==6u) &&
        ((params>>8)&3u)!=1u && ((params>>12)&3u)!=1u;
}

static bool texture_lod(uint32_t lod,bool mipmaps) {
    if(!mipmaps)return !lod;
    unsigned min=(lod>>24)&15u,max=(lod>>16)&15u;
    return !(lod&~UINT32_C(0x0f0f1fff)) && min<=max && max<CTR_SCENE_MAX_MIP_LEVELS;
}

ctr_scene_result ctr_scene_texture_layout_read(uint32_t dimensions,uint32_t params,uint32_t lod,
    unsigned format,bool mipmaps,ctr_scene_texture_layout *layout) {
    if(!layout)return CTR_SCENE_ARGUMENT;
    if((dimensions&~UINT32_C(0x07ff07ff)) || format>=14u || !texture_parameters(params,mipmaps) ||
       !texture_lod(lod,mipmaps))return CTR_SCENE_UNSUPPORTED;
    /* Earlier diagnostic descriptors also set this marker for ETC1A4. */
    if((mipmaps || format!=13u) && (params&32u)!=(format==12u?32u:0u))return CTR_SCENE_UNSUPPORTED;
    unsigned width=dimensions>>16,height=dimensions&2047u,min=(lod>>24)&15u,max=(lod>>16)&15u;
    size_t bytes,last;
    if(ctr_texture_size(format,width,height,&bytes)!=CTR_TEXTURE_OK ||
       ctr_texture_size(format,width>>max,height>>max,&last)!=CTR_TEXTURE_OK)return CTR_SCENE_LAYOUT;
    size_t total=bytes;
    for(unsigned level=1;level<=max;++level)total+=bytes>>(2u*level);
    *layout=(ctr_scene_texture_layout){width,height,min,max,total};
    return CTR_SCENE_OK;
}

typedef struct { uint64_t start,end; } scene_target_range;

static bool texture_target_alias(const scene_target_range targets[2],uint64_t start,uint64_t end) {
    for(unsigned i=0;i<2;++i)
        if(start<targets[i].end && targets[i].start<end)return true;
    return false;
}

static ctr_scene_result geometry_window(const ctr_scene_resources *resources,const scene_target_range targets[2],
                                        uint64_t start,bool software_game,ctr_scene_resource_span *source) {
    *source=(ctr_scene_resource_span){resources->base_address,resources->bytes,resources->size};
    if(software_game && (start<source->base_address || start-source->base_address>=source->size)) {
        for(unsigned i=0;i<resources->texture_count;++i) {
            const ctr_scene_resource_span *range=&resources->textures[i];
            if(start>=range->base_address && start<(uint64_t)range->base_address+range->size) {
                *source=*range;
                break;
            }
        }
        if(start<source->base_address || start-source->base_address>=source->size)return CTR_SCENE_LAYOUT;
    }
    for(unsigned i=0;i<2;++i) {
        if(start>=targets[i].end)continue;
        if(start>=targets[i].start)return CTR_SCENE_ALIAS;
        uint64_t stop=targets[i].start-source->base_address;
        if(stop<source->size)source->size=(size_t)stop;
    }
    return CTR_SCENE_OK;
}

static bool texture_register(unsigned reg,unsigned *field) {
    for(unsigned unit=0;unit<CTR_SCENE_TEXTURE_UNITS;++unit)
        for(unsigned index=0;index<5u;++index)if(reg==ctr_scene_texture_registers[unit][index]) {
            *field=index;return true;
        }
    return false;
}

static bool span(const void *p,size_t n) {
    return (!n || p) && n<=UINTPTR_MAX-(uintptr_t)p;
}
static bool overlap(const void *a,size_t an,const void *b,size_t bn) {
    return an && bn && (uintptr_t)a<(uintptr_t)b+bn && (uintptr_t)b<(uintptr_t)a+an;
}
static ctr_scene_result publish(ctr_scene_report *out,ctr_scene_report report,ctr_scene_result result) {
    report.result=result;if(out)*out=report;return result;
}
static float number(uint32_t bits) { float f;memcpy(&f,&bits,sizeof(f));return f; }
static unsigned bit_count(unsigned mask) {
    unsigned count=0;while(mask) { count+=mask&1u;mask>>=1; }return count;
}
static bool ordinary(unsigned reg) {
    return (reg>=0x040u && reg<=0x044u) || (reg>=0x04fu && reg<=0x056u) ||
           (reg>=0x065u && reg<=0x068u) || (reg>=0x200u && reg<=0x22au) ||
           reg==0x064u || reg==0x06fu || reg==0x25eu || reg==0x25fu ||
           (reg>=0x2b9u && reg<=0x2bdu);
}
static bool target_register(unsigned reg) {
    static const unsigned tev[]={0xc0,0xc8,0xd0,0xd8,0xf0,0xf8};
    for(unsigned i=0;i<6;++i)if(reg>=tev[i] && reg<=tev[i]+4u)return true;
    return reg==0x06eu || reg==0x080u || reg==0x08fu || reg==0x0e0u || reg==0x0fdu ||
        (reg>=0x100u && reg<=0x107u) || (reg>=0x112u && reg<=0x117u) ||
        (reg>=0x11bu && reg<=0x11eu);
}
static bool fragment_register(unsigned reg) {
    static const unsigned tev[]={0xc0,0xc8,0xd0,0xd8,0xf0,0xf8};
    for(unsigned i=0;i<6;++i)if(reg>=tev[i] && reg<=tev[i]+4u)return true;
    return (reg>=0x080u && reg<=0x08bu) || reg==0x08eu || reg==0x08fu ||
        (reg>=0x091u && reg<=0x096u) || (reg>=0x099u && reg<=0x09eu) ||
        reg==0x0e0u || reg==0x0fdu || reg==0x04du || reg==0x04eu || reg==0x06du ||
        reg==0x105u || reg==0x106u || reg==0x114u || reg==0x115u ||
        reg==0x116u || reg==0x11cu || ctr_fragment_register(reg);
}
static bool configured(const ctr_scene_state *s,unsigned reg,uint32_t value) {
    return s->register_defined[reg]==15u && s->registers[reg]==value;
}
static bool depth_access(uint32_t value,bool stencil,bool software_game) {
    if(stencil && software_game)return value<=3u;
    return value==0u || value==(stencil?3u:2u);
}
bool ctr_scene_color_format(uint32_t register_value,unsigned *format) {
    if(!format)return false;
    unsigned selected=register_value>>16,pixel_size=register_value&65535u;
    /* GPUREG_COLORBUFFER_FORMAT has independent size and format fields. */
    if(selected==CTR_SCENE_COLOR_RGBA8) {
        if(pixel_size!=0u && pixel_size!=2u)return false;
    } else if(selected<CTR_SCENE_COLOR_RGB5A1 || selected>CTR_SCENE_COLOR_RGBA4 || pixel_size!=0u)return false;
    *format=selected;
    return true;
}
static ctr_scene_result target_pipeline(const ctr_scene_state *s,const ctr_scene_surface *surface,
                                        uint32_t color_address,unsigned color_format,const ctr_scene_depth_surface *depth,
                                        bool software_game) {
    if(!configured(s,0x11bu,0u) || (!software_game && !configured(s,0x08fu,0u)))return CTR_SCENE_UNSUPPORTED;
    unsigned configured_format;
    if(s->register_defined[0x117u]!=15u || !ctr_scene_color_format(s->registers[0x117u],&configured_format) ||
       configured_format!=color_format)return CTR_SCENE_UNSUPPORTED;
    bool stencil=depth && depth->format==CTR_SCENE_DEPTH_D24S8;
    if(!stencil && (!configured(s,0x105u,0u) || !configured(s,0x106u,0u)))
        return CTR_SCENE_UNSUPPORTED;
    if(depth) {
        unsigned gate=stencil?3u:2u;
        unsigned required=(s->registers[0x107u]&1u)?2u:0u;
        if(stencil && (s->registers[0x105u]&1u))required|=1u;
        if(required && !software_game)required=gate;
        if(s->register_defined[0x114u]!=15u || s->register_defined[0x115u]!=15u ||
           !depth_access(s->registers[0x114u],stencil,software_game) ||
           !depth_access(s->registers[0x115u],stencil,software_game) ||
           !configured(s,0x116u,depth->format) || !configured(s,0x11cu,depth->address>>3) ||
           !configured(s,0x04du,UINT32_C(0xbf0000)) ||
           s->register_defined[0x04eu]!=15u ||
           (s->registers[0x04eu]!=0u && s->registers[0x04eu]!=UINT32_C(0x800000)) ||
           !configured(s,0x06du,1u) ||
           (s->registers[0x114u]&required)!=required)return CTR_SCENE_UNSUPPORTED;
    } else if(!configured(s,0x114u,0u) || !configured(s,0x115u,0u) ||
              s->registers[0x116u] || s->registers[0x11cu])return CTR_SCENE_UNSUPPORTED;
    uint32_t dimensions=surface->width|((surface->height-1u)<<12)|UINT32_C(0x01000000);
    if((color_address&7u) || (surface->width&7u) || (surface->height&7u) ||
       (s->register_defined[0x06eu] && !configured(s,0x06eu,dimensions)) ||
       !configured(s,0x11du,color_address>>3) || !configured(s,0x11eu,dimensions))return CTR_SCENE_LAYOUT;
    return CTR_SCENE_OK;
}
static unsigned exported_coordinates(const ctr_scene_state *s) {
    unsigned coordinates=0,total=s->registers[0x04fu];
    if(total>7u)return 0;
    for(unsigned word=0;word<total;++word)for(unsigned lane=0;lane<4u;++lane) {
        unsigned semantic=(s->registers[0x050u+word]>>(lane*8u))&31u;
        if(semantic==12u || semantic==13u)coordinates|=1u;
        else if(semantic==14u || semantic==15u)coordinates|=2u;
        else if(semantic==22u || semantic==23u)coordinates|=4u;
    }
    return coordinates;
}
static unsigned sampled_coordinates(uint32_t configuration) {
    unsigned enabled=configuration&7u;
    return (enabled&4u) && (configuration&(1u<<13))?(enabled&~4u)|2u:enabled;
}
static unsigned texture_coordinate(uint32_t configuration,unsigned unit) {
    return unit==2u && (configuration&(1u<<13))?1u:unit;
}
static unsigned combiner_sources(const ctr_tev_program *combiner) {
    unsigned sources=0;
    for(unsigned stage=0;stage<CTR_TEV_STAGES;++stage) {
        const ctr_tev_stage *p=&combiner->stages[stage];
        for(unsigned side=0;side<2u;++side)
            for(unsigned arg=0;arg<ctr_tev_arguments(p->operation[side]);++arg)
                sources|=1u<<p->source[side][arg];
    }
    return sources;
}
static bool fragment_clocks(const ctr_scene_state *s,unsigned required,unsigned allowed) {
    unsigned clocks=s->registers[0x06fu];
    return !s->register_defined[0x06fu] || (s->register_defined[0x06fu]==15u &&
           (clocks&required)==required && !(clocks&~allowed) &&
           (!(clocks&UINT32_C(0x20000)) || (clocks&UINT32_C(0x10000))));
}
static bool read_lighting(const ctr_scene_state *s,bool software_game,ctr_scene_light *light);
static ctr_scene_result fragment(const ctr_scene_state *s,const ctr_scene_resources *resources,
                                 bool bound,bool software_game,unsigned features,
                                 ctr_scene_sampler samplers[CTR_SCENE_TEXTURE_UNITS],
                                 ctr_texture_cache_lease leases[CTR_SCENE_TEXTURE_UNITS][CTR_SCENE_MAX_MIP_LEVELS],
                                 ctr_tev_program *combiner,unsigned *texture_mask,unsigned *sources,
                                 ctr_texture_cache *cache,const scene_target_range targets[2],ctr_scene_light *light) {
    unsigned configuration=s->registers[0x080u],coordinates=exported_coordinates(s);
    *texture_mask=configuration&7u;
    if(s->register_defined[0x064u] && !configured(s,0x064u,coordinates?1u:0u) &&
       !(software_game && !coordinates && !*texture_mask && configured(s,0x064u,1u)))
        return CTR_SCENE_UNSUPPORTED;
    if(!software_game && !fragment_clocks(s,3u|(sampled_coordinates(configuration)<<8),3u|(coordinates<<8)))
        return CTR_SCENE_UNSUPPORTED;
    if(configuration&~CTR_PICA_TEXTURE_CONFIG_MASK)return CTR_SCENE_UNSUPPORTED;
    bool configured_tev=s->register_defined[0x0e0u] || s->register_defined[0x0fdu];
    for(unsigned i=0;i<CTR_TEV_STAGES;++i)
        if(s->register_defined[ctr_tev_registers[i]])configured_tev=true;
    if(!*texture_mask && !bound && !configured_tev) {
        memset(combiner,0,sizeof(*combiner));
        for(unsigned i=1;i<CTR_TEV_STAGES;++i)
            combiner->stages[i].source[0][0]=combiner->stages[i].source[1][0]=15u;
        return CTR_SCENE_OK;
    }
    if(s->register_defined[0x080u]!=15u)return CTR_SCENE_UNSUPPORTED;
    bool native_mips=(features&NATIVE_MIPS)!=0u;
    bool native_fog=(features&CTR_SCENE_BACKEND_DEPTH_FOG)!=0u;
    bool decoded;
    if(software_game)decoded=ctr_tev_decode_game(s->registers,s->register_defined,*texture_mask,combiner);
    else if(native_fog)decoded=ctr_tev_decode_fog(s->registers,s->register_defined,*texture_mask,combiner);
    else decoded=ctr_tev_decode(s->registers,s->register_defined,*texture_mask,combiner);
    if(!decoded)return CTR_SCENE_UNSUPPORTED;
    if(native_fog && (s->registers[0x0e0u]&7u)==5u) {
        if(s->registers[0x0e1u]>0xffffffu)return CTR_SCENE_UNSUPPORTED;
        for(unsigned i=0;i<128u;++i)if(s->fog_lut[i]>0xffffffu)return CTR_SCENE_UNSUPPORTED;
    }
    if(software_game) {
        *sources=combiner_sources(combiner);
        if((*sources&6u) && !read_lighting(s,true,light))return CTR_SCENE_UNSUPPORTED;
        unsigned lighting_texture=light->active && light->bump_unit<CTR_SCENE_TEXTURE_UNITS?1u<<light->bump_unit:0u;
        *texture_mask&=(*sources>>3)|lighting_texture;
        unsigned required=1u|((*sources&1u)<<1)|(sampled_coordinates((configuration&~7u)|*texture_mask)<<8);
        /* Bit 17 is paired with the texture W clock in game sampler setup. */
        if(!fragment_clocks(s,required,UINT32_C(0x01030703)))return CTR_SCENE_UNSUPPORTED;
    }
    for(unsigned unit=0;unit<CTR_SCENE_TEXTURE_UNITS;++unit)if(*texture_mask&(1u<<unit)) {
        const unsigned *registers=ctr_scene_texture_registers[unit];
        for(unsigned field=0;field<5u;++field)
            if(s->register_defined[registers[field]]!=15u)return CTR_SCENE_UNSUPPORTED;
        if(!unit)for(unsigned reg=0x086u;reg<=0x08au;++reg)
            if(s->registers[reg])return CTR_SCENE_UNSUPPORTED;
        uint32_t dimensions=s->registers[registers[CTR_SCENE_TEXTURE_DIMENSIONS]],
            format=s->registers[registers[CTR_SCENE_TEXTURE_FORMAT]],
            params=s->registers[registers[CTR_SCENE_TEXTURE_PARAMETERS]],
            lod=s->registers[registers[CTR_SCENE_TEXTURE_LOD]];
        unsigned filters=params&6u;
        bool extended=software_game || (native_mips &&
            (lod || (params&UINT32_C(0x01000000)) || (filters!=0u && filters!=6u)));
        ctr_scene_texture_layout layout;
        ctr_scene_result result=ctr_scene_texture_layout_read(dimensions,params,lod,format,extended,&layout);
        if(result!=CTR_SCENE_OK)return result;
        uint32_t encoded_address=s->registers[registers[CTR_SCENE_TEXTURE_ADDRESS]];
        if(encoded_address>UINT32_C(0x0fffffff))return CTR_SCENE_UNSUPPORTED;
        uint32_t address=encoded_address*8u;const uint8_t *source=NULL;
        uint64_t end=(uint64_t)address+layout.byte_count;
        if(end>(uint64_t)UINT32_MAX+1u)return CTR_SCENE_OVERFLOW;
        if(texture_target_alias(targets,address,end))return CTR_SCENE_ALIAS;
        if(address>=resources->base_address && end<=(uint64_t)resources->base_address+resources->size)
            source=(const uint8_t *)resources->bytes+(address-resources->base_address);
        for(unsigned i=0;!source && i<resources->texture_count;++i) {
            const ctr_scene_resource_span *span=&resources->textures[i];
            if(address>=span->base_address && end<=(uint64_t)span->base_address+span->size)
                source=(const uint8_t *)span->bytes+(address-span->base_address);
        }
        if(!source)return CTR_SCENE_LAYOUT;
        ctr_scene_sampler *sampler=&samplers[unit];
        sampler->min_level=layout.min_level;
        sampler->max_level=layout.max_level;
        sampler->bias_q8=(int)(lod&0x0fffu)-(int)(lod&0x1000u);
        sampler->track_lod=layout.max_level>layout.min_level || (filters!=0u && filters!=6u);
        size_t offset=0;
        for(unsigned level=0;level<=layout.max_level;++level) {
            unsigned width=layout.width>>level,height=layout.height>>level;
            size_t bytes;
            if(ctr_texture_size(format,width,height,&bytes)!=CTR_TEXTURE_OK)return CTR_SCENE_LAYOUT;
            if(native_mips || level>=layout.min_level) {
                ctr_texture_cache_lease *lease=&leases[unit][level];
                ctr_texture_cache_result acquired=ctr_texture_cache_acquire(cache,source+offset,bytes,format,width,height,lease);
                if((software_game || native_mips) && acquired==CTR_TEXTURE_CACHE_BUSY)
                    acquired=ctr_texture_cache_acquire(NULL,source+offset,bytes,format,width,height,lease);
                if(acquired!=CTR_TEXTURE_CACHE_OK)
                    return acquired==CTR_TEXTURE_CACHE_NO_MEMORY || acquired==CTR_TEXTURE_CACHE_BUSY?
                        CTR_SCENE_NO_MEMORY:CTR_SCENE_LAYOUT;
                sampler->levels[level]=(ctr_scene_texture){lease->rgba,width,height,address+(uint32_t)offset,format,params};
            }
            offset+=bytes;
        }
    }
    return CTR_SCENE_OK;
}
static void free_textures(ctr_texture_cache_lease leases[CTR_SCENE_TEXTURE_UNITS][CTR_SCENE_MAX_MIP_LEVELS]) {
    for(unsigned unit=0;unit<CTR_SCENE_TEXTURE_UNITS;++unit)
        for(unsigned level=0;level<CTR_SCENE_MAX_MIP_LEVELS;++level)
            if(leases[unit][level].rgba)ctr_texture_cache_release(&leases[unit][level]);
}
/* Reject unsupported independently meaningful state even when it follows a
 * staged draw. Configuration may prepare a later list, but cannot hide an
 * unsupported filter, texture mode or combiner behind the last trigger. */
static bool fragment_value(unsigned reg,uint32_t value,bool software_game,unsigned features) {
    bool native_mips=(features&NATIVE_MIPS)!=0u;
    if(ctr_fragment_register(reg))return ctr_fragment_register_value(reg,value);
    if(reg==0x105u || reg==0x106u)return ctr_stencil_register_value(reg,value);
    if(reg==0x04du)return value==UINT32_C(0xbf0000);
    if(reg==0x04eu)return value==0u || value==UINT32_C(0x800000);
    if(reg==0x06du)return value==1u;
    if(reg==0x114u || reg==0x115u)return depth_access(value,true,software_game) || value==2u;
    if(reg==0x116u)return value==CTR_SCENE_DEPTH_D16 || value==CTR_SCENE_DEPTH_D24S8;
    if(reg==0x11cu)return value<=UINT32_C(0x0fffffff);
    if(reg==0x080u)return !(value&~CTR_PICA_TEXTURE_CONFIG_MASK);
    unsigned field;
    if(texture_register(reg,&field)) {
        if(field==CTR_SCENE_TEXTURE_DIMENSIONS)return !(value&~UINT32_C(0x07ff07ff));
        if(field==CTR_SCENE_TEXTURE_PARAMETERS) {
            /* Texture 0 can retain its disabled type. Active samplers still
             * require a checked 2D layout at the draw. */
            if(software_game && reg==0x083u && (value>>28)==5u)value&=UINT32_C(0x0fffffff);
            return texture_parameters(value,software_game || native_mips);
        }
        if(field==CTR_SCENE_TEXTURE_LOD)return texture_lod(value,software_game || native_mips);
        if(field==CTR_SCENE_TEXTURE_ADDRESS)return value<=UINT32_C(0x0fffffff);
        if(field==CTR_SCENE_TEXTURE_FORMAT)return value<14u;
    }
    if(reg==0x08bu)return value<=UINT32_C(0xffffff);
    if(reg>=0x086u && reg<=0x08au)return !value;
    if(reg==0x08fu)return value<=(software_game?1u:0u);
    if(software_game || (reg==0x0e0u && (features&CTR_SCENE_BACKEND_DEPTH_FOG)))
        return ctr_tev_game_register_value(reg,value);
    return ctr_tev_register_value(reg,value);
}
static ctr_scene_result packed(const uint32_t words[3],uint32_t bits[4]) {
    uint32_t raw[4]={words[2]&UINT32_C(0xffffff),
        ((words[1]&65535u)<<8)|(words[2]>>24),
        ((words[0]&255u)<<16)|(words[1]>>16),words[0]>>8};
    for(unsigned i=0;i<4;++i)if(!ctr_shader_f24_decode(raw[i],&bits[i]))return CTR_SCENE_NUMERIC;
    return CTR_SCENE_OK;
}
static ctr_scene_result immediate_attribute(ctr_scene_state *s,uint32_t value,unsigned mask,
                                             ctr_vertex *vertices,unsigned *count,unsigned *attribute) {
    unsigned last=s->registers[0x242u];
    if(mask!=15u || s->registers[0x245u] || s->registers[0x253u]!=1u ||
       last>=CTR_VERTEX_MAX_ATTRIBUTES || s->registers[0x2b9u]!=(UINT32_C(0xa0000000)|last))
        return CTR_SCENE_UNSUPPORTED;
    if(*count==CTR_SCENE_MAX_VERTICES)return CTR_SCENE_LIMIT;
    s->pending_fixed[s->fixed_words++]=value;
    if(s->fixed_words!=3u)return CTR_SCENE_OK;
    s->fixed_words=0;
    uint32_t bits[4];
    ctr_scene_result result=packed(s->pending_fixed,bits);
    if(result!=CTR_SCENE_OK)return result;
    ctr_vertex *vertex=&vertices[*count];
    if(!*attribute)memset(vertex,0,sizeof(*vertex));
    uint64_t order=(uint64_t)s->registers[0x2bbu]|(uint64_t)s->registers[0x2bcu]<<32;
    unsigned slot=(unsigned)(order>>(*attribute*4u))&15u;
    if(vertex->enabled&(1u<<slot))return CTR_SCENE_LAYOUT;
    vertex->enabled|=(uint16_t)(1u<<slot);
    memcpy(vertex->input[slot],bits,sizeof(bits));
    if(*attribute==last) {
        /* Immediate vertices load a fresh shader input bank. Unmapped slots
         * retain its zero initialization. */
        vertex->enabled=UINT16_MAX;
        ++*count;
        *attribute=0;
    } else ++*attribute;
    return CTR_SCENE_OK;
}
static ctr_scene_result upload(ctr_scene_state *s,unsigned reg,uint32_t value,unsigned mask) {
    if(mask!=15u)return CTR_SCENE_UNSUPPORTED;
    if(reg==0x2b0u) {
        unsigned padding=value>>16;
        if(padding && padding!=0x7fffu)return CTR_SCENE_UNSUPPORTED;
        s->registers[reg]=value;s->register_defined[reg]=15u;
        s->shader.boolean_uniforms=(uint16_t)value;s->shader.boolean_defined=true;
        return CTR_SCENE_OK;
    }
    if(reg==0x232u) {
        if(value>=12u || s->fixed_words)return CTR_SCENE_LAYOUT;
        s->registers[reg]=value;return CTR_SCENE_OK;
    }
    if(reg>=0x233u && reg<=0x235u) {
        unsigned index=s->registers[0x232u];
        if(index>=12u)return CTR_SCENE_LIMIT;
        s->pending_fixed[s->fixed_words++]=value;
        if(s->fixed_words==3u) {
            ctr_scene_result result=packed(s->pending_fixed,s->fixed[index]);
            if(result!=CTR_SCENE_OK)return result;
            s->fixed_defined|=(uint16_t)(1u<<index);
            s->fixed_words=0;s->registers[0x232u]=index+1u;
        }
    } else if(reg==0x2c0u) {
        if((value&UINT32_C(0x7fffff80)) || (value&127u)>=CTR_SHADER_UNIFORMS || s->uniform_words)
            return CTR_SCENE_LAYOUT;
        s->registers[reg]=value;
    } else if(reg>=0x2c1u && reg<=0x2c8u) {
        uint32_t config=s->registers[0x2c0u];unsigned index=config&127u;
        if(index>=CTR_SHADER_UNIFORMS)return CTR_SCENE_LIMIT;
        bool binary32=(config>>31)!=0;
        s->pending_uniform[s->uniform_words++]=value;
        if(s->uniform_words==(binary32?4u:3u)) {
            if(binary32) {
                for(unsigned i=0;i<4;++i) {
                    uint32_t bits=s->pending_uniform[i];
                    if(!s->shader.round_f24 && (bits&UINT32_C(0x7f800000))==UINT32_C(0x7f800000))
                        return CTR_SCENE_NUMERIC;
                    /* The game path also retains nonfinite bit patterns in
                       unused lanes. The shader still checks each operand read. */
                    s->shader.uniforms[index][3u-i]=bits;
                }
            } else {
                ctr_scene_result result=packed(s->pending_uniform,s->shader.uniforms[index]);
                if(result!=CTR_SCENE_OK)return result;
            }
            s->shader.uniform_defined[index]=15u;s->uniform_words=0;
            s->registers[0x2c0u]=(config&UINT32_C(0x80000000))|(index+1u);
        }
    } else if(reg==0x2cbu) {
        if(value>CTR_SHADER_MAX_CODE)return CTR_SCENE_LIMIT;
        s->registers[reg]=value;
    } else if(reg>=0x2ccu && reg<=0x2d3u) {
        unsigned index=s->registers[0x2cbu];
        if(index>=CTR_SHADER_MAX_CODE)return CTR_SCENE_LIMIT;
        s->shader.code[index]=value;s->shader.code_defined[index]=1u;
        s->registers[0x2cbu]=index+1u;
    } else if(reg==0x2d5u) {
        if(value>CTR_SHADER_MAX_DESCRIPTORS)return CTR_SCENE_LIMIT;
        s->registers[reg]=value;
    } else if(reg>=0x2d6u && reg<=0x2ddu) {
        unsigned index=s->registers[0x2d5u];
        if(index>=CTR_SHADER_MAX_DESCRIPTORS)return CTR_SCENE_LIMIT;
        /* The upload wire carries a 32-bit word with 31 operand bits. */
        s->shader.descriptors[index]=value&UINT32_C(0x7fffffff);s->shader.descriptor_defined[index]=1u;
        s->registers[0x2d5u]=index+1u;
    } else if(reg==0x2bfu) {
        if(value!=1u)return CTR_SCENE_UNSUPPORTED;
    } else return CTR_SCENE_UNSUPPORTED;
    return CTR_SCENE_OK;
}

/* Configuration writes modeled here never access a target or dispatch work.
 * Public register map: libctru GPU registers.h and Azahar d4090874 PICA register
 * declarations. Undocumented values remain limited to independently observed
 * hardware initialization defaults (nocash, nesdev 3DS reverse engineering). */
static bool setup_register(unsigned reg,uint32_t value) {
    switch(reg) {
    case 0x045u:case 0x046u:case 0x060u:case 0x08du:case 0x090u:
    case 0x097u:case 0x098u:return value==0u;
    case 0x04cu:case 0x1d4u:return value<=1u;
    case 0x058u:return value==UINT32_C(0x100);
    case 0x069u:return value==UINT32_C(0x20000);
    case 0x08cu:return value==UINT32_C(0xff0000);
    case 0x0aeu:return value==UINT32_C(0xfefcf8f0);
    case 0x11fu:return value==UINT32_C(0x10140);
    case 0x047u:case 0x062u:case 0x118u:case 0x1c6u:
    case 0x244u:case 0x245u:return value<=1u;
    case 0x253u:return !(value&~UINT32_C(0x101));
    case 0x061u:return value<=3u;
    case 0x242u:case 0x24au:case 0x251u:return value<=15u;
    case 0x238u:case 0x239u:return value<=0xfffffu;
    case 0x23au:case 0x23bu:return value<=0xfffffffu;
    case 0x0e1u:return value<=0xffffffu;
    case 0x048u:case 0x049u:case 0x04au:case 0x04bu:case 0x06au:
    case 0x0e4u:case 0x0e5u:case 0x120u:case 0x121u:case 0x122u:
    case 0x126u:case 0x130u:case 0x1c0u:case 0x1c2u:case 0x1c3u:case 0x1c4u:
    case 0x1d0u:case 0x1d1u:case 0x1d2u:case 0x1d9u:case 0x252u:case 0x254u:
    case 0x289u:case 0x28bu:case 0x28cu:return true;
    default:break;
    }
    if(reg>=0x140u && reg<=0x1bbu) {
        unsigned field=reg&15u;
        return field<=7u || (field>=9u && field<=11u);
    }
    return (reg>=0x0a8u && reg<=0x0adu) || (reg>=0x2b1u && reg<=0x2b4u);
}

static ctr_scene_result setup_write(ctr_scene_state *s,unsigned reg,uint32_t value,unsigned mask) {
    uint32_t merged=ctr_pica_merge(s->registers[reg],value,mask);
    if(ordinary(reg) || fragment_register(reg) || target_register(reg) || setup_register(reg,merged)) {
        /* Decode render state against the actual bindings when a draw uses it.
         * The texture-cache invalidate bit is a trigger. All texture reads use
         * current immutable source snapshots, so there is no stale cache data. */
        s->registers[reg]=reg==0x080u?merged&~CTR_PICA_CLEAR_TEXTURE_CACHE:merged;
        s->register_defined[reg]|=(uint8_t)mask;
        return CTR_SCENE_OK;
    }
    if(reg==0x2bfu && mask==15u && !value)return CTR_SCENE_OK;

    /* Fog entries can be prepared in a list before the first fogged draw. */
    if(reg==0x0e6u) {
        if(mask!=15u || value>=128u)return CTR_SCENE_LAYOUT;
        s->registers[reg]=value;return CTR_SCENE_OK;
    }
    if(reg>=0x0e8u && reg<=0x0efu) {
        if(mask!=15u || value>0xffffffu)return CTR_SCENE_UNSUPPORTED;
        unsigned index=s->registers[0x0e6u]&127u;
        s->fog_lut[index]=value;
        s->registers[0x0e6u]=(index+1u)&127u;
        return CTR_SCENE_OK;
    }

    /* The game path retains the two distribution tables and the Fresnel table. */
    if(reg==0x1c5u) {
        if(mask!=15u || value>=24u*256u)return CTR_SCENE_LAYOUT;
        s->registers[reg]=value;
    } else if(reg>=0x1c8u && reg<=0x1cfu) {
        uint32_t index=s->registers[0x1c5u];
        unsigned table=index>>8;
        bool retained=s->shader.round_f24 && (table<2u || table==3u);
        if(mask!=15u || value>0xffffffu || (!retained && value))return CTR_SCENE_UNSUPPORTED;
        if(retained)s->lighting_lut[table==3u?2u:table][index&255u]=value;
        s->registers[0x1c5u]=(index&~UINT32_C(255))|((index+1u)&255u);
    } else if(reg==0x29bu) {
        if(mask!=15u || value>4096u)return CTR_SCENE_LIMIT;
        s->registers[reg]=value;
    } else if(reg>=0x29cu && reg<=0x2a3u) {
        if(mask!=15u || value)return CTR_SCENE_UNSUPPORTED;
        if(s->registers[0x29bu]>=4096u)return CTR_SCENE_LIMIT;
        ++s->registers[0x29bu];
    } else if(reg==0x290u) {
        if(mask!=15u || (value&UINT32_C(0x7fffffff))>=96u || s->geometry_uniform_words)
            return CTR_SCENE_LAYOUT;
        s->registers[reg]=value;
    } else if(reg>=0x291u && reg<=0x298u) {
        uint32_t config=s->registers[0x290u];
        bool binary32=(config>>31)!=0;
        if(mask!=15u || (binary32?(value&0x7f800000u)==0x7f800000u:value!=0))return CTR_SCENE_UNSUPPORTED;
        if((config&127u)>=96u)return CTR_SCENE_LIMIT;
        /* Draws require geometry processing to be disabled. Consume the dormant
           uniform uploads with the same bounded cursor and word counts. */
        if(++s->geometry_uniform_words==(binary32?4u:3u)) {
            ++s->registers[0x290u];s->geometry_uniform_words=0;
        }
    } else return upload(s,reg,value,mask);
    return CTR_SCENE_OK;
}
typedef struct {
    unsigned total,fixed,slots[12];
    ctr_vertex_attribute attributes[12];
    ctr_vertex_stream streams[12];
    size_t attribute_count,stream_count;
    ctr_vertex_indices indices;
    float fixed_values[12][4];
} scene_vertex_layout;
static ctr_scene_result vertices_prepare(const ctr_scene_state *s,const ctr_scene_resources *resources,
                                         bool indexed,const scene_target_range targets[2],bool software_game,
                                         scene_vertex_layout *layout) {
    uint32_t high=s->registers[0x202u],low=s->registers[0x201u];
    unsigned total=(high>>28)+1u,fixed=(high>>16)&4095u;
    uint32_t config=s->registers[0x2b9u];
    if(total>12u || (fixed>>total) || config!=(UINT32_C(0xa0000000)|(total-1u)) ||
       (s->registers[0x200u]&UINT32_C(0xe0000001)))return CTR_SCENE_UNSUPPORTED;
    uint64_t base=(uint64_t)(s->registers[0x200u]>>1)*16u;
    if(base>UINT32_MAX)return CTR_SCENE_OVERFLOW;
    if(!software_game && base!=resources->base_address)return CTR_SCENE_LAYOUT;
    uint64_t permutation=(uint64_t)s->registers[0x2bbu]|(uint64_t)s->registers[0x2bcu]<<32;
    uint16_t inputs=0,loaded=0;
    layout->total=total;layout->fixed=fixed;
    for(unsigned a=0;a<total;++a) {
        layout->slots[a]=(unsigned)((permutation>>(a*4u))&15u);
        if(inputs&(1u<<layout->slots[a]))return CTR_SCENE_LAYOUT;
        inputs|=(uint16_t)(1u<<layout->slots[a]);
        if(fixed&(1u<<a)) {
            if(!(s->fixed_defined&(1u<<a)))return CTR_SCENE_LAYOUT;
            for(unsigned lane=0;lane<4;++lane)layout->fixed_values[a][lane]=number(s->fixed[a][lane]);
        }
    }
    size_t attribute_count=0,stream_count=0;
    for(unsigned bank=0;bank<12;++bank) {
        unsigned reg=0x203u+bank*3u;
        uint32_t offset=s->registers[reg],c1=s->registers[reg+1],c2=s->registers[reg+2];
        unsigned count=c2>>28,stride=(c2>>16)&255u,at=0;bool used=false;
        if((offset&UINT32_C(0xf0000000)) || (c2&UINT32_C(0x0f000000)) || count>12u)
            return CTR_SCENE_UNSUPPORTED;
        if(count && !stride)return CTR_SCENE_LAYOUT;
        for(unsigned k=0;k<count;++k) {
            unsigned id=k<8u?(c1>>(k*4u))&15u:(c2>>((k-8u)*4u))&15u;
            if(id>=12u) { at=(at+3u)&~3u;at+=(id-11u)*4u; }
            else {
                if(id>=total || (loaded&(1u<<id)))return CTR_SCENE_LAYOUT;
                loaded|=(uint16_t)(1u<<id);
                unsigned format=id<8u?(low>>(id*4u))&15u:(high>>((id-8u)*4u))&15u;
                unsigned type=format&3u,components=(format>>2)+1u;
                unsigned width=type==3u?4u:type==2u?2u:1u;
                at=(at+width-1u)&~(width-1u);
                if(!(fixed&(1u<<id))) {
                    layout->attributes[attribute_count++]=(ctr_vertex_attribute){
                        (unsigned)stream_count,layout->slots[id],type,components,at};used=true;
                }
                at+=width*components;
            }
            if(at>stride)return CTR_SCENE_LAYOUT;
        }
        if(used) {
            ctr_scene_resource_span source;
            ctr_scene_result result=geometry_window(resources,targets,base+offset,software_game,&source);
            if(result!=CTR_SCENE_OK)return result;
            layout->streams[stream_count++]=(ctr_vertex_stream){source.bytes,source.size,
                (size_t)(base+offset-source.base_address),stride};
        }
    }
    if(((loaded|fixed)&((1u<<total)-1u))!=((1u<<total)-1u))return CTR_SCENE_LAYOUT;
    layout->attribute_count=attribute_count;layout->stream_count=stream_count;
    layout->indices=(ctr_vertex_indices){0};
    if(indexed) {
        uint32_t raw=s->registers[0x227u],offset=raw&UINT32_C(0x0fffffff);
        if(raw&UINT32_C(0x70000000))return CTR_SCENE_UNSUPPORTED;
        ctr_scene_resource_span source;
        ctr_scene_result result=geometry_window(resources,targets,base+offset,software_game,&source);
        if(result!=CTR_SCENE_OK)return result;
        layout->indices=(ctr_vertex_indices){source.bytes,source.size,(size_t)(base+offset-source.base_address),raw>>31};
    }
    return CTR_SCENE_OK;
}
static ctr_scene_result vertices_decode(const ctr_scene_state *s,const scene_vertex_layout *layout,
                                        bool indexed,unsigned start,unsigned count,ctr_vertex *output,
                                        ctr_scene_report *report) {
    if(layout->attribute_count) {
        ctr_vertex_result result;
        if(indexed)result=ctr_vertex_decode_indexed(layout->attributes,layout->attribute_count,
            layout->streams,layout->stream_count,&layout->indices,start,count,output,count);
        else {
            uint32_t offset=s->registers[0x22au];
            if(start>UINT32_MAX-offset)return CTR_SCENE_OVERFLOW;
            result=ctr_vertex_decode(layout->attributes,layout->attribute_count,
                layout->streams,layout->stream_count,offset+start,count,output,count);
        }
        report->vertex_result=result;
        if(result!=CTR_VERTEX_OK)return CTR_SCENE_VERTEX;
    } else {
        /* Fixed-only shaders still validate an indexed draw's entire index span. */
        if(indexed) {
            uint64_t last=(uint64_t)layout->indices.offset+(uint64_t)(start+count)*(layout->indices.format+1u);
            if(last>layout->indices.size)return CTR_SCENE_LAYOUT;
        }
        memset(output,0,count*sizeof(*output));
        for(unsigned vertex=0;vertex<count;++vertex)
            for(unsigned input=0;input<16;++input)output[vertex].input[input][3]=1.0f;
    }
    for(unsigned vertex=0;vertex<count;++vertex)for(unsigned a=0;a<layout->total;++a)if(layout->fixed&(1u<<a)) {
        for(unsigned lane=0;lane<4;++lane)output[vertex].input[layout->slots[a]][lane]=layout->fixed_values[a][lane];
        output[vertex].enabled|=(uint16_t)(1u<<layout->slots[a]);
    }
    return CTR_SCENE_OK;
}
typedef struct {
    int32_t x,y,uv[CTR_SCENE_TEXTURE_UNITS][2];
    float position[4],color[4],orientation[4],view[3];
    double inverse_w,z;
    unsigned repeat_origin;
} scene_vertex;

static double vector_dot(const double a[3],const double b[3]) {
    return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
}

static void normalize(double *v,unsigned terms) {
    double length=0.0;
    for(unsigned i=0;i<terms;++i)length+=v[i]*v[i];
    if(length>0.0) {
        double inverse=1.0/sqrt(length);
        for(unsigned i=0;i<terms;++i)v[i]*=inverse;
    }
}

static bool light_component(uint16_t bits,double *value) {
    unsigned exponent=(bits>>10)&31u,fraction=bits&1023u;
    if(exponent==31u)return false;
    double component=ldexp((double)(exponent?1024u+fraction:fraction),exponent?(int)exponent-25:-24);
    *value=(bits&0x8000u)?-component:component;
    return true;
}

static unsigned light_channel(uint32_t color,unsigned channel) { return (color>>(20u-channel*10u))&1023u; }

static bool read_lighting(const ctr_scene_state *s,bool software_game,ctr_scene_light *light) {
    unsigned enable=s->registers[0x08fu],disable=s->registers[0x1c6u];
    if(!enable)return !s->register_defined[0x1c6u] || disable==1u;
    if(!software_game || enable!=1u || disable || (s->registers[0x1c2u]&7u))return false;
    unsigned slot=s->registers[0x1d9u]&7u,base=0x140u+slot*16u;
    uint32_t config=s->registers[0x1c3u],masks=s->registers[0x1c4u],flags=s->registers[base+9u];
    unsigned mode=(config>>4)&15u;
    if((config&~UINT32_C(0xd8c0043c)) || (mode!=0u && mode!=3u) ||
       !(masks&(1u<<(slot+24u))) || !(masks&(1u<<(slot+8u))) ||
       (masks&UINT32_C(0x700000))!=UINT32_C(0x700000) || !(flags&1u) || flags>15u)return false;
    light->active=true;
    light->flags=flags;
    light->alpha=(config>>2)&3u;
    light->bump_unit=(config&(1u<<28))?(config>>22)&3u:CTR_SCENE_TEXTURE_UNITS;
    light->clamp_highlights=(config&(1u<<27))!=0;
    light->reconstruct_normal=(config&(1u<<30))==0;
    if((config&(1u<<28)) && (light->bump_unit>=CTR_SCENE_TEXTURE_UNITS ||
       !(s->registers[0x080u]&(1u<<light->bump_unit))))return false;
    if(!light_component((uint16_t)s->registers[base+4u],&light->direction[0]) ||
       !light_component((uint16_t)(s->registers[base+4u]>>16),&light->direction[1]) ||
       !light_component((uint16_t)s->registers[base+5u],&light->direction[2]))return false;
    normalize(light->direction,3u);
    for(unsigned channel=0;channel<3u;++channel) {
        light->ambient[channel]=light_channel(s->registers[0x1c0u],channel)+light_channel(s->registers[base+3u],channel);
        light->diffuse[channel]=light_channel(s->registers[base+2u],channel);
        for(unsigned i=0;i<2u;++i)light->specular[i][channel]=light_channel(s->registers[base+i],channel);
    }
    static const double scales[]={1.0,2.0,4.0,8.0,0.0,0.0,0.25,0.5};
    static const unsigned disabled_bits[]={16,17,19},fields[]={0,4,12};
    light->needs_view=(flags&12u)!=0;
    for(unsigned table=0;table<3u;++table) {
        if((mode==0u && table) || (masks&(1u<<disabled_bits[table])))continue;
        unsigned field=fields[table];
        light->table[table]=s->lighting_lut[table];
        light->input[table]=(s->registers[0x1d1u]>>field)&7u;
        light->scale[table]=scales[(s->registers[0x1d2u]>>field)&7u];
        light->absolute[table]=(s->registers[0x1d0u]&(2u<<field))==0;
        if((light->input[table]>3u && light->input[table]!=5u) || light->scale[table]==0.0)return false;
        light->needs_view|=light->input[table]<3u;
    }
    return true;
}
static bool viewport(const ctr_scene_state *s,float half[2],int offsets[2]) {
    uint32_t hx,hy,origin=s->registers[0x068u];
    if(!ctr_shader_f24_decode(s->registers[0x041u],&hx) ||
       !ctr_shader_f24_decode(s->registers[0x043u],&hy) || (origin&~UINT32_C(0x03ff03ff)))return false;
    half[0]=number(hx);half[1]=number(hy);
    offsets[0]=(int)(origin&1023u);offsets[1]=(int)((origin>>16)&1023u);
    for(unsigned axis=0;axis<2;++axis)if(offsets[axis]&512)offsets[axis]-=1024;
    return true;
}
static ctr_scene_result project_vertex(scene_vertex *v,const ctr_scene_surface *surface,
                                       const float half[2],const int offsets[2],bool game) {
    if(v->position[3]<=0.0f)return CTR_SCENE_UNSUPPORTED;
    v->inverse_w=1.0/v->position[3];v->z=v->position[2]*v->inverse_w;
    for(unsigned axis=0;axis<2;++axis) {
        double coordinate=(v->position[axis]*v->inverse_w+1.0)*half[axis]+offsets[axis];
        unsigned extent=axis?surface->height:surface->width;
        if(game?(coordinate < -1024.0 || coordinate > 1024.0):(coordinate<0 || coordinate>extent))
            return CTR_SCENE_UNSUPPORTED;
        double scaled=coordinate*16.0;
        int32_t fixed=(int32_t)(scaled+(game?(scaled<0.0?-0.5:0.5):0.0));
        if(!game && (double)fixed!=scaled)return CTR_SCENE_NUMERIC;
        if(axis)v->y=fixed;else v->x=fixed;
    }
    return CTR_SCENE_OK;
}
static ctr_scene_result semantics(const ctr_scene_state *s,const ctr_shader_output *out,
                                  const ctr_scene_surface *surface,scene_vertex *v,unsigned required_coordinates,
                                  bool require_color,bool software_game,const ctr_scene_light *light,unsigned repeat_axes) {
    v->repeat_origin=0;
    unsigned mask=s->registers[0x2bdu],total=s->registers[0x04fu],packed_index=0,seen=0;
    if((mask&~65535u) || !mask || total>7u || total!=bit_count(mask))return CTR_SCENE_UNSUPPORTED;
    for(unsigned reg=0;reg<16;++reg)if(mask&(1u<<reg)) {
        uint32_t mapping=s->registers[0x050u+packed_index++];
        if(mapping&UINT32_C(0xe0e0e0e0))return CTR_SCENE_UNSUPPORTED;
        for(unsigned lane=0;lane<4;++lane) {
            unsigned id=(mapping>>(lane*8u))&31u;
            if(id==31u)continue;
            bool orientation=id>=4u && id<8u,view=id>=18u && id<=20u;
            if(software_game && (id==16u || ((orientation || view) && !light->active)))continue;
            if(!require_color && id>=8u && id<12u)continue;
            if(!((id<=15u && !orientation) || id==22u || id==23u || (light->active && (orientation || view))))
                return CTR_SCENE_UNSUPPORTED;
            unsigned unit=id>=22u?2u:(id>=12u?(id-12u)/2u:0u);
            if(s->shader.round_f24 && id>=12u && !view && !(required_coordinates&(1u<<unit)))continue;
            if((seen&(1u<<id)) || !(out->written[reg]&(1u<<lane)))return CTR_SCENE_SHADER;
            float component=out->values[reg][lane];
            if(!isfinite(component)) {
                bool uv=(id>=12u && id<=15u) || id==22u || id==23u;
                unsigned axis=1u<<(unit*2u+(id&1u));
                if(!software_game || !uv || !isinf(component) || component<0.0f || !(repeat_axes&axis))
                    return CTR_SCENE_NUMERIC;
                v->repeat_origin|=axis;
                component=0.0f;
            }
            seen|=1u<<id;
            if(id<4u)v->position[id]=component;
            else if(orientation)v->orientation[id-4u]=component;
            else if(id<12u)v->color[id-8u]=component;
            else if(view)v->view[id-18u]=component;
            else {
                double scaled=(double)component*65536.0;
                double lower=s->shader.round_f24?INT32_MIN:-1048576.0;
                double upper=s->shader.round_f24?INT32_MAX:1048576.0;
                if(scaled<lower || scaled>upper)return CTR_SCENE_UNSUPPORTED;
                int32_t fixed=(int32_t)scaled;
                if(!s->shader.round_f24 && (double)fixed!=scaled)return CTR_SCENE_NUMERIC;
                v->uv[unit][id&1u]=fixed;
            }
        }
    }
    unsigned required=0x0fu|(require_color?0x0f00u:0u);
    if((seen&required)!=required)return CTR_SCENE_SHADER;
    if(light->active && ((seen&0xf0u)!=0xf0u || (light->needs_view && (seen&UINT32_C(0x1c0000))!=UINT32_C(0x1c0000))))
        return CTR_SCENE_SHADER;
    const uint32_t pairs[CTR_SCENE_TEXTURE_UNITS]={UINT32_C(0x3000),UINT32_C(0xc000),UINT32_C(0xc00000)};
    unsigned coordinates=0;
    for(unsigned unit=0;unit<CTR_SCENE_TEXTURE_UNITS;++unit) {
        uint32_t pair=seen&pairs[unit];
        if(pair && pair!=pairs[unit])return CTR_SCENE_SHADER;
        if(pair)coordinates|=1u<<unit;
    }
    if((coordinates&required_coordinates)!=required_coordinates)return CTR_SCENE_SHADER;
    if(software_game)return CTR_SCENE_OK;
    bool game=s->shader.round_f24;
    if(game) {
        if(v->position[3]<=0.0f)return CTR_SCENE_UNSUPPORTED;
    } else {
        if(v->position[3]!=1.0f || v->position[0]<-1.0f || v->position[0]>1.0f ||
           v->position[1]<-1.0f || v->position[1]>1.0f || v->position[2]<-1.0f || v->position[2]>0.0f ||
           v->color[3]!=1.0f)return CTR_SCENE_UNSUPPORTED;
        for(unsigned lane=0;lane<4;++lane)if(v->color[lane]!=0.0f && v->color[lane]!=1.0f)
            return CTR_SCENE_UNSUPPORTED;
    }
    float half[2];int offsets[2];
    if(!viewport(s,half,offsets))return CTR_SCENE_NUMERIC;
    if(half[0]<=0 || half[1]<=0 || half[0]>(float)surface->width/2 || half[1]>(float)surface->height/2)
        return CTR_SCENE_UNSUPPORTED;
    return project_vertex(v,surface,half,offsets,game);
}

#define CLIPPED_TRIANGLE_VERTICES 10u

static double clip_distance(const scene_vertex *v,unsigned plane) {
    if(plane==6u)return (double)v->position[3]-FLT_MIN;
    double component=v->position[plane/2u];
    if(plane==5u)return -component;
    return (double)v->position[3]+((plane&1u)?-component:component);
}

static scene_vertex clip_crossing(const scene_vertex *a,const scene_vertex *b,double weight,unsigned plane) {
    scene_vertex v={0};
    for(unsigned i=0;i<4u;++i) {
        v.position[i]=(float)(a->position[i]+((double)b->position[i]-a->position[i])*weight);
        v.color[i]=(float)(a->color[i]+((double)b->color[i]-a->color[i])*weight);
        v.orientation[i]=(float)(a->orientation[i]+((double)b->orientation[i]-a->orientation[i])*weight);
    }
    for(unsigned i=0;i<3u;++i)v.view[i]=(float)(a->view[i]+((double)b->view[i]-a->view[i])*weight);
    for(unsigned unit=0;unit<CTR_SCENE_TEXTURE_UNITS;++unit)for(unsigned axis=0;axis<2;++axis)
        v.uv[unit][axis]=(int32_t)(a->uv[unit][axis]+((double)b->uv[unit][axis]-a->uv[unit][axis])*weight);
    v.repeat_origin=(weight<1.0?a->repeat_origin:0u)|(weight>0.0?b->repeat_origin:0u);
    if(plane==6u)v.position[3]=FLT_MIN;
    else if(plane==5u)v.position[2]=0.0f;
    else v.position[plane/2u]=(plane&1u)?v.position[3]:-v.position[3];
    return v;
}

static ctr_scene_result clip_and_project(const ctr_scene_surface *surface,const float half[2],const int offsets[2],
                                        const scene_vertex triangle[3],scene_vertex polygon[CLIPPED_TRIANGLE_VERTICES],
                                        unsigned *count) {
    unsigned common=127u,outside=0u;
    for(unsigned vertex=0;vertex<3u;++vertex) {
        unsigned mask=0;
        for(unsigned plane=0;plane<7u;++plane)
            if(clip_distance(&triangle[vertex],plane)<0.0)mask|=1u<<plane;
        common&=mask;outside|=mask;
    }
    *count=0;
    if(common)return CTR_SCENE_OK;
    scene_vertex scratch[CLIPPED_TRIANGLE_VERTICES];
    scene_vertex *current=polygon,*next=scratch;
    memcpy(current,triangle,3u*sizeof(*triangle));unsigned vertices=3u;
    static const unsigned plane_order[]={6,0,1,2,3,4,5};
    for(unsigned index=0;index<7u;++index) {
        unsigned plane=plane_order[index];
        if(!(outside&(1u<<plane)))continue;
        unsigned used=0;
        const scene_vertex *previous=&current[vertices-1u];
        double old=clip_distance(previous,plane);
        for(unsigned i=0;i<vertices;++i) {
            const scene_vertex *vertex=&current[i];double distance=clip_distance(vertex,plane);
            if((distance<0.0 && old>0.0) || (distance>0.0 && old<0.0)) {
                if(used==CLIPPED_TRIANGLE_VERTICES)return CTR_SCENE_LIMIT;
                next[used++]=clip_crossing(previous,vertex,old/(old-distance),plane);
            }
            if(distance>=0.0) {
                if(used==CLIPPED_TRIANGLE_VERTICES)return CTR_SCENE_LIMIT;
                next[used++]=*vertex;
            }
            old=distance;previous=vertex;
        }
        if(used<3u)return CTR_SCENE_OK;
        scene_vertex *swap=current;current=next;next=swap;vertices=used;
    }
    for(unsigned i=0;i<vertices;++i) {
        ctr_scene_result result=project_vertex(&current[i],surface,half,offsets,true);
        if(result!=CTR_SCENE_OK)return result;
        current[i].z=fmax(-1.0,fmin(0.0,current[i].z));
    }
    if(current!=polygon)memcpy(polygon,current,vertices*sizeof(*current));
    *count=vertices;
    return CTR_SCENE_OK;
}
static int64_t edge(scene_vertex a,scene_vertex b,int32_t x,int32_t y) {
    return (int64_t)(b.x-a.x)*(y-a.y)-(int64_t)(b.y-a.y)*(x-a.x);
}
static bool culled(int64_t area,unsigned mode) {
    return !area || (mode==1u && area>0) || (mode==2u && area<0);
}
static bool inclusive(scene_vertex a,scene_vertex b) {
    return b.y<a.y || (b.y==a.y && b.x<a.x);
}
static int64_t floor_div(int64_t numerator,int64_t denominator) {
    int64_t value=numerator/denominator;
    return value-(numerator%denominator<0);
}
static unsigned wrapped(int64_t coordinate,unsigned extent,unsigned mode) {
    if(!mode)return coordinate<0?0u:coordinate>=(int64_t)extent?extent-1u:(unsigned)coordinate;
    unsigned period=mode==3u?extent*2u:extent;
    unsigned remainder;
    if((period&(period-1u))==0u)remainder=(unsigned)coordinate&(period-1u);
    else {
        int64_t offset=coordinate%(int64_t)period;
        remainder=(unsigned)(offset<0?offset+period:offset);
    }
    return mode==3u && remainder>=extent?period-1u-remainder:remainder;
}
/* Bilinear weights retain the exact denominator. Explicit limbs work on
 * 32-bit PowerPC/MSVC without
 * compiler-specific wide integers or floating-point rounding dependencies. */
typedef struct { uint64_t hi,lo; } scene_wide;
static scene_wide multiply_wide(uint64_t a,uint64_t b) {
    uint64_t p0=(uint64_t)(uint32_t)a*(uint32_t)b;
    uint64_t p1=(uint64_t)(uint32_t)a*(b>>32),p2=(a>>32)*(uint32_t)b;
    uint64_t middle=(p0>>32)+(uint32_t)p1+(uint32_t)p2;
    return (scene_wide){(a>>32)*(b>>32)+(p1>>32)+(p2>>32)+(middle>>32),
        (middle<<32)|(uint32_t)p0};
}
static uint8_t bilinear_byte(unsigned a,unsigned b,unsigned c,unsigned d,
                             uint64_t x,uint64_t y,uint64_t denominator,double fraction_x,double fraction_y) {
    if(a==b && a==c && a==d)return (uint8_t)a;
#if FLT_RADIX==2 && DBL_MANT_DIG>=53
    /* With binary64 the error on a 0..255 weighted sum is below 2^-30.
     * Use exact arithmetic when that error could cross the half-byte tie. */
    double approx_top=a+(b-(double)a)*fraction_x;
    double approx_bottom=c+(d-(double)c)*fraction_x;
    double shifted=approx_top+(approx_bottom-approx_top)*fraction_y+0.5;
    unsigned rounded=(unsigned)shifted;
    double remainder=shifted-rounded;
    if(remainder>0x1p-30 && remainder<1.0-0x1p-30)return (uint8_t)rounded;
#else
    (void)fraction_x;(void)fraction_y;
#endif
    uint64_t top=a*(denominator-x)+b*x,bottom=c*(denominator-x)+d*x;
    /* Flooring horizontal rows underestimates the ideal value by less than
     * one byte. Correct its rounded estimate against the exact half threshold;
     * this avoids both intermediate rounding and a wide division per lane. */
    unsigned estimate=(unsigned)((top/denominator*(denominator-y)+
        bottom/denominator*y+denominator/2u)/denominator);
    if(estimate<255u) {
        scene_wide first=multiply_wide(top,denominator-y),second=multiply_wide(bottom,y);
        scene_wide twice={first.hi+second.hi,first.lo+second.lo};
        twice.hi+=twice.lo<first.lo;
        twice.hi=(twice.hi<<1)|(twice.lo>>63);twice.lo<<=1;
        scene_wide half=multiply_wide(denominator,denominator*(2u*estimate+1u));
        if(twice.hi>half.hi || (twice.hi==half.hi && twice.lo>=half.lo))++estimate;
    }
    return (uint8_t)estimate;
}

static void sample_texture(const ctr_scene_texture *texture,const int64_t uv[2],uint64_t denominator,
                           bool linear,uint8_t output[4]) {
    unsigned coordinates[2][2];
    uint64_t fraction[2]={0,0};
    for(unsigned axis=0;axis<2;++axis) {
        unsigned extent=axis?texture->height:texture->width;
        int64_t numerator=uv[axis],texel_offset=0,uv_scale=(int64_t)denominator;
        /* Separate whole repeats before scaling a large coordinate. */
        int64_t scale_limit=(INT64_MAX-uv_scale)/2048;
        if(denominator!=65536u && (numerator < -scale_limit || numerator > scale_limit)) {
            int64_t whole=floor_div(numerator,uv_scale);
            numerator-=whole*uv_scale;
            texel_offset=whole*extent;
        }
        int64_t position=numerator*extent-(linear?(int64_t)(denominator/2u):0);
        int64_t texel=denominator==65536u?floor_div(position,65536):floor_div(position,uv_scale);
        unsigned wrap=(texture->params>>(axis?8u:12u))&3u;
        coordinates[axis][0]=wrapped(texel_offset+texel,extent,wrap);
        if(linear) {
            coordinates[axis][1]=wrapped(texel_offset+texel+1,extent,wrap);
            fraction[axis]=(uint64_t)(position-texel*uv_scale);
        }
    }
    const uint8_t *samples[2][2];
    for(unsigned y=0;y<(linear?2u:1u);++y)
        for(unsigned x=0;x<(linear?2u:1u);++x)
            samples[y][x]=texture->rgba+((size_t)(texture->height-1u-coordinates[1][y])*
                texture->width+coordinates[0][x])*4u;
    if(!linear) { memcpy(output,samples[0][0],4u);return; }
    if(denominator==65536u) {
        uint32_t x=(uint32_t)fraction[0],y=(uint32_t)fraction[1];
        uint32_t left=65536u-x,top=65536u-y;
        /* The perspective sampler already supplies Q16 fractions. Keep the
         * two rows exact and round the complete weighted sum once. */
        for(unsigned lane=0;lane<4u;++lane) {
            uint32_t first=samples[0][0][lane]*left+samples[0][1][lane]*x;
            uint32_t second=samples[1][0][lane]*left+samples[1][1][lane]*x;
            uint64_t sum=(uint64_t)first*top+(uint64_t)second*y;
            output[lane]=(uint8_t)((sum+(UINT64_C(1)<<31))>>32);
        }
        return;
    }
    double inverse=1.0/(double)denominator;
    double fraction_x=(double)fraction[0]*inverse,fraction_y=(double)fraction[1]*inverse;
    for(unsigned lane=0;lane<4;++lane)
        output[lane]=bilinear_byte(samples[0][0][lane],samples[0][1][lane],samples[1][0][lane],
            samples[1][1][lane],fraction[0],fraction[1],denominator,fraction_x,fraction_y);
}

static void sample_mips(const ctr_scene_sampler *sampler,const int64_t uv[2],uint64_t denominator,double lod,
                        bool linear,uint8_t output[4]) {
    unsigned level=(unsigned)lod;
    if(sampler->levels[level].params&UINT32_C(0x01000000)) {
        sample_texture(&sampler->levels[level],uv,denominator,linear,output);
        double weight=lod-level;
        if(weight>0.0) {
            uint8_t higher[4];
            sample_texture(&sampler->levels[level+1u],uv,denominator,linear,higher);
            for(unsigned lane=0;lane<4u;++lane)
                output[lane]=(uint8_t)(output[lane]+(higher[lane]-(double)output[lane])*weight+0.5);
        }
    } else {
        level=(unsigned)(lod+0.5);
        sample_texture(&sampler->levels[level],uv,denominator,linear,output);
    }
}
/* Canonical scale -1/offset 0 means the exact dyadic is simply -clip_z.
 * Shader output is an already checked finite normal float24 or signed zero.
 * Multiply its integer significand before one truncating binary division. */
static uint32_t depth_value(float z,unsigned format) {
    uint32_t bits;memcpy(&bits,&z,sizeof(bits));bits&=UINT32_C(0x7fffffff);
    if(!bits)return 0u;
    unsigned exponent=bits>>23,shift=150u-exponent;
    uint32_t maximum=format==CTR_SCENE_DEPTH_D24S8?UINT32_C(0xffffff):65535u;
    uint64_t numerator=(uint64_t)((bits&UINT32_C(0x7fffff))|UINT32_C(0x800000))*maximum;
    return shift>=64u?0u:(uint32_t)(numerator>>shift);
}
static bool depth_passes(unsigned function,unsigned incoming,unsigned stored) {
    switch(function) {
    case 0:return false;
    case 1:return true;
    case 2:return incoming==stored;
    case 3:return incoming!=stored;
    case 4:return incoming<stored;
    case 5:return incoming<=stored;
    case 6:return incoming>stored;
    case 7:return incoming>=stored;
    default:return false;
    }
}
static uint8_t color_byte(double value) {
    if(value<=0.0)return 0;
    if(value>=1.0)return 255;
    return (uint8_t)(value*255.0+0.5);
}
static void apply_fog(const ctr_scene_state *s,double clip_z,uint8_t rgba[4]) {
    double position=((s->registers[0x0e0u]&UINT32_C(0x10000))?1.0+clip_z:-clip_z)*128.0;
    unsigned index=position>=127.0?127u:(unsigned)position;
    uint32_t entry=s->fog_lut[index],color=s->registers[0x0e1u];
    int difference=(int)(entry&4095u)-(int)(entry&4096u);
    double factor=((entry>>13)+difference*(position-index))/2047.0;
    if(factor<0.0)factor=0.0;
    if(factor>1.0)factor=1.0;
    for(unsigned lane=0;lane<3u;++lane) {
        unsigned fog=(color>>(lane*8u))&255u;
        rgba[lane]=(uint8_t)(fog+((double)rgba[lane]-fog)*factor);
    }
}

static double lighting_factor(const ctr_scene_light *light,unsigned table,const double cosines[4]) {
    /* Tangent projection selects zero in the supported lighting modes 0 and 3. */
    double cosine=light->input[table]==5u?0.0:cosines[light->input[table]];
    if(light->absolute[table])cosine=(light->flags&2u)?fabs(cosine):fmax(0.0,cosine);
    double position=fmax(-1.0,fmin(1.0,cosine))*(light->absolute[table]?256.0:128.0);
    int last=light->absolute[table]?255:127;
    int index=position>=last?last:(int)floor(position);
    uint32_t entry=light->table[table][(unsigned)index&255u];
    double slope=(double)((entry>>12)&2047u)/2047.0;
    if(entry&UINT32_C(0x800000))slope=-slope;
    return light->scale[table]*((double)(entry&4095u)/4095.0+slope*(position-index));
}

static uint8_t lighting_byte(double value) {
    if(value<=0.0)return 0;
    if(value>=255.0)return 255;
    return (uint8_t)value;
}

static void shade_lighting(const ctr_scene_light *light,const scene_vertex v[3],const double weights[3],double total,
                          const uint8_t sampled[CTR_SCENE_TEXTURE_UNITS][4],uint8_t rgba[2][4]) {
    double orientation[4]={0},view[3]={0},local[3]={0,0,1};
    for(unsigned i=0;i<3u;++i) {
        double weight=weights[i]/total;
        for(unsigned lane=0;lane<4u;++lane)orientation[lane]+=weight*v[i].orientation[lane];
        if(light->needs_view)for(unsigned lane=0;lane<3u;++lane)view[lane]+=weight*v[i].view[lane];
    }
    normalize(orientation,4u);
    if(light->bump_unit<CTR_SCENE_TEXTURE_UNITS) {
        const uint8_t *bump=sampled[light->bump_unit];
        for(unsigned lane=0;lane<3u;++lane)local[lane]=bump[lane]*(2.0/255.0)-1.0;
        if(light->reconstruct_normal)local[2]=sqrt(fmax(0.0,1.0-local[0]*local[0]-local[1]*local[1]));
    }
    /* Quaternion rotation in vector form. A zero vector leaves the normal in
     * local coordinates. Opposite quaternion signs were aligned per triangle. */
    double normal[3],axis_length=vector_dot(orientation,orientation),axis_dot=vector_dot(orientation,local);
    double cross[3]={orientation[1]*local[2]-orientation[2]*local[1],
                     orientation[2]*local[0]-orientation[0]*local[2],
                     orientation[0]*local[1]-orientation[1]*local[0]};
    for(unsigned lane=0;lane<3u;++lane)
        normal[lane]=(1.0-2.0*axis_length)*local[lane]+2.0*(axis_dot*orientation[lane]+orientation[3]*cross[lane]);
    double ln=vector_dot(light->direction,normal);
    double diffuse=(light->flags&2u)?fabs(ln):fmax(0.0,ln);
    double geometric=0.0,cosines[4]={0,0,0,ln};
    if(light->needs_view) {
        normalize(view,3u);
        double half[3]={view[0]+light->direction[0],view[1]+light->direction[1],view[2]+light->direction[2]};
        double length=vector_dot(half,half);
        geometric=length>0.0?fmin(1.0,diffuse/length):0.0;
        normalize(half,3u);
        cosines[0]=vector_dot(half,normal);
        cosines[1]=vector_dot(half,view);
        cosines[2]=vector_dot(normal,view);
    }
    double specular[2]={1.0,1.0};
    for(unsigned i=0;i<2u;++i) {
        if(light->table[i])specular[i]=lighting_factor(light,i,cosines);
        if(light->flags&(4u<<i))specular[i]*=geometric;
        if(light->clamp_highlights && diffuse==0.0)specular[i]=0.0;
    }
    for(unsigned lane=0;lane<3u;++lane) {
        rgba[0][lane]=lighting_byte(light->ambient[lane]+light->diffuse[lane]*diffuse);
        rgba[1][lane]=lighting_byte(light->specular[0][lane]*specular[0]+light->specular[1][lane]*specular[1]);
    }
    uint8_t alpha=light->table[2]?lighting_byte(255.0*lighting_factor(light,2u,cosines)):255;
    for(unsigned i=0;i<2u;++i)rgba[i][3]=(light->alpha&(1u<<i))?alpha:255;
}

static bool raster(const ctr_scene_state *s,const ctr_scene_surface *surface,
                   uint8_t *pixels,scene_vertex v[3],ctr_scene_report *report,
                   const ctr_scene_sampler samplers[CTR_SCENE_TEXTURE_UNITS],unsigned texture_mask,
                   const ctr_tev_plan *combiner,
                   const ctr_fragment_state *merger,const ctr_scene_depth_surface *depth,
                   uint8_t *depth_pixels,const ctr_stencil_state *stencil,unsigned color_format,const ctr_scene_light *light,
                   unsigned *remaining_samples) {
    int64_t area=edge(v[0],v[1],v[2].x,v[2].y);unsigned cull=s->registers[0x040u]&3u;
    if(culled(area,cull))return true;
    if(area<0) { scene_vertex t=v[1];v[1]=v[2];v[2]=t;area=-area; }
    int32_t min_x=v[0].x,max_x=v[0].x,min_y=v[0].y,max_y=v[0].y;
    for(unsigned i=1;i<3;++i) {
        if(v[i].x<min_x)min_x=v[i].x;
        if(v[i].x>max_x)max_x=v[i].x;
        if(v[i].y<min_y)min_y=v[i].y;
        if(v[i].y>max_y)max_y=v[i].y;
    }
    min_x/=16;min_y/=16;max_x/=16;max_y/=16;
    bool game=s->shader.round_f24;
    bool fog=(s->registers[0x0e0u]&7u)==5u;
    if(game) {
        float half[2];int offsets[2];
        if(!viewport(s,half,offsets))return true;
        double right=offsets[0]+2.0*half[0]-0.5,top=offsets[1]+2.0*half[1]-0.5;
        if(right<0.0 || top<0.0)return true;
        if(min_x<offsets[0])min_x=offsets[0];
        if(min_y<offsets[1])min_y=offsets[1];
        if(max_x>(int32_t)right)max_x=(int32_t)right;
        if(max_y>(int32_t)top)max_y=(int32_t)top;
    }
    if(min_x<0)min_x=0;
    if(min_y<0)min_y=0;
    if(max_x>=(int32_t)surface->width)max_x=(int32_t)surface->width-1;
    if(max_y>=(int32_t)surface->height)max_y=(int32_t)surface->height-1;
    if(min_x>max_x || min_y>max_y)return true;
    bool varying_depth=v[0].z!=v[1].z || v[0].z!=v[2].z,varying_color=false;
    if(!varying_depth && (v[0].z<-1.0 || v[0].z>0.0))return true;
    if(remaining_samples) {
        unsigned candidates=(unsigned)(max_x-min_x+1)*(unsigned)(max_y-min_y+1);
        if(candidates>*remaining_samples)return false;
        *remaining_samples-=candidates;
    }
    bool writes_color=merger->write_mask && (merger->blend_enabled || merger->logic_op!=6u);
    /* A stencil-only draw can still require fragment alpha for its tests. */
    bool shade=writes_color || (merger->alpha_enabled && merger->alpha_function!=1u);
    bool perspective=v[0].inverse_w!=v[1].inverse_w || v[0].inverse_w!=v[2].inverse_w;
    double inverse_w_gradient[2]={0,0},uv_gradient[CTR_SCENE_TEXTURE_UNITS][2][2]={{{0}}};
    unsigned routes[CTR_SCENE_TEXTURE_UNITS]={0,1,texture_coordinate(s->registers[0x080u],2u)};
    for(unsigned unit=0;unit<CTR_SCENE_TEXTURE_UNITS;++unit)
        if(samplers[unit].track_lod) {
            for(unsigned i=0;i<3;++i) {
                unsigned j=(i+1u)%3u,k=(i+2u)%3u;
                double scale=perspective?v[i].inverse_w:1.0;
                double dx=16.0*(v[j].y-v[k].y)*scale,dy=16.0*(v[k].x-v[j].x)*scale;
                for(unsigned uv=0;uv<2;++uv) {
                    uv_gradient[unit][uv][0]+=dx*v[i].uv[routes[unit]][uv];
                    uv_gradient[unit][uv][1]+=dy*v[i].uv[routes[unit]][uv];
                }
            }
        }
    if(perspective && (samplers[0].track_lod || samplers[1].track_lod || samplers[2].track_lod))
        for(unsigned i=0;i<3;++i) {
            unsigned j=(i+1u)%3u,k=(i+2u)%3u;
            inverse_w_gradient[0]+=16.0*(v[j].y-v[k].y)*v[i].inverse_w;
            inverse_w_gradient[1]+=16.0*(v[k].x-v[j].x)*v[i].inverse_w;
        }
    uint32_t constant_depth=depth && !varying_depth?depth_value((float)v[0].z,depth->format):0u;
    bool packed_depth=depth && depth->format==CTR_SCENE_DEPTH_D24S8;
    unsigned depth_width=packed_depth?4u:2u;
    uint32_t depth_control=s->registers[0x107u];
    unsigned write_gate=s->registers[0x115u];
    bool stencil_writable=packed_depth && (write_gate&1u);
    uint8_t primary[4];
    for(unsigned lane=0;lane<4;++lane) {
        primary[lane]=color_byte(v[0].color[lane]);
        varying_color|=v[0].color[lane]!=v[1].color[lane] || v[0].color[lane]!=v[2].color[lane];
    }
    unsigned mode=s->registers[0x065u]&3u,x1=s->registers[0x066u]&1023u,
        y1=(s->registers[0x066u]>>16)&1023u,x2=s->registers[0x067u]&1023u,
        y2=(s->registers[0x067u]>>16)&1023u;
    unsigned repeat_origin=v[0].repeat_origin|v[1].repeat_origin|v[2].repeat_origin;
    bool clip_inside=mode==3u,clip_outside=mode==1u;
    if(clip_inside) {
        if(min_x<(int32_t)x1)min_x=(int32_t)x1;
        if(min_y<(int32_t)y1)min_y=(int32_t)y1;
        if(max_x>(int32_t)x2)max_x=(int32_t)x2;
        if(max_y>(int32_t)y2)max_y=(int32_t)y2;
        if(min_x>max_x || min_y>max_y)return true;
    }
    /* Projected vertices are limited to +/-1024 and sample positions to
     * 512 pixels. At subpixel scale 16 each determinant fits signed 32 bits. */
    int32_t row_edge[3],step_x[3],step_y[3],threshold[3];
    for(unsigned i=0;i<3u;++i) {
        /* The edge opposite each vertex is also its barycentric numerator. */
        scene_vertex *a=&v[(i+1u)%3u],*b=&v[(i+2u)%3u];
        row_edge[i]=(int32_t)edge(*a,*b,min_x*16+8,min_y*16+8);
        step_x[i]=(a->y-b->y)*16;
        step_y[i]=(b->x-a->x)*16;
        threshold[i]=inclusive(*a,*b)?0:1;
    }
    int32_t width=max_x-min_x;
    for(int32_t y=min_y;y<=max_y;++y,
        row_edge[0]+=step_y[0],row_edge[1]+=step_y[1],row_edge[2]+=step_y[2]) {
        int32_t first=0,last=width;
        /* Intersect the three linear edge intervals once per row. */
        for(unsigned i=0;i<3u;++i) {
            int32_t start=row_edge[i]-threshold[i],end=start+step_x[i]*width;
            if(start<0) {
                if(end<0) { last=-1;break; }
                int32_t enter=(-start-1)/step_x[i]+1;
                if(enter>first)first=enter;
            } else if(end<0) {
                int32_t leave=start/-step_x[i];
                if(leave<last)last=leave;
            }
        }
        if(first>last)continue;
        int32_t weights[3]={row_edge[0]+first*step_x[0],row_edge[1]+first*step_x[1],
                            row_edge[2]+first*step_x[2]};
        bool exclude=clip_outside && (unsigned)y>=y1 && (unsigned)y<=y2;
        for(int32_t x=min_x+first;x<=min_x+last;++x,
            weights[0]+=step_x[0],weights[1]+=step_x[1],weights[2]+=step_x[2]) {
            if(exclude && (unsigned)x>=x1 && (unsigned)x<=x2)continue;
            double z=v[0].z;
            if(varying_depth) {
                z=(weights[0]*v[0].z+weights[1]*v[1].z+weights[2]*v[2].z)/(double)area;
                if(z < -1.0 || z > 0.0)continue;
            }
            ++report->fragments;
            uint8_t *depth_pixel=NULL;
            uint32_t incoming=constant_depth;
            bool stencil_pass=true,depth_pass=true;
            unsigned stencil_outcome=2u;
            if(depth) {
                depth_pixel=depth_pixels+(size_t)y*depth->stride+(size_t)x*depth_width;
                stencil_pass=!stencil->enabled || ctr_stencil_passes(stencil,depth_pixel[3]);
                if(stencil_pass) {
                    incoming=varying_depth?depth_value((float)z,depth->format):constant_depth;
                    uint32_t stored=(uint32_t)depth_pixel[0]|(uint32_t)depth_pixel[1]<<8;
                    if(packed_depth)stored|=(uint32_t)depth_pixel[2]<<16;
                    depth_pass=!(depth_control&1u) || depth_passes((depth_control>>4)&7u,incoming,stored);
                    if(!depth_pass)stencil_outcome=1u;
                } else stencil_outcome=0u;
                /* Rejected samples only need shading when alpha could suppress
                 * a stencil update. KEEP does not need a write. */
                if((!stencil_pass || !depth_pass) &&
                   (!stencil->enabled || !stencil_writable || !stencil->write_mask ||
                    !stencil->action[stencil_outcome]))continue;
            }
            uint8_t *pixel=pixels+(size_t)y*surface->stride+(size_t)x*4u;
            uint8_t source[4];
            if(shade) {
                double corrected[3]={0},total=(double)area;
                if(perspective || varying_color || light->active) {
                    for(unsigned i=0;i<3u;++i)corrected[i]=perspective?weights[i]*v[i].inverse_w:(double)weights[i];
                    if(perspective)total=corrected[0]+corrected[1]+corrected[2];
                }
                if(varying_color)for(unsigned lane=0;lane<4u;++lane)
                    primary[lane]=color_byte((corrected[0]*v[0].color[lane]+corrected[1]*v[1].color[lane]+
                                              corrected[2]*v[2].color[lane])/total);
                unsigned origin=0;
                if(repeat_origin)for(unsigned i=0;i<3u;++i)if(weights[i])origin|=v[i].repeat_origin;
                uint8_t sampled[CTR_SCENE_TEXTURE_UNITS][4]={{0}};
                for(unsigned unit=0;unit<CTR_SCENE_TEXTURE_UNITS;++unit)if(texture_mask&(1u<<unit)) {
                    const ctr_scene_sampler *sampler=&samplers[unit];
                    unsigned coordinate=routes[unit];
                    /* Perspective UVs have already been rounded to signed Q16.
                     * Preserve that precision without scaling both terms by area. */
                    uint64_t denominator=perspective?65536u:(uint64_t)area*65536u;
                    int64_t numerator[2]={0,0};
                    double fixed_uv[2];
                    for(unsigned axis=0;axis<2;++axis) {
                        if(origin&(1u<<(coordinate*2u+axis))) {
                            fixed_uv[axis]=0.0;
                            continue;
                        }
                        if(perspective) {
                            double uv=(corrected[0]*v[0].uv[coordinate][axis]+corrected[1]*v[1].uv[coordinate][axis]+
                                       corrected[2]*v[2].uv[coordinate][axis])/total;
                            if(uv<INT32_MIN)uv=INT32_MIN;
                            if(uv>INT32_MAX)uv=INT32_MAX;
                            fixed_uv[axis]=uv;
                            numerator[axis]=(int64_t)uv;
                        } else {
                            for(unsigned i=0;i<3u;++i)numerator[axis]+=(int64_t)weights[i]*v[i].uv[coordinate][axis];
                            fixed_uv[axis]=0.0;
                        }
                    }
                    double lod=sampler->min_level;
                    const ctr_scene_texture *first=&sampler->levels[sampler->min_level];
                    bool linear=(first->params&2u)!=0u;
                    if(sampler->track_lod) {
                        double rho=0.0;
                        for(unsigned uv=0;uv<2;++uv)for(unsigned axis=0;axis<2;++axis) {
                            unsigned extent=(uv?first->height:first->width)<<sampler->min_level;
                            double gradient=uv_gradient[unit][uv][axis]-fixed_uv[uv]*inverse_w_gradient[axis];
                            double component=fabs(gradient)*(double)extent;
                            if(component>rho)rho=component;
                        }
                        rho/=total*65536.0;
                        if(rho>0.0) {
                            double biased=log2(rho)+(double)sampler->bias_q8/256.0;
                            /* Select the texel filter before clamping the mip level. */
                            linear=(first->params&(biased>0.0?4u:2u))!=0u;
                            if(biased>lod)lod=biased<sampler->max_level?biased:sampler->max_level;
                        }
                    }
                    sample_mips(sampler,numerator,denominator,lod,linear,sampled[unit]);
                }
                uint8_t lighting[2][4];
                const uint8_t (*light_sources)[4]=NULL;
                if(light->active) {
                    shade_lighting(light,v,corrected,total,(const uint8_t (*)[4])sampled,lighting);
                    light_sources=(const uint8_t (*)[4])lighting;
                }
                ctr_tev_evaluate_prepared(combiner,primary,(const uint8_t (*)[4])sampled,light_sources,source);
                if(!ctr_fragment_alpha_passes(merger,source[3]))continue;
            }
            if(depth_pixel) {
                if(stencil->enabled && stencil_writable)
                    depth_pixel[3]=ctr_stencil_apply(stencil,stencil->action[stencil_outcome],depth_pixel[3]);
                if(!stencil_pass || !depth_pass)continue;
                if((depth_control&UINT32_C(0x1000)) && (write_gate&2u)) {
                    depth_pixel[0]=(uint8_t)incoming;depth_pixel[1]=(uint8_t)(incoming>>8);
                    if(packed_depth)depth_pixel[2]=(uint8_t)(incoming>>16);
                }
            }
            if(writes_color) {
                if(fog)apply_fog(s,z,source);
                ctr_fragment_apply(merger,source,pixel);
            }
            if(color_format==CTR_SCENE_COLOR_RGB5A1)
                ctr_rgb5a1_unpack(ctr_rgb5a1_pack(pixel),pixel);
            else if(color_format==CTR_SCENE_COLOR_RGB565)
                ctr_rgb565_unpack(ctr_rgb565_pack(pixel),pixel);
            else if(color_format==CTR_SCENE_COLOR_RGBA4)
                ctr_rgba4_unpack(ctr_rgba4_pack(pixel),pixel);
        }
    }
    return true;
}
typedef struct {
    const ctr_scene_surface *initial_color;
    const ctr_scene_depth_surface *initial_depth;
    bool active,began;
    ctr_shader_fast *fast_vs;
    unsigned *cache_id,*cache_steps,cache_slots;
    scene_vertex *cache_vertex;
    ctr_scene_transformed_vertex *transformed_batch;
    unsigned transformed_cap;
} scene_direct_ctx;
static ctr_scene_result draw(ctr_scene_state *s,const ctr_scene_resources *resources,
                             const ctr_scene_surface *surface,bool indexed,unsigned immediate_count,
                             ctr_vertex *decoded,uint8_t *pixels,ctr_scene_report *report,
                             bool bound,uint32_t color_address,unsigned color_format,const ctr_scene_depth_surface *depth,
                             uint8_t *depth_pixels,bool depth_endpoints,bool multi_supported,bool software_game,unsigned features,
                             const ctr_scene_backend *backend,ctr_texture_cache *cache,unsigned *remaining_samples,
                             scene_direct_ctx *direct) {
    if(s->registers[0x047u] || s->registers[0x062u] || s->registers[0x118u] ||
       s->registers[0x245u] || s->registers[0x252u])return CTR_SCENE_UNSUPPORTED;
    if(s->register_defined[0x253u] && s->registers[0x253u]!=(indexed?0x100u:1u))return CTR_SCENE_UNSUPPORTED;
    ctr_scene_light light={0};
    if(!software_game && !read_lighting(s,false,&light))return CTR_SCENE_UNSUPPORTED;
    if(s->register_defined[0x242u] && s->registers[0x242u]!=(s->registers[0x2b9u]&15u))
        return CTR_SCENE_UNSUPPORTED;
    if((s->register_defined[0x24au] && s->registers[0x24au]+1u!=s->registers[0x04fu]) ||
       (s->register_defined[0x251u] && s->registers[0x251u]+1u!=s->registers[0x04fu]))
        return CTR_SCENE_UNSUPPORTED;
    if(bound) {
        ctr_scene_result result=target_pipeline(s,surface,color_address,color_format,depth,software_game);
        if(result!=CTR_SCENE_OK)return result;
    }
    ctr_fragment_state merger;
    if(!ctr_fragment_decode_depth(s->registers,s->register_defined,bound,depth!=NULL,&merger))return CTR_SCENE_UNSUPPORTED;
    ctr_stencil_state stencil;
    if(!ctr_stencil_decode(s->registers,s->register_defined,
                           depth && depth->format==CTR_SCENE_DEPTH_D24S8,&stencil))return CTR_SCENE_UNSUPPORTED;
    unsigned count=immediate_count?immediate_count:s->registers[0x228u];
    if(immediate_count?count<3u:(!count || count%3u))return CTR_SCENE_LAYOUT;
    unsigned triangles=immediate_count?count-2u:count/3u;
    unsigned draw_limit=software_game?CTR_SCENE_MAX_GAME_DRAWS:CTR_SCENE_MAX_DRAWS;
    unsigned vertex_limit=software_game?CTR_SCENE_MAX_GAME_DRAW_VERTICES:CTR_SCENE_MAX_VERTICES;
    if(count>vertex_limit || report->draws==draw_limit ||
       (software_game && triangles>GAME_LIST_TRIANGLES-report->triangles))return CTR_SCENE_LIMIT;
    uint32_t primitive_config=s->registers[0x25eu];
    uint32_t shader_entry=s->registers[0x2bau];
    unsigned entry_flags=shader_entry>>16;
    uint32_t allowed_primitive=software_game?UINT32_C(0x30f):15u;
    unsigned topology=(primitive_config>>8)&3u,output_count=primitive_config&15u;
    /* Shader topology without a geometry stage assembles whole triangles. */
    if((s->registers[0x229u]&~(software_game?UINT32_C(0x100):0u)) || (primitive_config&~allowed_primitive) ||
       (immediate_count?topology!=1u:(topology!=0u && topology!=3u)) ||
       (primitive_config && output_count+1u!=s->registers[0x04fu]) ||
       (s->registers[0x040u]&~3u) ||
       (s->registers[0x065u]!=0u && s->registers[0x065u]!=1u && s->registers[0x065u]!=3u) ||
       (s->registers[0x066u]&~UINT32_C(0x03ff03ff)) ||
       (s->registers[0x067u]&~UINT32_C(0x03ff03ff)) || (entry_flags && entry_flags!=0x7fffu))
        return CTR_SCENE_UNSUPPORTED;
    if(s->fixed_words || s->uniform_words)return CTR_SCENE_LAYOUT;
    float viewport_half[2];int viewport_offsets[2];
    if(software_game) {
        if(!viewport(s,viewport_half,viewport_offsets))return CTR_SCENE_NUMERIC;
        if(viewport_half[0]<=0 || viewport_half[1]<=0 || viewport_half[0]>(float)surface->width/2 ||
           viewport_half[1]>(float)surface->height/2)return CTR_SCENE_UNSUPPORTED;
    }
    scene_target_range targets[2]={{0}};
    if(software_game && bound) {
        uint64_t target_pixels=(uint64_t)surface->width*surface->height;
        targets[0]=(scene_target_range){color_address,color_address+target_pixels*(color_format==CTR_SCENE_COLOR_RGBA8?4u:2u)};
        if(depth)targets[1]=(scene_target_range){depth->address,depth->address+target_pixels*(depth->format==CTR_SCENE_DEPTH_D24S8?4u:2u)};
    }
    unsigned capacity=CTR_SCENE_MAX_VERTICES/3u*3u;
    unsigned batch=count<capacity?count:capacity;
    scene_vertex_layout vertex_layout={0};
    bool lazy_indexed=software_game && indexed;
    ctr_scene_result result=CTR_SCENE_OK;
    if(immediate_count) {
        result=CTR_SCENE_OK;
    } else if(lazy_indexed) {
        result=vertices_prepare(s,resources,true,targets,true,&vertex_layout);
        if(result==CTR_SCENE_OK) {
            uint64_t last=(uint64_t)vertex_layout.indices.offset+(uint64_t)count*(vertex_layout.indices.format+1u);
            if(last>vertex_layout.indices.size)result=CTR_SCENE_LAYOUT;
        }
    } else {
        result=vertices_prepare(s,resources,indexed,targets,software_game,&vertex_layout);
        if(result==CTR_SCENE_OK)
            result=vertices_decode(s,&vertex_layout,indexed,0,batch,decoded,report);
    }
    if(result!=CTR_SCENE_OK)return result;
    ctr_vertex_indices indices=vertex_layout.indices;
    ctr_scene_sampler samplers[CTR_SCENE_TEXTURE_UNITS]={0};
    ctr_texture_cache_lease texture_allocations[CTR_SCENE_TEXTURE_UNITS][CTR_SCENE_MAX_MIP_LEVELS]={0};
    ctr_tev_program combiner;
    unsigned texture_mask=0,sources=1u;
    result=fragment(s,resources,bound,software_game,features,samplers,texture_allocations,&combiner,&texture_mask,
                    &sources,cache,targets,&light);
    if(result==CTR_SCENE_OK && (texture_mask&6u) && !multi_supported)result=CTR_SCENE_UNSUPPORTED;
    if(result!=CTR_SCENE_OK) { free_textures(texture_allocations);return result; }
    s->shader.entry=shader_entry&UINT32_C(0xffff);
    s->shader.integer_defined=0;
    for(unsigned i=0;i<4u;++i)if(s->register_defined[0x2b1u+i]==15u) {
        s->shader.integer_uniforms[i]=s->registers[0x2b1u+i];
        s->shader.integer_defined|=(uint8_t)(1u<<i);
    }
    if(backend && !software_game) {
        ctr_scene_texture textures[CTR_SCENE_TEXTURE_UNITS];
        for(unsigned unit=0;unit<CTR_SCENE_TEXTURE_UNITS;++unit)textures[unit]=samplers[unit].levels[0];
        const ctr_scene_texture *sampling=(texture_mask&1u)?&textures[0]:NULL;
        report->backend_stage=CTR_SCENE_BACKEND_DRAW;
        bool accepted;
        if(backend->draw_mips)accepted=backend->draw_mips(backend->context,s,decoded,count,samplers,texture_mask);
        else if((texture_mask&6u) || !backend->draw)
            accepted=backend->draw_textures(backend->context,s,decoded,count,textures,texture_mask);
        else accepted=backend->draw(backend->context,s,decoded,count,sampling);
        free_textures(texture_allocations);
        if(!accepted)return CTR_SCENE_BACKEND;
        ++report->draws;return CTR_SCENE_OK;
    }
    ctr_scene_transformed_vertex *transformed_batch=NULL;
    unsigned transformed_count=0,transformed_cap=capacity;
    bool owns_transformed=true;
    if(backend) {
        if(direct && direct->active) {
            if(!direct->began) {
                report->backend_stage=CTR_SCENE_BACKEND_BEGIN;
                bool began=direct->initial_depth?
                    backend->begin_depth(backend->context,direct->initial_color,direct->initial_depth):
                    backend->begin(backend->context,direct->initial_color);
                if(!began) { free_textures(texture_allocations);return CTR_SCENE_BACKEND; }
                direct->began=true;
            }
            size_t need=(size_t)count*3u;
            if(need<capacity)need=capacity;
            if(need>CTR_SCENE_MAX_GAME_DRAW_VERTICES)need=CTR_SCENE_MAX_GAME_DRAW_VERTICES;
            transformed_cap=(unsigned)need;
            if(direct->transformed_cap<transformed_cap) {
                ctr_scene_transformed_vertex *grown=realloc(direct->transformed_batch,
                    (size_t)transformed_cap*sizeof(*grown));
                if(!grown) { free_textures(texture_allocations);return CTR_SCENE_NO_MEMORY; }
                direct->transformed_batch=grown;
                direct->transformed_cap=transformed_cap;
            }
            transformed_batch=direct->transformed_batch;
            owns_transformed=false;
        } else {
            transformed_batch=malloc(transformed_cap*sizeof(*transformed_batch));
            if(!transformed_batch) { free_textures(texture_allocations);return CTR_SCENE_NO_MEMORY; }
        }
        report->backend_stage=CTR_SCENE_BACKEND_DRAW;
    }
    ctr_tev_plan tev;
    if(!backend)ctr_tev_prepare(&combiner,&tev);
    scene_vertex primitive[3]={0};
    /* A draw keeps its validated inputs, program and uniforms fixed. Count
     * every logical vertex's instructions, including repeated indices. */
    enum { SHADER_CACHE_SIZE=256 };
    unsigned stack_id[SHADER_CACHE_SIZE]={0},stack_steps[SHADER_CACHE_SIZE];
    scene_vertex stack_vertex[SHADER_CACHE_SIZE];
    unsigned *cached_id=stack_id,*cached_steps=stack_steps;
    scene_vertex *cached_vertex=stack_vertex;
    unsigned cache_slots=SHADER_CACHE_SIZE;
    if(direct && direct->active && direct->cache_slots) {
        cached_id=direct->cache_id;cached_steps=direct->cache_steps;
        cached_vertex=direct->cache_vertex;cache_slots=direct->cache_slots;
        memset(cached_id,0,cache_slots*sizeof(*cached_id));
    }
    bool use_fast=direct && direct->active && direct->fast_vs &&
        ctr_shader_prepare_fast(&s->shader,direct->fast_vs);
    const unsigned cull_mode=s->registers[0x040u]&3u;
    const float inv_65536=1.0f/65536.0f;
    uint32_t texture_config=(s->registers[0x080u]&~7u)|texture_mask;
    unsigned required_coordinates=sampled_coordinates(texture_config);
    /* Model the overflowed coordinate at the seam of a repeating texture.
     * Every sampler routed to that axis must use repeat without LOD derivatives. */
    unsigned repeat_axes=63u;
    for(unsigned unit=0;unit<CTR_SCENE_TEXTURE_UNITS;++unit)if(texture_mask&(1u<<unit)) {
        unsigned coordinate=texture_coordinate(texture_config,unit);
        const ctr_scene_sampler *sampler=&samplers[unit];
        uint32_t params=sampler->levels[0].params;
        if(sampler->max_level || sampler->track_lod || ((params>>12)&3u)!=2u)repeat_axes&=~(1u<<(coordinate*2u));
        if(sampler->max_level || sampler->track_lod || ((params>>8)&3u)!=2u)repeat_axes&=~(2u<<(coordinate*2u));
    }
    for(unsigned vertex=0;vertex<count;++vertex) {
        unsigned in_batch=immediate_count?vertex:vertex%capacity;
        if(!immediate_count && !lazy_indexed && vertex && !in_batch) {
            batch=count-vertex<capacity?count-vertex:capacity;
            result=vertices_decode(s,&vertex_layout,indexed,vertex,batch,decoded,report);
            if(result!=CTR_SCENE_OK)break;
        }
        scene_vertex *transformed=&primitive[vertex%3u];
        unsigned key=0,slot=0;
        if(software_game && indexed) {
            unsigned width=indices.format+1u;
            const uint8_t *id=(const uint8_t *)indices.bytes+indices.offset+vertex*width;
            key=1u+id[0]+(width==2u?(unsigned)id[1]<<8:0u);
            slot=(key-1u)%cache_slots;
        }
        if(key && cached_id[slot]==key) {
            *transformed=cached_vertex[slot];
            report->shader_instructions+=cached_steps[slot];
        } else {
            const ctr_vertex *vtx=&decoded[in_batch];
            if(lazy_indexed) {
                result=vertices_decode(s,&vertex_layout,true,vertex,1u,&decoded[0],report);
                if(result!=CTR_SCENE_OK)break;
                vtx=&decoded[0];
            }
            ctr_shader_output out;
            report->shader_result=use_fast?
                ctr_shader_execute_fast(direct->fast_vs,vtx,&out):
                ctr_shader_execute(&s->shader,vtx,&out);
            if(report->shader_result!=CTR_SHADER_OK) { result=CTR_SCENE_SHADER;break; }
            report->shader_instructions+=out.instructions;
            if(key)*transformed=(scene_vertex){0};
            result=semantics(s,&out,surface,transformed,required_coordinates,(sources&1u)!=0,
                             software_game,&light,repeat_axes);
            if(result!=CTR_SCENE_OK)break;
            if(key) {
                cached_vertex[slot]=*transformed;
                cached_steps[slot]=out.instructions;
                cached_id[slot]=key;
            }
        }
        if(immediate_count?vertex>=2u:vertex%3u==2u) {
            if(!s->shader.round_f24) {
                for(unsigned i=1;i<3;++i)for(unsigned lane=0;lane<4;++lane)
                    if(primitive[i].color[lane]!=primitive[0].color[lane])result=CTR_SCENE_UNSUPPORTED;
                if(depth) {
                    for(unsigned i=1;i<3;++i)if(primitive[i].position[2]!=primitive[0].position[2])
                        result=CTR_SCENE_UNSUPPORTED;
                    if(depth_endpoints && primitive[0].position[2]!=0.0f && primitive[0].position[2]!=-1.0f)
                        result=CTR_SCENE_UNSUPPORTED;
                }
                if(result!=CTR_SCENE_OK)break;
            }
            if(software_game) {
                scene_vertex strip[3];
                scene_vertex *triangle=primitive;
                /* Keep the previous two outputs intact across strip triangles. */
                if(immediate_count) {
                    unsigned swap=vertex&1u;
                    strip[0]=primitive[(vertex-2u+swap)%3u];
                    strip[1]=primitive[(vertex-1u-swap)%3u];
                    strip[2]=primitive[vertex%3u];
                    triangle=strip;
                }
                if(light.active)for(unsigned i=1u;i<3u;++i) {
                    double dot=0.0;
                    for(unsigned lane=0;lane<4u;++lane)
                        dot+=(double)triangle[0].orientation[lane]*triangle[i].orientation[lane];
                    if(dot<0.0)for(unsigned lane=0;lane<4u;++lane)triangle[i].orientation[lane]=-triangle[i].orientation[lane];
                }
                bool clipped_path=true;
                if(direct && direct->active && transformed_batch &&
                   !(triangle[0].repeat_origin|triangle[1].repeat_origin|triangle[2].repeat_origin)) {
                    unsigned common=127u,outside=0u;
                    for(unsigned vi=0;vi<3u;++vi) {
                        const float *pos=triangle[vi].position;
                        float w=pos[3];
                        unsigned m=0u;
                        if(w<FLT_MIN)m|=64u;
                        if(w+pos[0]<0.0f)m|=1u;
                        if(w-pos[0]<0.0f)m|=2u;
                        if(w+pos[1]<0.0f)m|=4u;
                        if(w-pos[1]<0.0f)m|=8u;
                        if(w+pos[2]<0.0f)m|=16u;
                        if(-pos[2]<0.0f)m|=32u;
                        common&=m;outside|=m;
                    }
                    if(common) {
                        clipped_path=false;
                    } else if(!outside) {
                        clipped_path=false;
                        float nx[3],ny[3];
                        for(unsigned vi=0;vi<3u;++vi) {
                            float iw=1.0f/triangle[vi].position[3];
                            nx[vi]=triangle[vi].position[0]*iw;
                            ny[vi]=triangle[vi].position[1]*iw;
                        }
                        float area=(nx[1]-nx[0])*(ny[2]-ny[0])-(ny[1]-ny[0])*(nx[2]-nx[0]);
                        if(area!=0.0f && !(cull_mode==1u && area>0.0f) && !(cull_mode==2u && area<0.0f)) {
                            if(transformed_count+3u>transformed_cap) {
                                if(!backend->draw_transformed(backend->context,s,transformed_batch,transformed_count,
                                                              samplers,texture_mask,&light))result=CTR_SCENE_BACKEND;
                                transformed_count=0;
                            }
                            if(result==CTR_SCENE_OK)for(unsigned lane=0;lane<3u;++lane) {
                                const scene_vertex *source=&triangle[lane];
                                ctr_scene_transformed_vertex *target=&transformed_batch[transformed_count++];
                                memcpy(target->position,source->position,sizeof(target->position));
                                memcpy(target->color,source->color,sizeof(target->color));
                                memcpy(target->orientation,source->orientation,sizeof(target->orientation));
                                memcpy(target->view,source->view,sizeof(target->view));
                                for(unsigned unit=0;unit<CTR_SCENE_TEXTURE_UNITS;++unit)
                                    for(unsigned axis=0;axis<2u;++axis)
                                        target->uv[unit][axis]=(float)source->uv[unit][axis]*inv_65536;
                            }
                        }
                    }
                }
                if(clipped_path) {
                    scene_vertex polygon[CLIPPED_TRIANGLE_VERTICES];unsigned polygon_count;
                    result=clip_and_project(surface,viewport_half,viewport_offsets,triangle,polygon,&polygon_count);
                    for(unsigned i=2u;result==CTR_SCENE_OK && i<polygon_count;++i) {
                        scene_vertex clipped[3]={polygon[0],polygon[i-1u],polygon[i]};
                        if(transformed_batch) {
                            int64_t area=edge(clipped[0],clipped[1],clipped[2].x,clipped[2].y);
                            if(culled(area,cull_mode))continue;
                            if(transformed_count+3u>transformed_cap) {
                                if(!backend->draw_transformed(backend->context,s,transformed_batch,transformed_count,
                                                              samplers,texture_mask,&light))result=CTR_SCENE_BACKEND;
                                transformed_count=0;
                            }
                            if(result==CTR_SCENE_OK)for(unsigned lane=0;lane<3u;++lane) {
                                const scene_vertex *source=&clipped[lane];
                                if(source->repeat_origin) { result=CTR_SCENE_UNSUPPORTED;break; }
                                ctr_scene_transformed_vertex *target=&transformed_batch[transformed_count++];
                                memcpy(target->position,source->position,sizeof(target->position));
                                memcpy(target->color,source->color,sizeof(target->color));
                                memcpy(target->orientation,source->orientation,sizeof(target->orientation));
                                memcpy(target->view,source->view,sizeof(target->view));
                                for(unsigned axis=0;axis<2u;++axis) {
                                    double screen=(axis?source->y:source->x)/16.0;
                                    double clip=(screen-viewport_offsets[axis])/viewport_half[axis]-1.0;
                                    target->position[axis]=(float)(clip*source->position[3]);
                                }
                                for(unsigned unit=0;unit<CTR_SCENE_TEXTURE_UNITS;++unit)
                                    for(unsigned axis=0;axis<2u;++axis)
                                        target->uv[unit][axis]=(float)((double)source->uv[unit][axis]/65536.0);
                            }
                        } else if(!raster(s,surface,pixels,clipped,report,samplers,texture_mask,&tev,&merger,depth,
                                          depth_pixels,&stencil,color_format,&light,remaining_samples))result=CTR_SCENE_LIMIT;
                    }
                }
            } else if(!raster(s,surface,pixels,primitive,report,samplers,texture_mask,&tev,&merger,depth,
                              depth_pixels,&stencil,color_format,&light,remaining_samples))result=CTR_SCENE_LIMIT;
            if(result!=CTR_SCENE_OK)break;
            ++report->triangles;
        }
    }
    if(result==CTR_SCENE_OK && transformed_count &&
       !backend->draw_transformed(backend->context,s,transformed_batch,transformed_count,samplers,texture_mask,&light))
        result=CTR_SCENE_BACKEND;
    if(owns_transformed)free(transformed_batch);
    free_textures(texture_allocations);
    if(result==CTR_SCENE_OK)++report->draws;
    return result;
}
void ctr_scene_init(ctr_scene_state *state) { if(state)memset(state,0,sizeof(*state)); }
static ctr_scene_result replay(ctr_scene_state *trial,const void *commands,
                                const ctr_pica_write *writes,size_t count,
                                const ctr_scene_resources *resources,const ctr_scene_surface *surface,
                                ctr_vertex *decoded,uint8_t *pixels,ctr_scene_report *report,
                                bool configuration,bool bound,uint32_t color_address,unsigned color_format,
                                const ctr_scene_depth_surface *depth,
                                uint8_t *depth_pixels,bool depth_endpoints,bool multi_supported,bool software_game,unsigned features,
                                const ctr_scene_backend *backend,ctr_texture_cache *cache,scene_direct_ctx *direct) {
    ctr_scene_result result=CTR_SCENE_OK;
    unsigned markers=0,fixed_reg=0,uniform_reg=0,geometry_reg=0;
    unsigned immediate_count=0,immediate_lane=0;
    unsigned remaining_samples=GAME_LIST_RASTER_SAMPLES;
    size_t fixed_offset=0,uniform_offset=0,geometry_offset=0;
    for(size_t i=0;i<count;++i) {
        const ctr_pica_write *write=&writes[i];unsigned reg=write->reg,mask=write->byte_mask;
        uint32_t value=write->value;report->reg=reg;report->byte_offset=write->byte_offset;
        bool immediate=software_game && bound && trial->registers[0x232u]==CTR_SCENE_IMMEDIATE_INDEX;
        bool inline_data=immediate && reg>=0x233u && reg<=0x235u;
        bool pending=immediate && (immediate_count || immediate_lane || trial->fixed_words);
        bool inline_draw=pending && reg==0x245u && (mask&1u) && value==1u;
        if(pending && !inline_data && !inline_draw) { result=CTR_SCENE_LAYOUT;break; }
        if(reg==CTR_PICA_FINALIZE) {
            const uint8_t *bytes=commands;size_t at=write->byte_offset+4u;
            uint32_t header=(uint32_t)bytes[at]|(uint32_t)bytes[at+1]<<8|
                (uint32_t)bytes[at+2]<<16|(uint32_t)bytes[at+3]<<24;
            if(mask!=15u || value!=CTR_PICA_FINALIZE_VALUE || markers==2u || ((header>>20)&255u))
                result=CTR_SCENE_WIRE;
            else ++markers;
        } else if(markers)result=CTR_SCENE_WIRE;
        else if(!reg && !mask && (!value || software_game)) {
            /* Register 0 with no enabled bytes has no write or trigger. */
        } else if(software_game && bound && reg==0x232u && mask==15u && value==CTR_SCENE_IMMEDIATE_INDEX) {
            if(trial->fixed_words)result=CTR_SCENE_LAYOUT;
            else trial->registers[reg]=value;
        } else if(inline_data) {
            result=immediate_attribute(trial,value,mask,decoded,&immediate_count,&immediate_lane);
        } else if(inline_draw) {
            if(!immediate_count || immediate_lane || trial->fixed_words)result=CTR_SCENE_LAYOUT;
            else result=draw(trial,resources,surface,false,immediate_count,decoded,pixels,report,
                             bound,color_address,color_format,depth,depth_pixels,depth_endpoints,multi_supported,software_game,features,
                             backend,cache,&remaining_samples,direct);
            if(result==CTR_SCENE_OK)result=setup_write(trial,reg,value,mask);
            immediate_count=0;
        } else if(reg==0x22eu || reg==0x22fu) {
            /* A zero trigger keeps vertex submission idle during setup. */
            if(software_game && mask==15u && !value)continue;
            if(configuration || mask!=15u || value!=1u)result=CTR_SCENE_UNSUPPORTED;
            else result=draw(trial,resources,surface,reg==0x22fu,0,decoded,pixels,report,
                             bound,color_address,color_format,depth,depth_pixels,depth_endpoints,multi_supported,software_game,features,
                             backend,cache,software_game?&remaining_samples:NULL,direct);
        } else if(reg==0x25fu) {
            if(mask!=15u || value!=1u)result=CTR_SCENE_UNSUPPORTED;
            /* Complete lists have no carried partial primitive to restart. */
        } else if(reg==0x231u) {
            if(mask!=15u || (value!=1u && !(software_game && !value)))result=CTR_SCENE_UNSUPPORTED;
            /* Each draw decodes current vertices from the validated snapshot. */
        } else if(!configuration && (ordinary(reg) || fragment_register(reg) || (bound && target_register(reg)))) {
            if(!bound && (reg==0x105u || reg==0x106u)) { result=CTR_SCENE_UNSUPPORTED;break; }
            trial->registers[reg]=ctr_pica_merge(trial->registers[reg],value,mask);
            if(reg==0x080u)trial->registers[reg]&=~CTR_PICA_CLEAR_TEXTURE_CACHE;
            trial->register_defined[reg]|=(uint8_t)mask;
            if(fragment_register(reg) && trial->register_defined[reg]==15u &&
               !fragment_value(reg,trial->registers[reg],software_game,features))result=CTR_SCENE_UNSUPPORTED;
            if(reg==0x107u && !depth && trial->register_defined[reg]==15u &&
               (trial->registers[reg]&~UINT32_C(0xf00)))result=CTR_SCENE_UNSUPPORTED;
            if((reg==0x105u || reg==0x106u) && (!depth || depth->format!=CTR_SCENE_DEPTH_D24S8) &&
               trial->register_defined[reg]==15u && trial->registers[reg])result=CTR_SCENE_UNSUPPORTED;
            if((reg==0x114u || reg==0x115u) && depth && trial->register_defined[reg]==15u &&
               !depth_access(trial->registers[reg],depth->format==CTR_SCENE_DEPTH_D24S8,software_game))
                result=CTR_SCENE_UNSUPPORTED;
            if((reg==0x114u || reg==0x115u || reg==0x116u || reg==0x11cu) && !depth &&
               trial->register_defined[reg]==15u && trial->registers[reg])result=CTR_SCENE_UNSUPPORTED;
        } else if((bound || configuration) && (reg==0x110u || reg==0x111u)) {
            if(mask!=15u || value!=1u)result=CTR_SCENE_UNSUPPORTED;
            /* The caller owns cache/coherency for any native scratch target. */
        } else if((reg>=0x047u && reg<=0x04bu) || reg==0x061u || reg==0x062u ||
                  reg==0x06au || (reg>=0x0a8u && reg<=0x0adu) || reg==0x0e1u ||
                  (reg>=0x0e4u && reg<=0x0e6u) || (reg>=0x0e8u && reg<=0x0efu) ||
                  reg==0x118u || (reg>=0x120u && reg<=0x122u) || reg==0x126u ||
                  reg==0x130u || (reg>=0x140u && reg<=0x1d9u) ||
                  (reg>=0x238u && reg<=0x23bu) ||
                  reg==0x242u || reg==0x244u || reg==0x245u || reg==0x24au ||
                  (reg>=0x251u && reg<=0x254u) || (software_game && reg==0x289u) ||
                  (reg>=0x2b1u && reg<=0x2b4u)) {
            /* Validate setup for features kept inactive by the draw checks. */
            result=setup_write(trial,reg,value,mask);
        } else {
            if(reg>=0x233u && reg<=0x235u && !trial->fixed_words) { fixed_offset=write->byte_offset;fixed_reg=reg; }
            if(reg>=0x2c1u && reg<=0x2c8u && !trial->uniform_words) { uniform_offset=write->byte_offset;uniform_reg=reg; }
            if(reg>=0x291u && reg<=0x298u && !trial->geometry_uniform_words) {
                geometry_offset=write->byte_offset;geometry_reg=reg;
            }
            bool geometry_uniform=reg>=0x290u && reg<=0x298u;
            result=(configuration || geometry_uniform)?setup_write(trial,reg,value,mask):upload(trial,reg,value,mask);
        }
        if(result!=CTR_SCENE_OK)break;
    }
    if(result==CTR_SCENE_OK && (!markers || immediate_count || immediate_lane || trial->fixed_words ||
                               trial->uniform_words || trial->geometry_uniform_words)) {
        result=CTR_SCENE_WIRE;
        if(trial->fixed_words) { report->byte_offset=fixed_offset;report->reg=fixed_reg; }
        else if(trial->uniform_words) { report->byte_offset=uniform_offset;report->reg=uniform_reg; }
        else if(trial->geometry_uniform_words) { report->byte_offset=geometry_offset;report->reg=geometry_reg; }
    }
    return result;
}
ctr_scene_result ctr_scene_apply_setup(ctr_scene_state *stage,const void *commands,
                                     size_t size,const ctr_pica_write *writes,
                                     size_t count,ctr_scene_report *out) {
    ctr_scene_report report={0};
    if(stage->lists==UINT64_MAX)return publish(out,report,CTR_SCENE_OVERFLOW);
    ctr_scene_result result=replay(stage,commands,writes,count,NULL,NULL,NULL,NULL,&report,
                                   true,false,0,0,NULL,NULL,false,false,stage->shader.round_f24,0,NULL,NULL,NULL);
    if(result==CTR_SCENE_OK) {
        ++stage->lists;
        report.byte_offset=size;report.reg=0;
    }
    return publish(out,report,result);
}
static ctr_scene_result execute(ctr_scene_state *state,const void *commands,size_t size,
                                  const ctr_scene_resources *resources,
                                  const ctr_scene_surface *surface,ctr_scene_report *out,
                                  bool bound,uint32_t color_address,unsigned color_format,const ctr_scene_depth_surface *depth,
                                  const ctr_scene_backend *backend,ctr_texture_cache *cache,bool guest_stream) {
    if(!state || !resources || !surface)return CTR_SCENE_ARGUMENT;
    if(!span(state,sizeof(*state)) || !span(resources,sizeof(*resources)) ||
       !span(surface,sizeof(*surface)) || !span(commands,size) ||
       (out && !span(out,sizeof(*out))) || (backend && !span(backend,sizeof(*backend))) ||
       (depth && !span(depth,sizeof(*depth))))return CTR_SCENE_OVERFLOW;
    if(!span(resources->bytes,resources->size) || !span(surface->bytes,surface->size))return CTR_SCENE_OVERFLOW;
    if(depth && !span(depth->bytes,depth->size))return CTR_SCENE_OVERFLOW;
    const void *objects[]={commands,resources,resources->bytes,surface};
    size_t lengths[]={size,sizeof(*resources),resources->size,sizeof(*surface)};
    if(overlap(state,sizeof(*state),surface->bytes,surface->size) ||
       (out && (overlap(out,sizeof(*out),state,sizeof(*state)) ||
                overlap(out,sizeof(*out),surface->bytes,surface->size))))return CTR_SCENE_ALIAS;
    for(unsigned i=0;i<4;++i)if(overlap(state,sizeof(*state),objects[i],lengths[i]) ||
        overlap(surface->bytes,surface->size,objects[i],lengths[i]) ||
        (out && overlap(out,sizeof(*out),objects[i],lengths[i])))return CTR_SCENE_ALIAS;
    if(depth) {
        if(overlap(depth,sizeof(*depth),state,sizeof(*state)) ||
           overlap(depth,sizeof(*depth),surface->bytes,surface->size) ||
           overlap(depth,sizeof(*depth),depth->bytes,depth->size) ||
           overlap(depth->bytes,depth->size,state,sizeof(*state)) ||
           overlap(depth->bytes,depth->size,surface->bytes,surface->size) ||
           (out && (overlap(out,sizeof(*out),depth,sizeof(*depth)) ||
                    overlap(out,sizeof(*out),depth->bytes,depth->size))))return CTR_SCENE_ALIAS;
        for(unsigned i=0;i<4;++i)if(overlap(depth,sizeof(*depth),objects[i],lengths[i]) ||
            overlap(depth->bytes,depth->size,objects[i],lengths[i]))return CTR_SCENE_ALIAS;
    }
    ctr_scene_report report={0};
    bool software_game=state->shader.round_f24 && (!backend || backend->draw_transformed);
    unsigned texture_limit=software_game?CTR_SCENE_MAX_GAME_TEXTURE_SPANS:CTR_SCENE_MAX_TEXTURE_SPANS;
    if(resources->texture_count>texture_limit)
        return publish(out,report,CTR_SCENE_LIMIT);
    size_t span_bytes=resources->texture_count*sizeof(*resources->textures);
    if(!span(resources->textures,span_bytes))return CTR_SCENE_OVERFLOW;
    if(overlap(resources->textures,span_bytes,state,sizeof(*state)) ||
       overlap(resources->textures,span_bytes,surface->bytes,surface->size) ||
       (depth && (overlap(resources->textures,span_bytes,depth,sizeof(*depth)) ||
                  overlap(resources->textures,span_bytes,depth->bytes,depth->size))) ||
       (out && overlap(resources->textures,span_bytes,out,sizeof(*out))))return CTR_SCENE_ALIAS;
    for(unsigned i=0;i<resources->texture_count;++i) {
        const ctr_scene_resource_span *p=&resources->textures[i];
        if(!span(p->bytes,p->size))return CTR_SCENE_OVERFLOW;
        if(overlap(p->bytes,p->size,state,sizeof(*state)) ||
           overlap(p->bytes,p->size,surface->bytes,surface->size) ||
           (depth && (overlap(p->bytes,p->size,depth,sizeof(*depth)) ||
                      overlap(p->bytes,p->size,depth->bytes,depth->size))) ||
           (out && overlap(p->bytes,p->size,out,sizeof(*out))))return CTR_SCENE_ALIAS;
    }
    if(backend && (overlap(backend,sizeof(*backend),state,sizeof(*state)) ||
       overlap(backend,sizeof(*backend),surface->bytes,surface->size) ||
       (depth && (overlap(backend,sizeof(*backend),depth,sizeof(*depth)) ||
                  overlap(backend,sizeof(*backend),depth->bytes,depth->size))) ||
       (out && overlap(backend,sizeof(*backend),out,sizeof(*out)))))return CTR_SCENE_ALIAS;
    if(backend && (!backend->begin ||
                   (!backend->draw && !backend->draw_textures && !backend->draw_mips &&
                    !(software_game && backend->draw_transformed)) ||
                   !backend->end || !backend->abort))
        return publish(out,report,CTR_SCENE_ARGUMENT);
    bool direct_backend=backend && (backend->features&CTR_SCENE_BACKEND_DIRECT)!=0u;
    bool forced_fallback=false;
    if(direct_backend && (color_format!=CTR_SCENE_COLOR_RGBA8 || !software_game ||
       (depth && (!backend->begin_depth || !backend->end_depth)))) {
        backend=NULL;direct_backend=false;forced_fallback=true;
    }
    if(sizeof(float)!=4 || FLT_RADIX!=2 || FLT_MANT_DIG!=24 || FLT_MAX_EXP!=128)
        return publish(out,report,CTR_SCENE_UNSUPPORTED);
    float one=1.0f;uint32_t one_bits;
    memcpy(&one_bits,&one,sizeof(one_bits));
    if(one_bits!=UINT32_C(0x3f800000))return publish(out,report,CTR_SCENE_UNSUPPORTED);
    if(resources->size>CTR_VERTEX_MAX_SOURCE_BYTES || surface->size>CTR_SCENE_MAX_SURFACE_BYTES)
        return publish(out,report,CTR_SCENE_LIMIT);
    if(color_format!=CTR_SCENE_COLOR_RGBA8 && (backend || color_format<CTR_SCENE_COLOR_RGB5A1 ||
       color_format>CTR_SCENE_COLOR_RGBA4 || (!software_game && color_format!=CTR_SCENE_COLOR_RGB5A1)))
        return publish(out,report,CTR_SCENE_UNSUPPORTED);
    if(depth && depth->format!=CTR_SCENE_DEPTH_D16 && depth->format!=CTR_SCENE_DEPTH_D24S8)
        return publish(out,report,CTR_SCENE_UNSUPPORTED);
    if(depth && depth->size>(depth->format==CTR_SCENE_DEPTH_D24S8?
                            CTR_SCENE_MAX_D24S8_BYTES:CTR_SCENE_MAX_DEPTH_BYTES))
        return publish(out,report,CTR_SCENE_LIMIT);
    if((uint64_t)resources->base_address+resources->size>(uint64_t)UINT32_MAX+1u)
        return publish(out,report,CTR_SCENE_OVERFLOW);
    if(depth) {
        if(depth->width>512u || depth->height>512u)return publish(out,report,CTR_SCENE_LAYOUT);
        unsigned width=depth->format==CTR_SCENE_DEPTH_D24S8?4u:2u;
        uint64_t first=depth->address,last=first+(uint64_t)depth->width*depth->height*width;
        if(!bound || !depth->width || depth->width!=surface->width || depth->height!=surface->height ||
           depth->stride<depth->width*width || (uint64_t)depth->height*depth->stride>depth->size ||
           (depth->address&UINT32_C(0x80000007)) || last>(uint64_t)UINT32_MAX+1u)
            return publish(out,report,CTR_SCENE_LAYOUT);
        unsigned color_bytes=color_format==CTR_SCENE_COLOR_RGBA8?4u:2u;
        uint64_t color_end=(uint64_t)color_address+(uint64_t)surface->width*surface->height*color_bytes;
        if((first<color_end && color_address<last) ||
           (!software_game && first<(uint64_t)resources->base_address+resources->size && resources->base_address<last))
            return publish(out,report,CTR_SCENE_ALIAS);
    }
    size_t texture_bytes=0;
    for(unsigned i=0;i<resources->texture_count;++i) {
        const ctr_scene_resource_span *p=&resources->textures[i];
        if(p->size>CTR_SCENE_MAX_TEXTURE_SOURCE_BYTES-texture_bytes)
            return publish(out,report,CTR_SCENE_LIMIT);
        texture_bytes+=p->size;
        if(!p->size)return publish(out,report,CTR_SCENE_LAYOUT);
        uint64_t end=(uint64_t)p->base_address+p->size;
        if(end>(uint64_t)UINT32_MAX+1u)return publish(out,report,CTR_SCENE_OVERFLOW);
        if(depth && p->base_address<(uint64_t)depth->address+(uint64_t)depth->width*depth->height*
           (depth->format==CTR_SCENE_DEPTH_D24S8?4u:2u) &&
           depth->address<end)return publish(out,report,CTR_SCENE_ALIAS);
        if(resources->size && p->base_address<(uint64_t)resources->base_address+resources->size &&
           resources->base_address<end)return publish(out,report,CTR_SCENE_ALIAS);
        for(unsigned j=0;j<i;++j) {
            const ctr_scene_resource_span *q=&resources->textures[j];
            if(p->base_address<(uint64_t)q->base_address+q->size && q->base_address<end)
                return publish(out,report,CTR_SCENE_ALIAS);
        }
    }
    if(!surface->width || !surface->height || surface->width>512u || surface->height>512u ||
       surface->stride<surface->width*4u || (uint64_t)surface->height*surface->stride>surface->size ||
       (resources->base_address&15u))return publish(out,report,CTR_SCENE_LAYOUT);
    if(state->fixed_words>=3u || state->uniform_words>=((state->registers[0x2c0u]>>31)?4u:3u))
        return publish(out,report,CTR_SCENE_LAYOUT);
    ctr_pica_report wire={0};
    ctr_pica_result (*inspect)(const void *,size_t,size_t,ctr_pica_write *,size_t,ctr_pica_report *)=
        guest_stream?ctr_pica_inspect_expanded:ctr_pica_inspect;
    size_t write_limit=guest_stream?CTR_PICA_MAX_CHAIN_WRITES:CTR_PICA_MAX_WRITES;
    if(inspect(commands,size,write_limit,NULL,0,&wire)!=CTR_PICA_OK ||
       !size || (!guest_stream && (size&15u))) {
        report.byte_offset=wire.parameter_offset;report.reg=wire.reg;
        return publish(out,report,CTR_SCENE_WIRE);
    }
    enum { DIRECT_CACHE_SLOTS=2048u };
    ctr_pica_write *writes=malloc(wire.writes*sizeof(*writes));
    ctr_scene_state *trial=malloc(sizeof(*trial));
    uint8_t *pixels=malloc(surface->size);
    uint8_t *depth_pixels=depth?malloc(depth->size):NULL;
    ctr_vertex *decoded=malloc(CTR_SCENE_MAX_VERTICES*sizeof(*decoded));
    ctr_shader_fast *direct_fast=direct_backend?malloc(sizeof(*direct_fast)):NULL;
    unsigned *direct_cache_id=direct_backend?malloc(DIRECT_CACHE_SLOTS*sizeof(*direct_cache_id)):NULL;
    unsigned *direct_cache_steps=direct_backend?malloc(DIRECT_CACHE_SLOTS*sizeof(*direct_cache_steps)):NULL;
    scene_vertex *direct_cache_vertex=direct_backend?malloc(DIRECT_CACHE_SLOTS*sizeof(*direct_cache_vertex)):NULL;
    if(!writes || !trial || !pixels || !decoded || (depth && !depth_pixels) ||
       (direct_backend && (!direct_fast || !direct_cache_id || !direct_cache_steps || !direct_cache_vertex))) {
        free(writes);free(trial);free(pixels);free(depth_pixels);free(decoded);
        free(direct_fast);free(direct_cache_id);free(direct_cache_steps);free(direct_cache_vertex);
        return publish(out,report,CTR_SCENE_NO_MEMORY);
    }
    *trial=*state;
    if(!direct_backend) {
        memcpy(pixels,surface->bytes,surface->size);
        if(depth)memcpy(depth_pixels,depth->bytes,depth->size);
    }
    inspect(commands,size,write_limit,writes,wire.writes,&wire);
    bool native_mips=backend && (backend->draw_mips || (software_game && backend->draw_transformed));
    unsigned features=backend?backend->features&CTR_SCENE_BACKEND_DEPTH_FOG:0u;
    if(native_mips)features|=NATIVE_MIPS;
    ctr_scene_result result=CTR_SCENE_OK;
    bool commit_color=true,commit_depth=depth!=NULL;
    if(direct_backend) {
        scene_direct_ctx direct={surface,depth,true,false,direct_fast,
            direct_cache_id,direct_cache_steps,DIRECT_CACHE_SLOTS,direct_cache_vertex,NULL,0u};
        result=replay(trial,commands,writes,wire.writes,resources,surface,
                      decoded,NULL,&report,false,bound,color_address,color_format,depth,NULL,true,
                      backend->draw_textures || native_mips,software_game,features,backend,cache,&direct);
        free(direct.transformed_batch);
        if(result==CTR_SCENE_OK && (trial->lists==UINT64_MAX || report.draws>UINT64_MAX-trial->draws ||
           report.triangles>UINT64_MAX-trial->triangles || report.fragments>UINT64_MAX-trial->fragments ||
           report.shader_instructions>UINT64_MAX-trial->shader_instructions))result=CTR_SCENE_OVERFLOW;
        if(result==CTR_SCENE_OK && direct.began) {
            ctr_scene_surface target=*surface;target.bytes=pixels;
            ctr_scene_depth_surface depth_target={0};
            if(depth) { depth_target=*depth;depth_target.bytes=depth_pixels; }
            report.backend_stage=CTR_SCENE_BACKEND_END;
            bool ended=depth?backend->end_depth(backend->context,&target,&depth_target):
                backend->end(backend->context,&target);
            if(!ended)result=CTR_SCENE_BACKEND;
            else report.backend_stage=CTR_SCENE_BACKEND_NONE;
        }
        commit_color=direct.began;commit_depth=false;
        if(result!=CTR_SCENE_OK && (direct.began || result==CTR_SCENE_BACKEND))
            backend->abort(backend->context);
        if(result==CTR_SCENE_BACKEND || result==CTR_SCENE_UNSUPPORTED) {
            *trial=*state;memcpy(pixels,surface->bytes,surface->size);
            if(depth)memcpy(depth_pixels,depth->bytes,depth->size);
            report=(ctr_scene_report){0};
            result=replay(trial,commands,writes,wire.writes,resources,surface,
                          decoded,pixels,&report,false,bound,color_address,color_format,depth,depth_pixels,false,
                          true,software_game,features&CTR_SCENE_BACKEND_DEPTH_FOG,NULL,cache,NULL);
            if(result==CTR_SCENE_OK && (trial->lists==UINT64_MAX || report.draws>UINT64_MAX-trial->draws ||
               report.triangles>UINT64_MAX-trial->triangles || report.fragments>UINT64_MAX-trial->fragments ||
               report.shader_instructions>UINT64_MAX-trial->shader_instructions))result=CTR_SCENE_OVERFLOW;
            report.backend_fallback=true;commit_color=true;commit_depth=depth!=NULL;
        }
    } else {
        result=replay(trial,commands,writes,wire.writes,resources,surface,
                      decoded,pixels,&report,false,bound,color_address,color_format,depth,depth_pixels,backend && depth,
                      !backend || backend->draw_textures || native_mips,software_game,features,NULL,cache,NULL);
        if(result==CTR_SCENE_OK && (trial->lists==UINT64_MAX || report.draws>UINT64_MAX-trial->draws ||
           report.triangles>UINT64_MAX-trial->triangles || report.fragments>UINT64_MAX-trial->fragments ||
           report.shader_instructions>UINT64_MAX-trial->shader_instructions))result=CTR_SCENE_OVERFLOW;
        if(result==CTR_SCENE_OK && backend && report.draws) {
            ctr_scene_state *native=malloc(sizeof(*native));uint8_t *readback=malloc(surface->size);
            uint8_t *depth_readback=depth?malloc(depth->size):NULL;
            if(depth && (!backend->begin_depth || !backend->end_depth))result=CTR_SCENE_UNSUPPORTED;
            else if(!native || !readback || (depth && !depth_readback))result=CTR_SCENE_NO_MEMORY;
            else {
                *native=*state;memcpy(readback,surface->bytes,surface->size);
                ctr_scene_surface target=*surface;target.bytes=readback;
                ctr_scene_depth_surface depth_target={0};
                if(depth) { depth_target=*depth;depth_target.bytes=depth_readback;memcpy(depth_readback,depth->bytes,depth->size); }
                report.backend_stage=CTR_SCENE_BACKEND_BEGIN;
                bool began=depth?backend->begin_depth(backend->context,surface,depth):backend->begin(backend->context,surface);
                if(!began)result=CTR_SCENE_BACKEND;
                else {
                    ctr_scene_report native_report={0};
                    result=replay(native,commands,writes,wire.writes,resources,surface,
                                  decoded,NULL,&native_report,false,bound,color_address,color_format,depth,NULL,true,
                                  backend->draw_textures || native_mips,software_game,features,backend,cache,NULL);
                    if(result!=CTR_SCENE_OK) {
                        report.byte_offset=native_report.byte_offset;report.reg=native_report.reg;
                        report.backend_stage=native_report.backend_stage;
                    } else {
                        report.backend_stage=CTR_SCENE_BACKEND_END;
                        bool ended=depth?backend->end_depth(backend->context,&target,&depth_target):backend->end(backend->context,&target);
                        if(!ended)result=CTR_SCENE_BACKEND;
                        else if(memcmp(readback,pixels,surface->size) ||
                                (depth && memcmp(depth_readback,depth_pixels,depth->size))) {
                            report.backend_stage=CTR_SCENE_BACKEND_PARITY;result=CTR_SCENE_BACKEND;
                        } else {
                            /* Publish actual native bytes after exact software parity. */
                            memcpy(pixels,readback,surface->size);report.backend_stage=CTR_SCENE_BACKEND_NONE;
                            if(depth)memcpy(depth_pixels,depth_readback,depth->size);
                        }
                    }
                }
                if(result!=CTR_SCENE_OK)backend->abort(backend->context);
            }
            free(native);free(readback);free(depth_readback);
        }
    }
    if(result==CTR_SCENE_OK) {
        if(forced_fallback && report.draws)report.backend_fallback=true;
        ++trial->lists;trial->draws+=report.draws;trial->triangles+=report.triangles;
        trial->fragments+=report.fragments;trial->shader_instructions+=report.shader_instructions;
        *state=*trial;
        if(commit_color)memcpy(surface->bytes,pixels,surface->size);
        if(commit_depth)memcpy(depth->bytes,depth_pixels,depth->size);
        report.byte_offset=size;report.reg=0;
    }
    free(writes);free(trial);free(pixels);free(depth_pixels);free(decoded);
    free(direct_fast);free(direct_cache_id);free(direct_cache_steps);free(direct_cache_vertex);
    return publish(out,report,result);
}
ctr_scene_result ctr_scene_execute(ctr_scene_state *state,const void *commands,size_t size,
                                  const ctr_scene_resources *resources,
                                  const ctr_scene_surface *surface,ctr_scene_report *out) {
    return execute(state,commands,size,resources,surface,out,false,0,0,NULL,NULL,NULL,false);
}
ctr_scene_result ctr_scene_execute_target(ctr_scene_state *state,const void *commands,size_t size,
                                         const ctr_scene_resources *resources,
                                         const ctr_scene_surface *surface,uint32_t color_address,
                                         ctr_scene_report *out) {
    return execute(state,commands,size,resources,surface,out,true,color_address,0,NULL,NULL,NULL,false);
}
ctr_scene_result ctr_scene_execute_target_backend(ctr_scene_state *state,const void *commands,size_t size,
                                                  const ctr_scene_resources *resources,
                                                  const ctr_scene_surface *surface,uint32_t color_address,
                                                  const ctr_scene_backend *backend,ctr_scene_report *out) {
    return execute(state,commands,size,resources,surface,out,true,color_address,0,NULL,backend,NULL,false);
}
ctr_scene_result ctr_scene_execute_targets(ctr_scene_state *state,const void *commands,size_t size,
                                           const ctr_scene_resources *resources,const ctr_scene_surface *surface,
                                           uint32_t color_address,const ctr_scene_depth_surface *depth,ctr_scene_report *out) {
    return execute(state,commands,size,resources,surface,out,true,color_address,0,depth,NULL,NULL,false);
}
ctr_scene_result ctr_scene_execute_targets_backend(ctr_scene_state *state,const void *commands,size_t size,
                                                   const ctr_scene_resources *resources,const ctr_scene_surface *surface,
                                                   uint32_t color_address,const ctr_scene_depth_surface *depth,
                                                   const ctr_scene_backend *backend,ctr_scene_report *out) {
    return execute(state,commands,size,resources,surface,out,true,color_address,0,depth,backend,NULL,false);
}
ctr_scene_result ctr_scene_execute_targets_cached(ctr_scene_state *state,const void *commands,size_t size,
                                                  const ctr_scene_resources *resources,const ctr_scene_surface *surface,
                                                  uint32_t color_address,const ctr_scene_depth_surface *depth,
                                                  const ctr_scene_backend *backend,ctr_texture_cache *cache,
                                                  ctr_scene_report *out) {
    return execute(state,commands,size,resources,surface,out,true,color_address,0,depth,backend,cache,false);
}
ctr_scene_result ctr_scene_execute_targets_format_cached(ctr_scene_state *state,const void *commands,size_t size,
                                                         const ctr_scene_resources *resources,const ctr_scene_surface *surface,
                                                         uint32_t color_address,unsigned color_format,const ctr_scene_depth_surface *depth,
                                                         const ctr_scene_backend *backend,ctr_texture_cache *cache,
                                                         ctr_scene_report *out) {
    return execute(state,commands,size,resources,surface,out,true,color_address,color_format,depth,backend,cache,true);
}
const char *ctr_scene_result_name(ctr_scene_result result) {
    static const char *const names[]={"ok","argument","overflow","alias","limit","no-memory",
        "invalid-wire-or-finalization","unsupported-pipeline","invalid-layout","vertex-input",
        "shader-execution-or-output","unsupported-numeric-value","native-backend-or-parity"};
    return (unsigned)result<sizeof(names)/sizeof(names[0])?names[result]:"unknown";
}
