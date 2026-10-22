/* Original instruction encoder from published R700/Latte field facts.
 * Reference facts: devkitPro/wut 937f0d6901f5e2a72672154a674ec47b63e2f417
 * samples/cmake/content/pos_col_shader.{vsh,psh,gsh}; decaf-emu/decaf-emu
 * e6c528a20a41c34e0f9eb91dd3da40f119db2dee src/libgpu/latte instruction
 * definitions. No external assembler or emulator implementation is embedded. */
#include "ctr_gx2_shader.h"
#include <stdlib.h>
#include <string.h>

#define LATTE_MOV 25u
#define LATTE_ADD 0u
#define LATTE_MUL_IEEE 2u
#define LATTE_ALU_CLAUSE_WORDS 128u

static bool span(const void *p,size_t n) {
    return p && n<=UINTPTR_MAX-(uintptr_t)p;
}
static bool overlap(const void *a,size_t an,const void *b,size_t bn) {
    return (uintptr_t)a<(uintptr_t)b+bn && (uintptr_t)b<(uintptr_t)a+an;
}
static unsigned bit_count(unsigned value) {
    unsigned count=0;while(value) { count+=value&1u;value>>=1; }return count;
}
static void put32(uint8_t *p,uint32_t value) {
    for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(value>>(8u*i));
}
static bool alu(ctr_gx2_shader *out,unsigned op,unsigned a,unsigned ac,bool negate,
                unsigned b,unsigned bc,unsigned dest,unsigned lane) {
    size_t offset=CTR_GX2_SHADER_ALU_OFFSET+(size_t)out->alu_instructions*8u;
    if(offset>CTR_GX2_SHADER_MAX_BYTES-8u)return false;
    /* A scalar instruction is a complete group. Capture all sources before
     * writing a PICA destination, including in-place cross-lane swizzles. */
    put32(out->program+offset,a|(ac<<10)|(negate?1u<<12:0u)|(b<<13)|(bc<<23)|UINT32_C(0x80000000));
    put32(out->program+offset+4u,(op<<7)|(1u<<4)|(dest<<21)|(lane<<29));
    ++out->alu_instructions;return true;
}
static ctr_gx2_shader_result source(const ctr_shader_program *p,
                                   ctr_gx2_shader *out,const uint8_t defined[16],
                                   unsigned reg,unsigned lane,unsigned *selector) {
    if(reg<16u) {
        out->input_mask|=(uint16_t)(1u<<reg);*selector=reg+1u;
    } else if(reg<32u) {
        if(!(defined[reg-16u]&(1u<<lane)))return CTR_GX2_SHADER_UNDEFINED;
        *selector=reg+1u;
    } else {
        unsigned index=reg-32u;uint32_t raw;
        if(!(p->uniform_defined[index]&(1u<<lane)))return CTR_GX2_SHADER_UNDEFINED;
        if(!ctr_shader_f24_exact(p->uniforms[index][lane],&raw))return CTR_GX2_SHADER_NUMERIC;
        if(!(out->uniform_used[index]&(1u<<lane))) {
            if(out->uniform_count>=CTR_SHADER_UNIFORMS*4u)return CTR_GX2_SHADER_LIMIT;
            out->uniform_order[out->uniform_count++]=(uint16_t)(index*4u+lane);
        }
        out->uniform_used[index]|=(uint8_t)(1u<<lane);*selector=256u+index;
    }
    return CTR_GX2_SHADER_OK;
}
static ctr_gx2_shader_result exports(ctr_gx2_shader *out,uint16_t mask,
                                     const uint32_t *maps,unsigned total,
                                     const uint8_t written[16]) {
    if(!mask || total>7u || total!=bit_count(mask))return CTR_GX2_SHADER_UNSUPPORTED;
    unsigned seen=0,index=0;
    for(unsigned reg=0;reg<16;++reg)if(mask&(1u<<reg)) {
        uint32_t mapping=maps[index++];
        if(mapping&UINT32_C(0xe0e0e0e0))return CTR_GX2_SHADER_UNSUPPORTED;
        for(unsigned lane=0;lane<4;++lane) {
            unsigned semantic=(mapping>>(lane*8u))&31u;
            if(semantic==31u)continue;
            if(semantic>13u || (semantic>=4u && semantic<8u))return CTR_GX2_SHADER_UNSUPPORTED;
            if((seen&(1u<<semantic)) || !(written[reg]&(1u<<lane)))return CTR_GX2_SHADER_UNDEFINED;
            seen|=1u<<semantic;
            unsigned target=semantic<4u?51u:semantic<12u?52u:53u;
            unsigned channel=semantic<4u?semantic:semantic<12u?semantic-8u:semantic-12u;
            if(!alu(out,LATTE_MOV,33u+reg,lane,false,0,0,target,channel))return CTR_GX2_SHADER_LIMIT;
        }
    }
    if(seen!=UINT32_C(0x0f0f) && seen!=UINT32_C(0x3f0f))return CTR_GX2_SHADER_UNDEFINED;
    out->texcoord0=(seen&UINT32_C(0x3000))!=0;
    return CTR_GX2_SHADER_OK;
}
static ctr_gx2_shader_result compile(const ctr_shader_program *p,uint16_t mask,
                                     const uint32_t *maps,unsigned total,
                                     ctr_gx2_shader *out) {
    uint8_t defined[16]={0},written[16]={0};bool ended=false;
    for(unsigned pc=p->entry;pc<CTR_SHADER_MAX_CODE;++pc) {
        if(!p->code_defined[pc])return CTR_GX2_SHADER_UNDEFINED;
        uint32_t word=p->code[pc];unsigned op=word>>26;++out->pica_instructions;
        if(op==0x21u || op==0x22u) {
            if(word&UINT32_C(0x03ffffff))return CTR_GX2_SHADER_UNSUPPORTED;
            if(op==0x22u) { ended=true;break; }continue;
        }
        if((op!=0x13u && op!=0u && op!=8u) || ((word>>19)&3u))return CTR_GX2_SHADER_UNSUPPORTED;
        unsigned descriptor=word&127u,dest=(word>>21)&31u,a=(word>>12)&127u,b=(word>>7)&31u;
        if(!p->descriptor_defined[descriptor])return CTR_GX2_SHADER_UNDEFINED;
        uint32_t pattern=p->descriptors[descriptor];
        if(pattern&UINT32_C(0x80000000))return CTR_GX2_SHADER_UNSUPPORTED;
        unsigned lanes=pattern&15u;
        /* Source snapshots are deliberately distinct from destination storage.
         * The separate groups also avoid depending on a VLIW bank scheduler. */
        for(unsigned operand=0;operand<(op==0x13u?1u:2u);++operand)
            for(unsigned lane=0;lane<4;++lane)if(lanes&(8u>>lane)) {
                unsigned selected=(pattern>>((operand?20u:11u)-2u*lane))&3u,selector;
                ctr_gx2_shader_result result=source(p,out,defined,operand?b:a,selected,&selector);
                if(result!=CTR_GX2_SHADER_OK)return result;
                if(!alu(out,LATTE_MOV,selector,selected,
                        (pattern&(operand?1u<<13:1u<<4))!=0,0,0,49u+operand,lane))return CTR_GX2_SHADER_LIMIT;
            }
        for(unsigned lane=0;lane<4;++lane)if(lanes&(8u>>lane)) {
            unsigned opcode=op==0x13u?LATTE_MOV:op==8u?LATTE_MUL_IEEE:LATTE_ADD;
            unsigned target=dest<16u?dest+33u:dest+1u;
            if(!alu(out,opcode,49u,lane,false,50u,lane,target,lane))return CTR_GX2_SHADER_LIMIT;
        }
        for(unsigned lane=0;lane<4;++lane)if(lanes&(8u>>lane)) {
            if(dest<16u)written[dest]|=(uint8_t)(1u<<lane);
            else defined[dest-16u]|=(uint8_t)(1u<<lane);
        }
    }
    if(!ended)return CTR_GX2_SHADER_NO_END;
    ctr_gx2_shader_result result=exports(out,mask,maps,total,written);
    if(result!=CTR_GX2_SHADER_OK)return result;
    unsigned clauses=(out->alu_instructions+LATTE_ALU_CLAUSE_WORDS-1u)/LATTE_ALU_CLAUSE_WORDS;
    if((clauses+3u+(out->texcoord0?1u:0u))*8u>CTR_GX2_SHADER_ALU_OFFSET)return CTR_GX2_SHADER_LIMIT;
    /* CALL_FS NO_BARRIER; ALU clauses; POS0, color PARAM0 and optional UV PARAM1.
     * Only the last export ends the program. Addresses/counts use 8-byte words. */
    put32(out->program+4u,UINT32_C(0x09800000));
    for(unsigned i=0;i<clauses;++i) {
        unsigned offset=i*LATTE_ALU_CLAUSE_WORDS;
        unsigned count=out->alu_instructions-offset;
        if(count>LATTE_ALU_CLAUSE_WORDS)count=LATTE_ALU_CLAUSE_WORDS;
        put32(out->program+(i+1u)*8u,CTR_GX2_SHADER_ALU_OFFSET/8u+offset);
        put32(out->program+(i+1u)*8u+4u,UINT32_C(0xa0000000)|((count-1u)<<18));
    }
    unsigned pos=(clauses+1u)*8u,param=pos+8u;
    put32(out->program+pos,60u|(1u<<13)|(51u<<15));
    put32(out->program+pos+4u,UINT32_C(0x94000688));
    put32(out->program+param,(2u<<13)|(52u<<15));
    put32(out->program+param+4u,out->texcoord0?UINT32_C(0x13800688):UINT32_C(0x14200688));
    if(out->texcoord0) {
        put32(out->program+param+8u,1u|(2u<<13)|(53u<<15));
        put32(out->program+param+12u,UINT32_C(0x14200b08));
    }
    /* GX2 shader storage is padded to its 256-byte program alignment, as in
     * the independent WUT GFD fixture. Keep the final fetch inside storage. */
    out->size=(CTR_GX2_SHADER_ALU_OFFSET+out->alu_instructions*8u+255u)&~255u;
    out->gprs=out->texcoord0?CTR_GX2_SHADER_TEXTURED_GPRS:CTR_GX2_SHADER_GPRS;
    return CTR_GX2_SHADER_OK;
}
ctr_gx2_shader_result ctr_gx2_shader_compile(const ctr_shader_program *p,uint16_t mask,
                                            const uint32_t *maps,unsigned total,
                                            ctr_gx2_shader *out) {
    if(!p || !maps || !out)return CTR_GX2_SHADER_ARGUMENT;
    if((uintptr_t)p%_Alignof(ctr_shader_program) || (uintptr_t)maps%_Alignof(uint32_t) ||
       (uintptr_t)out%_Alignof(ctr_gx2_shader))return CTR_GX2_SHADER_ARGUMENT;
    if(total>7u)return CTR_GX2_SHADER_RANGE;
    size_t map_bytes=(size_t)total*sizeof(*maps);
    if(!span(p,sizeof(*p)) || !span(maps,map_bytes) || !span(out,sizeof(*out)))return CTR_GX2_SHADER_OVERFLOW;
    if(overlap(out,sizeof(*out),p,sizeof(*p)) || overlap(out,sizeof(*out),maps,map_bytes))return CTR_GX2_SHADER_ALIAS;
    if(p->entry>=CTR_SHADER_MAX_CODE)return CTR_GX2_SHADER_RANGE;
    ctr_gx2_shader *trial=calloc(1,sizeof(*trial));
    if(!trial)return CTR_GX2_SHADER_NO_MEMORY;
    ctr_gx2_shader_result result=compile(p,mask,maps,total,trial);
    if(result==CTR_GX2_SHADER_OK)*out=*trial;
    free(trial);return result;
}
ctr_gx2_shader_result ctr_gx2_shader_validate_uniforms(const ctr_shader_program *p,
                                                       const ctr_gx2_shader *compiled) {
    if(!p || !compiled)return CTR_GX2_SHADER_ARGUMENT;
    if((uintptr_t)p%_Alignof(ctr_shader_program) ||
       (uintptr_t)compiled%_Alignof(ctr_gx2_shader))return CTR_GX2_SHADER_ARGUMENT;
    if(!span(p,sizeof(*p)) || !span(compiled,sizeof(*compiled)))return CTR_GX2_SHADER_OVERFLOW;
    if(compiled->uniform_count>CTR_SHADER_UNIFORMS*4u)return CTR_GX2_SHADER_ARGUMENT;
    /* Validate the complete record before reading dynamic values: malformed
     * metadata must not hide a missing or duplicated dependency. */
    uint8_t seen[CTR_SHADER_UNIFORMS]={0};
    for(unsigned i=0;i<compiled->uniform_count;++i) {
        unsigned slot=compiled->uniform_order[i];
        if(slot>=CTR_SHADER_UNIFORMS*4u)return CTR_GX2_SHADER_ARGUMENT;
        unsigned index=slot/4u,lane=slot%4u;
        if(seen[index]&(1u<<lane))return CTR_GX2_SHADER_ARGUMENT;
        seen[index]|=(uint8_t)(1u<<lane);
    }
    if(memcmp(seen,compiled->uniform_used,sizeof(seen)))return CTR_GX2_SHADER_ARGUMENT;
    for(unsigned i=0;i<compiled->uniform_count;++i) {
        unsigned slot=compiled->uniform_order[i],index=slot/4u,lane=slot%4u;uint32_t raw;
        if(!(p->uniform_defined[index]&(1u<<lane)))return CTR_GX2_SHADER_UNDEFINED;
        if(!ctr_shader_f24_exact(p->uniforms[index][lane],&raw))return CTR_GX2_SHADER_NUMERIC;
    }
    return CTR_GX2_SHADER_OK;
}
const char *ctr_gx2_shader_result_name(ctr_gx2_shader_result result) {
    static const char *const names[]={"ok","argument","overflow","alias","range",
        "unsupported-instruction-or-state","undefined-source-or-semantic","unsupported-numeric-value",
        "missing-end","no-memory","limit"};
    return (unsigned)result<sizeof(names)/sizeof(names[0])?names[result]:"unknown";
}
