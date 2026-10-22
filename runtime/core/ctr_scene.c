/* Original experimental owned-resource PICA scene boundary; no emulator linked. */
#include "ctr_scene.h"
#include "ctr_texture.h"
#include <float.h>
#include <stdlib.h>
#include <string.h>

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
    return reg==0x080u || reg==0x08fu || reg==0x0e0u || reg==0x0fdu ||
        (reg>=0x100u && reg<=0x107u) || (reg>=0x112u && reg<=0x117u) ||
        (reg>=0x11bu && reg<=0x11eu);
}
static bool fragment_register(unsigned reg) {
    static const unsigned tev[]={0xc0,0xc8,0xd0,0xd8,0xf0,0xf8};
    for(unsigned i=0;i<6;++i)if(reg>=tev[i] && reg<=tev[i]+4u)return true;
    return (reg>=0x080u && reg<=0x08bu) || reg==0x08eu || reg==0x08fu ||
        reg==0x0e0u || reg==0x0fdu || reg==0x04du || reg==0x04eu || reg==0x06du ||
        reg==0x105u || reg==0x106u || reg==0x114u || reg==0x115u ||
        reg==0x116u || reg==0x11cu || ctr_fragment_register(reg);
}
static bool configured(const ctr_scene_state *s,unsigned reg,uint32_t value) {
    return s->register_defined[reg]==15u && s->registers[reg]==value;
}
static ctr_scene_result target_pipeline(const ctr_scene_state *s,const ctr_scene_surface *surface,
                                        uint32_t color_address,const ctr_scene_depth_surface *depth) {
    static const unsigned disabled[]={0x08f,0x117,0x11b};
    for(unsigned i=0;i<sizeof(disabled)/sizeof(*disabled);++i)
        if(!configured(s,disabled[i],0u))return CTR_SCENE_UNSUPPORTED;
    bool stencil=depth && depth->format==CTR_SCENE_DEPTH_D24S8;
    if(!stencil && (!configured(s,0x105u,0u) || !configured(s,0x106u,0u)))
        return CTR_SCENE_UNSUPPORTED;
    if(depth) {
        unsigned gate=stencil?3u:2u;
        if(s->register_defined[0x114u]!=15u || s->register_defined[0x115u]!=15u ||
           (s->registers[0x114u]!=0u && s->registers[0x114u]!=gate) ||
           (s->registers[0x115u]!=0u && s->registers[0x115u]!=gate) ||
           !configured(s,0x116u,depth->format) || !configured(s,0x11cu,depth->address>>3) ||
           !configured(s,0x04du,UINT32_C(0xbf0000)) ||
           s->register_defined[0x04eu]!=15u ||
           (s->registers[0x04eu]!=0u && s->registers[0x04eu]!=UINT32_C(0x800000)) ||
           !configured(s,0x06du,1u) ||
           (((s->registers[0x107u]&1u) || (stencil && (s->registers[0x105u]&1u))) &&
            s->registers[0x114u]!=gate))return CTR_SCENE_UNSUPPORTED;
    } else if(!configured(s,0x114u,0u) || !configured(s,0x115u,0u) ||
              s->registers[0x116u] || s->registers[0x11cu])return CTR_SCENE_UNSUPPORTED;
    uint32_t dimensions=surface->width|((surface->height-1u)<<12)|UINT32_C(0x01000000);
    if((color_address&7u) || (surface->width&7u) || (surface->height&7u) ||
       !configured(s,0x11du,color_address>>3) || !configured(s,0x11eu,dimensions))return CTR_SCENE_LAYOUT;
    return CTR_SCENE_OK;
}
static ctr_scene_result fragment(const ctr_scene_state *s,const ctr_scene_resources *resources,
                                 bool bound,ctr_scene_texture *texture,uint8_t **allocation,
                                 ctr_tev_program *combiner) {
    bool textured=s->registers[0x080u]!=0u;
    if((s->register_defined[0x064u] && !configured(s,0x064u,textured?1u:0u)) ||
       (s->register_defined[0x06fu] && !configured(s,0x06fu,textured?UINT32_C(0x103):3u)))
        return CTR_SCENE_UNSUPPORTED;
    bool configured_tev=s->register_defined[0x0e0u] || s->register_defined[0x0fdu];
    for(unsigned i=0;i<CTR_TEV_STAGES;++i)
        if(s->register_defined[ctr_tev_registers[i]])configured_tev=true;
    if(!textured && !bound && !configured_tev) {
        memset(combiner,0,sizeof(*combiner));
        for(unsigned i=1;i<CTR_TEV_STAGES;++i)
            combiner->stages[i].source[0][0]=combiner->stages[i].source[1][0]=15u;
        return CTR_SCENE_OK;
    }
    if(!configured(s,0x080u,textured?1u:0u) || s->registers[0x08fu])return CTR_SCENE_UNSUPPORTED;
    if(!ctr_tev_decode(s->registers,s->register_defined,textured,combiner))return CTR_SCENE_UNSUPPORTED;
    if(!textured)return CTR_SCENE_OK;
    for(unsigned reg=0x082u;reg<=0x085u;++reg)
        if(s->register_defined[reg]!=15u)return CTR_SCENE_UNSUPPORTED;
    if(s->register_defined[0x08eu]!=15u || s->registers[0x084u])return CTR_SCENE_UNSUPPORTED;
    for(unsigned reg=0x086u;reg<=0x08bu;++reg)
        if(s->registers[reg])return CTR_SCENE_UNSUPPORTED;
    uint32_t dimensions=s->registers[0x082u],format=s->registers[0x08eu],params=s->registers[0x083u];
    unsigned width=(dimensions>>16)&2047u,height=dimensions&2047u;
    if((dimensions&~UINT32_C(0x07ff07ff)) || format>=14u)return CTR_SCENE_UNSUPPORTED;
    size_t bytes=0;
    if(ctr_texture_size(format,width,height,&bytes)!=CTR_TEXTURE_OK)return CTR_SCENE_LAYOUT;
    if((params&~UINT32_C(0x3326)) || ((params&6u)!=0u && (params&6u)!=6u) ||
       ((params>>8)&3u)==1u || ((params>>12)&3u)==1u ||
       (params&32u)!=(format>=12u?32u:0u))return CTR_SCENE_UNSUPPORTED;
    if(s->registers[0x085u]>UINT32_MAX/8u)return CTR_SCENE_OVERFLOW;
    uint32_t address=s->registers[0x085u]*8u;const uint8_t *source=NULL;
    uint64_t end=(uint64_t)address+bytes;
    if(end>(uint64_t)UINT32_MAX+1u)return CTR_SCENE_OVERFLOW;
    if(address>=resources->base_address && end<=(uint64_t)resources->base_address+resources->size)
        source=(const uint8_t *)resources->bytes+(address-resources->base_address);
    for(unsigned i=0;!source && i<resources->texture_count;++i) {
        const ctr_scene_resource_span *p=&resources->textures[i];
        if(address>=p->base_address && end<=(uint64_t)p->base_address+p->size)
            source=(const uint8_t *)p->bytes+(address-p->base_address);
    }
    if(!source)return CTR_SCENE_LAYOUT;
    uint8_t *rgba=malloc((size_t)width*height*4u);
    if(!rgba)return CTR_SCENE_NO_MEMORY;
    if(ctr_texture_decode(source,bytes,format,width,height,rgba,(size_t)width*height*4u)!=CTR_TEXTURE_OK) {
        free(rgba);return CTR_SCENE_LAYOUT;
    }
    *texture=(ctr_scene_texture){rgba,width,height,address,format,params};
    *allocation=rgba;return CTR_SCENE_OK;
}
/* Reject unsupported independently meaningful state even when it follows a
 * staged draw. Configuration may prepare a later list, but cannot hide an
 * unsupported filter, texture mode or combiner behind the last trigger. */
static bool fragment_value(unsigned reg,uint32_t value) {
    if(ctr_fragment_register(reg))return ctr_fragment_register_value(reg,value);
    if(reg==0x105u || reg==0x106u)return ctr_stencil_register_value(reg,value);
    if(reg==0x04du)return value==UINT32_C(0xbf0000);
    if(reg==0x04eu)return value==0u || value==UINT32_C(0x800000);
    if(reg==0x06du)return value==1u;
    if(reg==0x114u || reg==0x115u)return value==0u || value==2u || value==3u;
    if(reg==0x116u)return value==CTR_SCENE_DEPTH_D16 || value==CTR_SCENE_DEPTH_D24S8;
    if(reg==0x11cu)return value<=UINT32_C(0x0fffffff);
    if(reg==0x080u)return value<=1u;
    if(reg==0x082u)return !(value&~UINT32_C(0x07ff07ff));
    if(reg==0x083u)return !(value&~UINT32_C(0x3326)) &&
        ((value&6u)==0u || (value&6u)==6u) &&
        ((value>>8)&3u)!=1u && ((value>>12)&3u)!=1u;
    if(reg==0x084u || (reg>=0x086u && reg<=0x08bu) || reg==0x08fu)return !value;
    if(reg==0x085u)return value<=UINT32_MAX/8u;
    if(reg==0x08eu)return value<14u;
    return ctr_tev_register_value(reg,value);
}
static ctr_scene_result packed(const uint32_t words[3],uint32_t bits[4]) {
    uint32_t raw[4]={words[2]&UINT32_C(0xffffff),
        ((words[1]&65535u)<<8)|(words[2]>>24),
        ((words[0]&255u)<<16)|(words[1]>>16),words[0]>>8};
    for(unsigned i=0;i<4;++i)if(!ctr_shader_f24_decode(raw[i],&bits[i]))return CTR_SCENE_NUMERIC;
    return CTR_SCENE_OK;
}
static ctr_scene_result upload(ctr_scene_state *s,unsigned reg,uint32_t value,unsigned mask) {
    if(mask!=15u)return CTR_SCENE_UNSUPPORTED;
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
                    uint32_t raw;
                    if(!ctr_shader_f24_exact(s->pending_uniform[i],&raw))return CTR_SCENE_NUMERIC;
                    s->shader.uniforms[index][3u-i]=s->pending_uniform[i];
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
        s->shader.descriptors[index]=value;s->shader.descriptor_defined[index]=1u;
        s->registers[0x2d5u]=index+1u;
    } else if(reg==0x2bfu) {
        if(value!=1u)return CTR_SCENE_UNSUPPORTED;
    } else return CTR_SCENE_UNSUPPORTED;
    return CTR_SCENE_OK;
}
static ctr_scene_result vertices(const ctr_scene_state *s,const ctr_scene_resources *resources,
                                 bool indexed,ctr_vertex *output,ctr_scene_report *report) {
    uint32_t high=s->registers[0x202u],low=s->registers[0x201u];
    unsigned total=(high>>28)+1u,fixed=(high>>16)&4095u;
    uint32_t config=s->registers[0x2b9u];
    if(total>12u || (fixed>>total) || config!=(UINT32_C(0xa0000000)|(total-1u)) ||
       (s->registers[0x200u]&UINT32_C(0xe0000001)))return CTR_SCENE_UNSUPPORTED;
    uint64_t base=(uint64_t)(s->registers[0x200u]>>1)*16u;
    if(base>UINT32_MAX)return CTR_SCENE_OVERFLOW;
    if(base!=resources->base_address)return CTR_SCENE_LAYOUT;
    uint64_t permutation=(uint64_t)s->registers[0x2bbu]|(uint64_t)s->registers[0x2bcu]<<32;
    uint16_t inputs=0,loaded=0;unsigned slots[12];
    for(unsigned a=0;a<total;++a) {
        slots[a]=(unsigned)((permutation>>(a*4u))&15u);
        if(inputs&(1u<<slots[a]))return CTR_SCENE_LAYOUT;
        inputs|=(uint16_t)(1u<<slots[a]);
        if((fixed&(1u<<a)) && !(s->fixed_defined&(1u<<a)))return CTR_SCENE_LAYOUT;
    }
    ctr_vertex_attribute attributes[12];ctr_vertex_stream streams[12];
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
                    attributes[attribute_count++]=(ctr_vertex_attribute){
                        (unsigned)stream_count,slots[id],type,components,at};used=true;
                }
                at+=width*components;
            }
            if(at>stride)return CTR_SCENE_LAYOUT;
        }
        if(used)streams[stream_count++]=(ctr_vertex_stream){resources->bytes,resources->size,offset,stride};
    }
    if(((loaded|fixed)&((1u<<total)-1u))!=((1u<<total)-1u))return CTR_SCENE_LAYOUT;
    unsigned count=s->registers[0x228u];
    if(attribute_count) {
        ctr_vertex_result result;
        if(indexed) {
            uint32_t raw=s->registers[0x227u];
            if(raw&UINT32_C(0x70000000))return CTR_SCENE_UNSUPPORTED;
            ctr_vertex_indices indices={resources->bytes,resources->size,raw&UINT32_C(0x0fffffff),raw>>31};
            result=ctr_vertex_decode_indexed(attributes,attribute_count,streams,stream_count,&indices,0,count,output,count);
        } else result=ctr_vertex_decode(attributes,attribute_count,streams,stream_count,s->registers[0x22au],count,output,count);
        report->vertex_result=result;
        if(result!=CTR_VERTEX_OK)return CTR_SCENE_VERTEX;
    } else {
        /* Fixed-only shaders still validate an indexed draw's entire index span. */
        if(indexed) {
            uint32_t raw=s->registers[0x227u];
            if(raw&UINT32_C(0x70000000))return CTR_SCENE_UNSUPPORTED;
            uint64_t last=(uint64_t)(raw&UINT32_C(0x0fffffff))+(uint64_t)count*((raw>>31)+1u);
            if(last>resources->size)return CTR_SCENE_LAYOUT;
        }
        memset(output,0,count*sizeof(*output));
        for(unsigned vertex=0;vertex<count;++vertex)
            for(unsigned input=0;input<16;++input)output[vertex].input[input][3]=1.0f;
    }
    for(unsigned vertex=0;vertex<count;++vertex)for(unsigned a=0;a<total;++a)if(fixed&(1u<<a)) {
        for(unsigned lane=0;lane<4;++lane)output[vertex].input[slots[a]][lane]=number(s->fixed[a][lane]);
        output[vertex].enabled|=(uint16_t)(1u<<slots[a]);
    }
    return CTR_SCENE_OK;
}
typedef struct { int32_t x,y,uv[2];float position[4],color[4]; } scene_vertex;
static ctr_scene_result semantics(const ctr_scene_state *s,const ctr_shader_output *out,
                                  const ctr_scene_surface *surface,scene_vertex *v,bool textured) {
    unsigned mask=s->registers[0x2bdu],total=s->registers[0x04fu],packed_index=0,seen=0;
    if((mask&~65535u) || !mask || total>7u || total!=bit_count(mask))return CTR_SCENE_UNSUPPORTED;
    for(unsigned reg=0;reg<16;++reg)if(mask&(1u<<reg)) {
        uint32_t mapping=s->registers[0x050u+packed_index++];
        if(mapping&UINT32_C(0xe0e0e0e0))return CTR_SCENE_UNSUPPORTED;
        for(unsigned lane=0;lane<4;++lane) {
            unsigned id=(mapping>>(lane*8u))&31u;
            if(id==31u)continue;
            if(id>13u || (id>=4u && id<8u))return CTR_SCENE_UNSUPPORTED;
            if((seen&(1u<<id)) || !(out->written[reg]&(1u<<lane)))return CTR_SCENE_SHADER;
            seen|=1u<<id;
            if(id<4u)v->position[id]=out->values[reg][lane];
            else if(id<12u)v->color[id-8u]=out->values[reg][lane];
            else {
                double scaled=(double)out->values[reg][lane]*65536.0;
                if(scaled<-1048576.0 || scaled>1048576.0)return CTR_SCENE_UNSUPPORTED;
                int32_t fixed=(int32_t)scaled;
                if((double)fixed!=scaled)return CTR_SCENE_NUMERIC;
                v->uv[id-12u]=fixed;
            }
        }
    }
    if(seen!=UINT32_C(0x3f0f) && (textured || seen!=UINT32_C(0x0f0f)))return CTR_SCENE_SHADER;
    if(v->position[3]!=1.0f || v->position[0]<-1.0f || v->position[0]>1.0f ||
       v->position[1]<-1.0f || v->position[1]>1.0f || v->position[2]<-1.0f || v->position[2]>0.0f)
        return CTR_SCENE_UNSUPPORTED;
    if(v->color[3]!=1.0f)return CTR_SCENE_UNSUPPORTED;
    for(unsigned lane=0;lane<4;++lane)if(v->color[lane]!=0.0f && v->color[lane]!=1.0f)
        return CTR_SCENE_UNSUPPORTED;
    uint32_t hx,hy,origin=s->registers[0x068u];
    if(!ctr_shader_f24_decode(s->registers[0x041u],&hx) ||
       !ctr_shader_f24_decode(s->registers[0x043u],&hy) || (origin&~UINT32_C(0x03ff03ff)))
        return CTR_SCENE_NUMERIC;
    float half[2]={number(hx),number(hy)};
    if(half[0]<=0 || half[1]<=0 || half[0]>(float)surface->width/2 || half[1]>(float)surface->height/2)
        return CTR_SCENE_UNSUPPORTED;
    int offsets[2]={(int)(origin&1023u),(int)((origin>>16)&1023u)};
    for(unsigned axis=0;axis<2;++axis) {
        if(offsets[axis]&512)offsets[axis]-=1024;
        /* The restricted path permits only exact 12.4 grid coordinates. */
        double coordinate=((double)v->position[axis]+1.0)*(double)half[axis]+offsets[axis];
        unsigned extent=axis?surface->height:surface->width;
        if(coordinate<0 || coordinate>extent)return CTR_SCENE_UNSUPPORTED;
        double scaled=coordinate*16.0;int32_t fixed=(int32_t)scaled;
        if((double)fixed!=scaled)return CTR_SCENE_NUMERIC;
        if(axis)v->y=fixed;else v->x=fixed;
    }
    return CTR_SCENE_OK;
}
static int64_t edge(scene_vertex a,scene_vertex b,int32_t x,int32_t y) {
    return (int64_t)(b.x-a.x)*(y-a.y)-(int64_t)(b.y-a.y)*(x-a.x);
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
    int64_t period=mode==3u?(int64_t)extent*2:extent;
    int64_t remainder=coordinate%period;if(remainder<0)remainder+=period;
    if(mode==3u && remainder>=(int64_t)extent)remainder=period-1-remainder;
    return (unsigned)remainder;
}
/* Bilinear weights retain the exact affine denominator, up to 2^42. The final
 * numerator needs 93 bits. Explicit limbs work on 32-bit PowerPC/MSVC without
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
                             uint64_t x,uint64_t y,uint64_t denominator) {
    if(a==b && a==c && a==d)return (uint8_t)a;
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
static void raster(const ctr_scene_state *s,const ctr_scene_surface *surface,
                   uint8_t *pixels,scene_vertex v[3],ctr_scene_report *report,
                   const ctr_scene_texture *texture,const ctr_tev_program *combiner,
                   const ctr_fragment_state *merger,const ctr_scene_depth_surface *depth,
                   uint8_t *depth_pixels,const ctr_stencil_state *stencil) {
    uint8_t primary[4];
    for(unsigned lane=0;lane<4;++lane)primary[lane]=v[0].color[lane]==1.0f?255u:0u;
    uint32_t incoming=depth?depth_value(v[0].position[2],depth->format):0u;
    int64_t area=edge(v[0],v[1],v[2].x,v[2].y);unsigned cull=s->registers[0x040u]&3u;
    if(!area || (cull==1u && area>0) || (cull==2u && area<0))return;
    if(area<0) { scene_vertex t=v[1];v[1]=v[2];v[2]=t;area=-area; }
    int32_t min_x=v[0].x,max_x=v[0].x,min_y=v[0].y,max_y=v[0].y;
    for(unsigned i=1;i<3;++i) {
        if(v[i].x<min_x)min_x=v[i].x;
        if(v[i].x>max_x)max_x=v[i].x;
        if(v[i].y<min_y)min_y=v[i].y;
        if(v[i].y>max_y)max_y=v[i].y;
    }
    min_x/=16;min_y/=16;max_x/=16;max_y/=16;
    if(max_x>=(int32_t)surface->width)max_x=(int32_t)surface->width-1;
    if(max_y>=(int32_t)surface->height)max_y=(int32_t)surface->height-1;
    unsigned mode=s->registers[0x065u]&3u,x1=s->registers[0x066u]&1023u,
        y1=(s->registers[0x066u]>>16)&1023u,x2=s->registers[0x067u]&1023u,
        y2=(s->registers[0x067u]>>16)&1023u;
    for(int32_t y=min_y;y<=max_y;++y)for(int32_t x=min_x;x<=max_x;++x) {
        bool box=(unsigned)x>=x1 && (unsigned)x<=x2 && (unsigned)y>=y1 && (unsigned)y<=y2;
        if((mode==3u && !box) || (mode==1u && box))continue;
        bool inside=true;
        for(unsigned e=0;e<3;++e) {
            int64_t value=edge(v[e],v[(e+1u)%3u],x*16+8,y*16+8);
            if(value<0 || (!value && !inclusive(v[e],v[(e+1u)%3u]))) { inside=false;break; }
        }
        if(inside) {
            uint8_t *pixel=pixels+(size_t)y*surface->stride+(size_t)x*4u;
            uint8_t sampled[4]={0};
            if(texture) {
                int64_t weights[3]={edge(v[1],v[2],x*16+8,y*16+8),
                    edge(v[2],v[0],x*16+8,y*16+8),edge(v[0],v[1],x*16+8,y*16+8)};
                unsigned coordinates[2][2];uint64_t fraction[2]={0,0};
                uint64_t denominator=(uint64_t)area*65536u;
                bool linear=(texture->params&6u)==6u;
                for(unsigned axis=0;axis<2;++axis) {
                    int64_t numerator=0;
                    for(unsigned i=0;i<3;++i)numerator+=weights[i]*v[i].uv[axis];
                    unsigned extent=axis?texture->height:texture->width;
                    /* Geometry is inside 512x512 on a 12.4 grid, |UV|<=16:
                     * even the largest numerator*extent is below 2^57. */
                    int64_t position=numerator*extent-(linear?(int64_t)(denominator/2u):0);
                    int64_t texel=floor_div(position,(int64_t)denominator);
                    unsigned wrap=(texture->params>>(axis?8u:12u))&3u;
                    coordinates[axis][0]=wrapped(texel,extent,wrap);
                    if(linear) {
                        coordinates[axis][1]=wrapped(texel+1,extent,wrap);
                        fraction[axis]=(uint64_t)(position-texel*(int64_t)denominator);
                    }
                }
                const uint8_t *samples[2][2];
                for(unsigned y0=0;y0<(linear?2u:1u);++y0)
                    for(unsigned x0=0;x0<(linear?2u:1u);++x0)
                        samples[y0][x0]=texture->rgba+((size_t)(texture->height-1u-coordinates[1][y0])*
                            texture->width+coordinates[0][x0])*4u;
                for(unsigned lane=0;lane<4;++lane) {
                    unsigned sample=linear?bilinear_byte(samples[0][0][lane],samples[0][1][lane],
                        samples[1][0][lane],samples[1][1][lane],fraction[0],fraction[1],denominator):samples[0][0][lane];
                    sampled[lane]=(uint8_t)sample;
                }
            }
            uint8_t source[4];ctr_tev_evaluate(combiner,primary,sampled,source);
            ++report->fragments;
            if(!ctr_fragment_alpha_passes(merger,source[3]))continue;
            if(depth) {
                bool packed=depth->format==CTR_SCENE_DEPTH_D24S8;
                unsigned width=packed?4u:2u;
                uint8_t *z=depth_pixels+(size_t)y*depth->stride+(size_t)x*width;
                uint32_t control=s->registers[0x107u];
                bool writable=s->registers[0x115u]==(packed?3u:2u);
                if(stencil->enabled && !ctr_stencil_passes(stencil,z[3])) {
                    if(writable)z[3]=ctr_stencil_apply(stencil,stencil->action[0],z[3]);
                    continue;
                }
                uint32_t stored=(uint32_t)z[0]|(uint32_t)z[1]<<8;
                if(packed)stored|=(uint32_t)z[2]<<16;
                bool depth_pass=!(control&1u) || depth_passes((control>>4)&7u,incoming,stored);
                if(stencil->enabled && writable)
                    z[3]=ctr_stencil_apply(stencil,stencil->action[depth_pass?2u:1u],z[3]);
                if(!depth_pass)continue;
                if((control&UINT32_C(0x1000)) && writable) {
                    z[0]=(uint8_t)incoming;z[1]=(uint8_t)(incoming>>8);
                    if(packed)z[2]=(uint8_t)(incoming>>16);
                }
            }
            ctr_fragment_apply(merger,source,pixel);
        }
    }
}
static ctr_scene_result draw(ctr_scene_state *s,const ctr_scene_resources *resources,
                             const ctr_scene_surface *surface,bool indexed,
                             ctr_vertex *decoded,uint8_t *pixels,ctr_scene_report *report,
                             bool bound,uint32_t color_address,const ctr_scene_depth_surface *depth,
                             uint8_t *depth_pixels,bool depth_endpoints,const ctr_scene_backend *backend) {
    if(bound) {
        ctr_scene_result result=target_pipeline(s,surface,color_address,depth);
        if(result!=CTR_SCENE_OK)return result;
    }
    ctr_fragment_state merger;
    if(!ctr_fragment_decode_depth(s->registers,s->register_defined,bound,depth!=NULL,&merger))return CTR_SCENE_UNSUPPORTED;
    ctr_stencil_state stencil;
    if(!ctr_stencil_decode(s->registers,s->register_defined,
                           depth && depth->format==CTR_SCENE_DEPTH_D24S8,&stencil))return CTR_SCENE_UNSUPPORTED;
    unsigned count=s->registers[0x228u];
    if(!count || count%3u)return CTR_SCENE_LAYOUT;
    if(count>CTR_SCENE_MAX_VERTICES || report->draws==CTR_SCENE_MAX_DRAWS)return CTR_SCENE_LIMIT;
    if(s->registers[0x229u] || s->registers[0x25eu] || (s->registers[0x040u]&~3u) ||
       (s->registers[0x065u]!=0u && s->registers[0x065u]!=1u && s->registers[0x065u]!=3u) ||
       (s->registers[0x066u]&~UINT32_C(0x03ff03ff)) ||
       (s->registers[0x067u]&~UINT32_C(0x03ff03ff)) || (s->registers[0x2bau]&UINT32_C(0xffff0000)))
        return CTR_SCENE_UNSUPPORTED;
    if(s->fixed_words || s->uniform_words)return CTR_SCENE_LAYOUT;
    ctr_scene_result result=vertices(s,resources,indexed,decoded,report);
    if(result!=CTR_SCENE_OK)return result;
    ctr_scene_texture texture={0};uint8_t *texture_allocation=NULL;ctr_tev_program combiner;
    result=fragment(s,resources,bound,&texture,&texture_allocation,&combiner);
    if(result!=CTR_SCENE_OK)return result;
    const ctr_scene_texture *sampling=texture_allocation?&texture:NULL;
    s->shader.entry=s->registers[0x2bau];
    if(backend) {
        report->backend_stage=CTR_SCENE_BACKEND_DRAW;
        bool accepted=backend->draw(backend->context,s,decoded,count,sampling);
        free(texture_allocation);
        if(!accepted)return CTR_SCENE_BACKEND;
        ++report->draws;return CTR_SCENE_OK;
    }
    scene_vertex primitive[3];
    for(unsigned vertex=0;vertex<count;++vertex) {
        ctr_shader_output out;
        report->shader_result=ctr_shader_execute(&s->shader,&decoded[vertex],&out);
        if(report->shader_result!=CTR_SHADER_OK) { result=CTR_SCENE_SHADER;break; }
        report->shader_instructions+=out.instructions;
        result=semantics(s,&out,surface,&primitive[vertex%3u],sampling!=NULL);
        if(result!=CTR_SCENE_OK)break;
        if(vertex%3u==2u) {
            for(unsigned i=1;i<3;++i)for(unsigned lane=0;lane<4;++lane)
                if(primitive[i].color[lane]!=primitive[0].color[lane])result=CTR_SCENE_UNSUPPORTED;
            if(depth) {
                for(unsigned i=1;i<3;++i)if(primitive[i].position[2]!=primitive[0].position[2])
                    result=CTR_SCENE_UNSUPPORTED;
                if(depth_endpoints && primitive[0].position[2]!=0.0f && primitive[0].position[2]!=-1.0f)
                    result=CTR_SCENE_UNSUPPORTED;
            }
            if(result!=CTR_SCENE_OK)break;
            raster(s,surface,pixels,primitive,report,sampling,&combiner,&merger,depth,depth_pixels,&stencil);++report->triangles;
        }
    }
    free(texture_allocation);
    if(result==CTR_SCENE_OK)++report->draws;
    return result;
}
void ctr_scene_init(ctr_scene_state *state) { if(state)memset(state,0,sizeof(*state)); }
static ctr_scene_result replay(ctr_scene_state *trial,const void *commands,
                                const ctr_pica_write *writes,size_t count,
                                const ctr_scene_resources *resources,const ctr_scene_surface *surface,
                                ctr_vertex *decoded,uint8_t *pixels,ctr_scene_report *report,
                                bool bound,uint32_t color_address,const ctr_scene_depth_surface *depth,
                                uint8_t *depth_pixels,bool depth_endpoints,const ctr_scene_backend *backend) {
    ctr_scene_result result=CTR_SCENE_OK;
    unsigned markers=0,fixed_reg=0,uniform_reg=0;size_t fixed_offset=0,uniform_offset=0;
    for(size_t i=0;i<count;++i) {
        const ctr_pica_write *write=&writes[i];unsigned reg=write->reg,mask=write->byte_mask;
        uint32_t value=write->value;report->reg=reg;report->byte_offset=write->byte_offset;
        if(reg==CTR_PICA_FINALIZE) {
            const uint8_t *bytes=commands;size_t at=write->byte_offset+4u;
            uint32_t header=(uint32_t)bytes[at]|(uint32_t)bytes[at+1]<<8|
                (uint32_t)bytes[at+2]<<16|(uint32_t)bytes[at+3]<<24;
            if(mask!=15u || value!=CTR_PICA_FINALIZE_VALUE || markers==2u || ((header>>20)&255u))
                result=CTR_SCENE_WIRE;
            else ++markers;
        } else if(markers)result=CTR_SCENE_WIRE;
        else if(reg==0x22eu || reg==0x22fu) {
            if(mask!=15u || value!=1u)result=CTR_SCENE_UNSUPPORTED;
            else result=draw(trial,resources,surface,reg==0x22fu,decoded,pixels,report,
                             bound,color_address,depth,depth_pixels,depth_endpoints,backend);
        } else if(reg==0x25fu) {
            if(mask!=15u || value!=1u)result=CTR_SCENE_UNSUPPORTED;
            /* Complete lists have no carried partial primitive to restart. */
        } else if(ordinary(reg) || fragment_register(reg) || (bound && target_register(reg))) {
            if(!bound && (reg==0x105u || reg==0x106u)) { result=CTR_SCENE_UNSUPPORTED;break; }
            trial->registers[reg]=ctr_pica_merge(trial->registers[reg],value,mask);
            trial->register_defined[reg]|=(uint8_t)mask;
            if(fragment_register(reg) && trial->register_defined[reg]==15u &&
               !fragment_value(reg,trial->registers[reg]))result=CTR_SCENE_UNSUPPORTED;
            if(reg==0x107u && !depth && trial->register_defined[reg]==15u &&
               (trial->registers[reg]&~UINT32_C(0xf00)))result=CTR_SCENE_UNSUPPORTED;
            if((reg==0x105u || reg==0x106u) && (!depth || depth->format!=CTR_SCENE_DEPTH_D24S8) &&
               trial->register_defined[reg]==15u && trial->registers[reg])result=CTR_SCENE_UNSUPPORTED;
            if((reg==0x114u || reg==0x115u) && depth && trial->register_defined[reg]==15u &&
               trial->registers[reg] && trial->registers[reg]!=(depth->format==CTR_SCENE_DEPTH_D24S8?3u:2u))
                result=CTR_SCENE_UNSUPPORTED;
            if((reg==0x114u || reg==0x115u || reg==0x116u || reg==0x11cu) && !depth &&
               trial->register_defined[reg]==15u && trial->registers[reg])result=CTR_SCENE_UNSUPPORTED;
        } else if(bound && (reg==0x110u || reg==0x111u)) {
            if(mask!=15u || value!=1u)result=CTR_SCENE_UNSUPPORTED;
            /* The caller owns cache/coherency for any native scratch target. */
        } else {
            if(reg>=0x233u && reg<=0x235u && !trial->fixed_words) { fixed_offset=write->byte_offset;fixed_reg=reg; }
            if(reg>=0x2c1u && reg<=0x2c8u && !trial->uniform_words) { uniform_offset=write->byte_offset;uniform_reg=reg; }
            result=upload(trial,reg,value,mask);
        }
        if(result!=CTR_SCENE_OK)break;
    }
    if(result==CTR_SCENE_OK && (!markers || trial->fixed_words || trial->uniform_words)) {
        result=CTR_SCENE_WIRE;
        if(trial->fixed_words) { report->byte_offset=fixed_offset;report->reg=fixed_reg; }
        else if(trial->uniform_words) { report->byte_offset=uniform_offset;report->reg=uniform_reg; }
    }
    return result;
}
static ctr_scene_result execute(ctr_scene_state *state,const void *commands,size_t size,
                                  const ctr_scene_resources *resources,
                                  const ctr_scene_surface *surface,ctr_scene_report *out,
                                  bool bound,uint32_t color_address,const ctr_scene_depth_surface *depth,
                                  const ctr_scene_backend *backend) {
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
    if(resources->texture_count>CTR_SCENE_MAX_TEXTURE_SPANS)
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
    if(backend && (!backend->begin || !backend->draw || !backend->end || !backend->abort))
        return publish(out,report,CTR_SCENE_ARGUMENT);
    if(sizeof(float)!=4 || FLT_RADIX!=2 || FLT_MANT_DIG!=24 || FLT_MAX_EXP!=128)
        return publish(out,report,CTR_SCENE_UNSUPPORTED);
    float one=1.0f;uint32_t one_bits;
    memcpy(&one_bits,&one,sizeof(one_bits));
    if(one_bits!=UINT32_C(0x3f800000))return publish(out,report,CTR_SCENE_UNSUPPORTED);
    if(resources->size>CTR_VERTEX_MAX_SOURCE_BYTES || surface->size>CTR_SCENE_MAX_SURFACE_BYTES)
        return publish(out,report,CTR_SCENE_LIMIT);
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
        uint64_t color_end=(uint64_t)color_address+(uint64_t)surface->width*surface->height*4u;
        if((first<color_end && color_address<last) ||
           (first<(uint64_t)resources->base_address+resources->size && resources->base_address<last))
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
        if(p->base_address<(uint64_t)resources->base_address+resources->size &&
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
    if(ctr_pica_inspect(commands,size,CTR_PICA_MAX_WRITES,NULL,0,&wire)!=CTR_PICA_OK ||
       !size || (size&15u)) {
        report.byte_offset=wire.parameter_offset;report.reg=wire.reg;
        return publish(out,report,CTR_SCENE_WIRE);
    }
    ctr_pica_write *writes=malloc(wire.writes*sizeof(*writes));
    ctr_scene_state *trial=malloc(sizeof(*trial));
    uint8_t *pixels=malloc(surface->size);
    uint8_t *depth_pixels=depth?malloc(depth->size):NULL;
    ctr_vertex *decoded=malloc(CTR_SCENE_MAX_VERTICES*sizeof(*decoded));
    if(!writes || !trial || !pixels || !decoded || (depth && !depth_pixels)) {
        free(writes);free(trial);free(pixels);free(depth_pixels);free(decoded);return publish(out,report,CTR_SCENE_NO_MEMORY);
    }
    *trial=*state;memcpy(pixels,surface->bytes,surface->size);
    if(depth)memcpy(depth_pixels,depth->bytes,depth->size);
    ctr_pica_inspect(commands,size,CTR_PICA_MAX_WRITES,writes,wire.writes,&wire);
    ctr_scene_result result=replay(trial,commands,writes,wire.writes,resources,surface,
                                    decoded,pixels,&report,bound,color_address,depth,depth_pixels,backend && depth,NULL);
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
                              decoded,NULL,&native_report,bound,color_address,depth,NULL,true,backend);
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
    if(result==CTR_SCENE_OK) {
        ++trial->lists;trial->draws+=report.draws;trial->triangles+=report.triangles;
        trial->fragments+=report.fragments;trial->shader_instructions+=report.shader_instructions;
        *state=*trial;memcpy(surface->bytes,pixels,surface->size);
        if(depth)memcpy(depth->bytes,depth_pixels,depth->size);
        report.byte_offset=size;report.reg=0;
    }
    free(writes);free(trial);free(pixels);free(depth_pixels);free(decoded);return publish(out,report,result);
}
ctr_scene_result ctr_scene_execute(ctr_scene_state *state,const void *commands,size_t size,
                                  const ctr_scene_resources *resources,
                                  const ctr_scene_surface *surface,ctr_scene_report *out) {
    return execute(state,commands,size,resources,surface,out,false,0,NULL,NULL);
}
ctr_scene_result ctr_scene_execute_target(ctr_scene_state *state,const void *commands,size_t size,
                                         const ctr_scene_resources *resources,
                                         const ctr_scene_surface *surface,uint32_t color_address,
                                         ctr_scene_report *out) {
    return execute(state,commands,size,resources,surface,out,true,color_address,NULL,NULL);
}
ctr_scene_result ctr_scene_execute_target_backend(ctr_scene_state *state,const void *commands,size_t size,
                                                  const ctr_scene_resources *resources,
                                                  const ctr_scene_surface *surface,uint32_t color_address,
                                                  const ctr_scene_backend *backend,ctr_scene_report *out) {
    return execute(state,commands,size,resources,surface,out,true,color_address,NULL,backend);
}
ctr_scene_result ctr_scene_execute_targets(ctr_scene_state *state,const void *commands,size_t size,
                                           const ctr_scene_resources *resources,const ctr_scene_surface *surface,
                                           uint32_t color_address,const ctr_scene_depth_surface *depth,ctr_scene_report *out) {
    return execute(state,commands,size,resources,surface,out,true,color_address,depth,NULL);
}
ctr_scene_result ctr_scene_execute_targets_backend(ctr_scene_state *state,const void *commands,size_t size,
                                                   const ctr_scene_resources *resources,const ctr_scene_surface *surface,
                                                   uint32_t color_address,const ctr_scene_depth_surface *depth,
                                                   const ctr_scene_backend *backend,ctr_scene_report *out) {
    return execute(state,commands,size,resources,surface,out,true,color_address,depth,backend);
}
const char *ctr_scene_result_name(ctr_scene_result result) {
    static const char *const names[]={"ok","argument","overflow","alias","limit","no-memory",
        "invalid-wire-or-finalization","unsupported-pipeline","invalid-layout","vertex-input",
        "shader-execution-or-output","unsupported-numeric-value","native-backend-or-parity"};
    return (unsigned)result<sizeof(names)/sizeof(names[0])?names[result]:"unknown";
}
