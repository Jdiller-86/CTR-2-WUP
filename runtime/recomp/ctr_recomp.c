/* Copyright (c) 2026 CTR2WUP contributors. SPDX-License-Identifier: MIT */
#include "ctr_recomp.h"
#include "recomp.h"
#include "ctr_module.h"
#include <limits.h>
#include <setjmp.h>
#include <stdlib.h>

extern const uint32_t recomp_abi,recomp_generation,recomp_entry_count,recomp_module_count;
extern const Entry recomp_entries[];
extern const Origins recomp_origins[];
typedef struct {
    RecompFast fast;
    struct { uint32_t base,end; const uint8_t *storage; } previous_read;
    struct { uint32_t base,end; uint8_t *storage; } previous_write;
    ctr_guest *guest;
    uint64_t unwinds;
    bool fallback;
    bool batch_uncontended;
    uint32_t fpscr_mask;
    int32_t accounted_budget;
    /* The emitter writes an ARM LDM base before reads. Preserve the checked
       profile's original base on an abort while retaining earlier loaded words. */
    unsigned base_register;
    uint32_t base_value;
    jmp_buf escape;
    ctr_cpu_result fault;
    uint32_t lookup_base,lookup_pages;
    uint32_t *lookup_starts;
    struct { uint32_t addr; Code code; } lookup_cache[4096];
} Engine;
enum { LOOKUP_PAGE_SHIFT=8, LOOKUP_MAX_PAGES=65536 };
_Static_assert(sizeof(float)==4 && sizeof(double)==8,"IEEE scalar sizes required");
_Static_assert(offsetof(Context,budget)==72 && offsetof(Context,read_pages)==96,"ABI 4 fixed-width prefix");
_Static_assert(offsetof(Engine,fast)==0 && offsetof(RecompFast,context)==0,"Fast state prefix");

static bool cached_span_valid(const ctr_memory *mem,uint32_t base,uint32_t size,const uint8_t *storage,unsigned perm) {
    if(!storage || ctr_memory_backing_watched(mem,storage,size))return false;
    for(size_t i=0;i<mem->count;++i) {
        const ctr_mapping *m=&mem->maps[i];
        if((m->permissions&perm)==perm && base>=m->base && (uint64_t)base+size<=(uint64_t)m->base+m->size &&
           storage==m->storage+(base-m->base)) {
            if(perm==CTR_MEM_WRITE) {
                uintptr_t start=(uintptr_t)storage,end=start+size;
                for(size_t j=0;j<mem->count;++j) {
                    const ctr_mapping *o=&mem->maps[j];
                    if((o->permissions&CTR_MEM_EXEC) && start<(uintptr_t)o->storage+o->size && (uintptr_t)o->storage<end)
                        return false;
                }
            }
            return true;
        }
    }
    return false;
}
static inline void set_read_cache(Engine *e,uint32_t base,uint32_t end,const uint8_t *storage) {
    e->fast.rd_base=base;e->fast.rd_storage=storage;
    e->fast.rd_end=storage?end:0u;
    e->fast.rd_limit=(storage && end>=4u)?(end-3u):0u;
}
static inline void set_write_cache(Engine *e,uint32_t base,uint32_t end,uint8_t *storage) {
    e->fast.rw_base=base;e->fast.rw_storage=storage;
    e->fast.rw_end=storage?end:0u;
    e->fast.rw_limit=(storage && end>=4u && !(((uintptr_t)storage-base)&3u))?(end-3u):0u;
}
static void import_cpu_mode(Engine *e,bool copy_vfp) {
    Context *c=&e->fast.context;ctr_cpu *p=e->fast.cpu;
    /* Host services can capture new resource keys between translated slices. */
    e->fast.dirty_page_plus_one=0;e->fast.dirty_page2_plus_one=0;
    /* Host services and interpreter bridges can alter mappings. Generated stores
       use checked callbacks for executable backing and abort on invalidation. */
    uint64_t map_version=p->memory->resource_mapping_version;
    if(map_version!=e->fast.map_version || map_version==UINT64_MAX) {
        if(map_version==UINT64_MAX) {
            set_read_cache(e,0,0,NULL);set_write_cache(e,0,0,NULL);
            e->previous_read.storage=NULL;e->previous_write.storage=NULL;
        } else {
            if(!cached_span_valid(p->memory,e->fast.rd_base,e->fast.rd_end,e->fast.rd_storage,CTR_MEM_READ))set_read_cache(e,0,0,NULL);
            if(!cached_span_valid(p->memory,e->fast.rw_base,e->fast.rw_end,e->fast.rw_storage,CTR_MEM_WRITE))set_write_cache(e,0,0,NULL);
            if(!cached_span_valid(p->memory,e->previous_read.base,e->previous_read.end,e->previous_read.storage,CTR_MEM_READ))e->previous_read.storage=NULL;
            if(!cached_span_valid(p->memory,e->previous_write.base,e->previous_write.end,e->previous_write.storage,CTR_MEM_WRITE))e->previous_write.storage=NULL;
        }
        e->fast.map_version=map_version;
    }
    if(e->fast.code_generation!=p->memory->code_generation)e->fast.rx_size=0;
    memcpy(c->r,p->r,sizeof(c->r));
    if(copy_vfp) { memcpy(e->fast.vfp,p->vfp,sizeof(e->fast.vfp));e->fast.fpscr=p->fpscr; }
    c->n=p->cpsr>>31;c->z=(p->cpsr>>30)&1;c->c=(p->cpsr>>29)&1;c->v=(p->cpsr>>28)&1;
    c->q=(p->cpsr>>27)&1;c->ge=(p->cpsr>>16)&15;c->thumb=(p->cpsr&CTR_FLAG_T)!=0;
    c->exclusive=p->exclusive_valid && p->exclusive_size==4 && p->exclusive_generation==p->memory->generation;
    c->exclusive_address=p->exclusive_address;
    c->tls=p->tls_address;
}
static void import_cpu(Engine *e) { import_cpu_mode(e,true); }
static void export_cpu_mode(Engine *e,bool copy_vfp) {
    Context *c=&e->fast.context;ctr_cpu *p=e->fast.cpu;
    if(c->budget<0)c->budget=-c->budget;
    p->instructions+=(uint32_t)(e->accounted_budget-c->budget);
    e->accounted_budget=c->budget;
    memcpy(p->r,c->r,sizeof(p->r));
    if(copy_vfp) { memcpy(p->vfp,e->fast.vfp,sizeof(e->fast.vfp));p->fpscr=e->fast.fpscr; }
    p->cpsr=(p->cpsr&~0xf80f0020u)|((uint32_t)c->n<<31)|((uint32_t)c->z<<30)|
        ((uint32_t)c->c<<29)|((uint32_t)c->v<<28)|((uint32_t)c->q<<27)|((uint32_t)c->ge<<16)|
        (c->thumb?CTR_FLAG_T:0);
    p->exclusive_valid=c->exclusive!=0;p->exclusive_address=c->exclusive_address;
    p->exclusive_size=4;
    p->exclusive_generation=p->memory->generation;p->svc_number=c->svc;
}
static void export_cpu(Engine *e) { export_cpu_mode(e,true); }
_Noreturn void ctr_recomp_abort(Context *c,ctr_cpu_result result,uint32_t detail) {
    Engine *e=c->user;e->fault=result;
    if(result==CTR_CPU_MEMORY_FAULT) {
        if(e->fast.rollback_base)c->r[e->base_register]=e->base_value;
        else if(!c->thumb && detail!=c->r[15]) {
            uint32_t op=0;
            if(ctr_memory_read(e->fast.cpu->memory,c->r[15],4,CTR_MEM_EXEC,&op)==CTR_MEM_OK &&
               CTR_INSN_FAST_ARM_MULT(op) && (op&0x00300000u)==0x00300000u) {
                unsigned rn=(op>>16)&15u,bytes=0;
                for(unsigned m=op&0xffffu;m;m&=m-1u)bytes+=4u;
                c->r[rn]=(op&0x00800000u)?c->r[rn]-bytes:c->r[rn]+bytes;
            }
        }
    }
    export_cpu(e);ctr_cpu_stop(e->fast.cpu,result,detail);
    longjmp(e->escape,1);
}
static inline bool cond_passed(const Context *c, unsigned cond) {
    switch (cond) {
    case 0: return c->z;
    case 1: return !c->z;
    case 2: return c->c;
    case 3: return !c->c;
    case 4: return c->n;
    case 5: return !c->n;
    case 6: return c->v;
    case 7: return !c->v;
    case 8: return c->c && !c->z;
    case 9: return !c->c || c->z;
    case 10: return c->n == c->v;
    case 11: return c->n != c->v;
    case 12: return !c->z && c->n == c->v;
    case 13: return c->z || c->n != c->v;
    case 14: return true;
    default: return false;
    }
}
static int instruction(Context *c,uint32_t address,uint32_t opcode,bool resume_scalar_vfp) {
    Engine *e=c->user;
    if(c->budget<0)c->budget=-c->budget;
    e->fast.cpu->module_hash=e->guest->program.module_hash;
    if(c->depth>e->fast.maximum_depth)e->fast.maximum_depth=c->depth;
    e->fast.rollback_base=false;
    if(c->thumb)e->fast.single_data=(opcode&0xf000u)==0x5000u || (opcode&0xe000u)==0x6000u ||
        (opcode&0xf000u)==0x8000u || (opcode&0xf000u)==0x9000u || (opcode&0xf800u)==0x4800u;
    else {
        unsigned kind=(opcode>>5)&3u;
        e->fast.single_data=((opcode&0x0c000000u)==0x04000000u && !((opcode&0x100000u) && ((opcode>>12)&15u)==15u)) ||
            ((opcode&0x0e000090u)==0x00000090u && kind && (kind==1 || (opcode&0x100000u)));
    }
    c->r[15]=address;
    if (!c->budget) { c->exit=EXIT_BUDGET;return 0; }
    const ctr_memory *mem=e->fast.cpu->memory;
    if (mem->code_generation!=e->fast.code_generation)
        ctr_recomp_abort(c,CTR_CPU_CODE_INVALIDATED,address);
    uint32_t actual;
    unsigned sz=c->thumb?2u:4u;
    uint32_t offset=address-e->fast.rx_base;
    if ((uint64_t)offset+sz<=e->fast.rx_size && !(address&(sz-1u))) {
        const uint8_t *p=e->fast.rx_storage+offset;
        actual=sz==2u?((uint32_t)p[0]|((uint32_t)p[1]<<8)):
            ((uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24));
    } else {
        ctr_mem_result r=ctr_memory_read(mem,address,sz,CTR_MEM_EXEC,&actual);
        if (r!=CTR_MEM_OK) { e->fast.cpu->memory_fault=r;ctr_recomp_abort(c,CTR_CPU_MEMORY_FAULT,address); }
        for(size_t i=0;i<mem->count;++i) {
            const ctr_mapping *m=&mem->maps[i];
            if((m->permissions&CTR_MEM_EXEC) && address>=m->base && (uint64_t)address+sz<=(uint64_t)m->base+m->size) {
                uint64_t end=((uint64_t)m->base+m->size)&~UINT64_C(3);
                e->fast.rx_base=m->base;e->fast.rx_size=end>m->base?(uint32_t)(end-m->base):0u;
                e->fast.rx_storage=m->storage;
                break;
            }
        }
    }
    if (actual!=opcode) ctr_recomp_abort(c,CTR_CPU_CODE_INVALIDATED,address);
    --c->budget;
    uint32_t flags=((uint32_t)c->n<<31)|((uint32_t)c->z<<30)|
        ((uint32_t)c->c<<29)|((uint32_t)c->v<<28);
    bool fpscr_transfer=(opcode&0x0fff0fffu)==0x0ee10a10u || (opcode&0x0fff0fffu)==0x0ef10a10u;
    bool fp_memory=(opcode&0x0f200f00u)==0x0d000a00u || (opcode&0x0f200f00u)==0x0d000b00u;
    bool fp_multiple=(opcode&0x0e000e00u)==0x0c000a00u &&
        (((opcode&0x01000000u)==0 && (opcode&0x00800000u)) ||
         ((opcode&0x01800000u)==0x01000000u && (opcode&0x00200000u)));
    fp_memory=fp_memory || fp_multiple;
    /* Preserve controls across integer/kernel work. FP data operations under
       unsupported controls still stop before effects; FPSCR bit transport does
       not perform floating arithmetic. */
    bool fp=!c->thumb && ((opcode&0x0e000000u)==0x0c000000u || (opcode&0x0f000000u)==0x0e000000u) &&
        (((opcode>>8)&15u)==10u || ((opcode>>8)&15u)==11u);
    if(fp && opcode>>28==15u)ctr_recomp_abort(c,CTR_CPU_UNSUPPORTED,opcode);
    /* Explicit recovery mode uses shared scalar VFP for float-to-int conversions
       and non-default FPSCR modes; scalar IEEE VFP ops run natively in AOT. */
    bool fp_to_int=fp && (opcode&0x0fbe0e50u)==0x0ebc0a40u;
    bool fp_core_transfer=fp && (((opcode&0x0fe00f7fu)==0x0e000a10u && ((opcode>>12)&15u)!=15u) ||
        ((opcode&0x0fe00ed0u)==0x0c400a10u && ((opcode>>12)&15u)!=15u && ((opcode>>16)&15u)!=15u));
    if(fp && e->fallback && !fpscr_transfer && !fp_memory && !fp_core_transfer &&
       (fp_to_int || (*c->fpscr&e->fpscr_mask)) && (opcode>>28==14u || cond_passed(c,opcode>>28))) {
        if((opcode&0x0f000e10u)==0x0e000a00u) {
            uint32_t detail;
            if(!ctr_vfp_arm_data(c->vfp,c->fpscr,opcode,&detail))
                ctr_recomp_abort(c,CTR_CPU_UNSUPPORTED,opcode);
            c->r[15]=address+4u;
        } else {
            export_cpu(e);
            ctr_cpu_result result=ctr_cpu_vfp_arm(e->fast.cpu,opcode);
            if(result!=CTR_CPU_OK) { import_cpu(e);ctr_recomp_abort(c,result,opcode); }
            e->fast.cpu->r[15]=address+4u;
            import_cpu(e);
        }
        ++e->fast.cpu->fallback_instructions;
        if(resume_scalar_vfp && (opcode&0x0f000e10u)==0x0e000a00u)return CTR_VFP_HANDLED;
        c->exit=EXIT_UNWIND;return 0;
    }
    if(fp && ctr_cpu_condition_cpsr(flags,opcode>>28)) {
        if(fpscr_transfer && (opcode&0x00100000u)==0 && ((opcode>>12)&15u)==15u)
            ctr_recomp_abort(c,CTR_CPU_UNSUPPORTED,opcode);
        if(fp_memory && (((opcode&0xf00u)==0xb00u && (opcode&0x400000u)) ||
            (((opcode>>16)&15u)==15u && !(opcode&0x100000u))))ctr_recomp_abort(c,CTR_CPU_UNSUPPORTED,opcode);
        if(fp_multiple) {
            unsigned first=((opcode>>12)&15u)*2u,words=opcode&255u;
            bool wide=(opcode&0x100u)!=0;
            if(!wide)first|=(opcode>>22)&1u;
            if(!words || first+words>32u || (wide && (words&1u)) || ((opcode>>16)&15u)==15u)
                ctr_recomp_abort(c,CTR_CPU_UNSUPPORTED,opcode);
        }
        if(!fpscr_transfer && !fp_memory && !(e->fallback && fp_core_transfer) &&
           (*c->fpscr&e->fpscr_mask))ctr_recomp_abort(c,CTR_CPU_UNSUPPORTED,*c->fpscr);
    }
    uint32_t media=opcode&0x0ff00ff0u;
    if(!c->thumb && (opcode&0x0f900090u)==0x01000080u &&
       (opcode>>28==15 || ctr_cpu_condition_cpsr(flags,opcode>>28))) {
        bool smlaxy=(opcode&0x0ff00090u)==0x01000080u && ((opcode>>12)&15u)!=15u;
        bool smulxy=(opcode&0x0ff0f090u)==0x01600080u;
        if(opcode>>28==15u || (!smlaxy && !smulxy) || ((opcode>>16)&15u)==15 ||
           ((opcode>>8)&15u)==15 || (opcode&15u)==15)
            ctr_recomp_abort(c,CTR_CPU_UNSUPPORTED,opcode);
    }
    if(!c->thumb && (media==0x06500f90u || media==0x06500ff0u || media==0x06800fb0u) &&
        ctr_cpu_condition_cpsr(flags,opcode>>28) &&
        (((opcode>>16)&15u)==15 || ((opcode>>12)&15u)==15 || (opcode&15u)==15))
        ctr_recomp_abort(c,CTR_CPU_UNSUPPORTED,opcode);
    if(!c->thumb && (opcode&0x0e000000u)==0x08000000u &&
        (opcode>>28==15u || ctr_cpu_condition_cpsr(flags,opcode>>28))) {
        unsigned rn=(opcode>>16)&15u;bool load=(opcode&0x100000u)!=0,writeback=(opcode&0x200000u)!=0;
        if(opcode>>28==15u || (opcode&0x400000u) ||
            !ctr_cpu_multiple_supported(rn,(uint16_t)opcode,writeback,load,false))
            ctr_recomp_abort(c,CTR_CPU_UNSUPPORTED,opcode);
        if(load && writeback) { e->fast.rollback_base=true;e->base_register=rn;e->base_value=c->r[rn]; }
    }
    if(c->thumb && ((opcode&0xf600u)==0xb400u || (opcode&0xf000u)==0xc000u)) {
        bool stack=(opcode&0xf600u)==0xb400u,load=(opcode&0x800u)!=0;
        unsigned rn=stack?13u:(opcode>>8)&7u;uint16_t list=(uint16_t)(opcode&255u);
        if(stack && (opcode&0x100u))list|=(uint16_t)(1u<<(load?15:14));
        bool writeback=stack || !(load && (list&(1u<<rn)));
        if(!ctr_cpu_multiple_supported(rn,list,writeback,load,true))ctr_recomp_abort(c,CTR_CPU_UNSUPPORTED,opcode);
    }
    if((!c->thumb && (opcode&0x0fffffffu)==0x012fff3fu && ctr_cpu_condition_cpsr(flags,opcode>>28)) ||
        (c->thumb && opcode==0x47f8u))ctr_recomp_abort(c,CTR_CPU_UNSUPPORTED,opcode);
    bool barrier=!c->thumb && ((opcode&0x0fff0fffu)==0x0e070f9au || (opcode&0x0fff0fffu)==0x0e070fbau);
    if(barrier && ctr_cpu_condition_cpsr(flags,opcode>>28)) {
        if(((opcode>>12)&15u)==15u)ctr_recomp_abort(c,CTR_CPU_UNSUPPORTED,opcode);
        ctr_cpu_barrier();
    }
    /* The pinned emitter silently removes some CP15 cache operations. This
       profile implements only the user TLS read; reject other executed CP15
       operations until their coherency/privilege behavior is implemented. */
    if (!c->thumb && ((opcode&0xfffffff0u)==0xf57ff040u ||
        (opcode&0xfffffff0u)==0xf57ff050u || (opcode&0xfffffff0u)==0xf57ff060u))
        ctr_recomp_abort(c,CTR_CPU_UNSUPPORTED,opcode);
    if (!c->thumb && (opcode&0x0f000f10u)==0x0e000f10u &&
        ((opcode>>28)==15u || (!barrier && (opcode&0x0fff0fffu)!=0x0e1d0f70u) || ((opcode>>12)&15u)==15u)) {
        if (opcode>>28==15u || ctr_cpu_condition_cpsr(flags,opcode>>28))
            ctr_recomp_abort(c,CTR_CPU_UNSUPPORTED,opcode);
    }
    return 1;
}
static inline bool recomp_multiple_ok(unsigned rn,uint16_t list,bool writeback,bool load,bool thumb) {
    if(rn>=15u || !list || (thumb && !load && (list&0x8000u)))return false;
    return !(writeback && (list&(1u<<rn)) && (load || (list&((1u<<rn)-1u))));
}
int ctr_recomp_instruction(Context *c,uint32_t address,uint32_t opcode) {
    Engine *e=c->user;
    unsigned align_mask=c->thumb?1u:3u;
    if(c->budget>0 && !(address&align_mask) &&
       e->fast.cpu->memory->code_generation==e->fast.code_generation) {
        if(!c->thumb && (opcode>>28)<15u) {
            if((opcode&0x0e000000u)==0x08000000u && !(opcode&0x00400000u)) {
                unsigned rn=(opcode>>16)&15u;
                bool load=(opcode&0x100000u)!=0,writeback=(opcode&0x200000u)!=0;
                if(recomp_multiple_ok(rn,(uint16_t)opcode,writeback,load,false)) {
                    c->r[15]=address;--c->budget;e->fast.single_data=false;
                    if(load && writeback && ((opcode>>28)==14u || cond_passed(c,opcode>>28))) {
                        e->fast.rollback_base=true;e->base_register=rn;e->base_value=c->r[rn];
                    } else e->fast.rollback_base=false;
                    return 1;
                }
            } else {
                bool ok=false;
                if(((opcode&0x0f200f00u)==0x0d000a00u ||
                    ((opcode&0x0f200f00u)==0x0d000b00u && !(opcode&0x400000u))) &&
                   (((opcode>>16)&15u)!=15u || (opcode&0x100000u)))
                    ok=true;
                else if(e->fallback &&
                        (((opcode&0x0fe00f7fu)==0x0e000a10u && ((opcode>>12)&15u)!=15u) ||
                         ((opcode&0x0fe00ed0u)==0x0c400a10u && ((opcode>>12)&15u)!=15u && ((opcode>>16)&15u)!=15u)))
                    ok=true;
                else if((opcode&0x0fff0fffu)==0x0ef10a10u ||
                        ((opcode&0x0fff0fffu)==0x0ee10a10u && ((opcode>>12)&15u)!=15u) ||
                        ((opcode&0x0fff0fffu)==0x0e1d0f70u && ((opcode>>12)&15u)!=15u))
                    ok=true;
                else if((opcode&0x0e000e00u)==0x0c000a00u &&
                        (((opcode&0x01000000u)==0 && (opcode&0x00800000u)) ||
                         ((opcode&0x01800000u)==0x01000000u && (opcode&0x00200000u)))) {
                    unsigned first=((opcode>>12)&15u)*2u | (!(opcode&0x100u)?((opcode>>22)&1u):0u);
                    unsigned words=opcode&255u;
                    ok=words && first+words<=32u && !((opcode&0x100u) && (words&1u)) && ((opcode>>16)&15u)!=15u;
                }
                if(ok) {
                    c->r[15]=address;--c->budget;
                    e->fast.rollback_base=false;e->fast.single_data=false;
                    return 1;
                }
            }
        } else if(c->thumb && opcode<=0xffffu && ((opcode&0xf600u)==0xb400u || (opcode&0xf000u)==0xc000u)) {
            bool stack=(opcode&0xf600u)==0xb400u,load=(opcode&0x800u)!=0;
            unsigned rn=stack?13u:(opcode>>8)&7u;uint16_t list=(uint16_t)(opcode&255u);
            if(stack && (opcode&0x100u))list|=(uint16_t)(1u<<(load?15:14));
            bool writeback=stack || !(load && (list&(1u<<rn)));
            if(recomp_multiple_ok(rn,list,writeback,load,true)) {
                c->r[15]=address;--c->budget;
                e->fast.rollback_base=false;e->fast.single_data=false;
                return 1;
            }
        }
    }
    return instruction(c,address,opcode,false);
}
static inline void refresh_rx_cache(Engine *e,const ctr_memory *mem,uint32_t address,unsigned sz) {
    uint32_t actual;
    ctr_mem_result r=ctr_memory_read(mem,address,sz,CTR_MEM_EXEC,&actual);
    if(r!=CTR_MEM_OK) { e->fast.cpu->memory_fault=r;ctr_recomp_abort(&e->fast.context,CTR_CPU_MEMORY_FAULT,address); }
    for(size_t i=0;i<mem->count;++i) {
        const ctr_mapping *m=&mem->maps[i];
        if((m->permissions&CTR_MEM_EXEC) && address>=m->base && (uint64_t)address+sz<=(uint64_t)m->base+m->size) {
            uint64_t end=((uint64_t)m->base+m->size)&~UINT64_C(3);
            e->fast.rx_base=m->base;e->fast.rx_size=end>m->base?(uint32_t)(end-m->base):0u;
            e->fast.rx_storage=m->storage;
            break;
        }
    }
}
int ctr_recomp_step_slow(Context *c,uint32_t address,bool single_data) {
    Engine *e=c->user;
    if(c->budget<0)c->budget=-c->budget;
    e->fast.cpu->module_hash=e->guest->program.module_hash;
    if(c->depth>e->fast.maximum_depth)e->fast.maximum_depth=c->depth;
    e->fast.rollback_base=false;
    e->fast.single_data=single_data;
    c->r[15]=address;
    if(!c->budget) { c->exit=EXIT_BUDGET;return 0; }
    const ctr_memory *mem=e->fast.cpu->memory;
    if(mem->code_generation!=e->fast.code_generation)
        ctr_recomp_abort(c,CTR_CPU_CODE_INVALIDATED,address);
    unsigned sz=c->thumb?2u:4u;
    uint32_t offset=address-e->fast.rx_base;
    if((uint64_t)offset+sz>e->fast.rx_size || (address&(sz-1u)))
        refresh_rx_cache(e,mem,address,sz);
    --c->budget;
    return 1;
}
_Noreturn void ctr_recomp_unwind(Context *c) {
    Engine *e=c->user;
    longjmp(e->escape,c->exit==EXIT_BUDGET?2:3);
}
void ctr_recomp_step_slow_void(Context *c,uint32_t address,bool single_data) {
    if(!ctr_recomp_step_slow(c,address,single_data))ctr_recomp_unwind(c);
}
void ctr_recomp_step_run_slow(Context *c,uint32_t address,int32_t count) {
    Engine *e=c->user;
    if(c->budget<0)c->budget=-c->budget;
    e->fast.cpu->module_hash=e->guest->program.module_hash;
    if(c->depth>e->fast.maximum_depth)e->fast.maximum_depth=c->depth;
    e->fast.rollback_base=false;
    e->fast.single_data=false;
    c->r[15]=address;
    if(!c->budget) { c->exit=EXIT_BUDGET;ctr_recomp_unwind(c); }
    const ctr_memory *mem=e->fast.cpu->memory;
    if(mem->code_generation!=e->fast.code_generation)
        ctr_recomp_abort(c,CTR_CPU_CODE_INVALIDATED,address);
    uint32_t offset=address-e->fast.rx_base;
    if(!e->fast.rx_size || (uint64_t)offset+4u>e->fast.rx_size || (address&3u))
        refresh_rx_cache(e,mem,address,4u);
    if(c->budget>=count) {
        c->r[15]=address+(uint32_t)(count-1)*4u;
        c->budget-=count;
        return;
    }
    export_cpu(e);
    uint64_t before=e->fast.cpu->instructions;
    ctr_cpu_result step_r=ctr_cpu_step(e->fast.cpu);
    uint32_t stepped=(uint32_t)(e->fast.cpu->instructions-before);
    import_cpu(e);
    c->budget-=(int32_t)stepped;
    e->accounted_budget=c->budget;
    if(step_r==CTR_CPU_SVC) {
        c->svc=e->fast.cpu->svc_number;
        c->exit=EXIT_SVC;
        ctr_recomp_unwind(c);
    }
    if(step_r!=CTR_CPU_OK) ctr_recomp_abort(c,step_r,address);
    c->exit=(c->budget<=0)?EXIT_BUDGET:EXIT_UNWIND;
    ctr_recomp_unwind(c);
}
void ctr_recomp_instruction_void(Context *c,uint32_t address,uint32_t opcode) {
    if(!ctr_recomp_instruction(c,address,opcode))ctr_recomp_unwind(c);
}
int ctr_recomp_scalar_vfp(Context *c,uint32_t address,uint32_t opcode) {
    Engine *e=c->user;
    if(c->budget>0 && !c->thumb && e->fallback &&
       e->fast.cpu->memory->code_generation==e->fast.code_generation &&
       (opcode>>28)<15u) {
        c->r[15]=address;
        --c->budget;
        unsigned cond=opcode>>28;
        if(cond!=14u && !cond_passed(c,cond)) {
            c->r[15]=address+4u;
            return CTR_VFP_HANDLED;
        }
        if(((opcode&0x0fbe0e50u)==0x0ebc0a40u) || (*c->fpscr&e->fpscr_mask)) {
            uint32_t detail;
            if(!ctr_vfp_arm_data(c->vfp,c->fpscr,opcode,&detail))
                ctr_recomp_abort(c,CTR_CPU_UNSUPPORTED,opcode);
            c->r[15]=address+4u;
            e->fast.cpu->fallback_instructions+=1u;
            return CTR_VFP_HANDLED;
        }
        return CTR_VFP_EMIT;
    }
    return instruction(c,address,opcode,true);
}
bool ctr_recomp_scalar_vfp_emit(Context *c,uint32_t address,uint32_t opcode) {
    int s=ctr_recomp_scalar_vfp(c,address,opcode);
    if(s==CTR_VFP_STOP)ctr_recomp_unwind(c);
    return s==CTR_VFP_EMIT;
}
static uint32_t read_value(Context *c,uint32_t a,unsigned width) {
    Engine *e=c->user;ctr_memory *mem=e->fast.cpu->memory;
    const uint8_t *p=NULL;
    if(!(a&(width-1u)) || e->fast.single_data) {
        if(e->fast.rd_storage && a-e->fast.rd_base<=e->fast.rd_end-width)
            p=e->fast.rd_storage+(a-e->fast.rd_base);
        else if(e->previous_read.storage && a-e->previous_read.base<=e->previous_read.end-width) {
            uint32_t b=e->fast.rd_base,end=e->fast.rd_end;const uint8_t *s=e->fast.rd_storage;
            set_read_cache(e,e->previous_read.base,e->previous_read.end,e->previous_read.storage);
            e->previous_read.base=b;e->previous_read.end=end;e->previous_read.storage=s;
            p=e->fast.rd_storage+(a-e->fast.rd_base);
        }
    }
    if(p) {
        if(width==4u)return load32(p);
        if(width==2u)return (uint32_t)p[0]|((uint32_t)p[1]<<8);
        return p[0];
    }
    uint32_t n;
    ctr_mem_result r=e->fast.single_data?ctr_memory_read_data(mem,a,width,&n):ctr_memory_read(mem,a,width,CTR_MEM_READ,&n);
    if (r!=CTR_MEM_OK) { e->fast.cpu->memory_fault=r;ctr_recomp_abort(c,CTR_CPU_MEMORY_FAULT,a); }
    for(size_t i=0;i<mem->count;++i) {
        const ctr_mapping *m=&mem->maps[i];
        if((m->permissions&CTR_MEM_READ) && m->size>=4u && a>=m->base && (uint64_t)a+width<=(uint64_t)m->base+m->size) {
            uint32_t base=m->base,end=m->size;const uint8_t *storage=m->storage;
#ifdef CTR_MEMORY_WATCH_SLOTS
            ctr_mapping span;
            if(!ctr_memory_unwatched_span(mem,m,a,width,&span))break;
            base=span.base;end=span.size;storage=span.storage;
#else
            if(ctr_memory_backing_watched(mem,m->storage,m->size))break;
#endif
            if(e->fast.rd_storage && e->fast.rd_base!=base) {
                e->previous_read.base=e->fast.rd_base;e->previous_read.end=e->fast.rd_end;
                e->previous_read.storage=e->fast.rd_storage;
            }
            set_read_cache(e,base,end,storage);
            break;
        }
    }
    return n;
}
static inline void ctr_recomp_note_store(Context *c,RecompFast *f,const uint8_t *p,unsigned width,unsigned words);
static void write_value(Context *c,uint32_t a,unsigned width,uint32_t n) {
    Engine *e=c->user;ctr_memory *mem=e->fast.cpu->memory;
    uint8_t *p=NULL;
    if(!(a&(width-1u)) || e->fast.single_data) {
        if(e->fast.rw_storage && a-e->fast.rw_base<=e->fast.rw_end-width)
            p=e->fast.rw_storage+(a-e->fast.rw_base);
        else if(e->previous_write.storage && a-e->previous_write.base<=e->previous_write.end-width) {
            uint32_t b=e->fast.rw_base,end=e->fast.rw_end;uint8_t *s=e->fast.rw_storage;
            set_write_cache(e,e->previous_write.base,e->previous_write.end,e->previous_write.storage);
            e->previous_write.base=b;e->previous_write.end=end;e->previous_write.storage=s;
            p=e->fast.rw_storage+(a-e->fast.rw_base);
        }
    }
    if(p) {
        if(width==4u)store32(p,n);
        else if(width==2u) {
            p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);
        } else {
            p[0]=(uint8_t)n;
        }
        ctr_recomp_note_store(c,&e->fast,p,width,1);
        return;
    }
    ctr_mem_result r=e->fast.single_data?ctr_memory_write_data(mem,a,width,n):ctr_memory_write(mem,a,width,n);
    if (r!=CTR_MEM_OK) { e->fast.cpu->memory_fault=r;ctr_recomp_abort(c,CTR_CPU_MEMORY_FAULT,a); }
    for(size_t i=0;i<mem->count;++i) {
        const ctr_mapping *m=&mem->maps[i];
        if((m->permissions&CTR_MEM_WRITE) && m->size>=4u && a>=m->base && (uint64_t)a+width<=(uint64_t)m->base+m->size) {
            bool aliases=false;
            uintptr_t m_start=(uintptr_t)m->storage,m_end=m_start+m->size;
            for(size_t j=0;j<mem->count;++j) {
                const ctr_mapping *o=&mem->maps[j];
                if((o->permissions&CTR_MEM_EXEC) && m_start<(uintptr_t)o->storage+o->size && (uintptr_t)o->storage<m_end) {
                    aliases=true;break;
                }
            }
            uint32_t base=m->base,end=m->size;uint8_t *storage=m->storage;
#ifdef CTR_MEMORY_WATCH_SLOTS
            ctr_mapping span;
            bool watched=!ctr_memory_unwatched_span(mem,m,a,width,&span);
            if(!watched){base=span.base;end=span.size;storage=span.storage;}
#else
            bool watched=ctr_memory_backing_watched(mem,m->storage,m->size);
#endif
            if(e->fast.rw_storage && e->fast.rw_base!=base) {
                e->previous_write.base=e->fast.rw_base;e->previous_write.end=e->fast.rw_end;
                e->previous_write.storage=e->fast.rw_storage;
            }
            set_write_cache(e,base,end,(aliases || watched)?NULL:storage);
            break;
        }
    }
    c->exclusive=0;
    if (mem->code_generation!=e->fast.code_generation)
        ctr_recomp_abort(c,CTR_CPU_CODE_INVALIDATED,a);
}
static uint8_t read8(Context *c,uint32_t a) { return (uint8_t)read_value(c,a,1); }
static uint16_t read16(Context *c,uint32_t a) { return (uint16_t)read_value(c,a,2); }
static uint32_t read32(Context *c,uint32_t a) { return read_value(c,a,4); }
static void write8(Context *c,uint32_t a,uint8_t n) { write_value(c,a,1,n); }
static void write16(Context *c,uint32_t a,uint16_t n) { write_value(c,a,2,n); }
static void write32(Context *c,uint32_t a,uint32_t n) { write_value(c,a,4,n); }
void ctr_recomp_note_store_cold(RecompFast *f,uintptr_t begin,uintptr_t last) {
    uint64_t ver=*f->mem_resource_clock!=UINT64_MAX?++*f->mem_resource_clock:*f->mem_resource_clock;
    f->mem_resource_versions[(begin^(begin>>10u))&(CTR_MEMORY_RESOURCE_SLOTS-1u)]=ver;
    if(last!=begin)f->mem_resource_versions[(last^(last>>10u))&(CTR_MEMORY_RESOURCE_SLOTS-1u)]=ver;
    f->dirty_page2_plus_one=f->dirty_page_plus_one;
    f->dirty_page_plus_one=last+1u;
}
int ctr_recomp_step_vfp(Context *c,uint32_t address) {
    RecompFast *f=(RecompFast *)c;
    int32_t b=c->budget;
    if(b<=0 || (f->fpscr&0x04ff9f00u))return 0;
    c->r[15]=address;
    c->budget=b-1;
    return 1;
}
static inline void ctr_recomp_step_reg_body(Context *c,uint32_t address) {
    int32_t b=c->budget;
    if(b<=0){ctr_recomp_step_slow_void(c,address,false);return;}
    c->r[15]=address;
    c->budget=b-1;
}
void ctr_recomp_step_reg(Context *c,uint32_t address) { ctr_recomp_step_reg_body(c,address); }
void ctr_recomp_step_reg4(Context *c) { ctr_recomp_step_reg_body(c,c->r[15]+4u); }
static inline void ctr_recomp_step_mem_body(Context *c,uint32_t address,bool single_data) {
    RecompFast *f=(RecompFast *)c;
    int32_t b=c->budget;
    if(b<=0){ctr_recomp_step_slow_void(c,address,single_data);return;}
    c->r[15]=address;
    c->budget=b-1;
    f->rollback_base=false;
    f->single_data=single_data;
}
void ctr_recomp_step_mem00(Context *c,uint32_t address) { ctr_recomp_step_mem_body(c,address,false); }
void ctr_recomp_step_mem00_4(Context *c) { ctr_recomp_step_mem_body(c,c->r[15]+4u,false); }
void ctr_recomp_step_mem10(Context *c,uint32_t address) { ctr_recomp_step_mem_body(c,address,!c->thumb); }
void ctr_recomp_step_mem01(Context *c,uint32_t address) { ctr_recomp_step_mem_body(c,address,c->thumb!=0); }
void ctr_recomp_step_mem11(Context *c,uint32_t address) { ctr_recomp_step_mem_body(c,address,true); }
void ctr_recomp_step_mem11_4(Context *c) { ctr_recomp_step_mem_body(c,c->r[15]+4u,true); }
void ctr_recomp_step_run(Context *c,uint32_t address,int32_t count) {
    int32_t b=c->budget;
    if(b<count){ctr_recomp_step_run_slow(c,address,count);return;}
    c->r[15]=address+(uint32_t)(count-1)*4u;
    c->budget=b-count;
}
void ctr_recomp_step_run4(Context *c,int32_t count) {
    int32_t b=c->budget;
    if(b<count){ctr_recomp_step_run_slow(c,c->r[15]+4u,count);return;}
    c->r[15]+=(uint32_t)count*4u;
    c->budget=b-count;
}
uint8_t ctr_recomp_read8(Context *c,uint32_t a) {
    RecompFast *f=(RecompFast *)c;
    uint32_t off=a-f->rd_base;
    if(off<f->rd_end)return f->rd_storage[off];
    return c->host->read8(c,a);
}
uint16_t ctr_recomp_read16(Context *c,uint32_t a) {
    RecompFast *f=(RecompFast *)c;
    uint32_t off=a-f->rd_base;
    if(!(a&1u) && off<f->rd_limit) {
        const uint8_t *p=f->rd_storage+off;
        return (uint16_t)((uint32_t)p[0]|((uint32_t)p[1]<<8));
    }
    return c->host->read16(c,a);
}
uint32_t ctr_recomp_read32(Context *c,uint32_t a) {
    RecompFast *f=(RecompFast *)c;
    uint32_t off=a-f->rd_base;
    if(!(a&3u) && off<f->rd_limit) {
        const uint8_t *p=f->rd_storage+off;
        return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
    }
    return c->host->read32(c,a);
}
static inline void ctr_recomp_note_store1(Context *c,RecompFast *f,const uint8_t *p) {
    *f->mem_generation+=1u;
    c->exclusive=0;
    uintptr_t page_plus_one=((uintptr_t)p>>12u)+1u;
    if(f->dirty_page_plus_one==page_plus_one || f->dirty_page2_plus_one==page_plus_one)return;
    ctr_recomp_note_store_cold(f,page_plus_one-1u,page_plus_one-1u);
}
static inline void ctr_recomp_note_store(Context *c,RecompFast *f,const uint8_t *p,unsigned width,unsigned words) {
    *f->mem_generation+=words;
    c->exclusive=0;
    uintptr_t begin=(uintptr_t)p>>12u,last=((uintptr_t)p+width-1u)>>12u;
    if(last==begin && (f->dirty_page_plus_one==begin+1u || f->dirty_page2_plus_one==begin+1u))return;
    ctr_recomp_note_store_cold(f,begin,last);
}
void ctr_recomp_write8(Context *c,uint32_t a,uint8_t n) {
    RecompFast *f=(RecompFast *)c;
    uint32_t off=a-f->rw_base;
    if(off<f->rw_end) {
        uint8_t *p=f->rw_storage+off;*p=n;
        ctr_recomp_note_store1(c,f,p);return;
    }
    c->host->write8(c,a,n);
}
void ctr_recomp_write16(Context *c,uint32_t a,uint16_t n) {
    RecompFast *f=(RecompFast *)c;
    uint32_t off=a-f->rw_base;
    if(!(a&1u) && off<f->rw_limit) {
        uint8_t *p=f->rw_storage+off;
        p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);
        ctr_recomp_note_store1(c,f,p);return;
    }
    c->host->write16(c,a,n);
}
void ctr_recomp_write32(Context *c,uint32_t a,uint32_t n) {
    RecompFast *f=(RecompFast *)c;
    uint32_t off=a-f->rw_base;
    if(!(a&3u) && off<f->rw_limit) {
        uint8_t *p=f->rw_storage+off;
        p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);p[2]=(uint8_t)(n>>16);p[3]=(uint8_t)(n>>24);
        ctr_recomp_note_store1(c,f,p);return;
    }
    c->host->write32(c,a,n);
}
static inline bool ctr_recomp_span_fits(uint32_t a,uint32_t n,uint32_t base,uint32_t size,const uint8_t *storage) {
    uint32_t off=a-base;
    return storage && n && n<=sizeof(((RecompFast *)0)->vfp) && !((a|n)&3u) &&
           off<size && n<=size-off && !((uintptr_t)(storage+off)&3u);
}
const uint8_t *ctr_recomp_read_span(Context *c,uint32_t a,uint32_t n) {
    RecompFast *f=(RecompFast *)c;
    if(!ctr_recomp_span_fits(a,n,f->rd_base,f->rd_end,f->rd_storage)){f->span_addr=a;return NULL;}
    return f->rd_storage+(a-f->rd_base);
}
uint8_t *ctr_recomp_write_span(Context *c,uint32_t a,uint32_t n) {
    RecompFast *f=(RecompFast *)c;
    if(!ctr_recomp_span_fits(a,n,f->rw_base,f->rw_end,f->rw_storage)){f->span_addr=a;return NULL;}
    uint8_t *p=f->rw_storage+(a-f->rw_base);
    ctr_recomp_note_store(c,f,p,n,n/4u);
    return p;
}
void ctr_recomp_call(Context *c,Code code) {
    uint32_t d=c->depth;
    if(d>=CTR_RECOMP_CALL_LIMIT){c->exit=EXIT_UNWIND;ctr_recomp_unwind(c);}
    c->depth=d+1u;
    if(d+1u>((RecompFast *)c)->maximum_depth)((RecompFast *)c)->maximum_depth=d+1u;
    code(c);
    c->depth=d;
    if(c->exit)ctr_recomp_unwind(c);
}
void recomp_call(Context *c) { Code f=c->host->lookup(c,c->r[15]|c->thumb);if(f)f(c);else c->exit=EXIT_UNWIND; }
static uint32_t span_slow(Context *c,bool write,bool vfp,uint32_t spec) {
    RecompFast *f=(RecompFast *)c;
    uint32_t a=f->span_addr,last=0;
    if(vfp) {
        unsigned first=spec&31u,count=spec>>5;
        for(unsigned i=0;i<count;++i,a+=4u) {
            if(write)c->host->write32(c,a,c->vfp[first+i]);
            else c->vfp[first+i]=c->host->read32(c,a);
        }
        return 0;
    }
    for(unsigned i=0;spec;++i,spec>>=1) {
        if(!(spec&1u))continue;
        if(write)c->host->write32(c,a,c->r[i]);
        else { last=c->host->read32(c,a); if(i<15u)c->r[i]=last; }
        a+=4u;
    }
    return last;
}
uint32_t ctr_recomp_read_span_gpr(Context *c,uint32_t mask) { return span_slow(c,false,false,mask); }
void ctr_recomp_write_span_gpr(Context *c,uint32_t mask) { (void)span_slow(c,true,false,mask); }
void ctr_recomp_read_span_vfp(Context *c,uint32_t spec) { (void)span_slow(c,false,true,spec); }
void ctr_recomp_write_span_vfp(Context *c,uint32_t spec) { (void)span_slow(c,true,true,spec); }
static Code lookup(Context *c,uint32_t a) {
    Engine *e=c->user;
    if(a) {
        uint32_t slot=((a>>1)^(a>>13))&4095u;
        if(e->lookup_cache[slot].addr==a)return e->lookup_cache[slot].code;
        uint32_t lo=0,hi=recomp_entry_count;
        if(e->lookup_starts) {
            uint32_t page=(a>>LOOKUP_PAGE_SHIFT)-e->lookup_base;
            if(page>=e->lookup_pages)return NULL;
            lo=e->lookup_starts[page];hi=e->lookup_starts[page+1u];
        }
        uint32_t end=hi;
        while(lo<hi) { uint32_t m=lo+(hi-lo)/2;if(recomp_entries[m].address<a)lo=m+1;else hi=m; }
        Code found=lo<end && recomp_entries[lo].address==a?recomp_entries[lo].code:NULL;
        e->lookup_cache[slot].addr=a;e->lookup_cache[slot].code=found;
        return found;
    }
    return NULL;
}
static void interpret(Context *c,uint32_t a,uint32_t opcode) {
    Engine *e=c->user;
    if (!e->fallback) ctr_recomp_abort(c,CTR_CPU_UNSUPPORTED,opcode);
    bool copy_vfp=!c->thumb && ((opcode&0x0e000000u)==0x0c000000u || (opcode&0x0f000000u)==0x0e000000u);
    c->r[15]=a;export_cpu_mode(e,copy_vfp);
    uint64_t count=e->fast.cpu->instructions;
    ctr_cpu_result r=ctr_cpu_step(e->fast.cpu);
    e->fast.cpu->instructions=count; /* Already charged by CTR_INSTRUCTION. */
    ++e->fast.cpu->fallback_instructions;
    import_cpu_mode(e,copy_vfp);
    if(!e->fast.rx_size && c->budget>0)c->budget=-c->budget;
    if(r==CTR_CPU_SVC){c->svc=e->fast.cpu->svc_number;c->exit=EXIT_SVC;}
    else if(r!=CTR_CPU_OK) ctr_recomp_abort(c,r,opcode);
}
static const Host callbacks={read8,read16,read32,write8,write16,write32,interpret,lookup};
static ctr_cpu_result execute_slices(ctr_cpu *p,void *user,uint64_t budget,unsigned quantum) {
    Engine *e=user;Context *c=&e->fast.context;e->fast.cpu=p;
    e->fast.mem_generation=&p->memory->generation;
    e->fast.mem_resource_clock=&p->memory->resource_clock;
    e->fast.mem_resource_versions=p->memory->resource_versions;
    p->module_hash=e->guest->program.module_hash;
    /* Only the checked loader may refresh unchanged main-code ownership after
       adding/removing a packaged module. Foreign executable writes stay stale. */
    if(e->fast.code_generation!=p->memory->code_generation && ctr_modules_generation_current(e->guest)) {
        e->fast.rx_size=0;
        e->fast.code_generation=p->memory->code_generation;
    }
    volatile uint32_t remaining=budget>INT32_MAX?INT32_MAX:(uint32_t)budget;
    unsigned slice=quantum && quantum<remaining?quantum:remaining;
    remaining-=slice;
    import_cpu(e);c->budget=(int32_t)slice;c->exit=EXIT_NONE;c->depth=0;
    e->accounted_budget=c->budget;
    e->fast.rollback_base=false;
    int esc=setjmp(e->escape);
    if (esc) {
        if (esc==1) return e->fault;
        if (c->budget<0) c->budget=-c->budget;
        if (esc==2) goto budget_done;
        goto after_code;
    }
dispatch_slice:;
    volatile unsigned idle_dispatches=0;
    while(c->budget>0) {
        if(e->guest->modules && ctr_modules_has_code(e->guest,c->r[15])) {
            export_cpu(e);
            uint64_t before=p->instructions;
            ctr_cpu_result result=ctr_modules_dispatch(e->guest,1);
            p->module_hash=e->guest->program.module_hash;
            c->budget-=(int32_t)(p->instructions-before);import_cpu(e);
            e->accounted_budget=c->budget;
            if(result!=CTR_CPU_LIMIT)return result;
            continue;
        }
        volatile int32_t before_budget=c->budget;
        Code code=lookup(c,c->r[15]|c->thumb);
        if(!code) {
            e->fast.rollback_base=false;
            if(!e->fallback) ctr_recomp_abort(c,CTR_CPU_MISSING_BLOCK,c->r[15]);
            uint32_t op;ctr_mem_result r=ctr_memory_read(p->memory,c->r[15],c->thumb?2:4,CTR_MEM_EXEC,&op);
            if(r!=CTR_MEM_OK){p->memory_fault=r;ctr_recomp_abort(c,CTR_CPU_MEMORY_FAULT,c->r[15]);}
            if(ctr_recomp_instruction(c,c->r[15],op))interpret(c,c->r[15],op);
            if(c->budget<0)c->budget=-c->budget;
        } else {
            if(!e->fast.rx_size && c->budget>0)c->budget=-c->budget;
            code(c);
            if(c->budget<0)c->budget=-c->budget;
        }
after_code:;
        if(c->exit==EXIT_SVC){export_cpu(e);return CTR_CPU_SVC;}
        if(c->exit==EXIT_BUDGET)break;
        if(c->exit==EXIT_UNWIND) { ++e->unwinds;c->exit=EXIT_NONE;c->depth=0; }
        /* Empty/misrouted generated entries can otherwise unwind forever with
           an unchanged budget and prevent host input/exit from being polled. */
        if(c->budget==before_budget) {
            if(++idle_dispatches>=64)ctr_recomp_abort(c,CTR_CPU_MISSING_BLOCK,c->r[15]);
        } else idle_dispatches=0;
    }
budget_done:;
    if(!c->budget && remaining) {
        /* Keep each generated dispatch boundary while the thread stays runnable. */
        slice=remaining<e->guest->thread_quantum?remaining:e->guest->thread_quantum;
        remaining-=slice;
        c->budget=(int32_t)slice;c->exit=EXIT_NONE;c->depth=0;
        e->accounted_budget+=(int32_t)slice;
        e->fast.rollback_base=false;e->fast.dirty_page_plus_one=0;e->fast.dirty_page2_plus_one=0;
        goto dispatch_slice;
    }
    export_cpu(e);return CTR_CPU_LIMIT;
}
static ctr_cpu_result execute(ctr_cpu *p,void *user,uint64_t budget) {
    return execute_slices(p,user,budget,0);
}
static ctr_cpu_result execute_uncontended(ctr_cpu *p,void *user,uint64_t budget) {
    Engine *e=user;
    return execute_slices(p,e,budget,e->batch_uncontended?0u:e->guest->quantum_left);
}
static bool matches(const ctr_guest *g) {
    const Origins *o=&recomp_origins[0];
    if(o->piece_count>1000000u)return false;
    for(uint32_t i=0;i<o->piece_count;++i) {
        const Piece *p=&o->pieces[i];uint64_t hash=UINT64_C(14695981039346656037);
        if(p->end<=p->start || (uint64_t)p->end-p->start>CTR_GUEST_MAX_BYTES)return false;
        if(ctr_memory_check_range(&g->memory,p->start,p->end-p->start,CTR_MEM_EXEC)!=CTR_MEM_OK)return false;
        uint64_t cursor=p->start,end=p->end;
        while(cursor<end) {
            const ctr_mapping *found=NULL;
            for(size_t j=0;j<g->memory.count;++j) {
                const ctr_mapping *m=&g->memory.maps[j];
                if(cursor>=m->base && cursor<(uint64_t)m->base+m->size) { found=m;break; }
            }
            if(!found)return false;
            uint64_t stop=(uint64_t)found->base+found->size;
            size_t count=(size_t)((stop<end?stop:end)-cursor);
            const uint8_t *bytes=found->storage+(size_t)(cursor-found->base);
            for(size_t k=0;k<count;++k)hash=(hash^bytes[k])*UINT64_C(1099511628211);
            cursor+=count;
        }
        if(hash!=p->hash)return false;
    }
    return o->piece_count!=0;
}
static void index_lookup_pages(Engine *e) {
    uint32_t base=recomp_entries[0].address>>LOOKUP_PAGE_SHIFT;
    uint32_t pages=(recomp_entries[recomp_entry_count-1u].address>>LOOKUP_PAGE_SHIFT)-base+1u;
    if(pages>LOOKUP_MAX_PAGES || pages>recomp_entry_count)return;
    uint32_t *starts=malloc((pages+1u)*sizeof(*starts));
    if(!starts)return;
    uint32_t index=0;
    for(uint32_t page=0;page<pages;++page) {
        starts[page]=index;
        while(index<recomp_entry_count && (recomp_entries[index].address>>LOOKUP_PAGE_SHIFT)==base+page)
            ++index;
    }
    starts[pages]=index;
    e->lookup_starts=starts;e->lookup_base=base;e->lookup_pages=pages;
}
static void close_executor(void *user) {
    Engine *e=user;
    free(e->lookup_starts);
    free(e);
}
bool ctr_recomp_attach(ctr_guest *g,bool fallback) {
    if(g->executor || g->state!=CTR_GUEST_READY || recomp_abi!=4 || recomp_generation!=3 ||
        recomp_module_count || !recomp_entry_count || recomp_entry_count>10000000u || !matches(g))return false;
    for(uint32_t i=1;i<recomp_entry_count;++i)if(recomp_entries[i-1].address>=recomp_entries[i].address)return false;
    Engine *e=calloc(1,sizeof(*e));if(!e)return false;
    index_lookup_pages(e);
    e->fast.cpu=&g->cpu;e->guest=g;e->fallback=fallback;e->fpscr_mask=0x07ff9f00u;e->fast.code_generation=g->memory.code_generation;
    e->fast.mem_generation=&g->memory.generation;
    e->fast.mem_resource_clock=&g->memory.resource_clock;
    e->fast.mem_resource_versions=g->memory.resource_versions;
    e->fast.context.vfp=e->fast.vfp;e->fast.context.fpscr=&e->fast.fpscr;
    e->fast.context.host=&callbacks;e->fast.context.user=e;
    g->executor=execute;g->executor_uncontended=execute_uncontended;
    g->executor_context=e;g->executor_close=close_executor;return true;
}
void ctr_recomp_enable_runfast(ctr_guest *g) {
    if(g && g->executor==execute) {
        Engine *e=g->executor_context;
        if(e->fallback) {
            e->fpscr_mask=0x04ff9f00u;
            e->batch_uncontended=true;
        }
    }
}
bool ctr_recomp_vfp(const ctr_guest *g,unsigned word,uint32_t *value) {
    if(g->executor!=execute || word>=32 || !value)return false;
    *value=g->cpu.vfp[word];return true;
}
bool ctr_recomp_depth_stats(const ctr_guest *g,unsigned *maximum,uint64_t *unwinds) {
    if(!g || g->executor!=execute || !maximum || !unwinds)return false;
    const Engine *e=g->executor_context;*maximum=e->fast.maximum_depth;*unwinds=e->unwinds;return true;
}
