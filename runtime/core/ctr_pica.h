#ifndef CTR_PICA_H
#define CTR_PICA_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CTR_PICA_REGISTER_COUNT 0x400u
#define CTR_PICA_MAX_BYTES (4u*1024u*1024u)
#define CTR_PICA_MAX_WRITES 65536u
#define CTR_PICA_FINALIZE 0x0010u
#define CTR_PICA_FINALIZE_VALUE UINT32_C(0x12345678)

typedef enum {
    CTR_PICA_OK, CTR_PICA_ARGUMENT, CTR_PICA_ALIGNMENT, CTR_PICA_HEADER,
    CTR_PICA_RANGE, CTR_PICA_TRUNCATED, CTR_PICA_LIMIT, CTR_PICA_CAPACITY,
    CTR_PICA_ALIAS, CTR_PICA_UNSUPPORTED, CTR_PICA_FINALIZATION,
    CTR_PICA_OVERFLOW
} ctr_pica_result;
typedef struct {
    size_t byte_offset; /* Start of this command, shared by its parameters. */
    uint32_t value;
    uint16_t reg;
    uint8_t byte_mask;
} ctr_pica_write;
typedef struct {
    ctr_pica_result result;
    size_t byte_offset, parameter_offset, records, writes, finalizations;
    uint32_t header, value;
    uint16_t reg;
} ctr_pica_report;
typedef struct {
    uint32_t registers[CTR_PICA_REGISTER_COUNT];
    uint64_t committed_lists, register_writes, finalizations;
} ctr_pica_state;

/* Original little-endian inspection, not GPU execution. The command header has
 * an eight-bit extra-parameter count, four byte-enable bits, and an increment
 * flag. Header bits 28..30 are unused and rejected rather than guessed.
 * All commands include eight-byte record padding. Unknown header/register bits,
 * range/size/write limits, truncation and output aliases fail visibly.
 * NULL writes with capacity zero validates/counts only; otherwise the complete
 * stream is validated before any output write. Reports describe errors and may
 * change on failure, but an aliased report is never written. Unaligned host byte
 * pointers are supported. No guest-endian casts or native bitfields are used. */
ctr_pica_result ctr_pica_inspect(const void *bytes,size_t size,size_t max_writes,
                               ctr_pica_write *writes,size_t capacity,
                               ctr_pica_report *report);

/* This reset initializes the software snapshot, not physical PICA reset state.
 * Ordinary supported registers are viewport 0x041..0x044,0x068 and scissor
 * 0x065..0x067. These are raw masked
 * configuration writes; no rendering/float interpretation is claimed.
 * A list must be 16-byte aligned in length and end with one or two full-mask
 * FINALIZE=0x12345678 writes (the stock-libctru Split pattern). FINALIZE is an
 * explicit no-draw completion marker, never stored as ordinary register state.
 * All other registers, including unknown, shader/uniform/table uploads, draw,
 * command-buffer jumps and cache triggers, are unsupported even with mask zero.
 * Validation and all counters are transactional for the entire list. This API
 * never accesses guest memory, produces pixels, runs a shader or submits GX2. */
void ctr_pica_state_init(ctr_pica_state *state);
bool ctr_pica_register_supported(unsigned reg);
ctr_pica_result ctr_pica_execute(ctr_pica_state *state,const void *bytes,
                               size_t size,size_t max_writes,
                               ctr_pica_report *report);
const char *ctr_pica_result_name(ctr_pica_result result);
uint32_t ctr_pica_merge(uint32_t previous,uint32_t value,unsigned byte_mask);
#endif
