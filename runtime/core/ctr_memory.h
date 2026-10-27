#ifndef CTR_MEMORY_H
#define CTR_MEMORY_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define CTR_MEMORY_MAX_MAPS 32u
#define CTR_MEMORY_MAX_WRITE_RANGES 32u
#define CTR_MEMORY_RESOURCE_SLOTS 1024u
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

typedef struct ctr_memory {
    ctr_mapping maps[CTR_MEMORY_MAX_MAPS];
    size_t count;
    /* All writes invalidate exclusive monitors. Executable storage writes
       invalidate code snapshots, including writes through writable aliases. */
    uint64_t generation;
    uint64_t code_generation;
    /* Conservative 4 KiB backing-page versions. Hash collisions invalidate
     * extra resources; monotonic versions never hide a write. Saturation
     * permanently disables resource keys until the memory owner is reset. */
    uint64_t resource_clock, resource_mapping_version;
    uint64_t resource_versions[CTR_MEMORY_RESOURCE_SLOTS];
} ctr_memory;

typedef struct {
    const ctr_memory *owner;
    uint32_t address, size;
    unsigned permissions;
    uint64_t mapping_version, write_version;
} ctr_memory_resource_key;

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
typedef struct {
    uint32_t address;
    const void *input;
    uint32_t size;
} ctr_memory_write_range;
/* Publish up to CTR_MEMORY_MAX_WRITE_RANGES owned buffers as one transaction.
 * Preflight every destination and source before mutation. Destinations must not
 * overlap, including through backing aliases; the descriptor array and inputs
 * must not alias guest backing. Nonempty commits advance each affected
 * generation once. An empty array or all-zero sizes is a successful no-op. */
ctr_mem_result ctr_memory_write_ranges(ctr_memory *memory,
                                       const ctr_memory_write_range *ranges,size_t count);
/* Atomically split a fully mapped range to change permissions or remove it.
   Zero permissions create a guard mapping. A hole/capacity failure changes nothing. */
ctr_mem_result ctr_memory_change_range(ctr_memory *memory, uint32_t address,
                                      uint32_t size, unsigned permissions, bool remove);
/* Keys are valid only within this initialized owner's lifetime. Clear caches
 * before init/close/reuse; neither copying a memory owner nor retaining keys
 * across its reset transfers identity. Current permissions are checked on
 * capture and comparison. Empty/invalid ranges and exhausted clocks fail
 * closed, clearing the output key. Mapping changes invalidate all keys;
 * writes invalidate affected host backing pages, including guest aliases.
 * Invalid/misaligned outputs or outputs aliased to owner/backing are untouched. */
bool ctr_memory_resource_key_capture(const ctr_memory *memory,uint32_t address,
                                      uint32_t size,unsigned permissions,
                                      ctr_memory_resource_key *key);
bool ctr_memory_resource_key_current(const ctr_memory *memory,
                                      const ctr_memory_resource_key *key);
/* Notify trusted host writes after publication, including read-only HLE pages
 * and shared storage. These never copy bytes or authorize a guest write.
 * Both invalidate executable backing aliases. backing_changed also advances
 * the exclusive generation; resource_changed preserves the synthetic clock
 * page's existing exclusive-monitor policy.
 * Nonempty null/overflowing host ranges fail without changing versions. */
ctr_mem_result ctr_memory_resource_changed(ctr_memory *memory,const void *storage,size_t size);
ctr_mem_result ctr_memory_backing_changed(ctr_memory *memory,const void *storage,size_t size);
const char *ctr_mem_result_name(ctr_mem_result result);

#endif
