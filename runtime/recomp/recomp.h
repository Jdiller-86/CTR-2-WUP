/* Original CTR2WUP implementation of the generated-code ABI 4 interface.
 * Copyright (c) 2026 CTR2WUP contributors. SPDX-License-Identifier: MIT
 * Interface names/layouts match the separately installed lifter. No upstream
 * header implementation is included. All guest accesses use checked callbacks.
 */
#ifndef CTR_RECOMP_H
#define CTR_RECOMP_H
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("no-shrink-wrap")
#endif
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include "ctr_cpu.h"
#define RECOMP_ABI 4
#define RECOMP_EXPORT
#define RECOMP_HIDDEN
#define LIKELY(x) (x)
#define UNLIKELY(x) (x)
#if defined(__GNUC__) || defined(__clang__)
#define CTR_NOINLINE __attribute__((noinline, unused))
#else
#define CTR_NOINLINE
#endif
typedef struct Context Context;
typedef void (*Code)(Context *);
typedef struct Host {
    uint8_t (*read8)(Context *,uint32_t);
    uint16_t (*read16)(Context *,uint32_t);
    uint32_t (*read32)(Context *,uint32_t);
    void (*write8)(Context *,uint32_t,uint8_t);
    void (*write16)(Context *,uint32_t,uint16_t);
    void (*write32)(Context *,uint32_t,uint32_t);
    void (*interpret)(Context *,uint32_t,uint32_t);
    Code (*lookup)(Context *,uint32_t);
} Host;
enum { EXIT_NONE, EXIT_SVC, EXIT_BUDGET, EXIT_UNWIND };
struct Context {
    uint32_t r[16];
    uint8_t n,z,c,v,q,thumb,ge,exclusive;
    int32_t budget;
    uint32_t exit,svc,depth,exclusive_address,tls;
    uint8_t *const *read_pages,*const *write_pages;
    uint32_t *vfp,*fpscr;
    const Host *host;
    void *user;
};
typedef struct Entry { uint32_t address; Code code; } Entry;
typedef struct Module { const char *name; uint32_t *base; uint32_t size,count; const Entry *entries; } Module;
typedef struct Piece { uint32_t start,end; uint64_t hash; } Piece;
typedef struct Run { uint32_t first,end; } Run;
typedef struct Origin { uint32_t first,count,start; } Origin;
typedef struct Origins {
    uint32_t count,piece_count;
    const Piece *pieces;
    const Origin *functions;
    const Run *runs;
    const uint32_t *owners;
    uint8_t *stale;
    uint32_t *starts;
} Origins;
#define NO_ORIGIN UINT32_MAX
#define NO_START UINT32_MAX
#define SYNC_OUT() ((void)0)
#define SYNC_IN() ((void)0)
#define READ_PAGES(c) ((c)->read_pages)
#define WRITE_PAGES(c) ((c)->write_pages)
#define FPSCR(c) (*(c)->fpscr)
#define FPSCR_LEN (7u<<16)

typedef struct RecompFast {
    Context context;
    uint32_t vfp[32],fpscr;
    ctr_cpu *cpu;
    uint64_t code_generation,map_version;
    uint64_t *mem_generation;
    uint64_t *mem_resource_clock;
    uint64_t *mem_resource_versions;
    uint32_t rx_base,rx_size,rd_base,rd_end,rd_limit,rw_base,rw_end,rw_limit,span_addr;
    const uint8_t *rx_storage,*rd_storage;
    uint8_t *rw_storage;
    unsigned maximum_depth;
    bool single_data;
    bool rollback_base;
    uintptr_t dirty_page_plus_one,dirty_page2_plus_one;
} RecompFast;

_Noreturn void ctr_recomp_abort(Context *ctx,ctr_cpu_result result,uint32_t detail);
_Noreturn void ctr_recomp_unwind(Context *ctx);
int ctr_recomp_instruction(Context *ctx,uint32_t address,uint32_t opcode);
void ctr_recomp_instruction_void(Context *ctx,uint32_t address,uint32_t opcode);
int ctr_recomp_step_slow(Context *ctx,uint32_t address,bool single_data);
void ctr_recomp_step_slow_void(Context *ctx,uint32_t address,bool single_data);
void ctr_recomp_step_run_slow(Context *ctx,uint32_t address,int32_t count);
enum { CTR_VFP_STOP, CTR_VFP_EMIT, CTR_VFP_HANDLED };
int ctr_recomp_scalar_vfp(Context *ctx,uint32_t address,uint32_t opcode);
bool ctr_recomp_scalar_vfp_emit(Context *ctx,uint32_t address,uint32_t opcode);
int ctr_recomp_step_vfp(Context *c,uint32_t address);
#if defined(__GNUC__) || defined(__clang__)
#define CTR_SCALAR_VFP(address,opcode) \
    if((__builtin_constant_p(opcode) && ((opcode)>>28)==14u && (((opcode)&0x0fbe0e50u)!=0x0ebc0a40u) && \
        ctr_recomp_step_vfp(ctx,(address))) || ctr_recomp_scalar_vfp_emit(ctx,(address),(opcode)))
#else
#define CTR_SCALAR_VFP(address,opcode) \
    if(ctr_recomp_scalar_vfp_emit(ctx,(address),(opcode)))
#endif
#define CTR_INSN_MULT_OK(rn,list,wb,load,thumb) ( \
    (rn)<15u && (list)!=0u && !((thumb) && !(load) && ((list)&0x8000u)) && \
    !((wb) && ((list)&(1u<<(rn))) && ((load) || ((list)&((1u<<(rn))-1u)))))
#define CTR_INSN_FAST_ARM_MULT(op) ( \
    (op)>UINT16_MAX && ((op)>>28)<15u && (((op)&0x0e400000u)==0x08000000u) && \
    CTR_INSN_MULT_OK(((op)>>16)&15u,(op)&0xffffu,((op)&0x00200000u)!=0u,((op)&0x00100000u)!=0u,0))
#define CTR_INSN_FAST_VLDR_VSTR(op) ( \
    (op)>UINT16_MAX && ((op)>>28)<15u && \
    ((((op)&0x0f200f00u)==0x0d000a00u) || ((((op)&0x0f200f00u)==0x0d000b00u) && !((op)&0x00400000u))) && \
    ((((op)>>16)&15u)!=15u || ((op)&0x00100000u)!=0u))
#define CTR_INSN_FAST_VLDM_VSTM_FIRST(op) ( \
    ((((op)>>12)&15u)*2u) | (!((op)&0x100u)?(((op)>>22)&1u):0u))
#define CTR_INSN_FAST_VLDM_VSTM(op) ( \
    (op)>UINT16_MAX && ((op)>>28)<15u && (((op)&0x0e000e00u)==0x0c000a00u) && \
    ((((op)&0x01000000u)==0u && ((op)&0x00800000u)!=0u) || (((op)&0x01800000u)==0x01000000u && ((op)&0x00200000u)!=0u)) && \
    ((op)&255u)!=0u && (CTR_INSN_FAST_VLDM_VSTM_FIRST(op)+((op)&255u))<=32u && \
    !(((op)&0x100u) && ((op)&1u)) && (((op)>>16)&15u)!=15u)
#define CTR_INSN_FAST_VMRS_TLS(op) ( \
    (op)>UINT16_MAX && ((op)>>28)<15u && ( \
    (((op)&0x0fff0fffu)==0x0ef10a10u) || \
    ((((op)&0x0fff0fffu)==0x0ee10a10u || ((op)&0x0fff0fffu)==0x0e1d0f70u || ((op)&0x0fe00f7fu)==0x0e000a10u) && (((op)>>12)&15u)!=15u) || \
    (((op)&0x0fe00ed0u)==0x0c400a10u && (((op)>>12)&15u)!=15u && (((op)>>16)&15u)!=15u)))
#define CTR_INSN_FAST_THUMB_LIST(op) ( \
    ((op)&255u) | ((((op)&0xf600u)==0xb400u && ((op)&0x100u))?(1u<<(((op)&0x800u)?15u:14u)):0u))
#define CTR_INSN_FAST_THUMB_RN(op) ((((op)&0xf600u)==0xb400u)?13u:(((op)>>8)&7u))
#define CTR_INSN_FAST_THUMB_MULT(op) ( \
    (op)<=UINT16_MAX && ((((op)&0xf600u)==0xb400u) || (((op)&0xf000u)==0xc000u)) && \
    CTR_INSN_MULT_OK(CTR_INSN_FAST_THUMB_RN(op),CTR_INSN_FAST_THUMB_LIST(op), \
        (((op)&0xf600u)==0xb400u) || !(((op)&0x800u) && (CTR_INSN_FAST_THUMB_LIST(op)&(1u<<CTR_INSN_FAST_THUMB_RN(op)))), \
        ((op)&0x800u)!=0u,1))
#define CTR_INSN_MAYBE_SPECIAL(op) ( \
    (((op)&0x0c000000u)==0x0c000000u) || \
    (((op)&0x0f900090u)==0x01000080u) || \
    (((op)&0x0ff00ff0u)==0x06500f90u) || (((op)&0x0ff00ff0u)==0x06500ff0u) || \
    (((op)&0x0ff00ff0u)==0x06800fb0u) || (((op)&0x0e000000u)==0x08000000u) || \
    (((op)&0x0fffffffu)==0x012fff3fu) || (((op)&0xfffffff0u)==0xf57ff040u) || \
    (((op)&0xfffffff0u)==0xf57ff050u) || (((op)&0xfffffff0u)==0xf57ff060u) || \
    ((op)<=0xffffu && ((((op)&0xf600u)==0xb400u) || (((op)&0xf000u)==0xc000u) || (op)==0x47f8u)))
#define CTR_INSN_REG_ONLY(op) ( \
    ((op)<=UINT16_MAX && ( \
        (op)<0x4400u || \
        (((op)&0xfc00u)==0x4400u && ((op)&0x0300u)!=0x0300u && ((op)&0x0087u)!=0x0087u) || \
        (((op)&0xf000u)==0xa000u) || (((op)&0xff00u)==0xb000u) || \
        (((op)&0xff00u)==0xb200u) || (((op)&0xff00u)==0xba00u) || \
        (((op)&0xf000u)==0xd000u && ((op)&0x0f00u)<0x0e00u) || (((op)&0xf800u)==0xe000u))) || \
    ((op)>UINT16_MAX && ((op)>>28)!=15u && ( \
        (((op)&0x0e000000u)==0x0a000000u) || \
        (((op)&0x0ffffff0u)==0x012fff10u) || (((op)&0x0ffffff0u)==0x012fff30u && ((op)&15u)!=15u) || \
        ((((op)&0x0fc000f0u)==0x00000090u || ((op)&0x0f8000f0u)==0x00800090u) && (((op)>>16)&15u)!=15u) || \
        ((((op)&0x0e000000u)==0x02000000u || !((op)&0x0e000010u) || (((op)&0x0e000090u)==0x00000010u && (((op)>>8)&15u)!=15u && ((op)&15u)!=15u)) && \
         (((op)&0x01800000u)!=0x01000000u || ((op)&0x00100000u)) && (((op)>>12)&15u)!=15u) || \
        (((op)&0x0fb00000u)==0x03000000u && (((op)>>12)&15u)!=15u) || (((op)&0x0fffffffu)==0x0320f000u) || \
        (((op)&0x0fff0ff0u)==0x016f0f10u && (((op)>>12)&15u)!=15u && ((op)&15u)!=15u) || \
        (((op)&0x0f8003f0u)==0x06800070u && (((op)>>20)&7u)!=1u && (((op)>>20)&7u)!=5u && (((op)>>12)&15u)!=15u && ((op)&15u)!=15u) || \
        (((op)&0x0ff00f30u)==0x06b00f30u && (((op)>>12)&15u)!=15u && ((op)&15u)!=15u))))
#define CTR_INSN_SD_THUMB(op) ( \
    (((op)&0xf000u)==0x5000u) || (((op)&0xe000u)==0x6000u) || \
    (((op)&0xf000u)==0x8000u) || (((op)&0xf000u)==0x9000u) || (((op)&0xf800u)==0x4800u))
#define CTR_INSN_SD_ARM(op) ( \
    ((((op)&0x0c000000u)==0x04000000u) && !(((op)&0x100000u) && (((op)>>12)&15u)==15u)) || \
    ((((op)&0x0e000090u)==0x00000090u) && (((op)>>5)&3u) && ((((op)>>5)&3u)==1u || ((op)&0x100000u))))

static inline int ctr_recomp_instruction_fast(Context *c,uint32_t address,uint32_t opcode) {
    if(CTR_INSN_MAYBE_SPECIAL(opcode) || c->budget<=0)return ctr_recomp_instruction(c,address,opcode);
    RecompFast *f=(RecompFast *)c;
    c->r[15]=address;
    --c->budget;
    /* A register-only ARM instruction never consults data-access state. */
    if(CTR_INSN_REG_ONLY(opcode))return 1;
    f->rollback_base=false;
    f->single_data=c->thumb?CTR_INSN_SD_THUMB(opcode):CTR_INSN_SD_ARM(opcode);
    return 1;
}
#if defined(__GNUC__) || defined(__clang__)
void ctr_recomp_step_reg(Context *c,uint32_t address);
void ctr_recomp_step_reg4(Context *c);
void ctr_recomp_step_mem00(Context *c,uint32_t address);
void ctr_recomp_step_mem00_4(Context *c);
void ctr_recomp_step_mem10(Context *c,uint32_t address);
void ctr_recomp_step_mem01(Context *c,uint32_t address);
void ctr_recomp_step_mem11(Context *c,uint32_t address);
void ctr_recomp_step_mem11_4(Context *c);
/* 0=reg, 1=mem00, 2=mem11, 3=mem10, 4=mem01, 5=special */
#define CTR_INSN_KIND(op) ( \
    CTR_INSN_FAST_VMRS_TLS(op)?0: \
    (CTR_INSN_FAST_ARM_MULT(op)||CTR_INSN_FAST_VLDR_VSTR(op)||CTR_INSN_FAST_VLDM_VSTM(op)||CTR_INSN_FAST_THUMB_MULT(op))?1: \
    CTR_INSN_MAYBE_SPECIAL(op)?5: \
    CTR_INSN_REG_ONLY(op)?0: \
    ((op)>UINT16_MAX)?(CTR_INSN_SD_ARM(op)?2:1): \
    CTR_INSN_SD_ARM(op)?(CTR_INSN_SD_THUMB(op)?2:3): \
    (CTR_INSN_SD_THUMB(op)?4:1))
#define CTR_INSTRUCTION(address,opcode) do { \
    if(__builtin_constant_p(opcode)) { \
        const uint32_t ctr_op_=(opcode); \
        const int ctr_kind_=CTR_INSN_KIND(ctr_op_); \
        if(ctr_kind_==0)ctr_recomp_step_reg(ctx,(address)); \
        else if(ctr_kind_==1)ctr_recomp_step_mem00(ctx,(address)); \
        else if(ctr_kind_==2)ctr_recomp_step_mem11(ctx,(address)); \
        else if(ctr_kind_==3)ctr_recomp_step_mem10(ctx,(address)); \
        else if(ctr_kind_==4)ctr_recomp_step_mem01(ctx,(address)); \
        else ctr_recomp_instruction_void(ctx,(address),ctr_op_); \
    } else { \
        if(!ctr_recomp_instruction_fast(ctx,(address),(opcode)))ctr_recomp_unwind(ctx); \
    } \
} while (0)
#define CTR_INSTRUCTION_NEXT(opcode) do { \
    if(__builtin_constant_p(opcode)) { \
        const uint32_t ctr_op_=(opcode); \
        const int ctr_kind_=CTR_INSN_KIND(ctr_op_); \
        if(ctr_kind_==0)ctr_recomp_step_reg4(ctx); \
        else if(ctr_kind_==1)ctr_recomp_step_mem00_4(ctx); \
        else if(ctr_kind_==2)ctr_recomp_step_mem11_4(ctx); \
        else { uint32_t ctr_addr_=ctx->r[15]+4u; \
            if(ctr_kind_==3)ctr_recomp_step_mem10(ctx,ctr_addr_); \
            else if(ctr_kind_==4)ctr_recomp_step_mem01(ctx,ctr_addr_); \
            else ctr_recomp_instruction_void(ctx,ctr_addr_,ctr_op_); } \
    } else { \
        if(!ctr_recomp_instruction_fast(ctx,ctx->r[15]+4u,(opcode)))ctr_recomp_unwind(ctx); \
    } \
} while (0)
void ctr_recomp_step_run(Context *c,uint32_t address,int32_t count);
void ctr_recomp_step_run4(Context *c,int32_t count);
#define CTR_INSTRUCTION_RUN(address,count) ctr_recomp_step_run(ctx,(address),(count))
#define CTR_INSTRUCTION_RUN_NEXT(count) ctr_recomp_step_run4(ctx,(count))
#define CTR_STEP_MID(address) ctr_recomp_step_run_slow(ctx,(address),INT32_MAX)
#else
#define CTR_INSTRUCTION(address,opcode) do { if (!ctr_recomp_instruction_fast(ctx,(address),(opcode))) return; } while (0)
#define CTR_INSTRUCTION_NEXT(opcode) CTR_INSTRUCTION(ctx->r[15]+4u,(opcode))
#define CTR_INSTRUCTION_RUN(address,count) do { \
    if(ctx->budget<(count)){ctr_recomp_step_run_slow(ctx,(address),(count));} \
    else{ctx->r[15]=(address)+(uint32_t)((count)-1)*4u;ctx->budget-=(count);} \
} while (0)
#define CTR_INSTRUCTION_RUN_NEXT(count) do { \
    if(ctx->budget<(count)){ctr_recomp_step_run_slow(ctx,ctx->r[15]+4u,(count));} \
    else{ctx->r[15]+=(uint32_t)(count)*4u;ctx->budget-=(count);} \
} while (0)
#define CTR_STEP_MID(address) ctr_recomp_step_run_slow(ctx,(address),INT32_MAX)
#endif
uint8_t ctr_recomp_read8(Context *c,uint32_t a);
uint16_t ctr_recomp_read16(Context *c,uint32_t a);
uint32_t ctr_recomp_read32(Context *c,uint32_t a);
void ctr_recomp_note_store_cold(RecompFast *f,uintptr_t begin,uintptr_t last);
void ctr_recomp_write8(Context *c,uint32_t a,uint8_t n);
void ctr_recomp_write16(Context *c,uint32_t a,uint16_t n);
void ctr_recomp_write32(Context *c,uint32_t a,uint32_t n);
#define mem_read8(c,a) ctr_recomp_read8((c),(a))
#define mem_read16(c,a) ctr_recomp_read16((c),(a))
#define mem_read32(c,a) ctr_recomp_read32((c),(a))
#define mem_write8(c,a,v) ctr_recomp_write8((c),(a),(v))
#define mem_write16(c,a,v) ctr_recomp_write16((c),(a),(v))
#define mem_write32(c,a,v) ctr_recomp_write32((c),(a),(v))
/* A non-null span is backed by one checked mapping and aligned for word access. */
#if defined(__GNUC__)
#define CTR_WORD_ALIGNED __attribute__((assume_aligned(4)))
#else
#define CTR_WORD_ALIGNED
#endif
CTR_WORD_ALIGNED const uint8_t *ctr_recomp_read_span(Context *c,uint32_t a,uint32_t n);
CTR_WORD_ALIGNED uint8_t *ctr_recomp_write_span(Context *c,uint32_t a,uint32_t n);
uint32_t ctr_recomp_read_span_gpr(Context *c,uint32_t mask);
void ctr_recomp_write_span_gpr(Context *c,uint32_t mask);
void ctr_recomp_read_span_vfp(Context *c,uint32_t spec);
void ctr_recomp_write_span_vfp(Context *c,uint32_t spec);
#define mem_read_span(c,a,n) ctr_recomp_read_span((c),(a),(n))
#define mem_write_span(c,a,n) ctr_recomp_write_span((c),(a),(n))
#undef CTR_WORD_ALIGNED
static inline uint32_t load32(const uint8_t *p) {
#if defined(__GNUC__) && defined(__BYTE_ORDER__)
    uint32_t value;
    memcpy(&value,p,sizeof(value));
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    value=__builtin_bswap32(value);
#endif
    return value;
#else
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
#endif
}
static inline void store32(uint8_t *p,uint32_t n) {
#if defined(__GNUC__) && defined(__BYTE_ORDER__)
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    n=__builtin_bswap32(n);
#endif
    memcpy(p,&n,sizeof(n));
#else
    for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(n>>(8*i));
#endif
}
#define C_EQ (ctx->z)
#define C_NE (!ctx->z)
#define C_CS (ctx->c)
#define C_CC (!ctx->c)
#define C_MI (ctx->n)
#define C_PL (!ctx->n)
#define C_VS (ctx->v)
#define C_VC (!ctx->v)
#define C_HI (ctx->c&&!ctx->z)
#define C_LS (!ctx->c||ctx->z)
#define C_GE (ctx->n==ctx->v)
#define C_LT (ctx->n!=ctx->v)
#define C_GT (!ctx->z&&ctx->n==ctx->v)
#define C_LE (ctx->z||ctx->n!=ctx->v)
static inline uint32_t ror32(uint32_t v,uint32_t n) { n&=31;return n?(v>>n)|(v<<(32-n)):v; }
static inline uint32_t shift_lsl(uint32_t v,uint32_t n,uint8_t *c) { if(n-1u<31u){*c=(uint8_t)((v>>(32u-n))&1u);return v<<n;} unsigned out;uint32_t r=ctr_cpu_shift(v,0,n,false,*c,&out);*c=(uint8_t)out;return r; }
static inline uint32_t shift_lsr(uint32_t v,uint32_t n,uint8_t *c) { if(n-1u<31u){*c=(uint8_t)((v>>(n-1u))&1u);return v>>n;} unsigned out;uint32_t r=ctr_cpu_shift(v,1,n,false,*c,&out);*c=(uint8_t)out;return r; }
static inline uint32_t shift_asr(uint32_t v,uint32_t n,uint8_t *c) { if(n-1u<31u){*c=(uint8_t)((v>>(n-1u))&1u);return (v>>n)|((v>>31)?(UINT32_MAX<<(32u-n)):0u);} unsigned out;uint32_t r=ctr_cpu_shift(v,2,n,false,*c,&out);*c=(uint8_t)out;return r; }
static inline uint32_t shift_ror(uint32_t v,uint32_t n,uint8_t *c) { if(n-1u<31u){uint32_t r=(v>>n)|(v<<(32u-n));*c=(uint8_t)(r>>31);return r;} unsigned out;uint32_t r=ctr_cpu_shift(v,3,n,false,*c,&out);*c=(uint8_t)out;return r; }
/* The exercised ARMv6 unsigned byte forms used by newlib string routines. */
/* Widen before the pinned emitter multiplies/adds signed halves: an int32
   expression could overflow before a Q helper sees its value. */
#define HALF(word,high) ctr_cpu_signed_half((word),(high))
static inline uint32_t accumulate(Context *c,int64_t value) {
    if(value>INT32_MAX || value<INT32_MIN)c->q=1;
    return (uint32_t)value;
}
static inline uint32_t media_parallel(Context *c,unsigned operation,unsigned lane,uint32_t left,uint32_t right) {
    if(operation!=5 || (lane!=4 && lane!=7))ctr_recomp_abort(c,CTR_CPU_UNSUPPORTED,(operation<<20)|(lane<<5));
    ctr_cpu flags={0};uint32_t result=ctr_cpu_byte_parallel(&flags,left,right,lane==7);
    c->ge=(uint8_t)((flags.cpsr>>16)&15u);return result;
}
static inline uint32_t media_select(Context *c,uint32_t left,uint32_t right) {
    ctr_cpu flags={0};flags.cpsr=(uint32_t)c->ge<<16;return ctr_cpu_byte_select(&flags,left,right);
}
static inline uint32_t add_halves(uint32_t left,uint32_t right) {
    return ((left+right)&0xffffu)|((((left>>16)+(right>>16))&0xffffu)<<16);
}
static inline uint32_t media_saturate(Context *c,int64_t value,unsigned bits,unsigned is_unsigned) {
    if(is_unsigned>1 || bits>(is_unsigned?31u:32u) || (!is_unsigned && !bits) ||
       value<INT32_MIN || value>INT32_MAX)
        ctr_recomp_abort(c,CTR_CPU_UNSUPPORTED,c->r[15]);
    ctr_cpu flags={0};
    uint32_t result=ctr_cpu_saturate(&flags,(uint32_t)value,bits,is_unsigned!=0);
    if(flags.cpsr&0x08000000u)c->q=1;
    return result;
}
static inline uint32_t media_saturate16(Context *c,uint32_t value,unsigned bits,unsigned is_unsigned) {
    if(is_unsigned>1 || bits>(is_unsigned?15u:16u) || (!is_unsigned && !bits))
        ctr_recomp_abort(c,CTR_CPU_UNSUPPORTED,c->r[15]);
    uint32_t low=media_saturate(c,ctr_cpu_signed_half(value,0),bits,is_unsigned);
    uint32_t high=media_saturate(c,ctr_cpu_signed_half(value,1),bits,is_unsigned);
    return (low&0xffffu)|((high&0xffffu)<<16);
}

/* Scalar IEEE bit transport is independent of the host's byte order. Full
 * FPSCR exception/NaN/denormal semantics are outside the current profile. */
static inline float vfp_from_s(uint32_t n) { float v;memcpy(&v,&n,4);return v; }
static inline double vfp_from_d(uint32_t lo,uint32_t hi) { uint64_t n=((uint64_t)hi<<32)|lo;double v;memcpy(&v,&n,8);return v; }
static inline uint32_t vfp_to_s_in(uint32_t f,float v) { uint32_t n;(void)f;memcpy(&n,&v,4);return n; }
static inline uint64_t vfp_to_d_in(uint32_t f,double v) { uint64_t n;(void)f;memcpy(&n,&v,8);return n; }
#define vfp_to_s(c,v) vfp_to_s_in(FPSCR(c),(v))
#define vfp_to_d(c,v) vfp_to_d_in(FPSCR(c),(v))
/* Integer conversion semantics belong to the checked scalar interpreter.
 * Recovery dispatches before these emitted expressions. Strict execution must
 * stop before a host float-to-integer cast (NaNs/range/rounding differ). */
static inline uint32_t ctr_recomp_integer_conversion(Context *c,double value) {
    (void)value;ctr_recomp_abort(c,CTR_CPU_UNSUPPORTED,c->r[15]);
}
#define vfp_to_u32(value) ctr_recomp_integer_conversion(ctx,(value))
#define vfp_to_s32(value) ctr_recomp_integer_conversion(ctx,(value))
#define VFP_SET_D(lo,hi,value) do { uint64_t ctr_bits_=vfp_to_d(ctx,(value));(lo)=(uint32_t)ctr_bits_;(hi)=(uint32_t)(ctr_bits_>>32); } while (0)
static inline float vfp_s(Context *c,int r) { return vfp_from_s(c->vfp[r]); }
static inline double vfp_d(Context *c,int r) { return vfp_from_d(c->vfp[2*r],c->vfp[2*r+1]); }
static inline void vfp_set_s(Context *c,int r,float v) { c->vfp[r]=vfp_to_s(c,v); }
static inline void vfp_set_d(Context *c,int r,double v) { uint64_t n=vfp_to_d(c,v);c->vfp[2*r]=(uint32_t)n;c->vfp[2*r+1]=(uint32_t)(n>>32); }
static inline uint32_t vfp_compared(uint32_t f,double a,double b) {
    uint32_t flags=isnan(a)||isnan(b)?0x30000000u:a==b?0x60000000u:a<b?0x80000000u:0x20000000u;
    return (f&0x0fffffffu)|flags;
}
#define vfp_compare(c,a,b) (FPSCR(c)=vfp_compared(FPSCR(c),(a),(b)))
static inline void vfp_vector(Context *c,int op,int wide,int d,int n,int m) { (void)op;(void)wide;(void)d;(void)n;(void)m;ctr_recomp_abort(c,CTR_CPU_UNSUPPORTED,FPSCR(c)); }
#define VFP_VECTOR(c,...) vfp_vector((c),__VA_ARGS__)
#define RETURN() return
#ifndef CTR_RECOMP_CALL_LIMIT
#define CTR_RECOMP_CALL_LIMIT 64
#endif
#if CTR_RECOMP_CALL_LIMIT < 1 || CTR_RECOMP_CALL_LIMIT > 64
#error "CTR_RECOMP_CALL_LIMIT must be between 1 and 64"
#endif
void ctr_recomp_call(Context *c,Code code);
#define CALL(code) ctr_recomp_call(ctx,(code))
void recomp_call(Context *c);
#define RETURNED(address) do { if(ctx->r[15]!=(address)||ctx->thumb){target=ctx->r[15];goto dispatch;} } while (0)
#define RETURNED_T(address) do { if(ctx->r[15]!=(address)||!ctx->thumb){target=ctx->r[15];goto dispatch;} } while (0)
#define RETURN_TO(value) do { uint32_t ctr_target_=(value),ctr_t_=ctr_target_&1u;ctx->thumb=(uint8_t)ctr_t_;ctx->r[15]=ctr_target_&((ctr_t_<<1)-4u);return; } while (0)
#define RETURN_TO_A(value) RETURN_TO(value)
#define RETURN_TO_T(value) RETURN_TO(value)
#define JUMP_TO(value) do { uint32_t ctr_target_=(value),ctr_t_=ctr_target_&1u;ctx->thumb=(uint8_t)ctr_t_;target=ctr_target_&((ctr_t_<<1)-4u);goto dispatch; } while (0)
#define SVC(next,number) do { ctx->r[15]=(next);ctx->svc=(number);ctx->exit=EXIT_SVC;return; } while (0)
#define INTERPRET(address,opcode) do { ctx->host->interpret(ctx,(address),(opcode));if(ctx->exit)return; } while (0)
#define STALE_CHECK(flag) if(flag){ctx->exit=EXIT_UNWIND;return;}
#endif
