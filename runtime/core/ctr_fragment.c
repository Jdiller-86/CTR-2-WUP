/* Original checked byte-domain PICA output merger. */
#include "ctr_fragment.h"
#include <string.h>

static const unsigned merger_registers[]={0x100u,0x102u,0x104u,0x107u,0x112u,0x113u};

bool ctr_fragment_register(unsigned reg) {
    for(unsigned i=0;i<sizeof(merger_registers)/sizeof(*merger_registers);++i)
        if(reg==merger_registers[i])return true;
    return false;
}
bool ctr_fragment_register_value(unsigned reg,uint32_t value) {
    switch(reg) {
    case 0x100u:return !value; /* Default fragment mode, logic operation route. */
    case 0x102u:return value<16u;
    case 0x104u:return !(value&~UINT32_C(0xff71));
    case 0x107u:return !(value&~UINT32_C(0xf00)); /* Depth read/write disabled. */
    case 0x112u:case 0x113u:return value==0u || value==15u;
    default:return false;
    }
}
static bool reads_destination(unsigned op) {
    return op!=0u && op!=3u && op!=4u && op!=5u;
}
bool ctr_fragment_decode(const uint32_t registers[],const uint8_t defined[],
                         bool required,ctr_fragment_state *out) {
    if(!registers || !defined || !out)return false;
    bool present=required;
    for(unsigned i=0;i<sizeof(merger_registers)/sizeof(*merger_registers);++i)
        if(defined[merger_registers[i]])present=true;
    ctr_fragment_state trial={3u,15u,0u,0u,false};
    if(present) {
        for(unsigned i=0;i<sizeof(merger_registers)/sizeof(*merger_registers);++i) {
            unsigned reg=merger_registers[i];
            if(defined[reg]!=15u || !ctr_fragment_register_value(reg,registers[reg]))return false;
        }
        trial.logic_op=(uint8_t)registers[0x102u];
        trial.write_mask=registers[0x113u]?((registers[0x107u]>>8)&15u):0u;
        trial.alpha_enabled=(registers[0x104u]&1u)!=0u;
        trial.alpha_function=(uint8_t)((registers[0x104u]>>4)&7u);
        trial.alpha_reference=(uint8_t)(registers[0x104u]>>8);
        if(trial.write_mask && reads_destination(trial.logic_op) && registers[0x112u]!=15u)return false;
    }
    *out=trial;return true;
}
static bool alpha_passes(const ctr_fragment_state *state,uint8_t alpha) {
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
bool ctr_fragment_apply(const ctr_fragment_state *state,const uint8_t source[4],
                        uint8_t destination[4]) {
    uint8_t input[4],old[4];memcpy(input,source,4u);memcpy(old,destination,4u);
    if(!alpha_passes(state,input[3]))return false;
    for(unsigned lane=0;lane<4;++lane)if(state->write_mask&(1u<<lane))
        destination[lane]=logic(state->logic_op,input[lane],old[lane]);
    return true;
}
