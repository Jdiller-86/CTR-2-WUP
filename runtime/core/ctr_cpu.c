#include "ctr_cpu.h"
#include <stdatomic.h>

#include <string.h>

void ctr_cpu_init(ctr_cpu *cpu, ctr_memory *memory) {
    memset(cpu, 0, sizeof(*cpu));
    cpu->memory = memory;
    cpu->module_hash = "unidentified";
}

const char *ctr_cpu_result_name(ctr_cpu_result result) {
    static const char *const names[] = {"ok", "unsupported-instruction", "svc", "memory-fault", "budget-exhausted", "missing-block", "code-invalidated"};
    return (unsigned)result < sizeof(names) / sizeof(names[0]) ? names[result] : "unknown";
}

ctr_cpu_result ctr_cpu_stop(ctr_cpu *cpu, ctr_cpu_result result, uint32_t detail) {
    if (result == CTR_CPU_SVC) cpu->svc_number = detail;
    if (cpu->log != NULL)
        cpu->log(cpu->log_context, ctr_cpu_result_name(result), cpu->module_hash,
                 cpu->r[15], (cpu->cpsr & CTR_FLAG_T) != 0u, detail);
    return result;
}

bool ctr_cpu_condition(const ctr_cpu *cpu, unsigned condition) {
    bool n = (cpu->cpsr & CTR_FLAG_N) != 0u, z = (cpu->cpsr & CTR_FLAG_Z) != 0u;
    bool c = (cpu->cpsr & CTR_FLAG_C) != 0u, v = (cpu->cpsr & CTR_FLAG_V) != 0u;
    switch (condition) {
        case 0: return z; case 1: return !z; case 2: return c; case 3: return !c;
        case 4: return n; case 5: return !n; case 6: return v; case 7: return !v;
        case 8: return c && !z; case 9: return !c || z; case 10: return n == v;
        case 11: return n != v; case 12: return !z && n == v; case 13: return z || n != v;
        case 14: return true; default: return false;
    }
}

void ctr_cpu_logic_flags(ctr_cpu *cpu, uint32_t value, unsigned carry) {
    cpu->cpsr = (cpu->cpsr & ~(CTR_FLAG_N | CTR_FLAG_Z | CTR_FLAG_C)) |
                (value & CTR_FLAG_N) | (value == 0u ? CTR_FLAG_Z : 0u) |
                (carry != 0u ? CTR_FLAG_C : 0u);
}

uint32_t ctr_cpu_add(ctr_cpu *cpu, uint32_t left, uint32_t right, unsigned carry, bool flags) {
    uint64_t wide = (uint64_t)left + right + (carry & 1u);
    uint32_t result = (uint32_t)wide;
    if (flags) {
        ctr_cpu_logic_flags(cpu, result, (unsigned)(wide >> 32));
        cpu->cpsr = (cpu->cpsr & ~CTR_FLAG_V) |
                    ((~(left ^ right) & (left ^ result) & UINT32_C(0x80000000)) != 0u ? CTR_FLAG_V : 0u);
    }
    return result;
}

int64_t ctr_cpu_signed_half(uint32_t word,unsigned high) {
    uint32_t half=(word>>(high?16:0))&0xffffu;
    return (half&0x8000u)?(int64_t)half-65536:(int64_t)half;
}
uint32_t ctr_cpu_half_accumulate(ctr_cpu *cpu,uint32_t left,uint32_t right,uint32_t accumulator,unsigned x,unsigned y) {
    int64_t signed_acc=(accumulator&0x80000000u)?(int64_t)accumulator-INT64_C(4294967296):(int64_t)accumulator;
    int64_t result=ctr_cpu_signed_half(left,x)*ctr_cpu_signed_half(right,y)+signed_acc;
    if(result>INT32_MAX || result<INT32_MIN)cpu->cpsr|=0x08000000u;
    return (uint32_t)result;
}
uint32_t ctr_cpu_shift(uint32_t value, unsigned type, unsigned amount, bool immediate,
                       unsigned carry_in, unsigned *carry_out) {
    *carry_out = carry_in & 1u;
    if (amount == 0u) {
        if (!immediate || type == 0u) return value;
        if (type == 3u) {
            *carry_out = value & 1u;
            return (value >> 1) | ((carry_in & 1u) << 31);
        }
        amount = 32u; /* LSR/ASR immediate zero encodes 32. */
    }
    switch (type) {
        case 0:
            *carry_out = amount <= 32u ? ((value >> (32u - amount)) & 1u) : 0u;
            return amount < 32u ? value << amount : 0u;
        case 1:
            *carry_out = amount <= 32u ? ((value >> (amount - 1u)) & 1u) : 0u;
            return amount < 32u ? value >> amount : 0u;
        case 2:
            if (amount >= 32u) {
                *carry_out = value >> 31;
                return (value >> 31) != 0u ? UINT32_MAX : 0u;
            }
            *carry_out = (value >> (amount - 1u)) & 1u;
            return (value >> amount) | ((value >> 31) != 0u ? UINT32_MAX << (32u - amount) : 0u);
        case 3:
            amount &= 31u;
            if (amount == 0u) { *carry_out = value >> 31; return value; }
            value = (value >> amount) | (value << (32u - amount));
            *carry_out = value >> 31;
            return value;
        default: return value;
    }
}

void ctr_cpu_branch_exchange(ctr_cpu *cpu, uint32_t address) {
    cpu->cpsr = (cpu->cpsr & ~CTR_FLAG_T) | ((address & 1u) != 0u ? CTR_FLAG_T : 0u);
    cpu->r[15] = address & ((address & 1u) != 0u ? ~UINT32_C(1) : ~UINT32_C(3));
}

ctr_cpu_result ctr_cpu_load(ctr_cpu *cpu, uint32_t address, unsigned width, uint32_t *value) {
    ctr_mem_result result = ctr_memory_read(cpu->memory, address, width, CTR_MEM_READ, value);
    if (result == CTR_MEM_OK) return CTR_CPU_OK;
    cpu->memory_fault = result;
    return ctr_cpu_stop(cpu, CTR_CPU_MEMORY_FAULT, address);
}

ctr_cpu_result ctr_cpu_store(ctr_cpu *cpu, uint32_t address, unsigned width, uint32_t value) {
    ctr_mem_result result = ctr_memory_write(cpu->memory, address, width, value);
    if (result == CTR_MEM_OK) { cpu->exclusive_valid=false;return CTR_CPU_OK; }
    cpu->memory_fault = result;
    return ctr_cpu_stop(cpu, CTR_CPU_MEMORY_FAULT, address);
}

static uint32_t read_register(const ctr_cpu *cpu, unsigned reg, bool thumb) {
    return cpu->r[reg] + (reg == 15u ? (thumb ? 4u : 8u) : 0u);
}
ctr_cpu_result ctr_cpu_load_data(ctr_cpu *cpu,uint32_t address,unsigned width,uint32_t *value) {
    ctr_mem_result result=ctr_memory_read_data(cpu->memory,address,width,value);
    if(result==CTR_MEM_OK)return CTR_CPU_OK;
    cpu->memory_fault=result;return ctr_cpu_stop(cpu,CTR_CPU_MEMORY_FAULT,address);
}
ctr_cpu_result ctr_cpu_store_data(ctr_cpu *cpu,uint32_t address,unsigned width,uint32_t value) {
    ctr_mem_result result=ctr_memory_write_data(cpu->memory,address,width,value);
    if(result==CTR_MEM_OK) { cpu->exclusive_valid=false;return CTR_CPU_OK; }
    cpu->memory_fault=result;return ctr_cpu_stop(cpu,CTR_CPU_MEMORY_FAULT,address);
}

void ctr_cpu_barrier(void) { atomic_thread_fence(memory_order_seq_cst); }
ctr_cpu_result ctr_cpu_fp_transfer(ctr_cpu *cpu,unsigned first,unsigned words,uint32_t address,bool load,uint32_t opcode) {
    if(!words || words>2 || first+words>32)return ctr_cpu_stop(cpu,CTR_CPU_UNSUPPORTED,opcode);
    for(unsigned i=0;i<words;++i) {
        ctr_cpu_result result=load?ctr_cpu_load(cpu,address+i*4u,4,&cpu->vfp[first+i]):
            ctr_cpu_store(cpu,address+i*4u,4,cpu->vfp[first+i]);
        if(result!=CTR_CPU_OK)return result;
    }
    return CTR_CPU_OK;
}
uint32_t ctr_cpu_byte_parallel(ctr_cpu *cpu,uint32_t left,uint32_t right,bool subtract) {
    uint32_t result=0,ge=0;
    for(unsigned i=0;i<4;++i) {
        unsigned a=(left>>(i*8))&255u,b=(right>>(i*8))&255u;
        unsigned n=subtract?a-b:a+b;result|=(n&255u)<<(i*8);
        if(subtract?a>=b:n>255u)ge|=1u<<i;
    }
    cpu->cpsr=(cpu->cpsr&~0x000f0000u)|(ge<<16);return result;
}
uint32_t ctr_cpu_byte_select(const ctr_cpu *cpu,uint32_t left,uint32_t right) {
    uint32_t result=0;
    for(unsigned i=0;i<4;++i)result|=((cpu->cpsr&(1u<<(16+i))?left:right)>>(i*8)&255u)<<(i*8);
    return result;
}
bool ctr_cpu_multiple_supported(unsigned base,uint16_t registers,bool writeback,bool load,bool thumb) {
    if(base>=15 || !registers || (thumb && !load && (registers&0x8000u)))return false;
    if(writeback && (registers&(1u<<base))) {
        if(load || (registers&((1u<<base)-1u)))return false;
    }
    return true;
}
ctr_cpu_result ctr_cpu_multiple(ctr_cpu *cpu,unsigned rn,uint16_t registers,
    bool increment,bool before,bool writeback,bool load,bool thumb,uint32_t opcode) {
    if(!ctr_cpu_multiple_supported(rn,registers,writeback,load,thumb))return ctr_cpu_stop(cpu,CTR_CPU_UNSUPPORTED,opcode);
    unsigned count=0;for(unsigned i=0;i<16;++i)count+=(registers>>i)&1u;
    uint32_t base=cpu->r[rn],span=count*4u;
    uint32_t address=increment?base+(before?4u:0u):base-span+(before?0u:4u),target=0;
    for(unsigned reg=0;reg<16;++reg)if(registers&(1u<<reg)) {
        uint32_t value=reg==15 && !thumb?cpu->r[15]+8u:cpu->r[reg];
        ctr_cpu_result result=load?ctr_cpu_load(cpu,address,4u,&value):ctr_cpu_store(cpu,address,4u,value);
        if(result!=CTR_CPU_OK)return result;
        if(load) { if(reg==15)target=value;else cpu->r[reg]=value; }
        address+=4u;
    }
    if(writeback)cpu->r[rn]=increment?base+span:base-span;
    if(load && (registers&0x8000u))ctr_cpu_branch_exchange(cpu,target);
    return CTR_CPU_OK;
}

static ctr_cpu_result arm_step(ctr_cpu *cpu, uint32_t insn) {
    uint32_t pc = cpu->r[15], next = pc + 4u;
    unsigned cond = insn >> 28;
    if (cond == 15u) return ctr_cpu_stop(cpu, CTR_CPU_UNSUPPORTED, insn);
    if (!ctr_cpu_condition(cpu, cond)) { cpu->r[15] = next; return CTR_CPU_OK; }
    if((insn&0x0fff0fffu)==0x0e070f9au || (insn&0x0fff0fffu)==0x0e070fbau) {
        if(((insn>>12)&15u)==15u)return ctr_cpu_stop(cpu,CTR_CPU_UNSUPPORTED,insn);
        ctr_cpu_barrier();cpu->r[15]=next;return CTR_CPU_OK;
    }
    uint32_t fp_transfer=insn&0x0f200f00u;
    if(fp_transfer==0x0d000a00u || fp_transfer==0x0d000b00u) {
        unsigned rn=(insn>>16)&15u,first=(insn>>12)&15u;bool wide=fp_transfer==0x0d000b00u,load=(insn&0x100000u)!=0;
        first=wide?(first+((insn>>22)&1u)*16u)*2u:first*2u+((insn>>22)&1u);
        if(rn==15 && !load)return ctr_cpu_stop(cpu,CTR_CPU_UNSUPPORTED,insn);
        uint32_t base=read_register(cpu,rn,false),offset=(insn&255u)*4u;
        ctr_cpu_result result=ctr_cpu_fp_transfer(cpu,first,wide?2u:1u,(insn&0x800000u)?base+offset:base-offset,load,insn);
        if(result==CTR_CPU_OK)cpu->r[15]=next;
        return result;
    }
    if((insn&0x0fff0fffu)==0x0ee10a10u || (insn&0x0fff0fffu)==0x0ef10a10u) {
        unsigned rt=(insn>>12)&15u;bool read=(insn&0x00100000u)!=0;
        if(read) {
            if(rt==15)cpu->cpsr=(cpu->cpsr&0x0fffffffu)|(cpu->fpscr&0xf0000000u);
            else cpu->r[rt]=cpu->fpscr;
        } else {
            if(rt==15)return ctr_cpu_stop(cpu,CTR_CPU_UNSUPPORTED,insn);
            cpu->fpscr=cpu->r[rt];
        }
        cpu->r[15]=next;return CTR_CPU_OK;
    }
    if ((insn&UINT32_C(0x0fff0fff))==UINT32_C(0x0e1d0f70) && ((insn>>12)&15u)!=15u) {
        cpu->r[(insn>>12)&15u]=cpu->tls_address;
        cpu->r[15]=next;
        return CTR_CPU_OK;
    }
    if ((insn & UINT32_C(0x0ffffff0)) == UINT32_C(0x012fff10) ||
        (insn & UINT32_C(0x0ffffff0)) == UINT32_C(0x012fff30)) {
        if((insn&0x20u) && (insn&15u)==15u)return ctr_cpu_stop(cpu,CTR_CPU_UNSUPPORTED,insn);
        uint32_t target = read_register(cpu, insn & 15u, false);
        if ((insn & 0x20u) != 0u) cpu->r[14] = next;
        ctr_cpu_branch_exchange(cpu, target);
        return CTR_CPU_OK;
    }
    if ((insn & UINT32_C(0x0f000000)) == UINT32_C(0x0f000000)) {
        ctr_cpu_result result = ctr_cpu_stop(cpu, CTR_CPU_SVC, insn & UINT32_C(0x00ffffff));
        cpu->r[15] = next;
        cpu->exclusive_valid = false;
        return result;
    }
    if ((insn & UINT32_C(0x0e000000)) == UINT32_C(0x0a000000)) {
        uint32_t offset = insn & UINT32_C(0x00ffffff);
        if ((offset & UINT32_C(0x00800000)) != 0u) offset |= UINT32_C(0xff000000);
        if ((insn & UINT32_C(0x01000000)) != 0u) cpu->r[14] = next;
        cpu->r[15] = pc + 8u + (offset << 2);
        return CTR_CPU_OK;
    }
    uint32_t media=insn&0x0ff00ff0u;
    uint32_t extend=insn&0x0fff03f0u;
    if(extend==0x06af0070u || extend==0x06bf0070u ||
       extend==0x06ef0070u || extend==0x06ff0070u) {
        /* ARMv6 SXTB/SXTH/UXTB/UXTH, including ROR #0/8/16/24.
           Adding/paired forms remain unsupported; none of these alter flags. */
        unsigned rd=(insn>>12)&15u,rm=insn&15u;
        if(rd==15u || rm==15u)return ctr_cpu_stop(cpu,CTR_CPU_UNSUPPORTED,insn);
        unsigned ignored;
        uint32_t value=ctr_cpu_shift(cpu->r[rm],3,((insn>>10)&3u)*8u,false,0,&ignored);
        uint32_t mask=(insn&0x00100000u)?0xffffu:0xffu;
        value&=mask;
        if(!(insn&0x00400000u) && (value&((mask+1u)>>1)))value|=~mask;
        cpu->r[rd]=value;cpu->r[15]=next;return CTR_CPU_OK;
    }
    if((insn&0x0ff00090u)==0x01000080u) { /* SMLAxy, signed halves and sticky Q. */
        unsigned rd=(insn>>16)&15,rn=(insn>>12)&15,rs=(insn>>8)&15,rm=insn&15;
        if(rd==15 || rn==15 || rs==15 || rm==15)return ctr_cpu_stop(cpu,CTR_CPU_UNSUPPORTED,insn);
        cpu->r[rd]=ctr_cpu_half_accumulate(cpu,cpu->r[rm],cpu->r[rs],cpu->r[rn],(insn>>5)&1,(insn>>6)&1);
        cpu->r[15]=next;return CTR_CPU_OK;
    }
    if(media==0x06500f90u || media==0x06500ff0u || media==0x06800fb0u) {
        unsigned rn=(insn>>16)&15u,rd=(insn>>12)&15u,rm=insn&15u;
        if(rn==15 || rd==15 || rm==15)return ctr_cpu_stop(cpu,CTR_CPU_UNSUPPORTED,insn);
        cpu->r[rd]=media==0x06800fb0u?ctr_cpu_byte_select(cpu,cpu->r[rn],cpu->r[rm]):
            ctr_cpu_byte_parallel(cpu,cpu->r[rn],cpu->r[rm],media==0x06500ff0u);
        cpu->r[15]=next;return CTR_CPU_OK;
    }
    if((insn&UINT32_C(0x0e000000))==UINT32_C(0x08000000)) {
        if(insn&UINT32_C(0x00400000))return ctr_cpu_stop(cpu,CTR_CPU_UNSUPPORTED,insn);
        bool load=(insn&0x00100000u)!=0;
        ctr_cpu_result result=ctr_cpu_multiple(cpu,(insn>>16)&15u,(uint16_t)insn,
            (insn&0x00800000u)!=0,(insn&0x01000000u)!=0,(insn&0x00200000u)!=0,load,false,insn);
        if(result==CTR_CPU_OK && !(load && (insn&0x8000u)))cpu->r[15]=next;
        return result;
    }
    /* ARMv6K word exclusives. A single deterministic guest CPU is modeled. */
    if ((insn & UINT32_C(0x0ff00fff)) == UINT32_C(0x01900f9f)) {
        unsigned rn = (insn >> 16) & 15u, rd = (insn >> 12) & 15u;
        if (rn == 15u || rd == 15u) return ctr_cpu_stop(cpu, CTR_CPU_UNSUPPORTED, insn);
        uint32_t address = cpu->r[rn];
        ctr_cpu_result result = ctr_cpu_load(cpu, address, 4u, &cpu->r[rd]);
        if (result != CTR_CPU_OK) return result;
        cpu->exclusive_valid = true;
        cpu->exclusive_address = address;
        cpu->exclusive_generation = cpu->memory->generation;
        cpu->r[15] = next;
        return CTR_CPU_OK;
    }
    if ((insn & UINT32_C(0x0ff00ff0)) == UINT32_C(0x01800f90)) {
        unsigned rn = (insn >> 16) & 15u, rd = (insn >> 12) & 15u, rm = insn & 15u;
        if (rn == 15u || rd == 15u || rm == 15u || rd == rn || rd == rm)
            return ctr_cpu_stop(cpu, CTR_CPU_UNSUPPORTED, insn);
        bool valid = cpu->exclusive_valid && cpu->exclusive_address == cpu->r[rn] &&
                     cpu->exclusive_generation == cpu->memory->generation;
        cpu->exclusive_valid = false;
        if (valid) {
            ctr_cpu_result result = ctr_cpu_store(cpu, cpu->r[rn], 4u, cpu->r[rm]);
            if (result != CTR_CPU_OK) return result;
        }
        cpu->r[rd] = valid ? 0u : 1u;
        cpu->r[15] = next;
        return CTR_CPU_OK;
    }
    if ((insn & UINT32_C(0x0fc000f0)) == UINT32_C(0x00000090)) {
        unsigned rd = (insn >> 16) & 15u, rn = (insn >> 12) & 15u;
        unsigned rs = (insn >> 8) & 15u, rm = insn & 15u;
        if (rd == 15u || rs == 15u || rm == 15u || ((insn & UINT32_C(0x00200000)) != 0u && rn == 15u))
            return ctr_cpu_stop(cpu, CTR_CPU_UNSUPPORTED, insn);
        uint32_t value = cpu->r[rm] * cpu->r[rs];
        if ((insn & UINT32_C(0x00200000)) != 0u) value += cpu->r[rn];
        cpu->r[rd] = value;
        if ((insn & UINT32_C(0x00100000)) != 0u)
            ctr_cpu_logic_flags(cpu, value, (cpu->cpsr & CTR_FLAG_C) != 0u);
        cpu->r[15] = next;
        return CTR_CPU_OK;
    }
    if ((insn & UINT32_C(0x0e0000f0)) == UINT32_C(0x000000b0)) {
        unsigned rn=(insn>>16)&15u,rd=(insn>>12)&15u;
        bool immediate=(insn&UINT32_C(0x00400000))!=0;
        if(!(insn&UINT32_C(0x01000000)) || (!immediate && ((insn&0xf00u) || (insn&15u)==15)) ||
            (insn&UINT32_C(0x00200000)) || rd==15u)return ctr_cpu_stop(cpu,CTR_CPU_UNSUPPORTED,insn);
        uint32_t offset=immediate?((insn>>4)&0xf0u)|(insn&15u):cpu->r[insn&15u],base=read_register(cpu,rn,false);
        uint32_t address=(insn&UINT32_C(0x00800000))?base+offset:base-offset;
        bool load=(insn&UINT32_C(0x00100000))!=0;uint32_t value=cpu->r[rd];
        ctr_cpu_result result=load?ctr_cpu_load_data(cpu,address,2u,&value):ctr_cpu_store_data(cpu,address,2u,value);
        if(result!=CTR_CPU_OK)return result;
        if(load)cpu->r[rd]=value;
        cpu->r[15]=next;return CTR_CPU_OK;
    }
    if ((insn & UINT32_C(0x0c000000)) == UINT32_C(0x04000000)) {
        unsigned rn = (insn >> 16) & 15u, rd = (insn >> 12) & 15u;
        bool load = (insn & UINT32_C(0x00100000)) != 0u;
        bool pre = (insn & UINT32_C(0x01000000)) != 0u;
        bool writeback = !pre || (insn & UINT32_C(0x00200000)) != 0u;
        unsigned width = (insn & UINT32_C(0x00400000)) != 0u ? 1u : 4u;
        if ((writeback && (rn == 15u || (load && rn == rd))) || (rd == 15u && (!load || width != 4u)) ||
            (!pre && (insn & UINT32_C(0x00200000)) != 0u))
            return ctr_cpu_stop(cpu, CTR_CPU_UNSUPPORTED, insn);
        uint32_t offset = insn & 0xfffu;
        if ((insn & UINT32_C(0x02000000)) != 0u) {
            if ((insn & 0x10u) != 0u) return ctr_cpu_stop(cpu, CTR_CPU_UNSUPPORTED, insn);
            unsigned carry;
            offset = ctr_cpu_shift(read_register(cpu, insn & 15u, false), (insn >> 5) & 3u,
                                   (insn >> 7) & 31u, true, (cpu->cpsr & CTR_FLAG_C) != 0u, &carry);
        }
        uint32_t base = read_register(cpu, rn, false);
        uint32_t adjusted = (insn & UINT32_C(0x00800000)) != 0u ? base + offset : base - offset;
        uint32_t address = pre ? adjusted : base, value = cpu->r[rd];
        ctr_cpu_result result = load ? (rd==15u?ctr_cpu_load(cpu,address,width,&value):ctr_cpu_load_data(cpu,address,width,&value)) : ctr_cpu_store_data(cpu,address,width,value);
        if (result != CTR_CPU_OK) return result;
        if (writeback) cpu->r[rn] = adjusted;
        if (load) cpu->r[rd] = value;
        if (load && rd == 15u) ctr_cpu_branch_exchange(cpu, value); else cpu->r[15] = next;
        return CTR_CPU_OK;
    }
    if ((insn & UINT32_C(0x0c000000)) == 0u) {
        unsigned op = (insn >> 21) & 15u, rn = (insn >> 16) & 15u, rd = (insn >> 12) & 15u;
        bool flags = (insn & UINT32_C(0x00100000)) != 0u;
        bool test = op >= 8u && op <= 11u;
        if ((test && !flags) || (rd == 15u && flags)) return ctr_cpu_stop(cpu, CTR_CPU_UNSUPPORTED, insn);
        unsigned carry = (cpu->cpsr & CTR_FLAG_C) != 0u, old_carry = carry;
        uint32_t right;
        if ((insn & UINT32_C(0x02000000)) != 0u) {
            unsigned rotation = ((insn >> 8) & 15u) * 2u;
            right = rotation == 0u ? insn & 255u : ctr_cpu_shift(insn & 255u, 3u, rotation, false, carry, &carry);
        } else {
            bool reg_shift = (insn & 16u) != 0u;
            if (reg_shift && ((insn & 128u) != 0u || (insn & 15u) == 15u || ((insn >> 8) & 15u) == 15u || rn == 15u))
                return ctr_cpu_stop(cpu, CTR_CPU_UNSUPPORTED, insn);
            unsigned amount = reg_shift ? cpu->r[(insn >> 8) & 15u] & 255u : (insn >> 7) & 31u;
            right = ctr_cpu_shift(read_register(cpu, insn & 15u, false), (insn >> 5) & 3u, amount,
                                  !reg_shift, carry, &carry);
        }
        uint32_t left = read_register(cpu, rn, false), value = 0u;
        bool logical = false;
        switch (op) {
            case 0: case 8: value = left & right; logical = true; break;
            case 1: case 9: value = left ^ right; logical = true; break;
            case 2: case 10: value = ctr_cpu_add(cpu, left, ~right, 1u, flags); break;
            case 3: value = ctr_cpu_add(cpu, right, ~left, 1u, flags); break;
            case 4: case 11: value = ctr_cpu_add(cpu, left, right, 0u, flags); break;
            case 5: value = ctr_cpu_add(cpu, left, right, old_carry, flags); break;
            case 6: value = ctr_cpu_add(cpu, left, ~right, old_carry, flags); break;
            case 7: value = ctr_cpu_add(cpu, right, ~left, old_carry, flags); break;
            case 12: value = left | right; logical = true; break;
            case 13: value = right; logical = true; break;
            case 14: value = left & ~right; logical = true; break;
            case 15: value = ~right; logical = true; break;
            default: return ctr_cpu_stop(cpu, CTR_CPU_UNSUPPORTED, insn);
        }
        if (logical && flags) ctr_cpu_logic_flags(cpu, value, carry);
        if (!test) cpu->r[rd] = value;
        cpu->r[15] = !test && rd == 15u ? value & ~UINT32_C(3) : next;
        return CTR_CPU_OK;
    }
    return ctr_cpu_stop(cpu, CTR_CPU_UNSUPPORTED, insn);
}

static ctr_cpu_result thumb_step(ctr_cpu *cpu, uint32_t insn) {
    uint32_t pc = cpu->r[15], next = pc + 2u;
    unsigned carry = (cpu->cpsr & CTR_FLAG_C) != 0u;
    if ((insn & 0xf800u) < 0x1800u) {
        unsigned rd = insn & 7u, rm = (insn >> 3) & 7u;
        cpu->r[rd] = ctr_cpu_shift(cpu->r[rm], (insn >> 11) & 3u, (insn >> 6) & 31u, true, carry, &carry);
        ctr_cpu_logic_flags(cpu, cpu->r[rd], carry);
    } else if ((insn & 0xf800u) == 0x1800u) {
        unsigned rd = insn & 7u, rn = (insn >> 3) & 7u;
        uint32_t right = (insn & 0x400u) != 0u ? (insn >> 6) & 7u : cpu->r[(insn >> 6) & 7u];
        cpu->r[rd] = (insn & 0x200u) != 0u ? ctr_cpu_add(cpu, cpu->r[rn], ~right, 1u, true) :
                                            ctr_cpu_add(cpu, cpu->r[rn], right, 0u, true);
    } else if ((insn & 0xe000u) == 0x2000u) {
        unsigned rd = (insn >> 8) & 7u, op = (insn >> 11) & 3u;
        uint32_t right = insn & 255u;
        if (op == 0u) { cpu->r[rd] = right; ctr_cpu_logic_flags(cpu, right, carry); }
        else if (op == 1u) (void)ctr_cpu_add(cpu, cpu->r[rd], ~right, 1u, true);
        else if (op == 2u) cpu->r[rd] = ctr_cpu_add(cpu, cpu->r[rd], right, 0u, true);
        else cpu->r[rd] = ctr_cpu_add(cpu, cpu->r[rd], ~right, 1u, true);
    } else if ((insn & 0xfc00u) == 0x4000u) {
        unsigned rd = insn & 7u, rm = (insn >> 3) & 7u, op = (insn >> 6) & 15u;
        uint32_t left = cpu->r[rd], right = cpu->r[rm], value = left;
        bool logical = false, store = true;
        switch (op) {
            case 0: value = left & right; logical = true; break;
            case 1: value = left ^ right; logical = true; break;
            case 2: case 3: case 4: case 7:
                value = ctr_cpu_shift(left, op == 7u ? 3u : op - 2u, right & 255u, false, carry, &carry); logical = true; break;
            case 5: value = ctr_cpu_add(cpu, left, right, carry, true); break;
            case 6: value = ctr_cpu_add(cpu, left, ~right, carry, true); break;
            case 8: value = left & right; logical = true; store = false; break;
            case 9: value = ctr_cpu_add(cpu, 0u, ~right, 1u, true); break;
            case 10: value = ctr_cpu_add(cpu, left, ~right, 1u, true); store = false; break;
            case 11: value = ctr_cpu_add(cpu, left, right, 0u, true); store = false; break;
            case 12: value = left | right; logical = true; break;
            case 13: value = left * right; logical = true; break;
            case 14: value = left & ~right; logical = true; break;
            case 15: value = ~right; logical = true; break;
            default: return ctr_cpu_stop(cpu, CTR_CPU_UNSUPPORTED, insn);
        }
        if (logical) ctr_cpu_logic_flags(cpu, value, carry);
        if (store) cpu->r[rd] = value;
    } else if ((insn & 0xfc00u) == 0x4400u) {
        unsigned rd = (insn & 7u) | ((insn >> 4) & 8u), rm = (insn >> 3) & 15u;
        unsigned op = (insn >> 8) & 3u;
        uint32_t right = read_register(cpu, rm, true), left = read_register(cpu, rd, true);
        if (op == 3u) {
            if((insn&0x80u) && rm==15u)return ctr_cpu_stop(cpu,CTR_CPU_UNSUPPORTED,insn);
            if ((insn & 0x80u) != 0u) cpu->r[14] = next | 1u;
            ctr_cpu_branch_exchange(cpu, right);
            return CTR_CPU_OK;
        }
        if (op == 1u) (void)ctr_cpu_add(cpu, left, ~right, 1u, true);
        else {
            cpu->r[rd] = op == 0u ? left + right : right;
            if (rd == 15u) { cpu->r[15] &= ~UINT32_C(1); return CTR_CPU_OK; }
        }
    } else if ((insn & 0xf800u) == 0x4800u) {
        unsigned rd = (insn >> 8) & 7u;
        ctr_cpu_result result = ctr_cpu_load(cpu, ((pc + 4u) & ~UINT32_C(3)) + (insn & 255u) * 4u, 4u, &cpu->r[rd]);
        if (result != CTR_CPU_OK) return result;
    } else if ((insn & 0xe000u) == 0x6000u || (insn & 0xf000u) == 0x8000u) {
        unsigned rd = insn & 7u, rn = (insn >> 3) & 7u;
        unsigned width = (insn & 0xf000u) == 0x8000u ? 2u : ((insn & 0x1000u) != 0u ? 1u : 4u);
        uint32_t address = cpu->r[rn] + ((insn >> 6) & 31u) * width;
        ctr_cpu_result result = (insn & 0x800u) != 0u ? ctr_cpu_load_data(cpu, address, width, &cpu->r[rd]) :
                                                                    ctr_cpu_store_data(cpu, address, width, cpu->r[rd]);
        if (result != CTR_CPU_OK) return result;
    } else if ((insn & 0xf000u) == 0x9000u) {
        unsigned rd = (insn >> 8) & 7u;
        uint32_t address = cpu->r[13] + (insn & 255u) * 4u;
        ctr_cpu_result result = (insn & 0x800u) != 0u ? ctr_cpu_load_data(cpu, address, 4u, &cpu->r[rd]) :
                                                                    ctr_cpu_store_data(cpu, address, 4u, cpu->r[rd]);
        if (result != CTR_CPU_OK) return result;
    } else if ((insn & 0xf000u) == 0xa000u) {
        cpu->r[(insn >> 8) & 7u] = ((insn & 0x800u) != 0u ? cpu->r[13] : (pc + 4u) & ~UINT32_C(3)) + (insn & 255u) * 4u;
    } else if ((insn & 0xff00u) == 0xb000u) {
        cpu->r[13] = (insn & 128u) != 0u ? cpu->r[13] - (insn & 127u) * 4u : cpu->r[13] + (insn & 127u) * 4u;
    } else if((insn&0xf600u)==0xb400u) {
        bool load=(insn&0x800u)!=0;uint16_t list=(uint16_t)(insn&255u);
        if(insn&0x100u)list|=(uint16_t)(1u<<(load?15:14));
        ctr_cpu_result result=ctr_cpu_multiple(cpu,13,list,load,!load,true,load,true,insn);
        if(result!=CTR_CPU_OK)return result;
        if(load && (list&0x8000u))return result;
    } else if((insn&0xf000u)==0xc000u) {
        unsigned rn=(insn>>8)&7u;bool load=(insn&0x800u)!=0;
        uint16_t list=(uint16_t)(insn&255u);bool writeback=!(load && (list&(1u<<rn)));
        ctr_cpu_result result=ctr_cpu_multiple(cpu,rn,list,true,false,writeback,load,true,insn);
        if(result!=CTR_CPU_OK)return result;
    } else if ((insn & 0xf000u) == 0xd000u) {
        unsigned cond = (insn >> 8) & 15u;
        if (cond == 15u) {
            ctr_cpu_result result = ctr_cpu_stop(cpu, CTR_CPU_SVC, insn & 255u);
            cpu->r[15] = next; cpu->exclusive_valid = false; return result;
        }
        if (cond == 14u) return ctr_cpu_stop(cpu, CTR_CPU_UNSUPPORTED, insn);
        if (ctr_cpu_condition(cpu, cond)) {
            uint32_t offset = insn & 255u;
            if ((offset & 128u) != 0u) offset |= UINT32_C(0xffffff00);
            next = pc + 4u + (offset << 1);
        }
    } else if ((insn & 0xf800u) == 0xe000u) {
        uint32_t offset = insn & 0x7ffu;
        if ((offset & 0x400u) != 0u) offset |= UINT32_C(0xfffff800);
        next = pc + 4u + (offset << 1);
    } else return ctr_cpu_stop(cpu, CTR_CPU_UNSUPPORTED, insn);
    cpu->r[15] = next;
    return CTR_CPU_OK;
}

ctr_cpu_result ctr_cpu_step(ctr_cpu *cpu) {
    bool thumb = (cpu->cpsr & CTR_FLAG_T) != 0u;
    uint32_t instruction;
    ctr_mem_result result = ctr_memory_read(cpu->memory, cpu->r[15], thumb ? 2u : 4u,
                                            CTR_MEM_EXEC, &instruction);
    if (result != CTR_MEM_OK) {
        cpu->memory_fault = result;
        return ctr_cpu_stop(cpu, CTR_CPU_MEMORY_FAULT, cpu->r[15]);
    }
    ++cpu->instructions;
    return thumb ? thumb_step(cpu, instruction) : arm_step(cpu, instruction);
}

ctr_cpu_result ctr_cpu_run(ctr_cpu *cpu, uint64_t budget) {
    for (uint64_t i = 0; i < budget; ++i) {
        ctr_cpu_result result = ctr_cpu_step(cpu);
        if (result != CTR_CPU_OK) return result;
    }
    return CTR_CPU_LIMIT;
}

ctr_cpu_result ctr_cpu_dispatch(ctr_cpu *cpu, const ctr_program *program, uint64_t budget,
                                bool interpreter_fallback) {
    cpu->module_hash = program->module_hash;
    bool sorted = true;
    for (size_t i = 1; i < program->count; ++i)
        if (program->blocks[i-1].address >= program->blocks[i].address) sorted = false;
    uint64_t start = cpu->instructions;
    while (cpu->instructions - start < budget) {
        bool thumb = (cpu->cpsr & CTR_FLAG_T) != 0u;
        ctr_block_fn block = NULL;
        if (cpu->memory->code_generation == program->code_generation) {
            if (sorted) {
                size_t lo = 0, hi = program->count;
                while (lo < hi) {
                    size_t mid = lo + (hi-lo)/2;
                    if (program->blocks[mid].address < cpu->r[15]) lo = mid+1; else hi = mid;
                }
                if (lo < program->count && program->blocks[lo].address == cpu->r[15] && program->blocks[lo].thumb == thumb)
                    block = program->blocks[lo].run;
            } else for (size_t i = 0; i < program->count; ++i)
                if (program->blocks[i].address == cpu->r[15] && program->blocks[i].thumb == thumb) {
                    block = program->blocks[i].run;
                    break;
                }
        } else {
            ctr_cpu_result invalid = ctr_cpu_stop(cpu, CTR_CPU_CODE_INVALIDATED, 0u);
            if (!interpreter_fallback) return invalid;
        }
        ctr_cpu_result result;
        if (block != NULL) {
            uint64_t before = cpu->instructions;
            result = block(cpu);
            if (cpu->instructions == before && result == CTR_CPU_OK)
                return ctr_cpu_stop(cpu, CTR_CPU_UNSUPPORTED, 0u);
        } else {
            ctr_cpu_result missing = ctr_cpu_stop(cpu, CTR_CPU_MISSING_BLOCK, 0u);
            if (!interpreter_fallback) return missing;
            ++cpu->fallback_instructions;
            result = ctr_cpu_step(cpu);
        }
        if (result != CTR_CPU_OK) return result;
    }
    return CTR_CPU_LIMIT;
}
