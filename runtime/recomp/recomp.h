/* Original CTR2WUP implementation of the generated-code ABI 4 interface.
 * Copyright (c) 2026 CTR2WUP contributors. SPDX-License-Identifier: MIT
 * Interface names/layouts match the separately installed lifter. No upstream
 * header implementation is included. All guest accesses use checked callbacks.
 */
#ifndef CTR_RECOMP_H
#define CTR_RECOMP_H
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

_Noreturn void ctr_recomp_abort(Context *ctx,ctr_cpu_result result,uint32_t detail);
int ctr_recomp_instruction(Context *ctx,uint32_t address,uint32_t opcode);
#define CTR_INSTRUCTION(address,opcode) do { if (!ctr_recomp_instruction(ctx,(address),(opcode))) return; } while (0)
#define mem_read8(c,a) ((c)->host->read8((c),(a)))
#define mem_read16(c,a) ((c)->host->read16((c),(a)))
#define mem_read32(c,a) ((c)->host->read32((c),(a)))
#define mem_write8(c,a,v) ((c)->host->write8((c),(a),(v)))
#define mem_write16(c,a,v) ((c)->host->write16((c),(a),(v)))
#define mem_write32(c,a,v) ((c)->host->write32((c),(a),(v)))
/* Span bypasses would skip permissions, invalidation and exclusive monitors. */
static inline const uint8_t *mem_read_span(Context *c,uint32_t a,uint32_t n) { (void)c;(void)a;(void)n;return NULL; }
static inline uint8_t *mem_write_span(Context *c,uint32_t a,uint32_t n) { (void)c;(void)a;(void)n;return NULL; }
static inline uint32_t load32(const uint8_t *p) { return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static inline void store32(uint8_t *p,uint32_t n) { for (unsigned i=0;i<4;++i) p[i]=(uint8_t)(n>>(8*i)); }
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
static inline uint32_t shift_lsl(uint32_t v,uint32_t n,uint8_t *c) { unsigned out;uint32_t r=ctr_cpu_shift(v,0,n,false,*c,&out);*c=(uint8_t)out;return r; }
static inline uint32_t shift_lsr(uint32_t v,uint32_t n,uint8_t *c) { unsigned out;uint32_t r=ctr_cpu_shift(v,1,n,false,*c,&out);*c=(uint8_t)out;return r; }
static inline uint32_t shift_asr(uint32_t v,uint32_t n,uint8_t *c) { unsigned out;uint32_t r=ctr_cpu_shift(v,2,n,false,*c,&out);*c=(uint8_t)out;return r; }
static inline uint32_t shift_ror(uint32_t v,uint32_t n,uint8_t *c) { unsigned out;uint32_t r=ctr_cpu_shift(v,3,n,false,*c,&out);*c=(uint8_t)out;return r; }
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

/* Scalar IEEE bit transport is independent of the host's byte order. Full
 * FPSCR exception/NaN/denormal semantics are outside the current profile. */
static inline float vfp_from_s(uint32_t n) { float v;memcpy(&v,&n,4);return v; }
static inline double vfp_from_d(uint32_t lo,uint32_t hi) { uint64_t n=((uint64_t)hi<<32)|lo;double v;memcpy(&v,&n,8);return v; }
static inline uint32_t vfp_to_s_in(uint32_t f,float v) { uint32_t n;(void)f;memcpy(&n,&v,4);return n; }
static inline uint64_t vfp_to_d_in(uint32_t f,double v) { uint64_t n;(void)f;memcpy(&n,&v,8);return n; }
#define vfp_to_s(c,v) vfp_to_s_in(FPSCR(c),(v))
#define vfp_to_d(c,v) vfp_to_d_in(FPSCR(c),(v))
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
#define CALL(code) do { if (ctx->depth>=64) {ctx->exit=EXIT_UNWIND;return;} ++ctx->depth;code(ctx);--ctx->depth;if(ctx->exit)return; } while (0)
static inline void recomp_call(Context *c) { Code f=c->host->lookup(c,c->r[15]|c->thumb);if(f)f(c);else c->exit=EXIT_UNWIND; }
#define RETURNED(address) do { if(ctx->r[15]!=(address)||ctx->thumb){target=ctx->r[15];goto dispatch;} } while (0)
#define RETURNED_T(address) do { if(ctx->r[15]!=(address)||!ctx->thumb){target=ctx->r[15];goto dispatch;} } while (0)
#define RETURN_TO(value) do { uint32_t ctr_target_=(value);ctx->thumb=ctr_target_&1;ctx->r[15]=ctr_target_&(ctx->thumb?~1u:~3u);return; } while (0)
#define RETURN_TO_A(value) RETURN_TO(value)
#define RETURN_TO_T(value) RETURN_TO(value)
#define JUMP_TO(value) do { uint32_t ctr_target_=(value);ctx->thumb=ctr_target_&1;target=ctr_target_&(ctx->thumb?~1u:~3u);goto dispatch; } while (0)
#define SVC(next,number) do { ctx->r[15]=(next);ctx->svc=(number);ctx->exit=EXIT_SVC;return; } while (0)
#define INTERPRET(address,opcode) do { ctx->host->interpret(ctx,(address),(opcode));if(ctx->exit)return; } while (0)
#define STALE_CHECK(flag) if(flag){ctx->exit=EXIT_UNWIND;return;}
#endif
