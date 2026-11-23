/* Scalar VFPv2 semantics shared by the interpreter and original AOT. */
#include "ctr_cpu.h"
#include <errno.h>
#include <fenv.h>
#include <float.h>
#include <math.h>
#include <string.h>

#if defined(__clang__)
#pragma STDC FENV_ACCESS ON
#pragma clang fp contract(off)
#endif
_Static_assert(sizeof(float)==4 && sizeof(double)==8 && FLT_RADIX==2 &&
    FLT_MANT_DIG==24 && DBL_MANT_DIG==53,"VFP requires IEEE binary32/binary64");

static uint64_t fp_read(const uint32_t vfp[32],unsigned word,bool wide) {
    return vfp[word] | (wide?(uint64_t)vfp[word+1]<<32:0);
}
static void fp_write(uint32_t vfp[32],unsigned word,bool wide,uint64_t value) {
    vfp[word]=(uint32_t)value;
    if(wide)vfp[word+1]=(uint32_t)(value>>32);
}
static uint64_t fp_sign(bool wide) { return wide?UINT64_C(0x8000000000000000):0x80000000u; }
static uint64_t fp_exp(bool wide) { return wide?UINT64_C(0x7ff0000000000000):0x7f800000u; }
static uint64_t fp_frac(bool wide) { return wide?UINT64_C(0xfffffffffffff):0x7fffffu; }
static uint64_t fp_quiet(bool wide) { return wide?UINT64_C(0x8000000000000):0x400000u; }
static bool fp_nan(uint64_t v,bool wide) { return (v&fp_exp(wide))==fp_exp(wide) && (v&fp_frac(wide)); }
static bool fp_snan(uint64_t v,bool wide) { return fp_nan(v,wide) && !(v&fp_quiet(wide)); }
static bool fp_denormal(uint64_t v,bool wide) { return !(v&fp_exp(wide)) && (v&fp_frac(wide)); }
static uint64_t fp_default_nan(bool wide) { return fp_exp(wide)|fp_quiet(wide); }
static uint32_t fp_exceptions(void);
static uint64_t fp_input(uint64_t v,bool wide,uint32_t *status) {
    if((*status&0x01000000u) && fp_denormal(v,wide)) {
        *status|=0x80u;return v&fp_sign(wide);
    }
    return v;
}
static uint64_t fp_output(uint64_t v,bool wide,uint32_t *status) {
    uint32_t exceptions=fp_exceptions();
    /* VFP flush-to-zero raises underflow without inexact for that stage. */
    if((*status&0x01000000u) && (exceptions&8u))exceptions&=~16u;
    *status|=exceptions;
    if(fp_nan(v,wide))return fp_default_nan(wide);
    if((*status&0x01000000u) && fp_denormal(v,wide)) {
        *status|=8u;return v&fp_sign(wide);
    }
    return v;
}
static uint64_t fp_nan_result(uint64_t a,uint64_t b,bool wide,uint32_t *status) {
    if(fp_snan(a,wide) || fp_snan(b,wide))*status|=1u;
    if(*status&0x02000000u)return fp_default_nan(wide);
    uint64_t value=fp_snan(a,wide)?a:fp_snan(b,wide)?b:fp_nan(a,wide)?a:b;
    return value|fp_quiet(wide);
}
static double fp_double(uint64_t bits,bool wide) {
    if(wide) { double value;memcpy(&value,&bits,8);return value; }
    uint32_t word=(uint32_t)bits;float value;memcpy(&value,&word,4);return value;
}
static uint64_t fp_bits(double value,bool wide) {
    if(wide) { uint64_t bits;memcpy(&bits,&value,8);return bits; }
    volatile float rounded=(float)value;float copy=rounded;uint32_t bits;memcpy(&bits,&copy,4);return bits;
}
static uint32_t fp_trunc_int(uint64_t bits,bool wide,bool unsign,uint32_t *status) {
    bits=fp_input(bits,wide,status);
    bool negative=(bits&fp_sign(wide))!=0;
    unsigned fraction_bits=wide?52u:23u;
    int exponent=(int)((bits&fp_exp(wide))>>fraction_bits)-(wide?1023:127);
    if(exponent<0) {
        if(bits&~fp_sign(wide))*status|=16u;
        return 0;
    }
    if(exponent<=31) {
        uint64_t mantissa=(UINT64_C(1)<<fraction_bits)|(bits&fp_frac(wide));
        int shift=(int)fraction_bits-exponent;
        uint32_t magnitude=(uint32_t)(shift<0?mantissa<<-shift:mantissa>>shift);
        uint32_t limit=unsign?(negative?0u:UINT32_MAX):(negative?0x80000000u:0x7fffffffu);
        if(magnitude<=limit) {
            if(shift>0 && (mantissa&((UINT64_C(1)<<shift)-1u)))*status|=16u;
            return negative?0u-magnitude:magnitude;
        }
    }
    *status|=1u;
    if(fp_nan(bits,wide))return 0;
    return unsign?(negative?0u:UINT32_MAX):(negative?0x80000000u:0x7fffffffu);
}
static uint32_t fp_exceptions(void) {
    int flags=fetestexcept(FE_ALL_EXCEPT);
    return (flags&FE_INVALID?1u:0)|(flags&FE_DIVBYZERO?2u:0)|
        (flags&FE_OVERFLOW?4u:0)|(flags&FE_UNDERFLOW?8u:0)|(flags&FE_INEXACT?16u:0);
}

static uint32_t fp_round_normal32(uint32_t mantissa,uint32_t remainder,uint32_t half,
                                  uint32_t sign,uint32_t *status) {
    if(!remainder)return mantissa;
    unsigned mode=(*status>>22)&3u;
    bool round_up=(mode==0 && (remainder>half || (remainder==half && (mantissa&1u)))) ||
                  (mode==1 && !sign) || (mode==2 && sign);
    *status|=16u;
    return mantissa+round_up;
}

static bool fp_mul_normal32(uint32_t a,uint32_t b,uint32_t *status,uint32_t *result) {
    unsigned ea=(a>>23)&255u,eb=(b>>23)&255u;
    uint32_t sign=(a^b)&0x80000000u;
    if(ea==255u || eb==255u)return false;
    if(!ea && (a&0x7fffffu)) {
        if(!(*status&0x01000000u))return false;
        *status|=0x80u;a&=0x80000000u;
    }
    if(!eb && (b&0x7fffffu)) {
        if(!(*status&0x01000000u))return false;
        *status|=0x80u;b&=0x80000000u;
    }
    if(!ea || !eb) { *result=sign;return true; }
    int exponent=(int)(ea+eb)-127;
    if(exponent<0 && (*status&0x01000000u)) { *status|=8u;*result=sign;return true; }
    if(exponent<1 || exponent>252)return false;
    uint64_t product=(uint64_t)((a&0x7fffffu)|0x800000u)*((b&0x7fffffu)|0x800000u);
    unsigned normalization=product>=(UINT64_C(1)<<47);
    unsigned shift=23u+normalization;
    uint32_t mantissa=(uint32_t)(product>>shift);
    uint32_t remainder=(uint32_t)product&((1u<<shift)-1u);
    mantissa=fp_round_normal32(mantissa,remainder,1u<<(shift-1u),sign,status);
    *result=sign|((((unsigned)exponent+normalization-1u)<<23)+mantissa);
    return true;
}

static bool fp_add_normal32(uint32_t a,uint32_t b,uint32_t *status,uint32_t *result) {
    const uint32_t sign_bit=0x80000000u, fraction_mask=0x7fffffu;
    if(!(a&0x7f800000u) && (a&fraction_mask)) {
        if(!(*status&0x01000000u))return false;
        *status|=0x80u;a&=sign_bit;
    }
    if(!(b&0x7f800000u) && (b&fraction_mask)) {
        if(!(*status&0x01000000u))return false;
        *status|=0x80u;b&=sign_bit;
    }
    if((a&~sign_bit)<(b&~sign_bit)) { uint32_t swap=a;a=b;b=swap; }
    unsigned ea=(a>>23)&255u,eb=(b>>23)&255u,distance=ea-eb;
    bool subtract=((a^b)&sign_bit)!=0;
    if(ea==255u)return false;
    if(!eb) {
        *result=ea || !subtract?a:((*status>>22)&3u)==2u?sign_bit:0u;
        return true;
    }
    if(distance>31u) {
        /* The smaller operand is below half an ulp, even below a power of two. */
        if(ea==254u)return false;
        unsigned mode=(*status>>22)&3u;
        bool negative=(a&sign_bit)!=0;
        if(subtract && (mode==3u || mode==(negative?1u:2u)))--a;
        if(!subtract && mode==(negative?2u:1u))++a;
        *status|=16u;*result=a;return true;
    }
    /* Align exactly to the smaller exponent, including bits that decide ties. */
    uint32_t sign=a&sign_bit;
    uint64_t large=(uint64_t)((a&fraction_mask)|0x800000u)<<distance;
    uint32_t small=(b&fraction_mask)|0x800000u;
    uint64_t magnitude=subtract?large-small:large+small;
    if(!magnitude) {
        *result=((*status>>22)&3u)==2u?sign_bit:0u;
        return true;
    }
#if defined(__GNUC__) || defined(__clang__)
    int shift=40-__builtin_clzll(magnitude);
#else
    int shift=-23;
    for(uint64_t bits=magnitude;bits>1u;bits>>=1u)++shift;
#endif
    int exponent=(int)eb+shift;
    /* Near exponent limits, leave underflow and overflow flags to fenv. */
    if(exponent<1 || exponent>253)return false;
    uint32_t mantissa;
    if(shift>0) {
        uint32_t half=1u<<(shift-1);
        uint32_t remainder=(uint32_t)magnitude&(half+(half-1u));
        mantissa=fp_round_normal32((uint32_t)(magnitude>>shift),remainder,half,sign,status);
    } else mantissa=(uint32_t)magnitude<<-shift;
    *result=sign|((((uint32_t)exponent-1u)<<23)+mantissa);
    return true;
}

/* Volatile precision boundaries prevent host contraction/excess precision. */
#if defined(__GNUC__) && !defined(__clang__)
__attribute__((optimize("rounding-math","fp-contract=off")))
#endif
static uint64_t fp_binary(ctr_vfp_operation op,uint64_t a,uint64_t b,bool wide,uint32_t *status) {
    a=fp_input(a,wide,status);b=fp_input(b,wide,status);
    if(fp_nan(a,wide) || fp_nan(b,wide))return fp_nan_result(a,b,wide,status);
    uint64_t bits;
    if(wide) {
        volatile double x=fp_double(a,true),y=fp_double(b,true),z;
        if(op==CTR_VFP_ADD)z=x+y;
        else if(op==CTR_VFP_SUB)z=x-y;
        else if(op==CTR_VFP_DIV)z=x/y;
        else z=x*y;
        bits=fp_bits(z,true);
    } else {
        volatile float x=(float)fp_double(a,false),y=(float)fp_double(b,false),z;
        if(op==CTR_VFP_ADD)z=x+y;
        else if(op==CTR_VFP_SUB)z=x-y;
        else if(op==CTR_VFP_DIV)z=x/y;
        else z=x*y;
        bits=fp_bits(z,false);
    }
    return fp_output(bits,wide,status);
}

#if defined(__GNUC__) && !defined(__clang__)
__attribute__((optimize("rounding-math","fp-contract=off")))
#endif
static bool fp_execute(uint32_t vfp[32],uint32_t *fpscr,ctr_vfp_operation op,unsigned d,unsigned n,unsigned m,
                       bool wide,uint32_t *fault_detail) {
    bool from_int=op==CTR_VFP_FROM_I32 || op==CTR_VFP_FROM_U32;
    bool to_int=op>=CTR_VFP_TO_I32;
    bool source_wide=op==CTR_VFP_WIDEN?false:op==CTR_VFP_NARROW?true:wide;
    bool dest_wide=op==CTR_VFP_WIDEN?true:op==CTR_VFP_NARROW?false:wide;
    if(op>CTR_VFP_ROUND_U32 || d+(dest_wide && !to_int?2u:1u)>32 || n+(wide?2u:1u)>32 ||
        (m!=32 && m+(source_wide && !from_int?2u:1u)>32) || (m==32 && op!=CTR_VFP_CMP && op!=CTR_VFP_CMPE))
        return false;
    bool bitwise=op==CTR_VFP_MOV || op==CTR_VFP_ABS || op==CTR_VFP_NEG;
    uint32_t status=*fpscr;
    /* Trap enables, short vectors and alternate half precision are not scalar VFPv2. */
    if(status&(bitwise?0x00370000u:0x04379f00u)) {
        *fault_detail=status;
        return false;
    }
    uint64_t b=m==32?0:fp_read(vfp,m,source_wide && !from_int);
    if(bitwise) {
        if(op==CTR_VFP_ABS)b&=~fp_sign(wide);
        if(op==CTR_VFP_NEG)b^=fp_sign(wide);
        fp_write(vfp,d,wide,b);return true;
    }
    /* VCVT truncates regardless of FPSCR rounding and has no host FP side effects. */
    if(op==CTR_VFP_TO_I32 || op==CTR_VFP_TO_U32) {
        vfp[d]=fp_trunc_int(b,wide,op==CTR_VFP_TO_U32,fpscr);
        return true;
    }
    uint64_t a=fp_read(vfp,d,wide && op!=CTR_VFP_WIDEN && op!=CTR_VFP_NARROW && !to_int);
    if(op==CTR_VFP_CMP || op==CTR_VFP_CMPE) {
        a=fp_input(a,wide,&status);b=fp_input(b,wide,&status);
        uint64_t sign=fp_sign(wide);
        uint32_t flags;
        if(fp_nan(a,wide) || fp_nan(b,wide)) {
            flags=0x30000000u;
            if(op==CTR_VFP_CMPE || fp_snan(a,wide) || fp_snan(b,wide))status|=1u;
        } else if(a==b || !((a|b)&~sign))flags=0x60000000u;
        else {
            bool less=((a^b)&sign)?(a&sign)!=0:((a&sign)?a>b:a<b);
            flags=less?0x80000000u:0x20000000u;
        }
        *fpscr=(status&0x0fffffffu)|flags;
        return true;
    }
    bool accumulate=op>=CTR_VFP_MLA && op<=CTR_VFP_NMLS;
    uint64_t left=op<=CTR_VFP_DIV?fp_read(vfp,n,wide):0;
    uint32_t product32=0;
    bool normal_product=!wide && (op==CTR_VFP_MUL || op==CTR_VFP_NMUL || accumulate) &&
        fp_mul_normal32((uint32_t)left,(uint32_t)b,&status,&product32);
    if(normal_product && !accumulate) {
        vfp[d]=op==CTR_VFP_NMUL?product32^0x80000000u:product32;
        *fpscr=status;
        return true;
    }
    uint32_t sum32;
    if(!wide && (op==CTR_VFP_ADD || op==CTR_VFP_SUB || normal_product)) {
        uint32_t add_left=normal_product?(uint32_t)a:(uint32_t)left;
        uint32_t add_right=normal_product?product32:(uint32_t)b;
        if(op==CTR_VFP_NMLA || op==CTR_VFP_NMLS)add_left^=0x80000000u;
        if(op==CTR_VFP_SUB || op==CTR_VFP_MLS || op==CTR_VFP_NMLA)add_right^=0x80000000u;
        if(fp_add_normal32(add_left,add_right,&status,&sum32)) {
            vfp[d]=sum32;*fpscr=status;return true;
        }
    }
    if(from_int && wide) {
        double value=op==CTR_VFP_FROM_I32?(double)((int64_t)(b^0x80000000u)-INT64_C(0x80000000)):(double)(uint32_t)b;
        fp_write(vfp,d,true,fp_bits(value,true));
        return true;
    }
    unsigned mode=(status>>22)&3u;
    if(from_int && !wide && mode==0u && fegetround()==FE_TONEAREST) {
        bool exact;
        float v;
        if(op==CTR_VFP_FROM_I32) {
            int32_t s=(int32_t)((int64_t)(b^0x80000000u)-INT64_C(0x80000000));
            v=(float)s;
            exact=(s==INT32_MIN) || (v!=2147483648.0f && (int32_t)v==s);
        } else {
            uint32_t u=(uint32_t)b;
            v=(float)u;
            exact=(v<4294967296.0f && (uint32_t)v==u);
        }
        uint32_t bits;memcpy(&bits,&v,4);
        vfp[d]=bits;
        *fpscr=exact?status:(status|16u);
        return true;
    }
    fenv_t saved;int saved_errno=errno;
    if(fegetenv(&saved) || fesetenv(FE_DFL_ENV))return false;
    static const int rounding[]={FE_TONEAREST,FE_UPWARD,FE_DOWNWARD,FE_TOWARDZERO};
    if(mode && fesetround(rounding[mode])) {
        fesetenv(&saved);errno=saved_errno;return false;
    }
    uint64_t result=0;
    if(op<=CTR_VFP_DIV) {
        if(accumulate) {
            uint64_t product=product32;
            if(!normal_product) {
                product=fp_binary(CTR_VFP_MUL,left,b,wide,&status);
                /* The product's flags belong to a separate rounding stage. */
                feclearexcept(FE_ALL_EXCEPT);
            }
            if(op==CTR_VFP_NMLA || op==CTR_VFP_NMLS)a^=fp_sign(wide);
            result=fp_binary(op==CTR_VFP_MLS || op==CTR_VFP_NMLA?CTR_VFP_SUB:CTR_VFP_ADD,a,product,wide,&status);
        } else {
            result=fp_binary(op,left,b,wide,&status);
            if(op==CTR_VFP_NMUL)result^=fp_sign(wide);
        }
    } else if(from_int) {
        double value=op==CTR_VFP_FROM_I32?(double)((int64_t)(b^0x80000000u)-INT64_C(0x80000000)):(double)(uint32_t)b;
        result=fp_bits(value,wide);
        status|=fp_exceptions();
    } else if(to_int) {
        b=fp_input(b,wide,&status);double value=fp_double(b,wide),rounded;
        bool unsign=op==CTR_VFP_ROUND_U32;
        rounded=rint(value);
        if(fp_nan(b,wide) || rounded<(unsign?0.0:-2147483648.0) || rounded>(unsign?4294967295.0:2147483647.0)) {
            feclearexcept(FE_INEXACT);status|=1u;
            result=fp_nan(b,wide)?0:rounded<0?(unsign?0:0x80000000u):(unsign?0xffffffffu:0x7fffffffu);
        } else {
            if(rounded!=value)status|=16u;
            result=unsign?(uint32_t)rounded:(uint32_t)(int64_t)rounded;
        }
        status|=fp_exceptions();
    } else {
        b=fp_input(b,source_wide,&status);
        if(fp_nan(b,source_wide)) {
            uint64_t nan=fp_nan_result(b,b,source_wide,&status);
            result=op==CTR_VFP_WIDEN?((nan&0x80000000u)<<32)|((nan&0x7fffffu)<<29)|fp_exp(true):
                op==CTR_VFP_NARROW?((nan>>32)&0x80000000u)|((nan>>29)&0x7fffffu)|fp_exp(false):nan;
        } else if(op==CTR_VFP_SQRT) {
            if(wide) { volatile double value=sqrt(fp_double(b,true));result=fp_bits(value,true); }
            else { volatile float value=sqrtf((float)fp_double(b,false));result=fp_bits(value,false); }
        } else result=fp_bits(fp_double(b,source_wide),dest_wide);
        if(!fp_nan(b,source_wide))result=fp_output(result,dest_wide,&status);
    }
    fesetenv(&saved);errno=saved_errno;
    fp_write(vfp,d,dest_wide && !to_int,result);
    *fpscr=status;return true;
}

ctr_cpu_result ctr_cpu_vfp(ctr_cpu *cpu,ctr_vfp_operation op,unsigned d,unsigned n,unsigned m,bool wide,uint32_t opcode) {
    uint32_t detail=opcode;
    if(!fp_execute(cpu->vfp,&cpu->fpscr,op,d,n,m,wide,&detail))
        return ctr_cpu_stop(cpu,CTR_CPU_UNSUPPORTED,detail);
    return CTR_CPU_OK;
}

ctr_cpu_result ctr_cpu_vfp_multiple(ctr_cpu *cpu,unsigned base,unsigned first,unsigned words,bool before,bool writeback,bool load,uint32_t opcode) {
    if(base>=15 || !words || first+words>32)return ctr_cpu_stop(cpu,CTR_CPU_UNSUPPORTED,opcode);
    uint32_t address=cpu->r[base]-(before?words*4u:0u);
    ctr_cpu_result result=ctr_cpu_fp_transfer(cpu,first,words,address,load,opcode);
    if(result==CTR_CPU_OK && writeback)cpu->r[base]+=before?0u-words*4u:words*4u;
    return result;
}

static unsigned fp_register(unsigned reg,unsigned bank,bool wide) {
    return wide?(reg+bank*16u)*2u:reg*2u+bank;
}
bool ctr_vfp_arm_data(uint32_t vfp[32],uint32_t *fpscr,uint32_t insn,uint32_t *detail) {
    *detail=insn;
    if((insn&0x0f000e10u)!=0x0e000a00u)return false;
    bool wide=(insn&0x100u)!=0;
    unsigned vd=(insn>>12)&15u,d=fp_register(vd,(insn>>22)&1u,wide);
    unsigned rn=(insn>>16)&15u,rm=insn&15u;
    unsigned n=fp_register(rn,(insn>>7)&1u,wide),m=fp_register(rm,(insn>>5)&1u,wide);
    unsigned group=(insn>>20)&11u;bool bit6=(insn&0x40u)!=0,bit7=(insn&0x80u)!=0;
    ctr_vfp_operation op;
    switch(group) {
    case 0:op=bit6?CTR_VFP_MLS:CTR_VFP_MLA;break;
    case 1:op=bit6?CTR_VFP_NMLA:CTR_VFP_NMLS;break;
    case 2:op=bit6?CTR_VFP_NMUL:CTR_VFP_MUL;break;
    case 3:op=bit6?CTR_VFP_SUB:CTR_VFP_ADD;break;
    case 8:if(bit6)return false;op=CTR_VFP_DIV;break;
    case 11:
        if(!bit6)return false;
        n=0;
        switch(rn) {
        case 0:op=bit7?CTR_VFP_ABS:CTR_VFP_MOV;break;
        case 1:op=bit7?CTR_VFP_SQRT:CTR_VFP_NEG;break;
        case 4:case 5:
            op=bit7?CTR_VFP_CMPE:CTR_VFP_CMP;
            if(rn==4 && m>=32)return false;
            if(rn==5) { if(insn&0x2fu)return false;m=32; }
            break;
        case 7:
            if(!bit7)return false;
            op=wide?CTR_VFP_NARROW:CTR_VFP_WIDEN;
            d=fp_register(vd,(insn>>22)&1u,!wide);break;
        case 8:
            op=bit7?CTR_VFP_FROM_I32:CTR_VFP_FROM_U32;
            m=fp_register(rm,(insn>>5)&1u,false);break;
        case 12:case 13:
            op=rn==13?(bit7?CTR_VFP_TO_I32:CTR_VFP_ROUND_I32):(bit7?CTR_VFP_TO_U32:CTR_VFP_ROUND_U32);
            d=fp_register(vd,(insn>>22)&1u,false);break;
        default:return false;
        }
        break;
    default:return false;
    }
    return fp_execute(vfp,fpscr,op,d,n,m,wide,detail);
}
ctr_cpu_result ctr_cpu_vfp_arm(ctr_cpu *cpu,uint32_t insn) {
    bool wide=(insn&0x100u)!=0,load=(insn&0x100000u)!=0;
    unsigned vd=(insn>>12)&15u,d=fp_register(vd,(insn>>22)&1u,wide);
    unsigned rn=(insn>>16)&15u,rm=insn&15u;
    if((insn&0x0fff0fffu)==0x0ee10a10u || (insn&0x0fff0fffu)==0x0ef10a10u) {
        if(load) {
            if(vd==15)cpu->cpsr=(cpu->cpsr&0x0fffffffu)|(cpu->fpscr&0xf0000000u);
            else cpu->r[vd]=cpu->fpscr;
        } else {
            if(vd==15)return ctr_cpu_stop(cpu,CTR_CPU_UNSUPPORTED,insn);
            cpu->fpscr=cpu->r[vd];
        }
        return CTR_CPU_OK;
    }
    if((insn&0x0fe00f7fu)==0x0e000a10u) {
        unsigned word=rn*2u+((insn>>7)&1u);
        if(vd==15)return ctr_cpu_stop(cpu,CTR_CPU_UNSUPPORTED,insn);
        if(load)cpu->r[vd]=cpu->vfp[word];else cpu->vfp[word]=cpu->r[vd];
        return CTR_CPU_OK;
    }
    if((insn&0x0fe00ed0u)==0x0c400a10u) {
        unsigned word=wide?fp_register(rm,(insn>>5)&1u,true):rm*2u+((insn>>5)&1u);
        if(vd==15 || rn==15 || (load && vd==rn) || word+2>32)
            return ctr_cpu_stop(cpu,CTR_CPU_UNSUPPORTED,insn);
        if(load) { cpu->r[vd]=cpu->vfp[word];cpu->r[rn]=cpu->vfp[word+1]; }
        else { cpu->vfp[word]=cpu->r[vd];cpu->vfp[word+1]=cpu->r[rn]; }
        return CTR_CPU_OK;
    }
    if((insn&0x0f200e00u)==0x0d000a00u) {
        if(d+(wide?2u:1u)>32 || (rn==15 && !load))return ctr_cpu_stop(cpu,CTR_CPU_UNSUPPORTED,insn);
        uint32_t base=rn==15?cpu->r[15]+8u:cpu->r[rn],offset=(insn&255u)*4u;
        return ctr_cpu_fp_transfer(cpu,d,wide?2u:1u,insn&0x800000u?base+offset:base-offset,load,insn);
    }
    if((insn&0x0e000e00u)==0x0c000a00u &&
        ((insn&0x01800000u)==0x00800000u || (insn&0x01a00000u)==0x01200000u)) {
        unsigned words=insn&255u;bool before=(insn&0x1000000u)!=0;
        if(wide && (words&1u))return ctr_cpu_stop(cpu,CTR_CPU_UNSUPPORTED,insn);
        return ctr_cpu_vfp_multiple(cpu,rn,d,words,before,(insn&0x200000u)!=0,load,insn);
    }
    uint32_t detail=insn;
    if(!ctr_vfp_arm_data(cpu->vfp,&cpu->fpscr,insn,&detail))
        return ctr_cpu_stop(cpu,CTR_CPU_UNSUPPORTED,detail);
    return CTR_CPU_OK;
}
