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
        if(op!=0x13u || ((word>>19)&3u))return CTR_SHADER_UNSUPPORTED;
        unsigned descriptor=word&127u,source=(word>>12)&127u,dest=(word>>21)&31u;
        if(!program->descriptor_defined[descriptor])return CTR_SHADER_UNDEFINED;
        uint32_t pattern=program->descriptors[descriptor];
        if(pattern&UINT32_C(0x80000000))return CTR_SHADER_UNSUPPORTED;
        uint32_t moved[4]={0};unsigned mask=pattern&15u;
        for(unsigned lane=0;lane<4;++lane)if(mask&(8u>>lane)) {
            unsigned selected=(pattern>>(11u-lane*2u))&3u;
            uint32_t value=0;
            if(source<16u) {
                if(!(input->enabled&(1u<<source)))return CTR_SHADER_UNDEFINED;
                memcpy(&value,&input->input[source][selected],sizeof(value));
            } else if(source<32u) {
                if(!(defined[source-16u]&(1u<<selected)))return CTR_SHADER_UNDEFINED;
                value=temporary[source-16u][selected];
            } else {
                if(!(program->uniform_defined[source-32u]&(1u<<selected)))return CTR_SHADER_UNDEFINED;
                value=program->uniforms[source-32u][selected];
            }
            uint32_t raw;
            if(!ctr_shader_f24_exact(value,&raw))return CTR_SHADER_NUMERIC;
            moved[lane]=value^((pattern&16u)?UINT32_C(0x80000000):0);
        }
        /* All source lanes are captured before in-place temporary swizzling. */
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
