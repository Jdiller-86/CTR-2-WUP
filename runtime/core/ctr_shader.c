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
/* Round binary32 operands once to a 16-bit stored mantissa. Converting beyond
 * the normal float24 range produces a signed infinity on overflow. */
static bool round_input(uint32_t *bits) {
    uint32_t sign=*bits&UINT32_C(0x80000000),magnitude=*bits&UINT32_C(0x7fffffff);
    unsigned exponent=magnitude>>23;
    if(magnitude>UINT32_C(0x7f800000))return false;
    if(exponent>190u) { *bits=sign|UINT32_C(0x7f800000);return true; }
    if(exponent<65u) { *bits=sign;return true; }
    magnitude=(magnitude+63u+((magnitude>>7)&1u))&~UINT32_C(127);
    if((magnitude>>23)>190u)magnitude=UINT32_C(0x7f800000);
    *bits=sign|magnitude;return true;
}
/* Encode magnitude * 2^scale directly, without double rounding through f32. */
static bool pack(uint64_t magnitude,int scale,uint32_t sign,bool round,uint32_t *bits) {
    if(!magnitude) { *bits=sign;return true; }
    /* Normalize directly to the 17-bit significand. On PowerPC this avoids
     * shifting once per excess bit for every vertex multiply. */
#if defined(__GNUC__) || defined(__clang__)
    int shift=__builtin_clzll(1u)-__builtin_clzll(magnitude)-16;
#else
    int shift=-16;
    for(uint64_t value=magnitude;value>1u;value>>=1)++shift;
#endif
    if(shift>0) {
        uint64_t half=UINT64_C(1)<<(shift-1),remainder=magnitude&((half<<1)-1u);
        if(!round && remainder)return false;
        magnitude>>=shift;
        if(remainder>half || (remainder==half && (magnitude&1u))) {
            if(++magnitude==UINT64_C(0x20000)) { magnitude>>=1;++shift; }
        }
    } else magnitude<<=-shift;
    int exponent=scale+shift+79;
    if(exponent<1 && round) { *bits=0;return true; }
    if(exponent<1 || exponent>126)return false;
    return ctr_shader_f24_decode((sign>>8)|((uint32_t)exponent<<16)|
                                 ((uint32_t)magnitude&65535u),bits);
}
static float value(uint32_t bits) { float f;memcpy(&f,&bits,sizeof(f));return f; }
static bool arithmetic(unsigned op,uint32_t a,uint32_t b,bool round,uint32_t *bits) {
#if FLT_RADIX==2 && DBL_MANT_DIG==53 && DBL_MAX_EXP==1024
    if(round) {
        const uint32_t sign_mask=UINT32_C(0x80000000);
        uint32_t am=a&~sign_mask,bm=b&~sign_mask;
        if((!am || !bm) && am<=UINT32_C(0x7f800000) && bm<=UINT32_C(0x7f800000)) {
            /* Rounded shader sources already contain float24 values. */
            if(op==0x08u)*bits=0u;
            else if(am)*bits=a;
            else if(bm)*bits=b;
            else *bits=a&b&sign_mask;
            return true;
        }
        unsigned a_exp=am>>23,b_exp=bm>>23;
        if(a_exp>=65u && a_exp<=190u && b_exp>=65u && b_exp<=190u) {
            /* Binary64 holds the product and every sum close to a float24
             * boundary. Each rounding boundary is representable in binary32.
             * Narrowing only loses the rounding direction at a midpoint. */
            double left=value(a),right=value(b);
            double wide=op==0x08u?left*right:left+right;
            float narrow=(float)wide;
            uint32_t raw;
            memcpy(&raw,&narrow,sizeof(raw));
            uint32_t magnitude=raw&~sign_mask;
            unsigned discarded=magnitude&127u;
            bool increment=discarded>64u;
            if(discarded==64u) {
                double midpoint=narrow;
                if(wide==midpoint)increment=(magnitude&128u)!=0;
                else if(raw&sign_mask)increment=wide<midpoint;
                else increment=wide>midpoint;
            }
            if(increment)magnitude+=128u;
            unsigned exponent=magnitude>>23;
            if(exponent>190u)return false;
            *bits=exponent<65u?0u:(raw&sign_mask)|(magnitude&~UINT32_C(127));
            return true;
        }
    }
#endif
    uint32_t ar,br;
    uint32_t sign=(a^b)&UINT32_C(0x80000000);
    if(!ctr_shader_f24_exact(a,&ar) || !ctr_shader_f24_exact(b,&br)) {
        uint32_t am=a&UINT32_C(0x7fffffff),bm=b&UINT32_C(0x7fffffff);
        const uint32_t infinity=UINT32_C(0x7f800000);
        if(!round || am>infinity || bm>infinity || (am!=infinity && bm!=infinity))return false;
        if(op==0x08u) {
            /* PICA multiplication of infinity by zero yields zero. */
            *bits=am && bm?sign|infinity:0u;
        } else {
            if(am==bm && sign)return false;
            *bits=am==infinity?a:b;
        }
        return true;
    }
    bool az=(ar&UINT32_C(0x7fffff))==0,bz=(br&UINT32_C(0x7fffff))==0;
    unsigned ae=(ar>>16)&127u,be=(br>>16)&127u;
    uint64_t am=UINT64_C(0x10000)|(ar&65535u),bm=UINT64_C(0x10000)|(br&65535u);
    if(op==0x08u) {
        if(az || bz) { *bits=round?0u:sign;return true; }
        return pack(am*bm,(int)ae+(int)be-158,sign,round,bits);
    }
    if(az && bz) { *bits=a&b&UINT32_C(0x80000000);return true; }
    if(az) { *bits=b;return true; }
    if(bz) { *bits=a;return true; }
    unsigned low=ae<be?ae:be,high=ae>be?ae:be;
    /* Beyond 17 exponent steps a nonzero smaller operand necessarily leaves
     * more than 17 significant bits, even after opposite-sign cancellation.
     * Reject before shifting; neither a wide integer nor host FP may erase it. */
    if(high-low>17u && !round)return false;
    if(high-low>18u) { *bits=ae>be?a:b;return true; }
    am<<=ae-low;bm<<=be-low;
    if(!sign)return pack(am+bm,(int)low-79,a&UINT32_C(0x80000000),round,bits);
    if(am==bm) { *bits=0;return true; }
    return pack(am>bm?am-bm:bm-am,(int)low-79,
                (am>bm?a:b)&UINT32_C(0x80000000),round,bits);
}
static bool reciprocal(uint32_t bits,bool round,uint32_t *result) {
    uint32_t magnitude=bits&UINT32_C(0x7fffffff);
    if(round && (!magnitude || magnitude==UINT32_C(0x7f800000))) {
        *result=(bits&UINT32_C(0x80000000))|(magnitude?0u:UINT32_C(0x7f800000));
        return true;
    }
    uint32_t raw;
    if(!ctr_shader_f24_exact(bits,&raw) || !(raw&UINT32_C(0x7fffff)))return false;
    int exponent=126-(int)((raw>>16)&127u);
    uint32_t mantissa=UINT32_C(0x10000)|(raw&65535u);
    if(raw&65535u) {
        if(!round)return false;
        --exponent;
        uint64_t numerator=UINT64_C(1)<<33;
        uint32_t quotient=(uint32_t)(numerator/mantissa),remainder=(uint32_t)(numerator%mantissa);
        mantissa=quotient+(2u*remainder>mantissa || (2u*remainder==mantissa && (quotient&1u)));
    }
    return pack(mantissa,exponent-79,bits&UINT32_C(0x80000000),round,result);
}
static bool reciprocal_sqrt(uint32_t bits,uint32_t *result) {
    if(!(bits&UINT32_C(0x7fffffff))) { *result=bits|UINT32_C(0x7f800000);return true; }
    if(bits==UINT32_C(0x7f800000)) { *result=0;return true; }
    uint32_t raw;
    if(!ctr_shader_f24_exact(bits,&raw) || !raw || (raw&UINT32_C(0x800000)))return false;
    int exponent=(int)(raw>>16)-63;
    uint32_t mantissa=UINT32_C(0x10000)|(raw&65535u);
    if(exponent%2) { mantissa*=2u;--exponent; }
    /* The normalized result is in (0.5,1]. Compare squared integers to
     * round directly to 17 significant bits without a float32 intermediate. */
    uint32_t low=65536u,high=131073u;
    while(high-low>1u) {
        uint32_t midpoint=low+(high-low)/2u;
        if((uint64_t)midpoint*midpoint*mantissa<=(UINT64_C(1)<<50))low=midpoint;
        else high=midpoint;
    }
    uint64_t midpoint=(uint64_t)low*2u+1u;
    uint64_t boundary=midpoint*midpoint*mantissa;
    if(boundary<(UINT64_C(1)<<52) || (boundary==(UINT64_C(1)<<52) && (low&1u)))++low;
    return pack(low,-exponent/2-17,0,true,result);
}
static bool comparison(unsigned operation,float left,float right) {
    switch(operation) {
    case 0:return left==right;
    case 1:return left!=right;
    case 2:return left<right;
    case 3:return left<=right;
    case 4:return left>right;
    case 5:return left>=right;
    default:return true;
    }
}
static bool conditional(uint32_t word,const bool compared[2]) {
    unsigned mode=(word>>22)&3u;
    bool x=compared[0]==((word>>25)&1u),y=compared[1]==((word>>24)&1u);
    return mode==0u?(x||y):mode==1u?(x&&y):mode==2u?x:y;
}
typedef struct {
    const void *lanes;
    unsigned defined;
    bool checked;
} shader_operand;

/* Resolve register storage once. Each selected lane still has its own
 * validity check and numeric conversion at the point of use. */
static shader_operand shader_source(const ctr_shader_program *program,const ctr_vertex *input,
                                    uint32_t temporary[16][4],const uint8_t defined[16],unsigned source) {
    if(source<16u)
        return (shader_operand){input->input[source],input->enabled&(1u<<source)?15u:0u,false};
    if(source<32u)
        return (shader_operand){temporary[source-16u],defined[source-16u],true};
    if(source<32u+CTR_SHADER_UNIFORMS)
        return (shader_operand){program->uniforms[source-32u],program->uniform_defined[source-32u],false};
    static const uint32_t ones[4]={0x3f800000u,0x3f800000u,0x3f800000u,0x3f800000u};
    return (shader_operand){ones,15u,true};
}

static ctr_shader_result read_lane(shader_operand source,unsigned lane,bool negate,bool round,uint32_t *value) {
    if(!(source.defined&(1u<<lane)))return CTR_SHADER_UNDEFINED;
    memcpy(value,(const unsigned char *)source.lanes+lane*sizeof(*value),sizeof(*value));
    if(!source.checked) {
        uint32_t raw;
        if(round ? !round_input(value) : !ctr_shader_f24_exact(*value,&raw))return CTR_SHADER_NUMERIC;
    }
    if(negate && (*value || !round))*value^=UINT32_C(0x80000000);
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
    uint32_t temporary[16][4];uint8_t defined[16]={0};
    int address[3]={0};unsigned address_written=0;
    bool compare[2]={false,false},compared=false;
    struct { unsigned alternate,join; } branches[8];unsigned branch_count=0;
    struct { unsigned first,after,resume,branches; } calls[8];unsigned call_count=0;
    unsigned loop_first=0,loop_after=0,loop_count=0,loop_stride=0,loop_owner=0;
    ctr_shader_output result={0};
    for(unsigned pc=program->entry;;) {
        for(;;) {
            unsigned enclosing_branches=call_count?calls[call_count-1u].branches:0u;
            if(branch_count>enclosing_branches && pc==branches[branch_count-1u].alternate)
                pc=branches[--branch_count].join;
            if(loop_count && call_count==loop_owner && pc==loop_after) {
                address[2]+=(int)loop_stride;
                if(--loop_count)pc=loop_first;
            }
            if(!call_count)break;
            if(pc!=calls[call_count-1u].after) {
                if(pc<calls[call_count-1u].first || pc>calls[call_count-1u].after)return CTR_SHADER_RANGE;
                break;
            }
            if(branch_count!=enclosing_branches || (loop_count && call_count==loop_owner))return CTR_SHADER_RANGE;
            pc=calls[--call_count].resume;
        }
        if(pc>=CTR_SHADER_MAX_CODE)return CTR_SHADER_NO_END;
        if(!program->code_defined[pc])return CTR_SHADER_UNDEFINED;
        uint32_t word=program->code[pc++];unsigned op=word>>26;
        if(++result.instructions>CTR_SHADER_MAX_STEPS)return CTR_SHADER_LIMIT;
        if(op==0x21u || op==0x22u) {
            if(word&UINT32_C(0x03ffffff))return CTR_SHADER_UNSUPPORTED;
            if(op==0x22u) { *output=result;return CTR_SHADER_OK; }
            continue;
        }
        if(op==0x24u || (op==0x25u && program->round_f24)) {
            unsigned target=(word>>10)&4095u,length=word&255u;
            if(target>=CTR_SHADER_MAX_CODE || length>CTR_SHADER_MAX_CODE-target)return CTR_SHADER_RANGE;
            if(op==0x25u) {
                if(!compared)return CTR_SHADER_UNDEFINED;
                if(!conditional(word,compare))continue;
            }
            if(!length)continue;
            if(call_count==sizeof(calls)/sizeof(*calls))return CTR_SHADER_LIMIT;
            calls[call_count].first=target;
            calls[call_count].after=target+length;
            calls[call_count].resume=pc;
            calls[call_count++].branches=branch_count;
            pc=target;
            continue;
        }
        if((op==0x2cu || op==0x2du) && program->round_f24) {
            unsigned target=(word>>10)&4095u;
            if(target>=CTR_SHADER_MAX_CODE)return CTR_SHADER_RANGE;
            bool take;
            if(op==0x2du) {
                if(!program->boolean_defined)return CTR_SHADER_UNDEFINED;
                take=((program->boolean_uniforms&(1u<<((word>>22)&15u)))!=0)!=((word&1u)!=0);
            } else {
                if(!compared)return CTR_SHADER_UNDEFINED;
                take=conditional(word,compare);
            }
            if(take)pc=target;
            continue;
        }
        if(op>=0x27u && op<=0x29u) {
            unsigned target=(word>>10)&4095u;
            if(target<pc || target>=CTR_SHADER_MAX_CODE)return CTR_SHADER_RANGE;
            if(op!=0x29u) {
                unsigned join=target+(word&255u);
                if(join>CTR_SHADER_MAX_CODE)return CTR_SHADER_RANGE;
                bool taken;
                if(op==0x27u) {
                    if(!program->boolean_defined)return CTR_SHADER_UNDEFINED;
                    taken=(program->boolean_uniforms&(1u<<((word>>22)&15u)))!=0;
                } else {
                    if(!compared)return CTR_SHADER_UNDEFINED;
                    taken=conditional(word,compare);
                }
                if(!taken)pc=target;
                else if(join!=target) {
                    if(branch_count==sizeof(branches)/sizeof(*branches))return CTR_SHADER_LIMIT;
                    branches[branch_count].alternate=target;
                    branches[branch_count].join=join;
                    ++branch_count;
                }
            } else {
                unsigned uniform=(word>>22)&15u;
                if(loop_count || uniform>=4u)return CTR_SHADER_UNSUPPORTED;
                if(!(program->integer_defined&(1u<<uniform)))return CTR_SHADER_UNDEFINED;
                uint32_t inputs=program->integer_uniforms[uniform];
                loop_first=pc;loop_after=target+1u;loop_count=(inputs&255u)+1u;loop_stride=(inputs>>16)&255u;
                loop_owner=call_count;
                address[2]=(int)((inputs>>8)&255u);address_written|=4u;
            }
            continue;
        }
        bool cmp=op==0x2eu || op==0x2fu,mad=op>=0x38u;
        bool inverted=op==0x18u,homogeneous=op==0x03u || inverted;
        bool dot_product=op==0x02u || (program->round_f24 && (op==0x01u || homogeneous));
        bool reciprocal_op=op==0x0eu || (program->round_f24 && op==0x0fu);
        if(!cmp && !mad && !dot_product && op!=0x12u && op!=0x13u && op!=0x00u &&
           op!=0x08u && op!=0x0cu && !reciprocal_op)return CTR_SHADER_UNSUPPORTED;
        unsigned index=(word>>(mad?22u:19u))&3u;
        if(index && !(address_written&(1u<<(index-1u))))return CTR_SHADER_UNSUPPORTED;
        unsigned descriptor=word&(mad?31u:127u),dest=(word>>(mad?24u:21u))&31u;
        unsigned source=(word>>(mad?17u:12u))&(mad?31u:127u),second=(word>>(mad?10u:7u))&(mad?127u:31u);
        if(inverted) { source=(word>>14)&31u;second=(word>>7)&127u; }
        unsigned *relative=(mad || inverted)?&second:&source;
        if(index && *relative>=32u) {
            int offset=address[index-1u];
            if(offset<-128 || offset>127)offset=0;
            *relative=32u+((*relative-32u+(unsigned)offset)&127u);
        }
        if(!program->descriptor_defined[descriptor])return CTR_SHADER_UNDEFINED;
        uint32_t pattern=program->descriptors[descriptor];
        if(pattern&UINT32_C(0x80000000))return CTR_SHADER_UNSUPPORTED;
        uint32_t moved[4]={0},dot=0;unsigned mask=pattern&15u,unknown=0;
        bool unary=op==0x12u || op==0x13u || reciprocal_op;
        if(op==0x12u)mask&=12u;
        unsigned read_mask=mask;
        if(cmp)read_mask=12u;
        else if(mask && dot_product)read_mask=op==0x01u?14u:15u;
        else if(mask && reciprocal_op)read_mask=8u;
        if(!read_mask)continue;
        shader_operand left=shader_source(program,input,temporary,defined,source),right,extra;
        if(!unary)right=shader_source(program,input,temporary,defined,second);
        if(mad)extra=shader_source(program,input,temporary,defined,(word>>5)&31u);
        for(unsigned lane=0;lane<4;++lane)if(read_mask&(8u>>lane)) {
            unsigned selected=(pattern>>(11u-lane*2u))&3u;
            uint32_t a=0,b=0,addend=0;
            ctr_shader_result status=CTR_SHADER_OK;
            if(homogeneous && lane==3u)a=UINT32_C(0x3f800000);
            else status=read_lane(left,selected,(pattern&16u)!=0,program->round_f24,&a);
            if(status==CTR_SHADER_OK && !unary) {
                selected=(pattern>>(20u-lane*2u))&3u;
                status=read_lane(right,selected,(pattern&(1u<<13))!=0,program->round_f24,&b);
            }
            if(status==CTR_SHADER_OK && mad) {
                selected=(pattern>>(29u-lane*2u))&3u;
                status=read_lane(extra,selected,(pattern&(1u<<22))!=0,program->round_f24,&addend);
            }
            if(status==CTR_SHADER_UNDEFINED && program->round_f24 && !cmp && op!=0x12u) {
                unknown|=8u>>lane;
                continue;
            }
            if(status!=CTR_SHADER_OK)return status;
            if(op==0x12u || op==0x13u)moved[lane]=a;
            else if(reciprocal_op) {
                bool ok=op==0x0eu?reciprocal(a,program->round_f24,&dot):reciprocal_sqrt(a,&dot);
                if(!ok)return CTR_SHADER_NUMERIC;
            } else if(cmp)compare[lane]=comparison((word>>(24u-lane*3u))&7u,value(a),value(b));
            else if(op==0x0cu)moved[lane]=value(a)>value(b)?a:b;
            else if(mad || dot_product) {
                uint32_t product;
                if(!arithmetic(0x08u,a,b,program->round_f24,&product) ||
                   !arithmetic(0x00u,product,mad?addend:dot,program->round_f24,mad?&moved[lane]:&dot))
                    return CTR_SHADER_NUMERIC;
            } else if(!arithmetic(op,a,b,program->round_f24,&moved[lane]))return CTR_SHADER_NUMERIC;
        }
        if(cmp) { compared=true;continue; }
        if(op==0x12u) {
            for(unsigned lane=0;lane<2;++lane)if(mask&(8u>>lane)) {
                float component=value(moved[lane]);
                address[lane]=component>-129.0f && component<128.0f?(int)component:0;
                address_written|=1u<<lane;
            }
            continue;
        }
        bool scalar=dot_product || reciprocal_op;
        if(scalar && unknown)unknown=mask;
        /* Undefined data can flow to unused outputs, with its lane validity
         * cleared. It cannot supply an address, predicate or raster input. */
        for(unsigned lane=0;lane<4;++lane)if(mask&(8u>>lane)) {
            uint8_t *valid=dest<16u?&result.written[dest]:&defined[dest-16u];
            if(unknown&(8u>>lane)) { *valid&=(uint8_t)~(1u<<lane);continue; }
            uint32_t written=scalar?dot:moved[lane];
            if(dest<16u) {
                memcpy(&result.values[dest][lane],&written,sizeof(written));
            } else temporary[dest-16u][lane]=written;
            *valid|=(uint8_t)(1u<<lane);
        }
    }
}
enum {
    FAST_ERR=0, FAST_NOP, FAST_END, FAST_CALL, FAST_CALLC, FAST_JMPC, FAST_JMPU,
    FAST_IFU, FAST_IFC, FAST_LOOP, FAST_MASK_SKIP, FAST_CMP, FAST_MOVA, FAST_MOV,
    FAST_ADD, FAST_MUL, FAST_MAX, FAST_MAD, FAST_DP3, FAST_DP4, FAST_DPH,
    FAST_RCP, FAST_RSQ
};
enum {
    FAST_UNARY=1u, FAST_NEG0=2u, FAST_NEG1=4u, FAST_NEG2=8u,
    FAST_REL_SECOND=16u, FAST_POL=32u, FAST_DESC_ERR=64u
};
static float bits_to_float(uint32_t b) { float v;memcpy(&v,&b,sizeof(v));return v; }
static uint32_t float_to_bits(float v) { uint32_t b;memcpy(&b,&v,sizeof(b));return b; }
static bool fast_is_nan(float v) { return v!=v; }
static bool fast_cond(uint8_t bits,const bool compared[2]) {
    unsigned mode=bits&3u;
    bool x=compared[0]==((bits>>3)&1u),y=compared[1]==((bits>>2)&1u);
    return mode==0u?(x||y):mode==1u?(x&&y):mode==2u?x:y;
}
static ctr_shader_result fast_read_slow(const ctr_shader_fast *fast,float r[32][4],
                                        const uint8_t defined[32],unsigned src,unsigned lane,
                                        bool neg,float *out) {
    float v;
    if(src<32u) {
        if(!(defined[src]&(1u<<lane)))return CTR_SHADER_UNDEFINED;
        if(!(defined[src]&(1u<<(lane+4u))))return CTR_SHADER_NUMERIC;
        v=r[src][lane];
    } else if(src<32u+CTR_SHADER_UNIFORMS) {
        unsigned idx=src-32u;
        if(!(fast->uniform_defined[idx]&(1u<<lane)))return CTR_SHADER_UNDEFINED;
        if(!(fast->uniform_numeric[idx]&(1u<<lane)))return CTR_SHADER_NUMERIC;
        v=fast->uniforms[idx][lane];
    } else {
        v=1.0f;
    }
    if(neg && (v!=0.0f || (float_to_bits(v)&UINT32_C(0x80000000))))v=-v;
    *out=v;
    return CTR_SHADER_OK;
}
bool ctr_shader_prepare_fast(const ctr_shader_program *program,ctr_shader_fast *out) {
    if(!program || !out || !span(program,sizeof(*program)) || !span(out,sizeof(*out)))return false;
    if(overlap(out,sizeof(*out),program,sizeof(*program)))return false;
    if(!program->round_f24 || program->entry>=CTR_SHADER_MAX_CODE)return false;
    out->entry=program->entry;
    out->boolean_uniforms=program->boolean_uniforms;
    out->boolean_defined=program->boolean_defined;
    out->integer_defined=program->integer_defined;
    memcpy(out->integer_uniforms,program->integer_uniforms,sizeof(out->integer_uniforms));
    memcpy(out->uniform_defined,program->uniform_defined,sizeof(out->uniform_defined));
    for(unsigned i=0;i<CTR_SHADER_UNIFORMS;++i) {
        uint8_t num=0;
        for(unsigned lane=0;lane<4u;++lane) {
            if(program->uniform_defined[i]&(1u<<lane)) {
                uint32_t b=program->uniforms[i][lane];
                if(round_input(&b)) { num|=(uint8_t)(1u<<lane);out->uniforms[i][lane]=bits_to_float(b); }
                else out->uniforms[i][lane]=0.0f;
            } else out->uniforms[i][lane]=0.0f;
        }
        out->uniform_numeric[i]=num;
    }
    for(unsigned pc=0;pc<CTR_SHADER_MAX_CODE;++pc) {
        ctr_shader_fast_insn *insn=&out->insns[pc];
        memset(insn,0,sizeof(*insn));
        if(!program->code_defined[pc]) { insn->op=FAST_ERR;insn->err=CTR_SHADER_UNDEFINED;continue; }
        uint32_t word=program->code[pc];unsigned op=word>>26;
        if(op==0x21u || op==0x22u) {
            if(word&UINT32_C(0x03ffffff)) { insn->op=FAST_ERR;insn->err=CTR_SHADER_UNSUPPORTED; }
            else insn->op=(uint8_t)(op==0x22u?FAST_END:FAST_NOP);
            continue;
        }
        if(op==0x24u || op==0x25u) {
            unsigned target=(word>>10)&4095u,length=word&255u;
            if(target>=CTR_SHADER_MAX_CODE || length>CTR_SHADER_MAX_CODE-target) {
                insn->op=FAST_ERR;insn->err=CTR_SHADER_RANGE;continue;
            }
            insn->op=(uint8_t)(op==0x24u?FAST_CALL:FAST_CALLC);
            insn->target=(uint16_t)target;insn->extra=(uint16_t)length;
            unsigned mode=(word>>22)&3u,refs=((word>>24)&3u);
            insn->reg_idx=(uint8_t)(mode|(refs<<2));
            continue;
        }
        if(op==0x2cu || op==0x2du) {
            unsigned target=(word>>10)&4095u;
            if(target>=CTR_SHADER_MAX_CODE) { insn->op=FAST_ERR;insn->err=CTR_SHADER_RANGE;continue; }
            insn->target=(uint16_t)target;
            if(op==0x2du) {
                insn->op=FAST_JMPU;insn->reg_idx=(uint8_t)((word>>22)&15u);
                if(word&1u)insn->flags=FAST_POL;
            } else {
                insn->op=FAST_JMPC;
                unsigned mode=(word>>22)&3u,refs=(word>>24)&3u;
                insn->reg_idx=(uint8_t)(mode|(refs<<2));
            }
            continue;
        }
        if(op>=0x27u && op<=0x29u) {
            unsigned target=(word>>10)&4095u;
            if(target<pc+1u || target>=CTR_SHADER_MAX_CODE) { insn->op=FAST_ERR;insn->err=CTR_SHADER_RANGE;continue; }
            insn->target=(uint16_t)target;
            if(op==0x29u) {
                unsigned uniform=(word>>22)&15u;
                if(uniform>=4u) { insn->op=FAST_ERR;insn->err=CTR_SHADER_UNSUPPORTED; }
                else { insn->op=FAST_LOOP;insn->reg_idx=(uint8_t)uniform; }
            } else {
                unsigned join=target+(word&255u);
                if(join>CTR_SHADER_MAX_CODE) { insn->op=FAST_ERR;insn->err=CTR_SHADER_RANGE;continue; }
                insn->extra=(uint16_t)join;
                if(op==0x27u) { insn->op=FAST_IFU;insn->reg_idx=(uint8_t)((word>>22)&15u); }
                else {
                    insn->op=FAST_IFC;
                    unsigned mode=(word>>22)&3u,refs=(word>>24)&3u;
                    insn->reg_idx=(uint8_t)(mode|(refs<<2));
                }
            }
            continue;
        }
        bool cmp=op==0x2eu || op==0x2fu,mad=op>=0x38u;
        bool inverted=op==0x18u,homogeneous=op==0x03u || inverted;
        bool dot_product=op==0x02u || op==0x01u || homogeneous;
        bool reciprocal_op=op==0x0eu || op==0x0fu;
        if(!cmp && !mad && !dot_product && op!=0x12u && op!=0x13u && op!=0x00u &&
           op!=0x08u && op!=0x0cu && !reciprocal_op) {
            insn->op=FAST_ERR;insn->err=CTR_SHADER_UNSUPPORTED;continue;
        }
        unsigned index=(word>>(mad?22u:19u))&3u;
        unsigned descriptor=word&(mad?31u:127u),dest=(word>>(mad?24u:21u))&31u;
        unsigned src0=(word>>(mad?17u:12u))&(mad?31u:127u),src1=(word>>(mad?10u:7u))&(mad?127u:31u);
        if(inverted) { src0=(word>>14)&31u;src1=(word>>7)&127u; }
        insn->rel_index=(uint8_t)index;
        if(mad || inverted)insn->flags|=FAST_REL_SECOND;
        if(!program->descriptor_defined[descriptor]) {
            insn->op=FAST_ERR;insn->err=CTR_SHADER_UNDEFINED;insn->flags|=FAST_DESC_ERR;continue;
        }
        uint32_t pattern=program->descriptors[descriptor];
        if(pattern&UINT32_C(0x80000000)) {
            insn->op=FAST_ERR;insn->err=CTR_SHADER_UNSUPPORTED;insn->flags|=FAST_DESC_ERR;continue;
        }
        unsigned mask=pattern&15u;
        bool unary=op==0x12u || op==0x13u || reciprocal_op;
        if(op==0x12u)mask&=12u;
        unsigned read_mask=mask;
        if(cmp)read_mask=12u;
        else if(mask && dot_product)read_mask=op==0x01u?14u:15u;
        else if(mask && reciprocal_op)read_mask=8u;
        if(!read_mask) { insn->op=FAST_MASK_SKIP;continue; }
        insn->dest=(uint8_t)dest;
        insn->src[0]=(uint8_t)src0;insn->src[1]=(uint8_t)src1;insn->src[2]=(uint8_t)((word>>5)&31u);
        insn->mask=(uint8_t)mask;insn->read_mask=(uint8_t)read_mask;
        insn->wmask=(uint8_t)(((mask&8u)>>3)|((mask&4u)>>1)|((mask&2u)<<1)|((mask&1u)<<3));
        if(unary)insn->flags|=FAST_UNARY;
        if(pattern&16u)insn->flags|=FAST_NEG0;
        if(pattern&(1u<<13))insn->flags|=FAST_NEG1;
        if(pattern&(1u<<22))insn->flags|=FAST_NEG2;
        insn->cmp_op[0]=(uint8_t)((word>>24)&7u);
        insn->cmp_op[1]=(uint8_t)((word>>21)&7u);
        for(unsigned lane=0;lane<4u;++lane) {
            uint8_t w0=(uint8_t)((pattern>>(11u-lane*2u))&3u);
            uint8_t w1=(uint8_t)((pattern>>(20u-lane*2u))&3u);
            uint8_t w2=(uint8_t)((pattern>>(29u-lane*2u))&3u);
            insn->swiz[0][lane]=w0;insn->swiz[1][lane]=w1;insn->swiz[2][lane]=w2;
            if(read_mask&(8u>>lane)) {
                if(!(homogeneous && lane==3u))insn->src_mask[0]|=(uint8_t)(1u<<w0);
                if(!unary)insn->src_mask[1]|=(uint8_t)(1u<<w1);
                if(mad)insn->src_mask[2]|=(uint8_t)(1u<<w2);
            }
        }
        unsigned rel_op=(mad || inverted)?1u:0u;
        for(unsigned k=0;k<(mad?3u:unary?1u:2u);++k) {
            if(index && k==rel_op && insn->src[k]>=32u)continue;
            unsigned s=insn->src[k];
            if(s>=32u+CTR_SHADER_UNIFORMS)insn->safe_srcs|=(uint8_t)(1u<<k);
            else if(s>=32u) {
                unsigned idx=s-32u;
                uint8_t ok=out->uniform_defined[idx]&out->uniform_numeric[idx];
                if((ok&insn->src_mask[k])==insn->src_mask[k])insn->safe_srcs|=(uint8_t)(1u<<k);
            }
        }
        if(cmp)insn->op=FAST_CMP;
        else if(op==0x12u)insn->op=FAST_MOVA;
        else if(op==0x13u)insn->op=FAST_MOV;
        else if(op==0x00u)insn->op=FAST_ADD;
        else if(op==0x08u)insn->op=FAST_MUL;
        else if(op==0x0cu)insn->op=FAST_MAX;
        else if(mad)insn->op=FAST_MAD;
        else if(op==0x01u)insn->op=FAST_DP3;
        else if(op==0x02u)insn->op=FAST_DP4;
        else if(homogeneous)insn->op=FAST_DPH;
        else if(op==0x0eu)insn->op=FAST_RCP;
        else insn->op=FAST_RSQ;
    }
    return true;
}
ctr_shader_result ctr_shader_execute_fast(const ctr_shader_fast *fast,
                                          const ctr_vertex *input,
                                          ctr_shader_output *output) {
    if(!fast || !input || !output)return CTR_SHADER_ARGUMENT;
    if(!span(fast,sizeof(*fast)) || !span(input,sizeof(*input)) || !span(output,sizeof(*output)))return CTR_SHADER_OVERFLOW;
    if(overlap(output,sizeof(*output),fast,sizeof(*fast)) || overlap(output,sizeof(*output),input,sizeof(*input)))return CTR_SHADER_ALIAS;
    if(fast->entry>=CTR_SHADER_MAX_CODE)return CTR_SHADER_RANGE;
    float r[32][4];uint8_t defined[32];
    for(unsigned i=0;i<16u;++i) {
        if(input->enabled&(1u<<i)) {
            uint8_t ok=0;
            for(unsigned k=0;k<4u;++k) {
                float v=input->input[i][k];
                uint32_t b=float_to_bits(v);
                uint32_t exp=b&UINT32_C(0x7f800000);
                if(exp && exp!=UINT32_C(0x7f800000) && !(b&0xffu)) {
                    r[i][k]=v;ok|=(uint8_t)(1u<<k);
                } else {
                    if(round_input(&b))ok|=(uint8_t)(1u<<k);
                    r[i][k]=bits_to_float(b);
                }
            }
            /* Check if any enabled input lane is non-finite NaN -> record per-lane numeric ok in high nibble! */
            defined[i]=(uint8_t)(15u|(ok<<4));
        } else defined[i]=0xf0u;
        defined[16u+i]=0xf0u; /* Temps start undefined in low nibble, always numeric-ok in high nibble. */
    }
    static const float ones[4]={1.0f,1.0f,1.0f,1.0f};
    int address[3]={0};unsigned address_written=0;
    bool compare[2]={false,false},compared=false;
    struct { unsigned alternate,join; } branches[8];unsigned branch_count=0;
    struct { unsigned first,after,resume,branches; } calls[8];unsigned call_count=0;
    unsigned loop_first=0,loop_after=0,loop_count=0,loop_stride=0,loop_owner=0;
    ctr_shader_output result;
    memset(result.written,0,sizeof(result.written));
    result.instructions=0;
    for(unsigned pc=fast->entry;;) {
        if(branch_count | call_count | loop_count) {
            for(;;) {
                unsigned enclosing=call_count?calls[call_count-1u].branches:0u;
                if(branch_count>enclosing && pc==branches[branch_count-1u].alternate)
                    pc=branches[--branch_count].join;
                if(loop_count && call_count==loop_owner && pc==loop_after) {
                    address[2]+=(int)loop_stride;
                    if(--loop_count)pc=loop_first;
                }
                if(!call_count)break;
                if(pc!=calls[call_count-1u].after) {
                    if(pc<calls[call_count-1u].first || pc>calls[call_count-1u].after)return CTR_SHADER_RANGE;
                    break;
                }
                if(branch_count!=enclosing || (loop_count && call_count==loop_owner))return CTR_SHADER_RANGE;
                pc=calls[--call_count].resume;
            }
        }
        if(pc>=CTR_SHADER_MAX_CODE)return CTR_SHADER_NO_END;
        const ctr_shader_fast_insn *insn=&fast->insns[pc++];
        if(++result.instructions>CTR_SHADER_MAX_STEPS)return CTR_SHADER_LIMIT;
        if(insn->rel_index && !(address_written&(1u<<(insn->rel_index-1u))))return CTR_SHADER_UNSUPPORTED;
        unsigned op=insn->op;
        if(op==FAST_END) {
            output->instructions=result.instructions;
            memcpy(output->written,result.written,sizeof(result.written));
            for(unsigned i=0;i<16u;++i)if(result.written[i])
                for(unsigned k=0;k<4u;++k)if(result.written[i]&(1u<<k))
                    output->values[i][k]=result.values[i][k];
            return CTR_SHADER_OK;
        }
        if(op==FAST_NOP || op==FAST_MASK_SKIP)continue;
        if(op<=FAST_LOOP) {
            switch(op) {
            case FAST_ERR:return (ctr_shader_result)insn->err;
            case FAST_CALLC:
                if(!compared)return CTR_SHADER_UNDEFINED;
                if(!fast_cond(insn->reg_idx,compare))continue;
                /* fallthrough */
            case FAST_CALL:
                if(!insn->extra)continue;
                if(call_count==8u)return CTR_SHADER_LIMIT;
                calls[call_count].first=insn->target;calls[call_count].after=insn->target+insn->extra;
                calls[call_count].resume=pc;calls[call_count++].branches=branch_count;
                pc=insn->target;continue;
            case FAST_JMPC:
                if(!compared)return CTR_SHADER_UNDEFINED;
                if(fast_cond(insn->reg_idx,compare))pc=insn->target;
                continue;
            case FAST_JMPU:
                if(!fast->boolean_defined)return CTR_SHADER_UNDEFINED;
                if(((fast->boolean_uniforms&(1u<<insn->reg_idx))!=0)!=((insn->flags&FAST_POL)!=0))pc=insn->target;
                continue;
            case FAST_IFU: {
                if(!fast->boolean_defined)return CTR_SHADER_UNDEFINED;
                bool taken=(fast->boolean_uniforms&(1u<<insn->reg_idx))!=0;
                if(!taken)pc=insn->target;
                else if(insn->extra!=insn->target) {
                    if(branch_count==8u)return CTR_SHADER_LIMIT;
                    branches[branch_count].alternate=insn->target;branches[branch_count++].join=insn->extra;
                }
                continue;
            }
            case FAST_IFC: {
                if(!compared)return CTR_SHADER_UNDEFINED;
                bool taken=fast_cond(insn->reg_idx,compare);
                if(!taken)pc=insn->target;
                else if(insn->extra!=insn->target) {
                    if(branch_count==8u)return CTR_SHADER_LIMIT;
                    branches[branch_count].alternate=insn->target;branches[branch_count++].join=insn->extra;
                }
                continue;
            }
            case FAST_LOOP: {
                if(loop_count)return CTR_SHADER_UNSUPPORTED;
                if(!(fast->integer_defined&(1u<<insn->reg_idx)))return CTR_SHADER_UNDEFINED;
                uint32_t inputs=fast->integer_uniforms[insn->reg_idx];
                loop_first=pc;loop_after=insn->target+1u;loop_count=(inputs&255u)+1u;loop_stride=(inputs>>16)&255u;
                loop_owner=call_count;address[2]=(int)((inputs>>8)&255u);address_written|=4u;
                continue;
            }
            default:break;
            }
        }
        unsigned s0=insn->src[0],s1=insn->src[1],s2=insn->src[2];
        bool all_safe=true;
        if(insn->rel_index) {
            int off=address[insn->rel_index-1u];
            if(off<-128 || off>127)off=0;
            if(insn->flags&FAST_REL_SECOND) { if(s1>=32u) { s1=32u+((s1-32u+(unsigned)off)&127u);all_safe=false; } }
            else if(s0>=32u) { s0=32u+((s0-32u+(unsigned)off)&127u);all_safe=false; }
        }
        if(all_safe) {
            uint8_t m0=insn->src_mask[0];
            if(!(insn->safe_srcs&1u) && (s0>=32u || (defined[s0]&(m0|(m0<<4)))!=(m0|(m0<<4))))all_safe=false;
            if(all_safe && !(insn->flags&FAST_UNARY)) {
                uint8_t m1=insn->src_mask[1];
                if(!(insn->safe_srcs&2u) && (s1>=32u || (defined[s1]&(m1|(m1<<4)))!=(m1|(m1<<4))))all_safe=false;
            }
            if(all_safe && op==FAST_MAD) {
                uint8_t m2=insn->src_mask[2];
                if(!(insn->safe_srcs&4u) && (s2>=32u || (defined[s2]&(m2|(m2<<4)))!=(m2|(m2<<4))))all_safe=false;
            }
        }
        const float *p0=s0<32u?r[s0]:(s0<32u+CTR_SHADER_UNIFORMS?fast->uniforms[s0-32u]:ones);
        const float *p1=s1<32u?r[s1]:(s1<32u+CTR_SHADER_UNIFORMS?fast->uniforms[s1-32u]:ones);
        unsigned dest=insn->dest;
        if(all_safe) {
            float m0=(insn->flags&FAST_NEG0)?-1.0f:1.0f,m1=(insn->flags&FAST_NEG1)?-1.0f:1.0f;
            float *dst=dest<16u?result.values[dest]:r[dest];
            uint8_t mask=insn->mask;
            switch(op) {
            case FAST_MOV:
                if(mask&8u)dst[0]=p0[insn->swiz[0][0]]*m0;
                if(mask&4u)dst[1]=p0[insn->swiz[0][1]]*m0;
                if(mask&2u)dst[2]=p0[insn->swiz[0][2]]*m0;
                if(mask&1u)dst[3]=p0[insn->swiz[0][3]]*m0;
                if(dest<16u)result.written[dest]|=insn->wmask; else defined[dest]|=insn->wmask;
                continue;
            case FAST_ADD: {
                float v0=0,v1=0,v2=0,v3=0;
                if(mask&8u) { v0=p0[insn->swiz[0][0]]*m0+p1[insn->swiz[1][0]]*m1;if(fast_is_nan(v0))return CTR_SHADER_NUMERIC; }
                if(mask&4u) { v1=p0[insn->swiz[0][1]]*m0+p1[insn->swiz[1][1]]*m1;if(fast_is_nan(v1))return CTR_SHADER_NUMERIC; }
                if(mask&2u) { v2=p0[insn->swiz[0][2]]*m0+p1[insn->swiz[1][2]]*m1;if(fast_is_nan(v2))return CTR_SHADER_NUMERIC; }
                if(mask&1u) { v3=p0[insn->swiz[0][3]]*m0+p1[insn->swiz[1][3]]*m1;if(fast_is_nan(v3))return CTR_SHADER_NUMERIC; }
                if(mask&8u)dst[0]=v0;
                if(mask&4u)dst[1]=v1;
                if(mask&2u)dst[2]=v2;
                if(mask&1u)dst[3]=v3;
                if(dest<16u)result.written[dest]|=insn->wmask; else defined[dest]|=insn->wmask;
                continue;
            }
            case FAST_MUL: {
                float v[4]={0};bool fallback=false;
                for(unsigned l=0;l<4u;++l)if(mask&(8u>>l)) {
                    float a=p0[insn->swiz[0][l]]*m0,b=p1[insn->swiz[1][l]]*m1;
                    float res=a*b;
                    if(fast_is_nan(res)) { if(a==0.0f || b==0.0f)res=0.0f; else { fallback=true;break; } }
                    else if(res==0.0f)res=0.0f;
                    v[l]=res;
                }
                if(!fallback) {
                    if(mask&8u)dst[0]=v[0];
                    if(mask&4u)dst[1]=v[1];
                    if(mask&2u)dst[2]=v[2];
                    if(mask&1u)dst[3]=v[3];
                    if(dest<16u)result.written[dest]|=insn->wmask; else defined[dest]|=insn->wmask;
                    continue;
                }
                return CTR_SHADER_NUMERIC;
            }
            case FAST_MAD: {
                const float *p2=s2<32u?r[s2]:(s2<32u+CTR_SHADER_UNIFORMS?fast->uniforms[s2-32u]:ones);
                float m2=(insn->flags&FAST_NEG2)?-1.0f:1.0f;
                float v[4]={0};
                for(unsigned l=0;l<4u;++l)if(mask&(8u>>l)) {
                    float a=p0[insn->swiz[0][l]]*m0,b=p1[insn->swiz[1][l]]*m1,c=p2[insn->swiz[2][l]]*m2;
                    float prod=a*b;
                    if(fast_is_nan(prod)) { if(a==0.0f || b==0.0f)prod=0.0f; else return CTR_SHADER_NUMERIC; }
                    else if(prod==0.0f)prod=0.0f;
                    float res=prod+c;
                    if(fast_is_nan(res))return CTR_SHADER_NUMERIC;
                    v[l]=res;
                }
                if(mask&8u)dst[0]=v[0];
                if(mask&4u)dst[1]=v[1];
                if(mask&2u)dst[2]=v[2];
                if(mask&1u)dst[3]=v[3];
                if(dest<16u)result.written[dest]|=insn->wmask; else defined[dest]|=insn->wmask;
                continue;
            }
            case FAST_DP3: {
                float d=0.0f;
                for(unsigned l=0;l<3u;++l) {
                    float a=p0[insn->swiz[0][l]]*m0,b=p1[insn->swiz[1][l]]*m1,prod=a*b;
                    if(fast_is_nan(prod)) { if(a==0.0f || b==0.0f)prod=0.0f; else return CTR_SHADER_NUMERIC; }
                    else if(prod==0.0f)prod=0.0f;
                    d+=prod;
                }
                if(fast_is_nan(d))return CTR_SHADER_NUMERIC;
                if(mask&8u)dst[0]=d;
                if(mask&4u)dst[1]=d;
                if(mask&2u)dst[2]=d;
                if(mask&1u)dst[3]=d;
                if(dest<16u)result.written[dest]|=insn->wmask; else defined[dest]|=insn->wmask;
                continue;
            }
            case FAST_DP4: {
                float d=0.0f;
                for(unsigned l=0;l<4u;++l) {
                    float a=p0[insn->swiz[0][l]]*m0,b=p1[insn->swiz[1][l]]*m1,prod=a*b;
                    if(fast_is_nan(prod)) { if(a==0.0f || b==0.0f)prod=0.0f; else return CTR_SHADER_NUMERIC; }
                    else if(prod==0.0f)prod=0.0f;
                    d+=prod;
                }
                if(fast_is_nan(d))return CTR_SHADER_NUMERIC;
                if(mask&8u)dst[0]=d;
                if(mask&4u)dst[1]=d;
                if(mask&2u)dst[2]=d;
                if(mask&1u)dst[3]=d;
                if(dest<16u)result.written[dest]|=insn->wmask; else defined[dest]|=insn->wmask;
                continue;
            }
            case FAST_DPH: {
                float d=0.0f;
                for(unsigned l=0;l<4u;++l) {
                    float a=l==3u?1.0f:p0[insn->swiz[0][l]]*m0,b=p1[insn->swiz[1][l]]*m1,prod=a*b;
                    if(fast_is_nan(prod)) { if(a==0.0f || b==0.0f)prod=0.0f; else return CTR_SHADER_NUMERIC; }
                    else if(prod==0.0f)prod=0.0f;
                    d+=prod;
                }
                if(fast_is_nan(d))return CTR_SHADER_NUMERIC;
                if(mask&8u)dst[0]=d;
                if(mask&4u)dst[1]=d;
                if(mask&2u)dst[2]=d;
                if(mask&1u)dst[3]=d;
                if(dest<16u)result.written[dest]|=insn->wmask; else defined[dest]|=insn->wmask;
                continue;
            }
            case FAST_MAX: {
                float v0=0,v1=0,v2=0,v3=0;
                if(mask&8u) { float a=p0[insn->swiz[0][0]]*m0,b=p1[insn->swiz[1][0]]*m1;v0=a>b?a:b; }
                if(mask&4u) { float a=p0[insn->swiz[0][1]]*m0,b=p1[insn->swiz[1][1]]*m1;v1=a>b?a:b; }
                if(mask&2u) { float a=p0[insn->swiz[0][2]]*m0,b=p1[insn->swiz[1][2]]*m1;v2=a>b?a:b; }
                if(mask&1u) { float a=p0[insn->swiz[0][3]]*m0,b=p1[insn->swiz[1][3]]*m1;v3=a>b?a:b; }
                if(mask&8u)dst[0]=v0;
                if(mask&4u)dst[1]=v1;
                if(mask&2u)dst[2]=v2;
                if(mask&1u)dst[3]=v3;
                if(dest<16u)result.written[dest]|=insn->wmask; else defined[dest]|=insn->wmask;
                continue;
            }
            case FAST_CMP: {
                for(unsigned l=0;l<2u;++l) {
                    float a=p0[insn->swiz[0][l]]*m0,b=p1[insn->swiz[1][l]]*m1;
                    compare[l]=comparison(insn->cmp_op[l],a,b);
                }
                compared=true;continue;
            }
            case FAST_MOVA: {
                if(mask&8u) { float c=p0[insn->swiz[0][0]]*m0;address[0]=c>-129.0f && c<128.0f?(int)c:0;address_written|=1u; }
                if(mask&4u) { float c=p0[insn->swiz[0][1]]*m0;address[1]=c>-129.0f && c<128.0f?(int)c:0;address_written|=2u; }
                continue;
            }
            case FAST_RCP: {
                float a=p0[insn->swiz[0][0]];
                if(insn->flags&FAST_NEG0 && (a!=0.0f || (float_to_bits(a)&UINT32_C(0x80000000))))a=-a;
                float d=1.0f/a;
                if(mask&8u)dst[0]=d;
                if(mask&4u)dst[1]=d;
                if(mask&2u)dst[2]=d;
                if(mask&1u)dst[3]=d;
                if(dest<16u)result.written[dest]|=insn->wmask; else defined[dest]|=insn->wmask;
                continue;
            }
            case FAST_RSQ: {
                float a=p0[insn->swiz[0][0]];
                if(insn->flags&FAST_NEG0 && (a!=0.0f || (float_to_bits(a)&UINT32_C(0x80000000))))a=-a;
                uint32_t abits=float_to_bits(a);float d;
                if(!(abits&UINT32_C(0x7fffffff)))d=bits_to_float(abits|UINT32_C(0x7f800000));
                else if(abits==UINT32_C(0x7f800000))d=0.0f;
                else if(abits&UINT32_C(0x80000000))return CTR_SHADER_NUMERIC;
                else {
                    float y=bits_to_float(UINT32_C(0x5f375a86)-(abits>>1)),half=0.5f*a;
                    y=y*(1.5f-half*y*y);y=y*(1.5f-half*y*y);y=y*(1.5f-half*y*y);
                    d=y;
                }
                if(mask&8u)dst[0]=d;
                if(mask&4u)dst[1]=d;
                if(mask&2u)dst[2]=d;
                if(mask&1u)dst[3]=d;
                if(dest<16u)result.written[dest]|=insn->wmask; else defined[dest]|=insn->wmask;
                continue;
            }
            default:break;
            }
        }
        float moved[4]={0.0f,0.0f,0.0f,0.0f},dot=0.0f;
        unsigned unknown=0,read_mask=insn->read_mask,mask=insn->mask;
        bool unary=(insn->flags&FAST_UNARY)!=0,cmp=op==FAST_CMP;
        bool neg0=(insn->flags&FAST_NEG0)!=0,neg1=(insn->flags&FAST_NEG1)!=0,neg2=(insn->flags&FAST_NEG2)!=0;
        for(unsigned lane=0;lane<4u;++lane)if(read_mask&(8u>>lane)) {
            float a=0.0f,b=0.0f,c=0.0f;ctr_shader_result st=CTR_SHADER_OK;
            if(op==FAST_DPH && lane==3u)a=1.0f;
            else st=fast_read_slow(fast,r,defined,s0,insn->swiz[0][lane],neg0,&a);
            if(st==CTR_SHADER_OK && !unary)
                st=fast_read_slow(fast,r,defined,s1,insn->swiz[1][lane],neg1,&b);
            if(st==CTR_SHADER_OK && op==FAST_MAD)
                st=fast_read_slow(fast,r,defined,s2,insn->swiz[2][lane],neg2,&c);
            if(st==CTR_SHADER_UNDEFINED && !cmp && op!=FAST_MOVA) { unknown|=8u>>lane;continue; }
            if(st!=CTR_SHADER_OK)return st;
            switch(op) {
            case FAST_MOVA:case FAST_MOV:moved[lane]=a;break;
            case FAST_CMP:compare[lane]=comparison(insn->cmp_op[lane],a,b);break;
            case FAST_ADD: { float res=a+b;if(fast_is_nan(res))return CTR_SHADER_NUMERIC;moved[lane]=res;break; }
            case FAST_MUL: { float res=(a==0.0f || b==0.0f)?0.0f:a*b;if(fast_is_nan(res))return CTR_SHADER_NUMERIC;moved[lane]=res;break; }
            case FAST_MAX:moved[lane]=a>b?a:b;break;
            case FAST_MAD: {
                float p=(a==0.0f || b==0.0f)?0.0f:a*b;
                float res=p+c;if(fast_is_nan(res))return CTR_SHADER_NUMERIC;
                moved[lane]=res;break;
            }
            case FAST_DP3:case FAST_DP4:case FAST_DPH: {
                float p=(a==0.0f || b==0.0f)?0.0f:a*b;
                float res=p+dot;if(fast_is_nan(res))return CTR_SHADER_NUMERIC;
                dot=res;break;
            }
            case FAST_RCP:dot=1.0f/a;break;
            case FAST_RSQ: {
                uint32_t abits=float_to_bits(a);
                if(!(abits&UINT32_C(0x7fffffff)))dot=bits_to_float(abits|UINT32_C(0x7f800000));
                else if(abits==UINT32_C(0x7f800000))dot=0.0f;
                else if(abits&UINT32_C(0x80000000))return CTR_SHADER_NUMERIC;
                else {
                    float y=bits_to_float(UINT32_C(0x5f375a86)-(abits>>1)),half=0.5f*a;
                    y=y*(1.5f-half*y*y);y=y*(1.5f-half*y*y);y=y*(1.5f-half*y*y);
                    dot=y;
                }
                break;
            }
            default:break;
            }
        }
        if(cmp) { compared=true;continue; }
        if(op==FAST_MOVA) {
            for(unsigned lane=0;lane<2u;++lane)if(mask&(8u>>lane)) {
                float comp=moved[lane];
                address[lane]=comp>-129.0f && comp<128.0f?(int)comp:0;
                address_written|=1u<<lane;
            }
            continue;
        }
        bool scalar=op==FAST_DP3 || op==FAST_DP4 || op==FAST_DPH || op==FAST_RCP || op==FAST_RSQ;
        if(scalar && unknown)unknown=mask;
        for(unsigned lane=0;lane<4u;++lane)if(mask&(8u>>lane)) {
            uint8_t *valid=dest<16u?&result.written[dest]:&defined[dest];
            if(unknown&(8u>>lane)) { *valid&=(uint8_t)~(1u<<lane);continue; }
            float w=scalar?dot:moved[lane];
            if(dest<16u)result.values[dest][lane]=w;
            else r[dest][lane]=w;
            *valid|=(uint8_t)(1u<<lane);
        }
    }
}
const char *ctr_shader_result_name(ctr_shader_result result) {
    static const char *const names[]={"ok","argument","overflow","alias","range",
        "unsupported-instruction-or-state","undefined-source-or-code","unsupported-numeric-value","missing-end","step-limit"};
    return (unsigned)result<sizeof(names)/sizeof(names[0])?names[result]:"unknown";
}
