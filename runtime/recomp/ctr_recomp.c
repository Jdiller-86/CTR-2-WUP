/* Copyright (c) 2026 CTR2WUP contributors. SPDX-License-Identifier: MIT */
#include "ctr_recomp.h"
#include "recomp.h"
#include <limits.h>
#include <setjmp.h>
#include <stdlib.h>

extern const uint32_t recomp_abi,recomp_generation,recomp_entry_count,recomp_module_count;
extern const Entry recomp_entries[];
extern const Origins recomp_origins[];
typedef struct {
    Context context;
    uint32_t vfp[32],fpscr;
    ctr_cpu *cpu;
    uint64_t code_generation;
    bool fallback;
    /* The emitter writes an ARM LDM base before reads. Preserve the checked
       profile's original base on an abort while retaining earlier loaded words. */
    bool rollback_base;
    unsigned base_register;
    uint32_t base_value;
    jmp_buf escape;
    ctr_cpu_result fault;
} Engine;
_Static_assert(sizeof(float)==4 && sizeof(double)==8,"IEEE scalar sizes required");
_Static_assert(offsetof(Context,budget)==72 && offsetof(Context,read_pages)==96,"ABI 4 fixed-width prefix");

static void import_cpu(Engine *e) {
    Context *c=&e->context;ctr_cpu *p=e->cpu;
    memcpy(c->r,p->r,sizeof(c->r));
    c->n=p->cpsr>>31;c->z=(p->cpsr>>30)&1;c->c=(p->cpsr>>29)&1;c->v=(p->cpsr>>28)&1;
    c->q=(p->cpsr>>27)&1;c->ge=(p->cpsr>>16)&15;c->thumb=(p->cpsr&CTR_FLAG_T)!=0;
    c->exclusive=p->exclusive_valid && p->exclusive_generation==p->memory->generation;
    c->exclusive_address=p->exclusive_address;
    c->tls=p->tls_address;
}
static void export_cpu(Engine *e) {
    Context *c=&e->context;ctr_cpu *p=e->cpu;
    memcpy(p->r,c->r,sizeof(p->r));
    p->cpsr=(p->cpsr&~0xf80f0020u)|((uint32_t)c->n<<31)|((uint32_t)c->z<<30)|
        ((uint32_t)c->c<<29)|((uint32_t)c->v<<28)|((uint32_t)c->q<<27)|((uint32_t)c->ge<<16)|
        (c->thumb?CTR_FLAG_T:0);
    p->exclusive_valid=c->exclusive!=0;p->exclusive_address=c->exclusive_address;
    p->exclusive_generation=p->memory->generation;p->svc_number=c->svc;
}
_Noreturn void ctr_recomp_abort(Context *c,ctr_cpu_result result,uint32_t detail) {
    Engine *e=c->user;e->fault=result;
    if(result==CTR_CPU_MEMORY_FAULT && e->rollback_base)c->r[e->base_register]=e->base_value;
    export_cpu(e);ctr_cpu_stop(e->cpu,result,detail);
    longjmp(e->escape,1);
}
int ctr_recomp_instruction(Context *c,uint32_t address,uint32_t opcode) {
    Engine *e=c->user;
    e->rollback_base=false;
    c->r[15]=address;
    if (!c->budget) { c->exit=EXIT_BUDGET;return 0; }
    if (e->cpu->memory->code_generation!=e->code_generation)
        ctr_recomp_abort(c,CTR_CPU_CODE_INVALIDATED,address);
    uint32_t actual;
    ctr_mem_result r=ctr_memory_read(e->cpu->memory,address,c->thumb?2:4,CTR_MEM_EXEC,&actual);
    if (r!=CTR_MEM_OK) { e->cpu->memory_fault=r;ctr_recomp_abort(c,CTR_CPU_MEMORY_FAULT,address); }
    if (actual!=opcode) ctr_recomp_abort(c,CTR_CPU_CODE_INVALIDATED,address);
    /* Non-default FP controls require a correctness implementation first. */
    if (*c->fpscr&0x07ff9f00u) ctr_recomp_abort(c,CTR_CPU_UNSUPPORTED,*c->fpscr);
    --c->budget;++e->cpu->instructions;
    ctr_cpu flags={0};flags.cpsr=((uint32_t)c->n<<31)|((uint32_t)c->z<<30)|
        ((uint32_t)c->c<<29)|((uint32_t)c->v<<28);
    if(!c->thumb && (opcode&0x0e000000u)==0x08000000u &&
        (opcode>>28==15u || ctr_cpu_condition(&flags,opcode>>28))) {
        unsigned rn=(opcode>>16)&15u;bool load=(opcode&0x100000u)!=0,writeback=(opcode&0x200000u)!=0;
        if(opcode>>28==15u || (opcode&0x400000u) ||
            !ctr_cpu_multiple_supported(rn,(uint16_t)opcode,writeback,load,false))
            ctr_recomp_abort(c,CTR_CPU_UNSUPPORTED,opcode);
        if(load && writeback) { e->rollback_base=true;e->base_register=rn;e->base_value=c->r[rn]; }
    }
    if(c->thumb && ((opcode&0xf600u)==0xb400u || (opcode&0xf000u)==0xc000u)) {
        bool stack=(opcode&0xf600u)==0xb400u,load=(opcode&0x800u)!=0;
        unsigned rn=stack?13u:(opcode>>8)&7u;uint16_t list=(uint16_t)(opcode&255u);
        if(stack && (opcode&0x100u))list|=(uint16_t)(1u<<(load?15:14));
        bool writeback=stack || !(load && (list&(1u<<rn)));
        if(!ctr_cpu_multiple_supported(rn,list,writeback,load,true))ctr_recomp_abort(c,CTR_CPU_UNSUPPORTED,opcode);
    }
    if((!c->thumb && (opcode&0x0fffffffu)==0x012fff3fu && ctr_cpu_condition(&flags,opcode>>28)) ||
        (c->thumb && opcode==0x47f8u))ctr_recomp_abort(c,CTR_CPU_UNSUPPORTED,opcode);
    /* The pinned emitter silently removes some CP15 cache operations. This
       profile implements only the user TLS read; reject other executed CP15
       operations until their coherency/privilege behavior is implemented. */
    if (!c->thumb && ((opcode&0xfffffff0u)==0xf57ff040u ||
        (opcode&0xfffffff0u)==0xf57ff050u || (opcode&0xfffffff0u)==0xf57ff060u))
        ctr_recomp_abort(c,CTR_CPU_UNSUPPORTED,opcode);
    if (!c->thumb && (opcode&0x0f000f10u)==0x0e000f10u &&
        ((opcode>>28)==15u || (opcode&0x0fff0fffu)!=0x0e1d0f70u || ((opcode>>12)&15u)==15u)) {
        ctr_cpu flags={0};
        flags.cpsr=((uint32_t)c->n<<31)|((uint32_t)c->z<<30)|
                   ((uint32_t)c->c<<29)|((uint32_t)c->v<<28);
        if (opcode>>28==15u || ctr_cpu_condition(&flags,opcode>>28))
            ctr_recomp_abort(c,CTR_CPU_UNSUPPORTED,opcode);
    }
    return 1;
}
static uint32_t read_value(Context *c,uint32_t a,unsigned width) {
    Engine *e=c->user;uint32_t n;
    ctr_mem_result r=ctr_memory_read(e->cpu->memory,a,width,CTR_MEM_READ,&n);
    if (r!=CTR_MEM_OK) { e->cpu->memory_fault=r;ctr_recomp_abort(c,CTR_CPU_MEMORY_FAULT,a); }
    return n;
}
static void write_value(Context *c,uint32_t a,unsigned width,uint32_t n) {
    Engine *e=c->user;
    ctr_mem_result r=ctr_memory_write(e->cpu->memory,a,width,n);
    if (r!=CTR_MEM_OK) { e->cpu->memory_fault=r;ctr_recomp_abort(c,CTR_CPU_MEMORY_FAULT,a); }
    c->exclusive=0;
    if (e->cpu->memory->code_generation!=e->code_generation)
        ctr_recomp_abort(c,CTR_CPU_CODE_INVALIDATED,a);
}
static uint8_t read8(Context *c,uint32_t a) { return (uint8_t)read_value(c,a,1); }
static uint16_t read16(Context *c,uint32_t a) { return (uint16_t)read_value(c,a,2); }
static uint32_t read32(Context *c,uint32_t a) { return read_value(c,a,4); }
static void write8(Context *c,uint32_t a,uint8_t n) { write_value(c,a,1,n); }
static void write16(Context *c,uint32_t a,uint16_t n) { write_value(c,a,2,n); }
static void write32(Context *c,uint32_t a,uint32_t n) { write_value(c,a,4,n); }
static Code lookup(Context *c,uint32_t a) {
    (void)c;uint32_t lo=0,hi=recomp_entry_count;
    while(lo<hi) { uint32_t m=lo+(hi-lo)/2;if(recomp_entries[m].address<a)lo=m+1;else hi=m; }
    return lo<recomp_entry_count && recomp_entries[lo].address==a?recomp_entries[lo].code:NULL;
}
static void interpret(Context *c,uint32_t a,uint32_t opcode) {
    Engine *e=c->user;
    if (!e->fallback) ctr_recomp_abort(c,CTR_CPU_UNSUPPORTED,opcode);
    c->r[15]=a;export_cpu(e);
    uint64_t count=e->cpu->instructions;
    ctr_cpu_result r=ctr_cpu_step(e->cpu);
    e->cpu->instructions=count; /* Already charged by CTR_INSTRUCTION. */
    ++e->cpu->fallback_instructions;
    import_cpu(e);
    if(r==CTR_CPU_SVC){c->svc=e->cpu->svc_number;c->exit=EXIT_SVC;}
    else if(r!=CTR_CPU_OK) ctr_recomp_abort(c,r,opcode);
}
static const Host callbacks={read8,read16,read32,write8,write16,write32,interpret,lookup};
static ctr_cpu_result execute(ctr_cpu *p,void *user,uint64_t budget) {
    Engine *e=user;Context *c=&e->context;e->cpu=p;
    import_cpu(e);c->budget=(int32_t)(budget>INT32_MAX?INT32_MAX:budget);c->exit=EXIT_NONE;c->depth=0;
    e->rollback_base=false;
    if (setjmp(e->escape)) return e->fault;
    while(c->budget>0) {
        Code code=lookup(c,c->r[15]|c->thumb);
        if(!code) {
            e->rollback_base=false;
            if(!e->fallback) ctr_recomp_abort(c,CTR_CPU_MISSING_BLOCK,c->r[15]);
            uint32_t op;ctr_mem_result r=ctr_memory_read(p->memory,c->r[15],c->thumb?2:4,CTR_MEM_EXEC,&op);
            if(r!=CTR_MEM_OK){p->memory_fault=r;ctr_recomp_abort(c,CTR_CPU_MEMORY_FAULT,c->r[15]);}
            if(ctr_recomp_instruction(c,c->r[15],op))interpret(c,c->r[15],op);
        } else code(c);
        if(c->exit==EXIT_SVC){export_cpu(e);return CTR_CPU_SVC;}
        if(c->exit==EXIT_BUDGET)break;
        if(c->exit==EXIT_UNWIND) { c->exit=EXIT_NONE;c->depth=0; }
    }
    export_cpu(e);return CTR_CPU_LIMIT;
}
static bool matches(const ctr_guest *g) {
    const Origins *o=&recomp_origins[0];
    if(o->piece_count>1000000u)return false;
    for(uint32_t i=0;i<o->piece_count;++i) {
        const Piece *p=&o->pieces[i];uint64_t hash=UINT64_C(14695981039346656037);
        if(p->end<=p->start || (uint64_t)p->end-p->start>CTR_GUEST_MAX_BYTES)return false;
        for(uint32_t a=p->start;a<p->end;++a) {
            uint32_t n;
            if(ctr_memory_read(&g->memory,a,1,CTR_MEM_EXEC,&n)!=CTR_MEM_OK)return false;
            hash=(hash^n)*UINT64_C(1099511628211);
        }
        if(hash!=p->hash)return false;
    }
    return o->piece_count!=0;
}
bool ctr_recomp_attach(ctr_guest *g,bool fallback) {
    if(g->executor || g->state!=CTR_GUEST_READY || recomp_abi!=4 || recomp_generation!=3 ||
        recomp_module_count || !recomp_entry_count || recomp_entry_count>10000000u || !matches(g))return false;
    for(uint32_t i=1;i<recomp_entry_count;++i)if(recomp_entries[i-1].address>=recomp_entries[i].address)return false;
    Engine *e=calloc(1,sizeof(*e));if(!e)return false;
    e->cpu=&g->cpu;e->fallback=fallback;e->code_generation=g->memory.code_generation;
    e->context.vfp=e->vfp;e->context.fpscr=&e->fpscr;e->context.host=&callbacks;e->context.user=e;
    g->executor=execute;g->executor_context=e;g->executor_close=free;return true;
}
bool ctr_recomp_vfp(const ctr_guest *g,unsigned word,uint32_t *value) {
    if(g->executor!=execute || word>=32 || !value)return false;
    const Engine *e=g->executor_context;*value=e->vfp[word];return true;
}
