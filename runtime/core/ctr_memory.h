#ifndef CTR_MEMORY_H
#define CTR_MEMORY_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define CTR_MEMORY_MAX_MAPS 32u
enum { CTR_MEM_READ = 1u, CTR_MEM_WRITE = 2u, CTR_MEM_EXEC = 4u };
typedef enum {
    CTR_MEM_OK = 0, CTR_MEM_UNMAPPED, CTR_MEM_PERMISSION,
    CTR_MEM_ALIGNMENT, CTR_MEM_OVERFLOW, CTR_MEM_OVERLAP, CTR_MEM_CAPACITY
} ctr_mem_result;

typedef struct {
    uint32_t base;
    uint32_t size;
    unsigned permissions;
    uint8_t *storage;
} ctr_mapping;

typedef struct {
    ctr_mapping maps[CTR_MEMORY_MAX_MAPS];
    size_t count;
    /* All writes invalidate exclusive monitors. Executable storage writes
       invalidate code snapshots, including writes through writable aliases. */
    uint64_t generation;
    uint64_t code_generation;
} ctr_memory;

void ctr_memory_init(ctr_memory *memory);
ctr_mem_result ctr_memory_map(ctr_memory *memory, uint32_t base, uint32_t size,
                             unsigned permissions, uint8_t *storage,
                             size_t storage_size);
ctr_mem_result ctr_memory_read(const ctr_memory *memory, uint32_t address,
                              unsigned width, unsigned permission, uint32_t *value);
/* ARMv6 U=1/A=0 single-data transfers. Instruction, multiple, exclusive and
 * FP accesses retain the strict APIs. Validate the full range before mutation. */
ctr_mem_result ctr_memory_read_data(const ctr_memory *memory,uint32_t address,
                                   unsigned width,uint32_t *value);
ctr_mem_result ctr_memory_write_data(ctr_memory *memory,uint32_t address,
                                    unsigned width,uint32_t value);
ctr_mem_result ctr_memory_write(ctr_memory *memory, uint32_t address,
                               unsigned width, uint32_t value);
/* Validate an entire byte range before a service publishes any output. Adjacent
   guest mappings are allowed; permissions/overflow are checked without writes. */
ctr_mem_result ctr_memory_check_range(const ctr_memory *memory, uint32_t address,
                                     uint32_t size, unsigned permissions);
/* Owned host buffers must not alias any guest backing. Validate the entire
 * guest range before copying, including adjacent mappings. Bulk writes update
 * exclusive/code generations once and detect executable storage aliases. */
ctr_mem_result ctr_memory_read_bytes(const ctr_memory *memory,uint32_t address,void *output,uint32_t size);
ctr_mem_result ctr_memory_write_bytes(ctr_memory *memory,uint32_t address,const void *input,uint32_t size);
/* Atomically split a fully mapped range to change permissions or remove it.
   Zero permissions create a guard mapping. A hole/capacity failure changes nothing. */
ctr_mem_result ctr_memory_change_range(ctr_memory *memory, uint32_t address,
                                      uint32_t size, unsigned permissions, bool remove);
const char *ctr_mem_result_name(ctr_mem_result result);

#endif
