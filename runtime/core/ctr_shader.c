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
const char *ctr_shader_result_name(ctr_shader_result result) {
    static const char *const names[]={"ok","argument","overflow","alias","range",
        "unsupported-instruction-or-state","undefined-source-or-code","unsupported-numeric-value","missing-end","step-limit"};
    return (unsigned)result<sizeof(names)/sizeof(names[0])?names[result]:"unknown";
}
