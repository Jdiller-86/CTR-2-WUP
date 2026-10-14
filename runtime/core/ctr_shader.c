/* Original implementation of published PICA instruction/descriptor facts. */
#include "ctr_shader.h"
#include <float.h>
#include <string.h>

static bool span(const void *p,size_t n) {
    return p && n<=UINTPTR_MAX-(uintptr_t)p;
}
static bool overlap(const void *a,size_t an,const void *b,size_t bn) {
    return (uintptr_t)a<(uintptr_t)b+bn && (uintptr_t)b<(uintptr_t)a+an;
}
bool ctr_shader_f24_decode(uint32_t raw,uint32_t *bits) {
    if(!span(bits,sizeof(*bits)) || (raw&UINT32_C(0xff000000)))return false;
    unsigned exponent=(raw>>16)&127u;
    uint32_t fraction=raw&65535u,sign=(raw&UINT32_C(0x800000))<<8;
    if(exponent==127u || (!exponent && fraction))return false;
    *bits=exponent?sign|((exponent+64u)<<23)|(fraction<<7):sign;
    return true;
}
bool ctr_shader_f24_exact(uint32_t bits,uint32_t *raw) {
    if(!span(raw,sizeof(*raw)))return false;
    unsigned exponent=(bits>>23)&255u;
    uint32_t fraction=bits&UINT32_C(0x7fffff),sign=(bits>>8)&UINT32_C(0x800000);
    if(!exponent && !fraction) { *raw=sign;return true; }
    if(exponent<65u || exponent>190u || (fraction&127u))return false;
    *raw=sign|((exponent-64u)<<16)|(fraction>>7);return true;
}
/* magnitude * 2^scale, with no rounding, flush or overflow approximation. */
static bool pack_exact(uint64_t magnitude,int scale,uint32_t sign,uint32_t *bits) {
    if(!magnitude) { *bits=sign;return true; }
    while(magnitude>UINT64_C(0x1ffff)) {
        if(magnitude&1u)return false;
        magnitude>>=1;++scale;
    }
    while(magnitude<UINT64_C(0x10000)) { magnitude<<=1;--scale; }
    int exponent=scale+79;
    if(exponent<1 || exponent>126)return false;
    return ctr_shader_f24_decode((sign>>8)|((uint32_t)exponent<<16)|
                                 ((uint32_t)magnitude&65535u),bits);
}
static bool arithmetic(unsigned op,uint32_t a,uint32_t b,uint32_t *bits) {
    uint32_t ar,br;
    if(!ctr_shader_f24_exact(a,&ar) || !ctr_shader_f24_exact(b,&br))return false;
    uint32_t sign=(a^b)&UINT32_C(0x80000000);
    bool az=(ar&UINT32_C(0x7fffff))==0,bz=(br&UINT32_C(0x7fffff))==0;
    unsigned ae=(ar>>16)&127u,be=(br>>16)&127u;
    uint64_t am=UINT64_C(0x10000)|(ar&65535u),bm=UINT64_C(0x10000)|(br&65535u);
    if(op==0x08u) {
        if(az || bz) { *bits=sign;return true; }
        return pack_exact(am*bm,(int)ae+(int)be-158,sign,bits);
    }
    if(az && bz) { *bits=a&b&UINT32_C(0x80000000);return true; }
    if(az) { *bits=b;return true; }
    if(bz) { *bits=a;return true; }
    unsigned low=ae<be?ae:be,high=ae>be?ae:be;
    /* Beyond 17 exponent steps a nonzero smaller operand necessarily leaves
     * more than 17 significant bits, even after opposite-sign cancellation.
     * Reject before shifting; neither a wide integer nor host FP may erase it. */
    if(high-low>17u)return false;
    am<<=ae-low;bm<<=be-low;
    if(!sign)return pack_exact(am+bm,(int)low-79,a&UINT32_C(0x80000000),bits);
    if(am==bm) { *bits=0;return true; }
    return pack_exact(am>bm?am-bm:bm-am,(int)low-79,
                      (am>bm?a:b)&UINT32_C(0x80000000),bits);
}
static ctr_shader_result read_lane(const ctr_shader_program *program,const ctr_vertex *input,
                                  uint32_t temporary[16][4],const uint8_t *defined,
                                  unsigned source,unsigned lane,bool negate,uint32_t *value) {
    if(source<16u) {
        if(!(input->enabled&(1u<<source)))return CTR_SHADER_UNDEFINED;
        memcpy(value,&input->input[source][lane],sizeof(*value));
    } else if(source<32u) {
        if(!(defined[source-16u]&(1u<<lane)))return CTR_SHADER_UNDEFINED;
        *value=temporary[source-16u][lane];
    } else {
        if(!(program->uniform_defined[source-32u]&(1u<<lane)))return CTR_SHADER_UNDEFINED;
        *value=program->uniforms[source-32u][lane];
    }
    uint32_t raw;
    if(!ctr_shader_f24_exact(*value,&raw))return CTR_SHADER_NUMERIC;
    if(negate)*value^=UINT32_C(0x80000000);
    return CTR_SHADER_OK;
}
ctr_shader_result ctr_shader_execute(const ctr_shader_program *program,
                                     const ctr_vertex *input,
                                     ctr_shader_output *output) {
    if(!program || !input || !output)return CTR_SHADER_ARGUMENT;
    if(!span(program,sizeof(*program)) || !span(input,sizeof(*input)) ||
       !span(output,sizeof(*output)))return CTR_SHADER_OVERFLOW;
    if(overlap(output,sizeof(*output),program,sizeof(*program)) ||
       overlap(output,sizeof(*output),input,sizeof(*input)))return CTR_SHADER_ALIAS;
    if(program->entry>=CTR_SHADER_MAX_CODE)return CTR_SHADER_RANGE;
    if(sizeof(float)!=4 || FLT_RADIX!=2 || FLT_MANT_DIG!=24 || FLT_MAX_EXP!=128)
        return CTR_SHADER_UNSUPPORTED;
    float one=1.0f;uint32_t one_bits;
    memcpy(&one_bits,&one,sizeof(one_bits));
    if(one_bits!=UINT32_C(0x3f800000))return CTR_SHADER_UNSUPPORTED;
    uint32_t temporary[16][4]={{0}};uint8_t defined[16]={0};
    ctr_shader_output result={0};
    for(unsigned pc=program->entry;pc<CTR_SHADER_MAX_CODE;++pc) {
        if(!program->code_defined[pc])return CTR_SHADER_UNDEFINED;
        uint32_t word=program->code[pc];unsigned op=word>>26;
        ++result.instructions;
        if(op==0x21u || op==0x22u) {
            if(word&UINT32_C(0x03ffffff))return CTR_SHADER_UNSUPPORTED;
            if(op==0x22u) { *output=result;return CTR_SHADER_OK; }
            continue;
        }
        if((op!=0x13u && op!=0x00u && op!=0x08u) || ((word>>19)&3u))return CTR_SHADER_UNSUPPORTED;
        unsigned descriptor=word&127u,source=(word>>12)&127u,dest=(word>>21)&31u;
        if(!program->descriptor_defined[descriptor])return CTR_SHADER_UNDEFINED;
        uint32_t pattern=program->descriptors[descriptor];
        if(pattern&UINT32_C(0x80000000))return CTR_SHADER_UNSUPPORTED;
        uint32_t moved[4]={0};unsigned mask=pattern&15u;
        for(unsigned lane=0;lane<4;++lane)if(mask&(8u>>lane)) {
            unsigned selected=(pattern>>(11u-lane*2u))&3u;
            uint32_t a,b;
            ctr_shader_result status=read_lane(program,input,temporary,defined,
                                               source,selected,(pattern&16u)!=0,&a);
            if(status!=CTR_SHADER_OK)return status;
            if(op==0x13u)moved[lane]=a;
            else {
                selected=(pattern>>(20u-lane*2u))&3u;
                status=read_lane(program,input,temporary,defined,
                                 (word>>7)&31u,selected,(pattern&(1u<<13))!=0,&b);
                if(status!=CTR_SHADER_OK)return status;
                if(!arithmetic(op,a,b,&moved[lane]))return CTR_SHADER_NUMERIC;
            }
        }
        /* Both sources are captured before any in-place temporary write. */
        for(unsigned lane=0;lane<4;++lane)if(mask&(8u>>lane)) {
            if(dest<16u) {
                memcpy(&result.values[dest][lane],&moved[lane],sizeof(moved[lane]));
                result.written[dest]|=(uint8_t)(1u<<lane);
            } else {
                temporary[dest-16u][lane]=moved[lane];
                defined[dest-16u]|=(uint8_t)(1u<<lane);
            }
        }
    }
    return CTR_SHADER_NO_END;
}
const char *ctr_shader_result_name(ctr_shader_result result) {
    static const char *const names[]={"ok","argument","overflow","alias","range",
        "unsupported-instruction-or-state","undefined-source-or-code","unsupported-numeric-value","missing-end"};
    return (unsigned)result<sizeof(names)/sizeof(names[0])?names[result]:"unknown";
}
