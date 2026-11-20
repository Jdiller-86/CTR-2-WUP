#ifndef CTR_SHADER_H
#define CTR_SHADER_H
#include "ctr_vertex.h"
#include <stdbool.h>

/* Vertex code has 512 words. Geometry storage/entry semantics are unsupported. */
#define CTR_SHADER_MAX_CODE 512u
#define CTR_SHADER_MAX_DESCRIPTORS 128u
#define CTR_SHADER_UNIFORMS 96u
#define CTR_SHADER_MAX_STEPS 4096u
typedef enum {
    CTR_SHADER_OK, CTR_SHADER_ARGUMENT, CTR_SHADER_OVERFLOW, CTR_SHADER_ALIAS,
    CTR_SHADER_RANGE, CTR_SHADER_UNSUPPORTED, CTR_SHADER_UNDEFINED,
    CTR_SHADER_NUMERIC, CTR_SHADER_NO_END, CTR_SHADER_LIMIT
} ctr_shader_result;
typedef struct {
    uint32_t code[CTR_SHADER_MAX_CODE], descriptors[CTR_SHADER_MAX_DESCRIPTORS];
    uint32_t uniforms[CTR_SHADER_UNIFORMS][4]; /* IEEE binary32 bits, not casts. */
    uint8_t code_defined[CTR_SHADER_MAX_CODE], descriptor_defined[CTR_SHADER_MAX_DESCRIPTORS];
    uint8_t uniform_defined[CTR_SHADER_UNIFORMS]; /* One bit per defined lane. */
    uint32_t integer_uniforms[4];
    uint8_t integer_defined;
    uint16_t boolean_uniforms;
    bool boolean_defined;
    unsigned entry;
    bool round_f24; /* Game software model. Zero selects exact diagnostic arithmetic. */
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

/* Bounded PICA vertex execution with independent swizzles/negation and write
 * masks. All operands are captured before temporary writes. Arithmetic uses
 * finite float24 values with either exact diagnostic or rounded game mode.
 * Exact cancellation is +0, -0 + -0 is -0, and MUL uses the XOR of signs.
 * MOVA and LOOP supply bounded relative addressing. CALL, CMP/IFC, IFU and
 * a single active LOOP have checked targets. Arithmetic includes ADD, MUL,
 * DP4, MAX, RCP and MAD with a separately computed product. The game path
 * also supports conditional calls, JMPC/JMPU, DP3, DPH/DPHI and positive RSQ.
 * Read-before-write fails for temporary/uniform/input lanes; an actual END is
 * required within code words 0..511; execution never wraps past word 511.
 * Exact mode requires finite, exactly representable float24 operands. The game
 * mode rounds to nearest-even and flushes underflow. Full output is published
 * only after successful termination.
 * Caller-owned typed objects must be aligned and stable during the call. */
ctr_shader_result ctr_shader_execute(const ctr_shader_program *program,
                                     const ctr_vertex *input,
                                     ctr_shader_output *output);
typedef struct {
    uint8_t op,dest,src[3],mask,read_mask,rel_index;
    uint8_t flags,wmask,reg_idx,err,cmp_op[2],src_mask[3],safe_srcs;
    uint8_t swiz[3][4];
    uint16_t target,extra;
} ctr_shader_fast_insn;
typedef struct {
    ctr_shader_fast_insn insns[CTR_SHADER_MAX_CODE];
    float uniforms[CTR_SHADER_UNIFORMS][4];
    uint8_t uniform_defined[CTR_SHADER_UNIFORMS];
    uint8_t uniform_numeric[CTR_SHADER_UNIFORMS];
    uint32_t integer_uniforms[4];
    uint8_t integer_defined;
    uint16_t boolean_uniforms;
    bool boolean_defined;
    unsigned entry;
} ctr_shader_fast;
/* Pre-decoded rounded vertex shader fast path for real-time game rendering.
 * Requires program->round_f24 == true. Preserves operand checking, undefined
 * lane propagation, control flow and relative addressing in native float32. */
bool ctr_shader_prepare_fast(const ctr_shader_program *program,ctr_shader_fast *out);
ctr_shader_result ctr_shader_execute_fast(const ctr_shader_fast *fast,
                                          const ctr_vertex *input,
                                          ctr_shader_output *output);
const char *ctr_shader_result_name(ctr_shader_result result);
#endif
