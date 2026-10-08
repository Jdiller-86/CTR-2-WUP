#ifndef CTR_CPU_H
#define CTR_CPU_H

#include "ctr_memory.h"
#include <stdbool.h>

#define CTR_FLAG_N UINT32_C(0x80000000)
#define CTR_FLAG_Z UINT32_C(0x40000000)
#define CTR_FLAG_C UINT32_C(0x20000000)
#define CTR_FLAG_V UINT32_C(0x10000000)
#define CTR_FLAG_T UINT32_C(0x00000020)
typedef enum {
    CTR_CPU_OK = 0, CTR_CPU_UNSUPPORTED, CTR_CPU_SVC, CTR_CPU_MEMORY_FAULT,
    CTR_CPU_LIMIT, CTR_CPU_MISSING_BLOCK, CTR_CPU_CODE_INVALIDATED
} ctr_cpu_result;
typedef void (*ctr_cpu_log_fn)(void *context, const char *event, const char *module_hash,
                               uint32_t pc, bool thumb, uint32_t detail);
typedef struct {
    uint32_t r[16];
    uint32_t cpsr;
    ctr_memory *memory;
    uint64_t instructions;
    uint64_t fallback_instructions;
    uint64_t exclusive_generation;
    uint32_t exclusive_address;
    bool exclusive_valid;
    ctr_mem_result memory_fault;
    ctr_cpu_log_fn log;
    void *log_context;
    const char *module_hash;
    uint32_t svc_number;
    /* User read-only CP15 c13,c0,3; supplied by the guest thread context. */
    uint32_t tls_address;
} ctr_cpu;

typedef ctr_cpu_result (*ctr_block_fn)(ctr_cpu *cpu);
typedef struct { uint32_t address; bool thumb; ctr_block_fn run; } ctr_block;
typedef struct {
    const ctr_block *blocks;
    size_t count;
    const char *module_hash;
    uint64_t code_generation;
} ctr_program;

void ctr_cpu_init(ctr_cpu *cpu, ctr_memory *memory);
bool ctr_cpu_condition(const ctr_cpu *cpu, unsigned condition);
uint32_t ctr_cpu_add(ctr_cpu *cpu, uint32_t left, uint32_t right, unsigned carry, bool flags);
void ctr_cpu_logic_flags(ctr_cpu *cpu, uint32_t value, unsigned carry);
uint32_t ctr_cpu_shift(uint32_t value, unsigned type, unsigned amount, bool immediate,
                       unsigned carry_in, unsigned *carry_out);
void ctr_cpu_branch_exchange(ctr_cpu *cpu, uint32_t address);
ctr_cpu_result ctr_cpu_load(ctr_cpu *cpu, uint32_t address, unsigned width, uint32_t *value);
ctr_cpu_result ctr_cpu_store(ctr_cpu *cpu, uint32_t address, unsigned width, uint32_t value);
ctr_cpu_result ctr_cpu_stop(ctr_cpu *cpu, ctr_cpu_result result, uint32_t detail);
ctr_cpu_result ctr_cpu_step(ctr_cpu *cpu);
ctr_cpu_result ctr_cpu_run(ctr_cpu *cpu, uint64_t budget);
ctr_cpu_result ctr_cpu_dispatch(ctr_cpu *cpu, const ctr_program *program, uint64_t budget,
                                bool interpreter_fallback);
const char *ctr_cpu_result_name(ctr_cpu_result result);
/* Small portable instruction/memory check for the physical-console diagnostic. */
int ctr_cpu_selftest(void);

#endif
