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
static bool source_valid(unsigned value,bool game) {
    return value==0u || (value>=3u && value<=5u) || (game && value<=2u) || value>=13u;
}
static bool register_value(unsigned reg,uint32_t value,bool game,bool fog) {
    if(reg==0x0e0u) {
        unsigned mode=value&7u;
        uint32_t allowed=(game || (fog && mode==5u))?UINT32_C(0x0001ff07):UINT32_C(0x0000ff00);
        return !(value&~allowed) && (mode==0u || mode==5u);
    }
    if(reg==0x0fdu)return true;
    for(unsigned stage=0;stage<CTR_TEV_STAGES;++stage) {
        unsigned base=ctr_tev_registers[stage];
        if(reg==base) {
            if(value&~UINT32_C(0x0fff0fff))return false;
            for(unsigned side=0;side<2;++side)for(unsigned arg=0;arg<3;++arg) {
                unsigned source=(value>>(side*16u+arg*4u))&15u;
                if(!source_valid(source,game) || (!game && !stage && source==15u))return false;
            }
        } else if(reg==base+1u) {
            if(value&~UINT32_C(0x00777fff))return false;
            for(unsigned arg=0;arg<3;++arg) {
                unsigned rgb=(value>>(arg*4u))&15u;
                if(rgb>5u && rgb!=8u && rgb!=9u && rgb!=12u && rgb!=13u)return false;
            }
        } else if(reg==base+2u) {
            unsigned rgb=value&15u;
            if((value&~UINT32_C(0x000f000f)) || (!operation_valid(rgb) && !(game && rgb==6u)) ||
               !operation_valid((value>>16)&15u))return false;
        } else if(reg==base+4u) {
            if((value&~UINT32_C(0x00030003)) || (value&3u)>2u || ((value>>16)&3u)>2u)return false;
        }
    }
    return true;
}
bool ctr_tev_register_value(unsigned reg,uint32_t value) { return register_value(reg,value,false,false); }
bool ctr_tev_game_register_value(unsigned reg,uint32_t value) { return register_value(reg,value,true,true); }
bool ctr_tev_buffered(const ctr_tev_program *program) {
    if(program->buffer_update[0] || program->buffer_update[1])return true;
    for(unsigned lane=0;lane<4;++lane)if(program->buffer_color[lane])return true;
    for(unsigned stage=0;stage<CTR_TEV_STAGES;++stage)
        for(unsigned side=0;side<2;++side)
            for(unsigned arg=0;arg<ctr_tev_arguments(program->stages[stage].operation[side]);++arg)
                if(program->stages[stage].source[side][arg]==13u)return true;
    return false;
}
static bool decode(const uint32_t *registers,const uint8_t *defined,unsigned texture_mask,
                    ctr_tev_program *out,bool game,bool fog) {
    if(!registers || !defined || !out || texture_mask>7u)return false;
    if(!register_value(0x0e0u,registers[0x0e0u],game,fog) ||
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
            if(!register_value(base+offset,registers[base+offset],game,fog) ||
               (offset!=3u && defined[base+offset]!=15u))return false;
        }
        for(unsigned lane=0;lane<4;++lane)p->constant[lane]=(uint8_t)(registers[base+3u]>>(lane*8u));
        for(unsigned side=0;side<2;++side) {
            p->operation[side]=(uint8_t)((registers[base+2u]>>(side*16u))&15u);
            p->scale[side]=(uint8_t)((registers[base+4u]>>(side*16u))&3u);
            for(unsigned arg=0;arg<3;++arg) {
                unsigned source=(registers[base]>>(side*16u+arg*4u))&15u;
                if(game && !stage && source==15u)source=(registers[base]>>(side*16u+8u))&15u;
                p->source[side][arg]=(uint8_t)source;
                p->operand[side][arg]=(uint8_t)((registers[base+1u]>>((side?12u:0u)+arg*4u))&(side?7u:15u));
                if(arg<ctr_tev_arguments(p->operation[side]) &&
                   (((source>=3u && source<=5u) && !(texture_mask&(1u<<(source-3u)))) ||
                    (source==14u && defined[base+3u]!=15u)))return false;
            }
        }
    }
    if(ctr_tev_buffered(&next) && (defined[0x0e0u]!=15u || defined[0x0fdu]!=15u))return false;
    *out=next;return true;
}
bool ctr_tev_decode(const uint32_t *registers,const uint8_t *defined,unsigned texture_mask,ctr_tev_program *out) {
    return decode(registers,defined,texture_mask,out,false,false);
}
bool ctr_tev_decode_fog(const uint32_t *registers,const uint8_t *defined,unsigned texture_mask,ctr_tev_program *out) {
    return decode(registers,defined,texture_mask,out,false,true);
}
bool ctr_tev_decode_game(const uint32_t *registers,const uint8_t *defined,unsigned texture_mask,ctr_tev_program *out) {
    return decode(registers,defined,texture_mask,out,true,true);
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
void ctr_tev_prepare(const ctr_tev_program *program,ctr_tev_plan *plan) {
    plan->program=program;plan->active=0;plan->reads_buffer=false;
    for(unsigned stage=0;stage<CTR_TEV_STAGES;++stage) {
        const ctr_tev_stage *p=&program->stages[stage];
        if(ctr_tev_passthrough(p))continue;
        plan->active|=(uint8_t)(1u<<stage);
        for(unsigned side=0;side<2u;++side)
            plan->arguments[stage][side]=(uint8_t)ctr_tev_arguments(p->operation[side]);
        for(unsigned lane=0;lane<4u;++lane) {
            unsigned side=lane==3u;
            for(unsigned arg=0;arg<plan->arguments[stage][side];++arg) {
                unsigned id=p->source[side][arg],operand=p->operand[side][arg];
                unsigned channel=side?operand<2u?3u:operand/2u-1u:
                    operand<2u?lane:operand<4u?3u:operand<6u?0u:operand<10u?1u:2u;
                unsigned source=id<=5u?id:id-7u;
                plan->reads_buffer|=id==13u;
                plan->operands[stage][lane][arg]=(uint8_t)((source<<3)|(operand&1u)<<2|channel);
            }
        }
    }
}
void ctr_tev_evaluate_prepared(const ctr_tev_plan *plan,const uint8_t primary[4],
                               const uint8_t textures[CTR_TEV_TEXTURES][4],const uint8_t lighting[2][4],
                               uint8_t output[4]) {
    static const uint8_t dark[4]={0};
    const ctr_tev_program *program=plan->program;
    uint8_t previous[4]={0},buffer[4]={0},pending[4];
    if(plan->reads_buffer)memcpy(pending,program->buffer_color,4);
    const uint8_t *source[]={primary,lighting?lighting[0]:dark,lighting?lighting[1]:dark,
        textures?textures[0]:dark,textures?textures[1]:dark,textures?textures[2]:dark,buffer,NULL,previous};
    if(!plan->reads_buffer && plan->active==1u) {
        const ctr_tev_stage *p=&program->stages[0];
        if(!p->scale[0] && !p->scale[1] && !p->operation[0] && !p->operation[1]) {
            uint8_t replaced[4];
            source[7]=p->constant;
            for(unsigned lane=0;lane<4u;++lane) {
                unsigned operand=plan->operands[0][lane][0];
                unsigned value=source[operand>>3][operand&3u];
                replaced[lane]=(uint8_t)((operand&4u)?255u-value:value);
            }
            memcpy(output,replaced,4u);
            return;
        }
    }
    for(unsigned stage=0;stage<CTR_TEV_STAGES;++stage) {
        const ctr_tev_stage *p=&program->stages[stage];uint8_t next[4];
        bool passthrough=!(plan->active&(1u<<stage));
        bool dot_rgb=p->operation[0]==6u;
        int dot=0;
        source[7]=p->constant;
        if(!passthrough)for(unsigned lane=0;lane<4;++lane) {
            unsigned side=lane==3u,values[3]={0};
            for(unsigned arg=0;arg<plan->arguments[stage][side];++arg) {
                unsigned operand=plan->operands[stage][lane][arg];
                unsigned value=source[operand>>3][operand&3u];
                values[arg]=(operand&4u)?255u-value:value;
            }
            if(dot_rgb && !side)dot+=(2*(int)values[0]-255)*(2*(int)values[1]-255);
            else next[lane]=(uint8_t)saturated((int)(combine(p->operation[side],values)<<p->scale[side]));
        }
        if(!passthrough) {
            if(dot_rgb) {
                /* Convert the signed normalized RGB dot product back to a byte. */
                uint8_t rgb=(uint8_t)saturated((int)(saturated(dot/255)<<p->scale[0]));
                next[0]=next[1]=next[2]=rgb;
            }
            memcpy(previous,next,4);
        }
        /* Publication is delayed by one complete stage: stage 0 reads zero,
         * stage 1 reads the initial color, and stage 2 can read stage 0 output.
         * Passthrough stages still advance visible/pending state. */
        if(plan->reads_buffer) {
            memcpy(buffer,pending,4);
            if(stage<4u)for(unsigned lane=0;lane<4;++lane)
                if(program->buffer_update[lane==3u]&(1u<<stage))pending[lane]=previous[lane];
        }
    }
    memcpy(output,previous,4);
}
void ctr_tev_evaluate_game(const ctr_tev_program *program,const uint8_t primary[4],
                          const uint8_t textures[CTR_TEV_TEXTURES][4],const uint8_t lighting[2][4],
                          uint8_t output[4]) {
    ctr_tev_plan plan;
    ctr_tev_prepare(program,&plan);
    ctr_tev_evaluate_prepared(&plan,primary,textures,lighting,output);
}
void ctr_tev_evaluate_textures(const ctr_tev_program *program,const uint8_t primary[4],
                               const uint8_t textures[CTR_TEV_TEXTURES][4],uint8_t output[4]) {
    ctr_tev_evaluate_game(program,primary,textures,NULL,output);
}
void ctr_tev_evaluate(const ctr_tev_program *program,const uint8_t primary[4],
                      const uint8_t texture[4],uint8_t output[4]) {
    uint8_t textures[CTR_TEV_TEXTURES][4]={{0}};
    if(texture)memcpy(textures[0],texture,4);
    ctr_tev_evaluate_textures(program,primary,(const uint8_t (*)[4])textures,output);
}
