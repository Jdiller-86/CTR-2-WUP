/* Original instruction encoder from published R700/Latte field facts.
 * Reference facts: devkitPro/wut 937f0d6901f5e2a72672154a674ec47b63e2f417
 * samples/cmake/content/pos_col_shader.{vsh,psh,gsh}; decaf-emu/decaf-emu
 * e6c528a20a41c34e0f9eb91dd3da40f119db2dee src/libgpu/latte instruction
 * definitions. No external assembler or emulator implementation is embedded. */
#include "ctr_gx2_shader.h"
#include <stdlib.h>
#include <string.h>

#define LATTE_ADD 0u
#define LATTE_MUL_IEEE 2u
#define LATTE_MAX 3u
#define LATTE_MIN 4u
#define LATTE_SETE 8u
#define LATTE_SETGT 9u
#define LATTE_SETGE 10u
#define LATTE_SETNE 11u
#define LATTE_MOVA_INT 24u
#define LATTE_MOV 25u
#define LATTE_PRED_SETNE 35u
#define LATTE_EXP_IEEE 0x61u
#define LATTE_LOG_IEEE 0x63u
#define LATTE_RECIP_IEEE 0x66u
#define LATTE_RECIPSQRT_IEEE 0x69u
#define LATTE_FLT_TO_INT_TRUNC 0x6eu
#define LATTE_INT_TO_FLT 0x6cu
#define LATTE_UINT_TO_FLT 0x6du
#define LATTE_ZERO 248u
#define LATTE_ONE 249u
#define LATTE_ALU_CLAUSE_WORDS 128u
/* CF POP_COUNT is in word1 bits 0..2. Restore the outer execution mask. */
#define LATTE_CF_POP_ONE UINT32_C(0x87000001)

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
static bool alu_at(ctr_gx2_shader *out,size_t alu_base,unsigned op,unsigned a,unsigned ac,bool negate,
                   unsigned b,unsigned bc,unsigned dest,unsigned lane) {
    size_t offset=alu_base+(size_t)out->alu_instructions*8u;
    if(offset>CTR_GX2_SHADER_MAX_BYTES-8u)return false;
    /* A scalar instruction is a complete group. Capture all sources before
     * writing a PICA destination, including in-place cross-lane swizzles. */
    put32(out->program+offset,a|(ac<<10)|(negate?1u<<12:0u)|(b<<13)|(bc<<23)|UINT32_C(0x80000000));
    put32(out->program+offset+4u,(op<<7)|(1u<<4)|(dest<<21)|(lane<<29));
    ++out->alu_instructions;return true;
}
static bool alu(ctr_gx2_shader *out,unsigned op,unsigned a,unsigned ac,bool negate,
                unsigned b,unsigned bc,unsigned dest,unsigned lane) {
    return alu_at(out,CTR_GX2_SHADER_ALU_OFFSET,op,a,ac,negate,b,bc,dest,lane);
}
static bool alu_raw(ctr_gx2_shader *out,size_t alu_base,uint32_t w0,uint32_t w1) {
    size_t offset=alu_base+(size_t)out->alu_instructions*8u;
    if(offset>CTR_GX2_SHADER_MAX_BYTES-8u)return false;
    put32(out->program+offset,w0|UINT32_C(0x80000000));
    put32(out->program+offset+4u,w1);
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
static ctr_gx2_shader_result export_uv_pairs(ctr_gx2_shader *out,unsigned seen) {
    static const unsigned uv_semantics[3]={12u,14u,22u};
    for(unsigned unit=0;unit<3u;++unit) {
        unsigned pair=3u<<uv_semantics[unit],present=seen&pair;
        if(present && present!=pair)return CTR_GX2_SHADER_UNDEFINED;
        if(present)out->texcoord_mask|=(uint8_t)(1u<<unit);
    }
    out->texcoord0=(out->texcoord_mask&1u)!=0;
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
            if((semantic>15u && semantic!=22u && semantic!=23u) ||
               (semantic>=4u && semantic<8u))return CTR_GX2_SHADER_UNSUPPORTED;
            if((seen&(1u<<semantic)) || !(written[reg]&(1u<<lane)))return CTR_GX2_SHADER_UNDEFINED;
            seen|=1u<<semantic;
            unsigned unit=semantic<12u?0u:semantic<16u?(semantic-12u)/2u:2u;
            unsigned target=semantic<4u?51u:semantic<12u?52u:53u+unit;
            unsigned channel=semantic<4u?semantic:semantic<12u?semantic-8u:semantic&1u;
            if(!alu(out,LATTE_MOV,33u+reg,lane,false,0,0,target,channel))return CTR_GX2_SHADER_LIMIT;
        }
    }
    if((seen&UINT32_C(0x0f0f))!=UINT32_C(0x0f0f))return CTR_GX2_SHADER_UNDEFINED;
    return export_uv_pairs(out,seen);
}
static ctr_gx2_shader_result compile(const ctr_shader_program *p,uint16_t mask,
                                     const uint32_t *maps,unsigned total,
                                     ctr_gx2_shader *out) {
    uint8_t defined[16]={0},written[16]={0};
    struct { unsigned first,end,resume,branches; } calls[8];
    struct { unsigned alternate,join; } branches[8];
    unsigned call_count=0,branch_count=0,pc=p->entry;
    for(;;) {
        for(;;) {
            unsigned enclosing=call_count?calls[call_count-1u].branches:0u;
            if(branch_count>enclosing && pc==branches[branch_count-1u].alternate)
                pc=branches[--branch_count].join;
            if(!call_count)break;
            if(pc!=calls[call_count-1u].end) {
                if(pc<calls[call_count-1u].first || pc>calls[call_count-1u].end)
                    return CTR_GX2_SHADER_RANGE;
                break;
            }
            if(branch_count!=enclosing)return CTR_GX2_SHADER_RANGE;
            pc=calls[--call_count].resume;
        }
        if(pc>=CTR_SHADER_MAX_CODE)return CTR_GX2_SHADER_NO_END;
        if(!p->code_defined[pc])return CTR_GX2_SHADER_UNDEFINED;
        uint32_t word=p->code[pc++];unsigned op=word>>26;
        if(++out->pica_instructions>CTR_SHADER_MAX_STEPS)return CTR_GX2_SHADER_LIMIT;
        if(op==0x21u || op==0x22u) {
            if(word&UINT32_C(0x03ffffff))return CTR_GX2_SHADER_UNSUPPORTED;
            if(op==0x22u)break;
            continue;
        }
        if(op==0x24u) {
            unsigned first=(word>>10)&4095u,length=word&255u;
            if(first>=CTR_SHADER_MAX_CODE || length>CTR_SHADER_MAX_CODE-first)
                return CTR_GX2_SHADER_RANGE;
            if(!length)continue;
            if(call_count==sizeof(calls)/sizeof(*calls))return CTR_GX2_SHADER_LIMIT;
            calls[call_count].first=first;
            calls[call_count].end=first+length;
            calls[call_count].resume=pc;
            calls[call_count++].branches=branch_count;
            pc=first;
            continue;
        }
        if(op==0x27u || (op==0x2du && p->round_f24)) {
            unsigned target=(word>>10)&4095u;
            if(target>=CTR_SHADER_MAX_CODE)return CTR_GX2_SHADER_RANGE;
            if(!p->boolean_defined)return CTR_GX2_SHADER_UNDEFINED;
            bool enabled=(p->boolean_uniforms&(1u<<((word>>22)&15u)))!=0;
            if(op==0x2du) {
                if(enabled!=((word&1u)!=0))pc=target;
            } else {
                unsigned join=target+(word&255u);
                if(target<pc || join>CTR_SHADER_MAX_CODE)return CTR_GX2_SHADER_RANGE;
                if(!enabled)pc=target;
                else if(join!=target) {
                    if(branch_count==sizeof(branches)/sizeof(*branches))return CTR_GX2_SHADER_LIMIT;
                    branches[branch_count].alternate=target;
                    branches[branch_count++].join=join;
                }
            }
            continue;
        }
        bool dot=op==2u || (op==1u && p->round_f24),mad=op>=0x38u;
        unsigned relative=(word>>(mad?22u:19u))&3u;
        if((op!=0x13u && op!=0u && op!=8u && op!=0x0cu && !dot && !mad) || relative)
            return CTR_GX2_SHADER_UNSUPPORTED;
        unsigned descriptor=word&(mad?31u:127u),dest=(word>>(mad?24u:21u))&31u;
        unsigned sources[]={(word>>(mad?17u:12u))&(mad?31u:127u),
            (word>>(mad?10u:7u))&(mad?127u:31u),(word>>5)&31u};
        if(!p->descriptor_defined[descriptor])return CTR_GX2_SHADER_UNDEFINED;
        uint32_t pattern=p->descriptors[descriptor];
        if(pattern&UINT32_C(0x80000000))return CTR_GX2_SHADER_UNSUPPORTED;
        unsigned lanes=pattern&15u,read_lanes=dot && lanes?(op==1u?14u:15u):lanes;
        /* Source snapshots are deliberately distinct from destination storage.
         * The separate groups also avoid depending on a VLIW bank scheduler. */
        for(unsigned operand=0;operand<(mad?3u:op==0x13u?1u:2u);++operand)
            for(unsigned lane=0;lane<4;++lane)if(read_lanes&(8u>>lane)) {
                unsigned selected=(pattern>>(11u+9u*operand-2u*lane))&3u,selector;
                ctr_gx2_shader_result result=source(p,out,defined,sources[operand],selected,&selector);
                if(result!=CTR_GX2_SHADER_OK)return result;
                if(!alu(out,LATTE_MOV,selector,selected,
                        (pattern&(1u<<(4u+9u*operand)))!=0,0,0,49u+operand,lane))return CTR_GX2_SHADER_LIMIT;
            }
        if(dot && lanes) {
            /* Keep each product and add separate, in the guest's lane order.
             * R50.x is reusable once all products have consumed R50. */
            unsigned terms=op==1u?3u:4u;
            for(unsigned lane=0;lane<terms;++lane)
                if(!alu(out,LATTE_MUL_IEEE,49u,lane,false,50u,lane,49u,lane))return CTR_GX2_SHADER_LIMIT;
            for(unsigned lane=0;lane<terms;++lane)
                if(!alu(out,LATTE_ADD,lane?50u:LATTE_ZERO,0,false,49u,lane,50u,0))return CTR_GX2_SHADER_LIMIT;
        }
        for(unsigned lane=0;lane<4;++lane)if(lanes&(8u>>lane)) {
            unsigned opcode=(op==0x13u || dot)?LATTE_MOV:op==8u?LATTE_MUL_IEEE:op==0x0cu?LATTE_MAX:LATTE_ADD;
            unsigned target=dest<16u?dest+33u:dest+1u;
            if(mad && !alu(out,LATTE_MUL_IEEE,49u,lane,false,50u,lane,49u,lane))return CTR_GX2_SHADER_LIMIT;
            if(!alu(out,opcode,dot?50u:49u,dot?0u:lane,false,mad?51u:50u,lane,target,lane))
                return CTR_GX2_SHADER_LIMIT;
        }
        for(unsigned lane=0;lane<4;++lane)if(lanes&(8u>>lane)) {
            if(dest<16u)written[dest]|=(uint8_t)(1u<<lane);
            else defined[dest-16u]|=(uint8_t)(1u<<lane);
        }
    }
    ctr_gx2_shader_result result=exports(out,mask,maps,total,written);
    if(result!=CTR_GX2_SHADER_OK)return result;
    unsigned clauses=(out->alu_instructions+LATTE_ALU_CLAUSE_WORDS-1u)/LATTE_ALU_CLAUSE_WORDS;
    unsigned uv_count=bit_count(out->texcoord_mask);
    if((clauses+3u+uv_count)*8u>CTR_GX2_SHADER_ALU_OFFSET)return CTR_GX2_SHADER_LIMIT;
    /* CALL_FS NO_BARRIER; ALU clauses; POS0, color PARAM0 and dense UV PARAM1..3.
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
    put32(out->program+param+4u,uv_count?UINT32_C(0x13800688):UINT32_C(0x14200688));
    unsigned slot=0;
    for(unsigned unit=0;unit<3u;++unit)if(out->texcoord_mask&(1u<<unit)) {
        ++slot;
        put32(out->program+param+slot*8u,slot|(2u<<13)|((53u+unit)<<15));
        put32(out->program+param+slot*8u+4u,
            slot==uv_count?UINT32_C(0x14200b08):UINT32_C(0x13800b08));
    }
    /* GX2 shader storage is padded to its 256-byte program alignment, as in
     * the independent WUT GFD fixture. Keep the final fetch inside storage. */
    out->size=(CTR_GX2_SHADER_ALU_OFFSET+out->alu_instructions*8u+255u)&~255u;
    out->gprs=CTR_GX2_SHADER_GPRS;
    for(unsigned unit=0;unit<3u;++unit)
        if(out->texcoord_mask&(1u<<unit))out->gprs=CTR_GX2_SHADER_TEXTURED_GPRS+unit;
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

typedef struct {
    const ctr_shader_program *p;
    const ctr_gx2_shader_game_options *opt;
    ctr_gx2_shader *out;
    unsigned cf_count;
    unsigned clause_start;
    bool has_relative;
    bool in_loop;
    int loop_aL;
    unsigned cond_depth;
} game_ctx;

static bool game_flush_alu(game_ctx *g,bool push_before) {
    unsigned total=g->out->alu_instructions-g->clause_start;
    if(!total)return true;
    unsigned pos=g->clause_start;
    while(pos<g->out->alu_instructions) {
        unsigned count=g->out->alu_instructions-pos;
        if(count>LATTE_ALU_CLAUSE_WORDS)count=LATTE_ALU_CLAUSE_WORDS;
        bool last=(pos+count==g->out->alu_instructions);
        if((g->cf_count+1u)*8u>CTR_GX2_SHADER_GAME_ALU_OFFSET)return false;
        uint32_t inst=(push_before && last)?UINT32_C(0xa4000000):UINT32_C(0xa0000000);
        put32(g->out->program+g->cf_count*8u,CTR_GX2_SHADER_GAME_ALU_OFFSET/8u+pos);
        put32(g->out->program+g->cf_count*8u+4u,inst|((count-1u)<<18));
        ++g->cf_count;
        pos+=count;
    }
    g->clause_start=g->out->alu_instructions;
    return true;
}

static bool game_emit_cf(game_ctx *g,unsigned idx,uint32_t w0,uint32_t w1) {
    if((idx+1u)*8u>CTR_GX2_SHADER_GAME_ALU_OFFSET)return false;
    put32(g->out->program+idx*8u,w0);
    put32(g->out->program+idx*8u+4u,w1);
    return true;
}

static ctr_gx2_shader_result game_source(game_ctx *g,unsigned reg,unsigned lane,
                                         bool rel,unsigned *selector) {
    if(reg<16u) {
        if(g->opt->fixed_mask&(1u<<reg)) { *selector=256u+96u+reg; }
        else { g->out->input_mask|=(uint16_t)(1u<<reg);*selector=reg+1u; }
    } else if(reg<32u) {
        *selector=reg+1u;
    } else {
        unsigned index=reg-32u;
        if(rel) {
            g->has_relative=true;
            *selector=(256u+index)|(1u<<9);
            return CTR_GX2_SHADER_OK;
        }
        if(!(g->p->uniform_defined[index]&(1u<<lane))) {
            if(g->cond_depth) { *selector=LATTE_ZERO;return CTR_GX2_SHADER_OK; }
            return CTR_GX2_SHADER_UNDEFINED;
        }
        if(!(g->out->uniform_used[index]&(1u<<lane))) {
            if(g->out->uniform_count>=CTR_SHADER_UNIFORMS*4u)return CTR_GX2_SHADER_LIMIT;
            g->out->uniform_order[g->out->uniform_count++]=(uint16_t)(index*4u+lane);
        }
        g->out->uniform_used[index]|=(uint8_t)(1u<<lane);
        *selector=256u+index;
    }
    return CTR_GX2_SHADER_OK;
}

static bool game_emit_cond(game_ctx *g,uint32_t word,bool invert) {
    unsigned mode=(word>>22)&3u,refx=(word>>25)&1u,refy=(word>>24)&1u;
    /* R52.z = cmp[0] (0/1 float), R52.w = cmp[1] (0/1 float). Compute R51.w = 0/1 float. */
    if(mode==2u || mode==0u || mode==1u) {
        if(refx) {
            if(!alu_at(g->out,CTR_GX2_SHADER_GAME_ALU_OFFSET,LATTE_MOV,52u,2u,false,0,0,51u,2u))return false;
        } else {
            if(!alu_at(g->out,CTR_GX2_SHADER_GAME_ALU_OFFSET,LATTE_SETE,52u,2u,false,LATTE_ZERO,0,51u,2u))return false;
        }
    }
    if(mode==3u || mode==0u || mode==1u) {
        if(refy) {
            if(!alu_at(g->out,CTR_GX2_SHADER_GAME_ALU_OFFSET,LATTE_MOV,52u,3u,false,0,0,51u,3u))return false;
        } else {
            if(!alu_at(g->out,CTR_GX2_SHADER_GAME_ALU_OFFSET,LATTE_SETE,52u,3u,false,LATTE_ZERO,0,51u,3u))return false;
        }
    }
    if(mode==2u) {
        if(!alu_at(g->out,CTR_GX2_SHADER_GAME_ALU_OFFSET,LATTE_MOV,51u,2u,false,0,0,51u,3u))return false;
    } else if(mode==0u) {
        if(!alu_at(g->out,CTR_GX2_SHADER_GAME_ALU_OFFSET,LATTE_MAX,51u,2u,false,51u,3u,51u,3u))return false;
    } else if(mode==1u) {
        if(!alu_at(g->out,CTR_GX2_SHADER_GAME_ALU_OFFSET,LATTE_MUL_IEEE,51u,2u,false,51u,3u,51u,3u))return false;
    }
    if(invert) {
        if(!alu_at(g->out,CTR_GX2_SHADER_GAME_ALU_OFFSET,LATTE_SETE,51u,3u,false,LATTE_ZERO,0,51u,3u))return false;
    }
    /* PRED_SETNE(R51.w, 0.0f) with UPDATE_EXEC_MASK (1<<2) | UPDATE_PRED (1<<3), WRITE_MASK=0 */
    return alu_raw(g->out,CTR_GX2_SHADER_GAME_ALU_OFFSET,
                   51u|(3u<<10)|(LATTE_ZERO<<13),
                   (LATTE_PRED_SETNE<<7)|(1u<<2)|(1u<<3)|(51u<<21)|(3u<<29));
}

static ctr_gx2_shader_result game_walk(game_ctx *g,unsigned pc,unsigned end_pc,unsigned depth,bool *ended) {
    if(depth>8u)return CTR_GX2_SHADER_LIMIT;
    const ctr_shader_program *p=g->p;
    while(pc<end_pc) {
        if(pc>=CTR_SHADER_MAX_CODE)return CTR_GX2_SHADER_NO_END;
        if(!p->code_defined[pc])return CTR_GX2_SHADER_UNDEFINED;
        uint32_t word=p->code[pc++];unsigned op=word>>26;
        if(++g->out->pica_instructions>CTR_SHADER_MAX_STEPS)return CTR_GX2_SHADER_LIMIT;
        if(op==0x21u) {
            if(word&UINT32_C(0x03ffffff))return CTR_GX2_SHADER_UNSUPPORTED;
            continue;
        }
        if(op==0x22u) {
            if(word&UINT32_C(0x03ffffff))return CTR_GX2_SHADER_UNSUPPORTED;
            if(ended)*ended=true;
            return CTR_GX2_SHADER_OK;
        }
        if(op==0x24u || op==0x25u) {
            unsigned first=(word>>10)&4095u,length=word&255u;
            if(first>=CTR_SHADER_MAX_CODE || length>CTR_SHADER_MAX_CODE-first)return CTR_GX2_SHADER_RANGE;
            if(!length)continue;
            if(op==0x24u) {
                bool sub_end=false;
                ctr_gx2_shader_result r=game_walk(g,first,first+length,depth+1u,&sub_end);
                if(r!=CTR_GX2_SHADER_OK)return r;
                if(sub_end && ended) { *ended=true;return CTR_GX2_SHADER_OK; }
            } else {
                if(!game_emit_cond(g,word,false))return CTR_GX2_SHADER_LIMIT;
                if(!game_flush_alu(g,true))return CTR_GX2_SHADER_LIMIT;
                unsigned jump_cf=g->cf_count++;
                if((jump_cf+1u)*8u>CTR_GX2_SHADER_GAME_ALU_OFFSET)return CTR_GX2_SHADER_LIMIT;
                ++g->cond_depth;
                ctr_gx2_shader_result r=game_walk(g,first,first+length,depth+1u,NULL);
                --g->cond_depth;
                if(r!=CTR_GX2_SHADER_OK)return r;
                if(!game_flush_alu(g,false))return CTR_GX2_SHADER_LIMIT;
                unsigned pop_cf=g->cf_count++;
                if(!game_emit_cf(g,jump_cf,pop_cf,UINT32_C(0x85000000)))return CTR_GX2_SHADER_LIMIT;
                if(!game_emit_cf(g,pop_cf,pop_cf+1u,LATTE_CF_POP_ONE))return CTR_GX2_SHADER_LIMIT;
            }
            continue;
        }
        if(op==0x27u || op==0x2du) {
            unsigned target=(word>>10)&4095u;
            if(target>=CTR_SHADER_MAX_CODE)return CTR_GX2_SHADER_RANGE;
            if(!p->boolean_defined)return CTR_GX2_SHADER_UNDEFINED;
            bool enabled=(p->boolean_uniforms&(1u<<((word>>22)&15u)))!=0;
            if(op==0x2du) {
                if(enabled!=((word&1u)!=0)) {
                    if(target<pc)return CTR_GX2_SHADER_UNSUPPORTED;
                    pc=target;
                }
            } else {
                unsigned join=target+(word&255u);
                if(target<pc || join>CTR_SHADER_MAX_CODE)return CTR_GX2_SHADER_RANGE;
                unsigned branch_first=enabled?pc:target,branch_end=enabled?target:join;
                if(branch_end>branch_first) {
                    bool sub_end=false;
                    ctr_gx2_shader_result r=game_walk(g,branch_first,branch_end,depth+1u,&sub_end);
                    if(r!=CTR_GX2_SHADER_OK)return r;
                    if(sub_end && ended) { *ended=true;return CTR_GX2_SHADER_OK; }
                }
                pc=join;
            }
            continue;
        }
        if(op==0x28u || op==0x2cu) {
            unsigned target=(word>>10)&4095u;
            if(target<pc || target>=CTR_SHADER_MAX_CODE)return CTR_GX2_SHADER_RANGE;
            unsigned join=op==0x28u?target+(word&255u):target;
            if(join>CTR_SHADER_MAX_CODE)return CTR_GX2_SHADER_RANGE;
            if(!game_emit_cond(g,word,op==0x2cu))return CTR_GX2_SHADER_LIMIT;
            unsigned cond_gpr=60u+(depth>>2),cond_ch=depth&3u;
            if(op==0x28u && join>target) {
                if(!alu_at(g->out,CTR_GX2_SHADER_GAME_ALU_OFFSET,LATTE_MOV,51u,3u,false,0,0,cond_gpr,cond_ch))
                    return CTR_GX2_SHADER_LIMIT;
            }
            if(!game_flush_alu(g,true))return CTR_GX2_SHADER_LIMIT;
            unsigned jump_cf=g->cf_count++;
            if((jump_cf+1u)*8u>CTR_GX2_SHADER_GAME_ALU_OFFSET)return CTR_GX2_SHADER_LIMIT;
            ++g->cond_depth;
            ctr_gx2_shader_result r=game_walk(g,pc,target,depth+1u,NULL);
            --g->cond_depth;
            if(r!=CTR_GX2_SHADER_OK)return r;
            if(!game_flush_alu(g,false))return CTR_GX2_SHADER_LIMIT;
            {
                unsigned pop_cf=g->cf_count++;
                if(!game_emit_cf(g,jump_cf,pop_cf,UINT32_C(0x85000000)))return CTR_GX2_SHADER_LIMIT;
                if(!game_emit_cf(g,pop_cf,pop_cf+1u,LATTE_CF_POP_ONE))return CTR_GX2_SHADER_LIMIT;
            }
            if(op==0x28u && join>target) {
                if(!alu_at(g->out,CTR_GX2_SHADER_GAME_ALU_OFFSET,LATTE_SETE,cond_gpr,cond_ch,false,LATTE_ZERO,0,51u,3u))
                    return CTR_GX2_SHADER_LIMIT;
                if(!alu_raw(g->out,CTR_GX2_SHADER_GAME_ALU_OFFSET,
                            51u|(3u<<10)|(LATTE_ZERO<<13),
                            (LATTE_PRED_SETNE<<7)|(1u<<2)|(1u<<3)|(51u<<21)|(3u<<29)))
                    return CTR_GX2_SHADER_LIMIT;
                if(!game_flush_alu(g,true))return CTR_GX2_SHADER_LIMIT;
                unsigned jump2_cf=g->cf_count++;
                if((jump2_cf+1u)*8u>CTR_GX2_SHADER_GAME_ALU_OFFSET)return CTR_GX2_SHADER_LIMIT;
                ++g->cond_depth;
                r=game_walk(g,target,join,depth+1u,NULL);
                --g->cond_depth;
                if(r!=CTR_GX2_SHADER_OK)return r;
                if(!game_flush_alu(g,false))return CTR_GX2_SHADER_LIMIT;
                unsigned pop2_cf=g->cf_count++;
                if(!game_emit_cf(g,jump2_cf,pop2_cf,UINT32_C(0x85000000)))return CTR_GX2_SHADER_LIMIT;
                if(!game_emit_cf(g,pop2_cf,pop2_cf+1u,LATTE_CF_POP_ONE))return CTR_GX2_SHADER_LIMIT;
            }
            pc=join;
            continue;
        }
        if(op==0x29u) {
            unsigned target=(word>>10)&4095u,uniform=(word>>22)&15u;
            if(g->in_loop || target<pc || target>=CTR_SHADER_MAX_CODE || uniform>=4u)return CTR_GX2_SHADER_UNSUPPORTED;
            if(!(p->integer_defined&(1u<<uniform)))return CTR_GX2_SHADER_UNDEFINED;
            uint32_t inputs=p->integer_uniforms[uniform];
            unsigned l_count=(inputs&255u)+1u,l_stride=(inputs>>16)&255u;
            g->in_loop=true;g->loop_aL=(int)((inputs>>8)&255u);
            for(unsigned it=0;it<l_count;++it) {
                ctr_gx2_shader_result r=game_walk(g,pc,target+1u,depth+1u,NULL);
                if(r!=CTR_GX2_SHADER_OK) { g->in_loop=false;return r; }
                g->loop_aL+=(int)l_stride;
            }
            g->in_loop=false;pc=target+1u;
            continue;
        }
        bool cmp=(op==0x2eu || op==0x2fu),mad=(op>=0x38u);
        bool inverted=(op==0x18u),homogeneous=(op==0x03u || inverted);
        bool dot=(op==1u || op==2u || homogeneous);
        bool recip=(op==0x0eu || op==0x0fu);
        if(!cmp && !mad && !dot && op!=0u && op!=8u && op!=0x0cu && op!=0x0du && op!=0x12u && op!=0x13u && !recip)
            return CTR_GX2_SHADER_UNSUPPORTED;
        unsigned rel_idx=(word>>(mad?22u:19u))&3u;
        if(rel_idx==3u && !g->in_loop)return CTR_GX2_SHADER_UNSUPPORTED;
        unsigned descriptor=word&(mad?31u:127u),dest=(word>>(mad?24u:21u))&31u;
        unsigned sources[]={(word>>(mad?17u:12u))&(mad?31u:127u),
            (word>>(mad?10u:7u))&(mad?127u:31u),(word>>5)&31u};
        if(inverted) { sources[0]=(word>>14)&31u;sources[1]=(word>>7)&127u; }
        if(!p->descriptor_defined[descriptor])return CTR_GX2_SHADER_UNDEFINED;
        uint32_t pattern=p->descriptors[descriptor];
        if(pattern&UINT32_C(0x80000000))return CTR_GX2_SHADER_UNSUPPORTED;
        unsigned lanes=pattern&15u;
        if(op==0x12u)lanes&=12u;
        if(cmp)lanes=12u;
        unsigned read_lanes=(dot && lanes)?(op==1u?14u:15u):recip?(lanes?8u:0u):lanes;
        unsigned op_count=mad?3u:(op==0x13u || op==0x12u || recip)?1u:2u;
        unsigned rel_operand=(mad || inverted)?1u:0u;
        if(rel_idx==3u && sources[rel_operand]>=32u) {
            int off=g->loop_aL;
            if(off<-128 || off>127)off=0;
            sources[rel_operand]=32u+((sources[rel_operand]-32u+(unsigned)off)&127u);
            rel_idx=0u;
        }
        if(rel_idx && sources[rel_operand]>=32u) {
            unsigned in_clause=(g->out->alu_instructions-g->clause_start)%LATTE_ALU_CLAUSE_WORDS;
            if(in_clause+1u+op_count*4u>LATTE_ALU_CLAUSE_WORDS && !game_flush_alu(g,false))
                return CTR_GX2_SHADER_LIMIT;
            if(!alu_raw(g->out,CTR_GX2_SHADER_GAME_ALU_OFFSET,
                        52u|((rel_idx-1u)<<10),LATTE_MOVA_INT<<7))return CTR_GX2_SHADER_LIMIT;
        }
        for(unsigned operand=0;operand<op_count;++operand)
            for(unsigned lane=0;lane<4u;++lane)if(read_lanes&(8u>>lane)) {
                if(homogeneous && operand==0u && lane==3u) {
                    if(!alu_at(g->out,CTR_GX2_SHADER_GAME_ALU_OFFSET,LATTE_MOV,LATTE_ONE,0u,
                               false,0,0,49u+operand,lane))return CTR_GX2_SHADER_LIMIT;
                    continue;
                }
                unsigned selected=(pattern>>(11u+9u*operand-2u*lane))&3u,selector;
                bool is_rel=(rel_idx && operand==rel_operand && sources[operand]>=32u);
                ctr_gx2_shader_result r=game_source(g,sources[operand],selected,is_rel,&selector);
                if(r!=CTR_GX2_SHADER_OK)return r;
                if(!alu_at(g->out,CTR_GX2_SHADER_GAME_ALU_OFFSET,LATTE_MOV,selector,selected,
                           (pattern&(1u<<(4u+9u*operand)))!=0,0,0,49u+operand,lane))return CTR_GX2_SHADER_LIMIT;
            }
        if(cmp) {
            unsigned opx=(word>>24)&7u,opy=(word>>21)&7u;
            for(unsigned lane=0;lane<2u;++lane) {
                unsigned cop=lane?opy:opx;
                if(cop>=6u) {
                    if(!alu_at(g->out,CTR_GX2_SHADER_GAME_ALU_OFFSET,LATTE_MOV,LATTE_ONE,0,false,0,0,52u,2u+lane))
                        return CTR_GX2_SHADER_LIMIT;
                } else {
                    static const unsigned map_op[6]={LATTE_SETE,LATTE_SETNE,LATTE_SETGT,LATTE_SETGE,LATTE_SETGT,LATTE_SETGE};
                    bool swap=(cop==2u || cop==3u);
                    if(!alu_at(g->out,CTR_GX2_SHADER_GAME_ALU_OFFSET,map_op[cop],
                               swap?50u:49u,lane,false,swap?49u:50u,lane,52u,2u+lane))
                        return CTR_GX2_SHADER_LIMIT;
                }
            }
            continue;
        }
        if(op==0x12u) {
            for(unsigned lane=0;lane<2u;++lane)if(lanes&(8u>>lane)) {
                if(!alu_at(g->out,CTR_GX2_SHADER_GAME_ALU_OFFSET,LATTE_FLT_TO_INT_TRUNC,49u,lane,false,0,0,52u,lane))
                    return CTR_GX2_SHADER_LIMIT;
            }
            continue;
        }
        if(recip && lanes) {
            unsigned rop=(op==0x0eu)?LATTE_RECIP_IEEE:LATTE_RECIPSQRT_IEEE;
            if(!alu_at(g->out,CTR_GX2_SHADER_GAME_ALU_OFFSET,rop,49u,0,false,0,0,51u,0))
                return CTR_GX2_SHADER_LIMIT;
            unsigned target=dest<16u?dest+33u:dest+1u;
            for(unsigned lane=0;lane<4u;++lane)if(lanes&(8u>>lane)) {
                if(!alu_at(g->out,CTR_GX2_SHADER_GAME_ALU_OFFSET,LATTE_MOV,51u,0,false,0,0,target,lane))
                    return CTR_GX2_SHADER_LIMIT;
            }
            continue;
        }
        if(dot && lanes) {
            unsigned terms=op==1u?3u:4u;
            for(unsigned lane=0;lane<terms;++lane)
                if(!alu_at(g->out,CTR_GX2_SHADER_GAME_ALU_OFFSET,LATTE_MUL_IEEE,49u,lane,false,50u,lane,49u,lane))
                    return CTR_GX2_SHADER_LIMIT;
            for(unsigned lane=0;lane<terms;++lane)
                if(!alu_at(g->out,CTR_GX2_SHADER_GAME_ALU_OFFSET,LATTE_ADD,lane?50u:LATTE_ZERO,0,false,49u,lane,50u,0))
                    return CTR_GX2_SHADER_LIMIT;
        }
        for(unsigned lane=0;lane<4u;++lane)if(lanes&(8u>>lane)) {
            unsigned opcode=(op==0x13u || dot)?LATTE_MOV:op==8u?LATTE_MUL_IEEE:op==0x0cu?LATTE_MAX:op==0x0du?LATTE_MIN:LATTE_ADD;
            unsigned target=dest<16u?dest+33u:dest+1u;
            if(mad && !alu_at(g->out,CTR_GX2_SHADER_GAME_ALU_OFFSET,LATTE_MUL_IEEE,49u,lane,false,50u,lane,49u,lane))
                return CTR_GX2_SHADER_LIMIT;
            if(!alu_at(g->out,CTR_GX2_SHADER_GAME_ALU_OFFSET,opcode,dot?50u:49u,dot?0u:lane,false,mad?51u:50u,lane,target,lane))
                return CTR_GX2_SHADER_LIMIT;
        }
    }
    return CTR_GX2_SHADER_OK;
}

static ctr_gx2_shader_result compile_game(const ctr_shader_program *p,const ctr_gx2_shader_game_options *opt,
                                          ctr_gx2_shader *out) {
    game_ctx g={p,opt,out,1u,0u,false,false,0,0u};
    for(unsigned r=17u;r<=32u;++r)
        for(unsigned lane=0;lane<4u;++lane)
            if(!alu_at(out,CTR_GX2_SHADER_GAME_ALU_OFFSET,LATTE_MOV,LATTE_ZERO,0,false,0,0,r,lane))
                return CTR_GX2_SHADER_LIMIT;
    for(unsigned lane=0;lane<4u;++lane)
        if(!alu_at(out,CTR_GX2_SHADER_GAME_ALU_OFFSET,LATTE_MOV,LATTE_ONE,0,false,0,0,54u,lane))
            return CTR_GX2_SHADER_LIMIT;
    for(unsigned v=0;v<16u;++v) {
        unsigned op=(opt->int_signed_mask&(1u<<v))?LATTE_INT_TO_FLT:
                    ((opt->int_unsigned_mask&(1u<<v))?LATTE_UINT_TO_FLT:0u);
        if(!op)continue;
        unsigned comps=(opt->int_components[v] && opt->int_components[v]<=4u)?opt->int_components[v]:4u;
        for(unsigned lane=0;lane<comps;++lane)
            if(!alu_at(out,CTR_GX2_SHADER_GAME_ALU_OFFSET,op,1u+v,lane,false,0,0,1u+v,lane))
                return CTR_GX2_SHADER_LIMIT;
        for(unsigned lane=comps;lane<4u;++lane)
            if(!alu_at(out,CTR_GX2_SHADER_GAME_ALU_OFFSET,LATTE_MOV,(lane==3u)?LATTE_ONE:LATTE_ZERO,0,false,0,0,1u+v,lane))
                return CTR_GX2_SHADER_LIMIT;
    }
    bool ended=false;
    ctr_gx2_shader_result r=game_walk(&g,p->entry,CTR_SHADER_MAX_CODE,0u,&ended);
    if(r!=CTR_GX2_SHADER_OK)return r;
    if(!ended)return CTR_GX2_SHADER_NO_END;
    if(!opt->output_mask || opt->output_count>7u || opt->output_count!=bit_count(opt->output_mask))
        return CTR_GX2_SHADER_UNSUPPORTED;
    unsigned seen=0,index=0;
    for(unsigned reg=0;reg<16;++reg)if(opt->output_mask&(1u<<reg)) {
        uint32_t mapping=opt->output_maps[index++];
        if(mapping&UINT32_C(0xe0e0e0e0))return CTR_GX2_SHADER_UNSUPPORTED;
        for(unsigned lane=0;lane<4;++lane) {
            unsigned sem=(mapping>>(lane*8u))&31u;
            if(sem==31u)continue;
            bool orient=sem>=4u && sem<8u,view=sem>=18u && sem<=20u;
            if(!((sem<=15u && !orient) || sem==22u || sem==23u || (opt->light_active && (orient || (opt->light_needs_view && view)))))
                continue;
            if(seen&(1u<<sem))return CTR_GX2_SHADER_UNDEFINED;
            seen|=1u<<sem;
            unsigned target,channel;
            if(sem<4u) { target=53u;channel=sem; }
            else if(orient) { target=58u;channel=sem-4u; }
            else if(sem<12u) { target=54u;channel=sem-8u; }
            else if(view) { target=59u;channel=sem-18u; }
            else {
                unsigned unit=sem<16u?(sem-12u)/2u:2u;
                target=55u+unit;channel=sem&1u;
            }
            if(!alu_at(out,CTR_GX2_SHADER_GAME_ALU_OFFSET,LATTE_MOV,33u+reg,lane,false,0,0,target,channel))
                return CTR_GX2_SHADER_LIMIT;
        }
    }
    if((seen&UINT32_C(0x000f))!=UINT32_C(0x000f))return CTR_GX2_SHADER_UNDEFINED;
    if(opt->light_active && ((seen&0xf0u)!=0xf0u || (opt->light_needs_view && (seen&UINT32_C(0x1c0000))!=UINT32_C(0x1c0000))))
        return CTR_GX2_SHADER_UNDEFINED;
    r=export_uv_pairs(out,seen);
    if(r!=CTR_GX2_SHADER_OK)return r;
    out->light_active=opt->light_active;
    out->light_needs_view=opt->light_needs_view;
    if(g.has_relative) {
        for(unsigned idx=0;idx<CTR_SHADER_UNIFORMS;++idx)if(p->uniform_defined[idx])
            out->uniform_used[idx]=15u;
    }
    if(!game_flush_alu(&g,false))return CTR_GX2_SHADER_LIMIT;
    unsigned uv_count=bit_count(out->texcoord_mask);
    unsigned param_count=1u+uv_count+(opt->light_active?(1u+(opt->light_needs_view?1u:0u)):0u);
    if((g.cf_count+1u+param_count)*8u>CTR_GX2_SHADER_GAME_ALU_OFFSET)return CTR_GX2_SHADER_LIMIT;
    put32(out->program+4u,UINT32_C(0x09800000));
    /* POS0 */
    if(!game_emit_cf(&g,g.cf_count++,60u|(1u<<13)|(53u<<15),UINT32_C(0x94000688)))return CTR_GX2_SHADER_LIMIT;
    /* Dense PARAMs matching ctr_gx2_transformed_program order: color, UVs, orientation, view */
    unsigned regs[6],reg_count=0;
    regs[reg_count++]=54u;
    for(unsigned unit=0;unit<3u;++unit)if(out->texcoord_mask&(1u<<unit))regs[reg_count++]=55u+unit;
    if(opt->light_active) {
        regs[reg_count++]=58u;
        if(opt->light_needs_view)regs[reg_count++]=59u;
    }
    for(unsigned slot=0;slot<reg_count;++slot) {
        bool last=(slot+1u==reg_count);
        uint32_t w1=last?UINT32_C(0x14200688):UINT32_C(0x13800688);
        if(!game_emit_cf(&g,g.cf_count++,slot|(2u<<13)|(regs[slot]<<15),w1))return CTR_GX2_SHADER_LIMIT;
    }
    out->size=(CTR_GX2_SHADER_GAME_ALU_OFFSET+out->alu_instructions*8u+255u)&~255u;
    out->gprs=CTR_GX2_SHADER_GAME_GPRS;
    return CTR_GX2_SHADER_OK;
}

ctr_gx2_shader_result ctr_gx2_shader_compile_game(const ctr_shader_program *p,
                                                  const ctr_gx2_shader_game_options *opt,
                                                  ctr_gx2_shader *out) {
    if(!p || !opt || !out)return CTR_GX2_SHADER_ARGUMENT;
    if((uintptr_t)p%_Alignof(ctr_shader_program) || (uintptr_t)opt%_Alignof(ctr_gx2_shader_game_options) ||
       (uintptr_t)out%_Alignof(ctr_gx2_shader))return CTR_GX2_SHADER_ARGUMENT;
    if(opt->output_count>7u)return CTR_GX2_SHADER_RANGE;
    if(!span(p,sizeof(*p)) || !span(opt,sizeof(*opt)) || !span(out,sizeof(*out)))return CTR_GX2_SHADER_OVERFLOW;
    if(overlap(out,sizeof(*out),p,sizeof(*p)) || overlap(out,sizeof(*out),opt,sizeof(*opt)))return CTR_GX2_SHADER_ALIAS;
    if(p->entry>=CTR_SHADER_MAX_CODE)return CTR_GX2_SHADER_RANGE;
    ctr_gx2_shader *trial=calloc(1,sizeof(*trial));
    if(!trial)return CTR_GX2_SHADER_NO_MEMORY;
    ctr_gx2_shader_result result=compile_game(p,opt,trial);
    if(result==CTR_GX2_SHADER_OK)*out=*trial;
    free(trial);return result;
}
