#ifndef CTR_SHADER_H
#define CTR_SHADER_H
#include "ctr_vertex.h"
#include <stdbool.h>

/* Vertex code has 512 words. Geometry storage/entry semantics are unsupported. */
#define CTR_SHADER_MAX_CODE 512u
#define CTR_SHADER_MAX_DESCRIPTORS 128u
#define CTR_SHADER_UNIFORMS 96u
typedef enum {
    CTR_SHADER_OK, CTR_SHADER_ARGUMENT, CTR_SHADER_OVERFLOW, CTR_SHADER_ALIAS,
    CTR_SHADER_RANGE, CTR_SHADER_UNSUPPORTED, CTR_SHADER_UNDEFINED,
    CTR_SHADER_NUMERIC, CTR_SHADER_NO_END
} ctr_shader_result;
typedef struct {
    uint32_t code[CTR_SHADER_MAX_CODE], descriptors[CTR_SHADER_MAX_DESCRIPTORS];
    uint32_t uniforms[CTR_SHADER_UNIFORMS][4]; /* IEEE binary32 bits, not casts. */
    uint8_t code_defined[CTR_SHADER_MAX_CODE], descriptor_defined[CTR_SHADER_MAX_DESCRIPTORS];
    uint8_t uniform_defined[CTR_SHADER_UNIFORMS]; /* One bit per defined lane. */
    unsigned entry;
} ctr_shader_program;
typedef struct {
    float values[16][4];
    uint8_t written[16]; /* Undefined lanes are never claimed as shader output. */
    unsigned instructions;
} ctr_shader_output;

/* Exact normal float24 interchange only. Preserve signed zero; reject raw high
 * bits, nonzero denormals, NaN/Inf and float32 values requiring rounding. No
 * float24 arithmetic or hardware rounding/denormal policy is approximated. */
bool ctr_shader_f24_decode(uint32_t raw, uint32_t *float32_bits);
bool ctr_shader_f24_exact(uint32_t float32_bits, uint32_t *raw);

/* Original bounded PICA MOV/NOP/END execution. MOV supports input/temporary/
 * float-uniform sources, swizzles, source negation and destination masks.
 * Relative addressing, arithmetic, branches and geometry instructions fail.
 * Read-before-write fails for temporary/uniform/input lanes; an actual END is
 * required within code words 0..511; execution never wraps past word 511.
 * Input values used must have exact finite normal float24/zero
 * representation. Full output is published only after successful termination.
 * Caller-owned typed objects must be aligned and stable during the call. */
ctr_shader_result ctr_shader_execute(const ctr_shader_program *program,
                                     const ctr_vertex *input,
                                     ctr_shader_output *output);
const char *ctr_shader_result_name(ctr_shader_result result);
#endif
