/* Original bounded TEV implementation. Register/enumeration facts: libctru
 * 9b55eda44cf80b971503991e0c78f9bc8fe50425 gpu/enums.h; Azahar
 * 987f18405073960622e1043bb44005989135c6e1 pica/regs_texturing.h.
 * Numeric contract is explicit; no external renderer code is incorporated. */
#include "ctr_tev.h"
#include <string.h>

const unsigned ctr_tev_registers[CTR_TEV_STAGES]={0xc0,0xc8,0xd0,0xd8,0xf0,0xf8};
unsigned ctr_tev_arguments(unsigned operation) {
    return !operation?1u:operation==4u || operation==8u || operation==9u?3u:2u;
}
static bool operation_valid(unsigned value) { return value<=5u || value==8u || value==9u; }
static bool source_valid(unsigned value) {
    return value==0u || value==3u || value==13u || value==14u || value==15u;
}
bool ctr_tev_register_value(unsigned reg,uint32_t value) {
    if(reg==0x0e0u)return !(value&~UINT32_C(0x0000ff00));
    if(reg==0x0fdu)return true;
    for(unsigned stage=0;stage<CTR_TEV_STAGES;++stage) {
        unsigned base=ctr_tev_registers[stage];
        if(reg==base) {
            if(value&~UINT32_C(0x0fff0fff))return false;
            for(unsigned side=0;side<2;++side)for(unsigned arg=0;arg<3;++arg)
                if(!source_valid((value>>(side*16u+arg*4u))&15u) ||
                   (!stage && ((value>>(side*16u+arg*4u))&15u)==15u))return false;
        } else if(reg==base+1u) {
            if(value&~UINT32_C(0x00777fff))return false;
            for(unsigned arg=0;arg<3;++arg) {
                unsigned rgb=(value>>(arg*4u))&15u;
                if(rgb>5u && rgb!=8u && rgb!=9u && rgb!=12u && rgb!=13u)return false;
            }
        } else if(reg==base+2u) {
            if((value&~UINT32_C(0x000f000f)) || !operation_valid(value&15u) ||
               !operation_valid((value>>16)&15u))return false;
        } else if(reg==base+4u) {
            if((value&~UINT32_C(0x00030003)) || (value&3u)>2u || ((value>>16)&3u)>2u)return false;
        }
    }
    return true;
}
bool ctr_tev_buffered(const ctr_tev_program *program) {
    if(program->buffer_update[0] || program->buffer_update[1])return true;
    for(unsigned lane=0;lane<4;++lane)if(program->buffer_color[lane])return true;
    for(unsigned stage=0;stage<CTR_TEV_STAGES;++stage)
        for(unsigned side=0;side<2;++side)
            for(unsigned arg=0;arg<ctr_tev_arguments(program->stages[stage].operation[side]);++arg)
                if(program->stages[stage].source[side][arg]==13u)return true;
    return false;
}
bool ctr_tev_decode(const uint32_t *registers,const uint8_t *defined,bool textured,
                    ctr_tev_program *out) {
    if(!registers || !defined || !out)return false;
    if(!ctr_tev_register_value(0x0e0u,registers[0x0e0u]) ||
       (defined[0x0e0u] && defined[0x0e0u]!=15u) ||
       (defined[0x0fdu] && defined[0x0fdu]!=15u))return false;
    ctr_tev_program next={0};
    for(unsigned lane=0;lane<4;++lane)
        next.buffer_color[lane]=(uint8_t)(registers[0x0fdu]>>(lane*8u));
    for(unsigned side=0;side<2;++side)
        next.buffer_update[side]=(uint8_t)((registers[0x0e0u]>>(8u+side*4u))&15u);
    for(unsigned stage=0;stage<CTR_TEV_STAGES;++stage) {
        unsigned base=ctr_tev_registers[stage];ctr_tev_stage *p=&next.stages[stage];
        for(unsigned offset=0;offset<5;++offset) {
            if(!ctr_tev_register_value(base+offset,registers[base+offset]) ||
               (offset!=3u && defined[base+offset]!=15u))return false;
        }
        for(unsigned lane=0;lane<4;++lane)p->constant[lane]=(uint8_t)(registers[base+3u]>>(lane*8u));
        for(unsigned side=0;side<2;++side) {
            p->operation[side]=(uint8_t)((registers[base+2u]>>(side*16u))&15u);
            p->scale[side]=(uint8_t)((registers[base+4u]>>(side*16u))&3u);
            for(unsigned arg=0;arg<3;++arg) {
                unsigned source=(registers[base]>>(side*16u+arg*4u))&15u;
                p->source[side][arg]=(uint8_t)source;
                p->operand[side][arg]=(uint8_t)((registers[base+1u]>>((side?12u:0u)+arg*4u))&(side?7u:15u));
                if(arg<ctr_tev_arguments(p->operation[side]) &&
                   ((source==3u && !textured) || (source==15u && !stage) ||
                    (source==14u && defined[base+3u]!=15u)))return false;
            }
        }
    }
    if(ctr_tev_buffered(&next) && (defined[0x0e0u]!=15u || defined[0x0fdu]!=15u))return false;
    *out=next;return true;
}
bool ctr_tev_passthrough(const ctr_tev_stage *p) {
    return !p->operation[0] && !p->operation[1] && !p->scale[0] && !p->scale[1] &&
        p->source[0][0]==15u && p->source[1][0]==15u && !p->operand[0][0] && !p->operand[1][0];
}
static unsigned saturated(int value) { return value<0?0u:value>255?255u:(unsigned)value; }
static unsigned combine(unsigned op,const unsigned v[3]) {
    int value=(int)v[0];
    switch(op) {
        case 1:value=(int)(v[0]*v[1]/255u);break;
        case 2:value=(int)(v[0]+v[1]);break;
        case 3:value=(int)(v[0]+v[1])-128;break;
        case 4:value=(int)((v[0]*v[2]+v[1]*(255u-v[2]))/255u);break;
        case 5:value=(int)v[0]-(int)v[1];break;
        case 8:value=(int)(v[0]*v[1]/255u+v[2]);break;
        case 9:value=(int)(saturated((int)(v[0]+v[1]))*v[2]/255u);break;
        default:break;
    }
    return saturated(value);
}
void ctr_tev_evaluate(const ctr_tev_program *program,const uint8_t primary[4],
                      const uint8_t texture[4],uint8_t output[4]) {
    uint8_t previous[4]={0},buffer[4]={0},pending[4];
    memcpy(pending,program->buffer_color,4);
    for(unsigned stage=0;stage<CTR_TEV_STAGES;++stage) {
        const ctr_tev_stage *p=&program->stages[stage];uint8_t next[4];
        bool passthrough=ctr_tev_passthrough(p);
        if(!passthrough)for(unsigned lane=0;lane<4;++lane) {
            unsigned side=lane==3u,values[3]={0};
            for(unsigned arg=0;arg<ctr_tev_arguments(p->operation[side]);++arg) {
                unsigned id=p->source[side][arg],operand=p->operand[side][arg];
                const uint8_t *source=id==0u?primary:id==3u?texture:id==13u?buffer:
                    id==14u?p->constant:previous;
                unsigned channel=side?operand<2u?3u:operand/2u-1u:
                    operand<2u?lane:operand<4u?3u:operand<6u?0u:operand<10u?1u:2u;
                unsigned value=source[channel];values[arg]=(operand&1u)?255u-value:value;
            }
            next[lane]=(uint8_t)saturated((int)(combine(p->operation[side],values)<<p->scale[side]));
        }
        if(!passthrough)memcpy(previous,next,4);
        /* Publication is delayed by one complete stage: stage 0 reads zero,
         * stage 1 reads the initial color, and stage 2 can read stage 0 output.
         * Passthrough stages still advance visible/pending state. */
        memcpy(buffer,pending,4);
        if(stage<4u)for(unsigned lane=0;lane<4;++lane)
            if(program->buffer_update[lane==3u]&(1u<<stage))pending[lane]=previous[lane];
    }
    memcpy(output,previous,4);
}
