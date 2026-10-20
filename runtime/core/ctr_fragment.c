/* Original checked byte-domain PICA output merger. */
#include "ctr_fragment.h"
#include <string.h>

static const unsigned base_registers[]={0x100u,0x102u,0x104u,0x107u,0x112u,0x113u};
static const unsigned merger_registers[]={0x100u,0x101u,0x102u,0x103u,0x104u,0x107u,0x112u,0x113u};

bool ctr_fragment_register(unsigned reg) {
    for(unsigned i=0;i<sizeof(merger_registers)/sizeof(*merger_registers);++i)
        if(reg==merger_registers[i])return true;
    return false;
}
bool ctr_fragment_register_value(unsigned reg,uint32_t value) {
    switch(reg) {
    case 0x100u:return value==0u || value==UINT32_C(0x100);
    case 0x101u:
        if(value&~UINT32_C(0xffff0707))return false;
        if((value&7u)>4u || ((value>>8)&7u)>4u)return false;
        for(unsigned shift=16;shift<32;shift+=4u)if(((value>>shift)&15u)>14u)return false;
        return true;
    case 0x102u:return value<16u;
    case 0x103u:return true;
    case 0x104u:return !(value&~UINT32_C(0xff71));
    case 0x107u:return !(value&~UINT32_C(0x1f71));
    case 0x112u:case 0x113u:return value==0u || value==15u;
    default:return false;
    }
}
static bool reads_destination(unsigned op) {
    return op!=0u && op!=3u && op!=4u && op!=5u;
}
static bool factor_reads_destination(unsigned factor,bool alpha) {
    return factor==4u || factor==5u || factor==8u || factor==9u || (!alpha && factor==14u);
}
bool ctr_fragment_decode_depth(const uint32_t registers[],const uint8_t defined[],
                               bool required,bool depth_allowed,ctr_fragment_state *out) {
    if(!registers || !defined || !out)return false;
    bool present=required;
    for(unsigned i=0;i<sizeof(merger_registers)/sizeof(*merger_registers);++i)
        if(defined[merger_registers[i]])present=true;
    ctr_fragment_state trial={.logic_op=3u,.write_mask=15u};
    if(present) {
        for(unsigned i=0;i<sizeof(base_registers)/sizeof(*base_registers);++i) {
            unsigned reg=base_registers[i];
            if(defined[reg]!=15u || !ctr_fragment_register_value(reg,registers[reg]))return false;
        }
        if(!depth_allowed && (registers[0x107u]&~UINT32_C(0xf00)))return false;
        if(defined[0x101u]==15u && !ctr_fragment_register_value(0x101u,registers[0x101u]))return false;
        trial.logic_op=(uint8_t)registers[0x102u];
        trial.write_mask=registers[0x113u]?((registers[0x107u]>>8)&15u):0u;
        trial.alpha_enabled=(registers[0x104u]&1u)!=0u;
        trial.alpha_function=(uint8_t)((registers[0x104u]>>4)&7u);
        trial.alpha_reference=(uint8_t)(registers[0x104u]>>8);
        trial.blend_enabled=registers[0x100u]!=0u;
        if(defined[0x103u]==15u)for(unsigned lane=0;lane<4;++lane)
            trial.blend_constant[lane]=(uint8_t)(registers[0x103u]>>(lane*8u));
        bool needs_read=false;
        if(trial.blend_enabled) {
            if(defined[0x101u]!=15u)return false;
            bool constant=false;
            for(unsigned side=0;side<2;++side) {
                uint32_t word=registers[0x101u];
                unsigned source=(word>>(16u+side*8u))&15u,destination=(word>>(20u+side*8u))&15u;
                trial.blend_equation[side]=(uint8_t)((word>>(side*8u))&7u);
                trial.blend_source[side]=(uint8_t)source;trial.blend_destination[side]=(uint8_t)destination;
                if((source>=10u && source<=13u) || (destination>=10u && destination<=13u))constant=true;
                if(trial.write_mask&(side?8u:7u))
                    needs_read|=destination!=0u || factor_reads_destination(source,side!=0u);
            }
            if(constant && defined[0x103u]!=15u)return false;
        } else needs_read=trial.write_mask && reads_destination(trial.logic_op);
        if(needs_read && registers[0x112u]!=15u)return false;
    }
    *out=trial;return true;
}
bool ctr_fragment_decode(const uint32_t registers[],const uint8_t defined[],
                         bool required,ctr_fragment_state *out) {
    return ctr_fragment_decode_depth(registers,defined,required,false,out);
}
bool ctr_fragment_alpha_passes(const ctr_fragment_state *state,uint8_t alpha) {
    if(!state->alpha_enabled)return true;
    switch(state->alpha_function) {
    case 0:return false;
    case 1:return true;
    case 2:return alpha==state->alpha_reference;
    case 3:return alpha!=state->alpha_reference;
    case 4:return alpha<state->alpha_reference;
    case 5:return alpha<=state->alpha_reference;
    case 6:return alpha>state->alpha_reference;
    case 7:return alpha>=state->alpha_reference;
    default:return false;
    }
}
static uint8_t logic(unsigned op,unsigned source,unsigned destination) {
    switch(op) {
    case 0:return 0;
    case 1:return (uint8_t)(source&destination);
    case 2:return (uint8_t)(source&~destination);
    case 3:return (uint8_t)source;
    case 4:return 255;
    case 5:return (uint8_t)~source;
    case 6:return (uint8_t)destination;
    case 7:return (uint8_t)~destination;
    case 8:return (uint8_t)~(source&destination);
    case 9:return (uint8_t)(source|destination);
    case 10:return (uint8_t)~(source|destination);
    case 11:return (uint8_t)(source^destination);
    case 12:return (uint8_t)~(source^destination);
    case 13:return (uint8_t)(~source&destination);
    case 14:return (uint8_t)(source|~destination);
    case 15:return (uint8_t)(~source|destination);
    default:return (uint8_t)destination;
    }
}
static unsigned blend_factor(unsigned factor,unsigned lane,const uint8_t source[4],
                              const uint8_t destination[4],const uint8_t constant[4]) {
    switch(factor) {
    case 0:return 0;
    case 1:return 255;
    case 2:return source[lane];
    case 3:return 255u-source[lane];
    case 4:return destination[lane];
    case 5:return 255u-destination[lane];
    case 6:return source[3];
    case 7:return 255u-source[3];
    case 8:return destination[3];
    case 9:return 255u-destination[3];
    case 10:return constant[lane];
    case 11:return 255u-constant[lane];
    case 12:return constant[3];
    case 13:return 255u-constant[3];
    case 14:return lane==3u?255u:source[3]<255u-destination[3]?source[3]:255u-destination[3];
    default:return 0;
    }
}
static uint8_t blend(const ctr_fragment_state *state,unsigned lane,const uint8_t source[4],
                      const uint8_t destination[4]) {
    unsigned side=lane==3u;
    int32_t a=(int32_t)(source[lane]*blend_factor(state->blend_source[side],lane,source,destination,state->blend_constant));
    int32_t b=(int32_t)(destination[lane]*blend_factor(state->blend_destination[side],lane,source,destination,state->blend_constant));
    int32_t numerator;
    switch(state->blend_equation[side]) {
    case 0:numerator=a+b;break;
    case 1:numerator=a-b;break;
    case 2:numerator=b-a;break;
    case 3:numerator=a<b?a:b;break;
    case 4:numerator=a>b?a:b;break;
    default:return destination[lane];
    }
    int32_t value=numerator/255;
    return value<0?0u:value>255?255u:(uint8_t)value;
}
bool ctr_fragment_apply(const ctr_fragment_state *state,const uint8_t source[4],
                        uint8_t destination[4]) {
    uint8_t input[4],old[4];memcpy(input,source,4u);memcpy(old,destination,4u);
    if(!ctr_fragment_alpha_passes(state,input[3]))return false;
    for(unsigned lane=0;lane<4;++lane)if(state->write_mask&(1u<<lane))
        destination[lane]=state->blend_enabled?blend(state,lane,input,old):logic(state->logic_op,input[lane],old[lane]);
    return true;
}
