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

static uint64_t fp_read(const ctr_cpu *cpu,unsigned word,bool wide) {
    return cpu->vfp[word] | (wide?(uint64_t)cpu->vfp[word+1]<<32:0);
}
static void fp_write(ctr_cpu *cpu,unsigned word,bool wide,uint64_t value) {
    cpu->vfp[word]=(uint32_t)value;
    if(wide)cpu->vfp[word+1]=(uint32_t)(value>>32);
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
    *status|=exceptions;feclearexcept(FE_ALL_EXCEPT);
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
static uint32_t fp_exceptions(void) {
    int flags=fetestexcept(FE_ALL_EXCEPT);
    return (flags&FE_INVALID?1u:0)|(flags&FE_DIVBYZERO?2u:0)|
        (flags&FE_OVERFLOW?4u:0)|(flags&FE_UNDERFLOW?8u:0)|(flags&FE_INEXACT?16u:0);
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
ctr_cpu_result ctr_cpu_vfp(ctr_cpu *cpu,ctr_vfp_operation op,unsigned d,unsigned n,unsigned m,bool wide,uint32_t opcode) {
    bool from_int=op==CTR_VFP_FROM_I32 || op==CTR_VFP_FROM_U32;
    bool to_int=op>=CTR_VFP_TO_I32;
    bool source_wide=op==CTR_VFP_WIDEN?false:op==CTR_VFP_NARROW?true:wide;
    bool dest_wide=op==CTR_VFP_WIDEN?true:op==CTR_VFP_NARROW?false:wide;
    if(op>CTR_VFP_ROUND_U32 || d+(dest_wide && !to_int?2u:1u)>32 || n+(wide?2u:1u)>32 ||
        (m!=32 && m+(source_wide && !from_int?2u:1u)>32) || (m==32 && op!=CTR_VFP_CMP && op!=CTR_VFP_CMPE))
        return ctr_cpu_stop(cpu,CTR_CPU_UNSUPPORTED,opcode);
    uint64_t a=fp_read(cpu,d,wide && !to_int && op!=CTR_VFP_WIDEN && op!=CTR_VFP_NARROW);
    uint64_t b=m==32?0:fp_read(cpu,m,source_wide && !from_int);
    if(cpu->fpscr&0x00370000u)return ctr_cpu_stop(cpu,CTR_CPU_UNSUPPORTED,cpu->fpscr);
    if(op==CTR_VFP_MOV || op==CTR_VFP_ABS || op==CTR_VFP_NEG) {
        if(op==CTR_VFP_ABS)b&=~fp_sign(wide);
        if(op==CTR_VFP_NEG)b^=fp_sign(wide);
        fp_write(cpu,d,wide,b);return CTR_CPU_OK;
    }
    /* Trap enables, short vectors and alternate half precision are not scalar VFPv2. */
    if(cpu->fpscr&0x04379f00u)return ctr_cpu_stop(cpu,CTR_CPU_UNSUPPORTED,cpu->fpscr);
    uint32_t status=cpu->fpscr;fenv_t saved;int saved_errno=errno;
    if(fegetenv(&saved) || fesetenv(FE_DFL_ENV))return ctr_cpu_stop(cpu,CTR_CPU_UNSUPPORTED,opcode);
    static const int rounding[]={FE_TONEAREST,FE_UPWARD,FE_DOWNWARD,FE_TOWARDZERO};
    if(fesetround(rounding[(status>>22)&3u])) {
        fesetenv(&saved);errno=saved_errno;return ctr_cpu_stop(cpu,CTR_CPU_UNSUPPORTED,opcode);
    }
    uint64_t result=0;
    if(op<=CTR_VFP_DIV) {
        uint64_t left=fp_read(cpu,n,wide);
        if(op>=CTR_VFP_MLA && op<=CTR_VFP_NMLS) {
            uint64_t product=fp_binary(CTR_VFP_MUL,left,b,wide,&status);
            if(op==CTR_VFP_NMLA || op==CTR_VFP_NMLS)a^=fp_sign(wide);
            result=fp_binary(op==CTR_VFP_MLS || op==CTR_VFP_NMLA?CTR_VFP_SUB:CTR_VFP_ADD,a,product,wide,&status);
        } else {
            result=fp_binary(op,left,b,wide,&status);
            if(op==CTR_VFP_NMUL)result^=fp_sign(wide);
        }
    } else if(op==CTR_VFP_CMP || op==CTR_VFP_CMPE) {
        a=fp_input(a,wide,&status);b=fp_input(b,wide,&status);
        uint32_t flags;
        if(fp_nan(a,wide) || fp_nan(b,wide)) {
            flags=0x30000000u;
            if(op==CTR_VFP_CMPE || fp_snan(a,wide) || fp_snan(b,wide))status|=1u;
        } else {
            double x=fp_double(a,wide),y=fp_double(b,wide);
            flags=x==y?0x60000000u:x<y?0x80000000u:0x20000000u;
        }
        status=(status&0x0fffffffu)|flags;
    } else if(from_int) {
        double value=op==CTR_VFP_FROM_I32?(double)((int64_t)(b^0x80000000u)-INT64_C(0x80000000)):(double)(uint32_t)b;
        result=fp_bits(value,wide);
    } else if(to_int) {
        b=fp_input(b,wide,&status);double value=fp_double(b,wide),rounded;
        bool unsign=op==CTR_VFP_TO_U32 || op==CTR_VFP_ROUND_U32;
        if(op==CTR_VFP_TO_I32 || op==CTR_VFP_TO_U32)fesetround(FE_TOWARDZERO);
        rounded=rint(value);
        if(fp_nan(b,wide) || rounded<(unsign?0.0:-2147483648.0) || rounded>(unsign?4294967295.0:2147483647.0)) {
            feclearexcept(FE_INEXACT);status|=1u;
            result=fp_nan(b,wide)?0:rounded<0?(unsign?0:0x80000000u):(unsign?0xffffffffu:0x7fffffffu);
        } else {
            if(rounded!=value)status|=16u;
            result=unsign?(uint32_t)rounded:(uint32_t)(int64_t)rounded;
        }
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
    status|=fp_exceptions();fesetenv(&saved);errno=saved_errno;
    if(op!=CTR_VFP_CMP && op!=CTR_VFP_CMPE)fp_write(cpu,d,dest_wide && !to_int,result);
    cpu->fpscr=status;return CTR_CPU_OK;
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
    if((insn&0x0f000e10u)==0x0e000a00u) {
        unsigned n=fp_register(rn,(insn>>7)&1u,wide),m=fp_register(rm,(insn>>5)&1u,wide);
        unsigned group=(insn>>20)&11u;bool bit6=(insn&0x40u)!=0,bit7=(insn&0x80u)!=0;
        ctr_vfp_operation op;
        switch(group) {
        case 0:op=bit6?CTR_VFP_MLS:CTR_VFP_MLA;break;
        case 1:op=bit6?CTR_VFP_NMLA:CTR_VFP_NMLS;break;
        case 2:op=bit6?CTR_VFP_NMUL:CTR_VFP_MUL;break;
        case 3:op=bit6?CTR_VFP_SUB:CTR_VFP_ADD;break;
        case 8:if(bit6)goto unsupported;op=CTR_VFP_DIV;break;
        case 11:
            if(!bit6)goto unsupported;
            n=0;
            switch(rn) {
            case 0:op=bit7?CTR_VFP_ABS:CTR_VFP_MOV;break;
            case 1:op=bit7?CTR_VFP_SQRT:CTR_VFP_NEG;break;
            case 4:case 5:
                op=bit7?CTR_VFP_CMPE:CTR_VFP_CMP;
                if(rn==4 && m>=32)goto unsupported;
                if(rn==5) { if(insn&0x2fu)goto unsupported;m=32; }
                break;
            case 7:
                if(!bit7)goto unsupported;
                op=wide?CTR_VFP_NARROW:CTR_VFP_WIDEN;
                d=fp_register(vd,(insn>>22)&1u,!wide);break;
            case 8:
                op=bit7?CTR_VFP_FROM_I32:CTR_VFP_FROM_U32;
                m=fp_register(rm,(insn>>5)&1u,false);break;
            case 12:case 13:
                op=rn==13?(bit7?CTR_VFP_TO_I32:CTR_VFP_ROUND_I32):(bit7?CTR_VFP_TO_U32:CTR_VFP_ROUND_U32);
                d=fp_register(vd,(insn>>22)&1u,false);break;
            default:goto unsupported;
            }
            break;
        default:goto unsupported;
        }
        return ctr_cpu_vfp(cpu,op,d,n,m,wide,insn);
    }
unsupported:
    return ctr_cpu_stop(cpu,CTR_CPU_UNSUPPORTED,insn);
}
