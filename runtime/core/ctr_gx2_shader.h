#ifndef CTR_GX2_SHADER_H
#define CTR_GX2_SHADER_H
#include "ctr_shader.h"

/* Original bounded Latte encoder. Program storage is explicitly little endian;
 * the native caller copies it into GX2_SHADER_PROGRAM_ALIGNMENT storage. */
#define CTR_GX2_SHADER_MAX_BYTES 65536u
#define CTR_GX2_SHADER_ALU_OFFSET 512u
#define CTR_GX2_SHADER_GPRS 53u
#define CTR_GX2_SHADER_TEXTURED_GPRS 54u
typedef enum {
    CTR_GX2_SHADER_OK, CTR_GX2_SHADER_ARGUMENT, CTR_GX2_SHADER_OVERFLOW,
    CTR_GX2_SHADER_ALIAS, CTR_GX2_SHADER_RANGE, CTR_GX2_SHADER_UNSUPPORTED,
    CTR_GX2_SHADER_UNDEFINED, CTR_GX2_SHADER_NUMERIC, CTR_GX2_SHADER_NO_END,
    CTR_GX2_SHADER_NO_MEMORY, CTR_GX2_SHADER_LIMIT
} ctr_gx2_shader_result;
typedef struct {
    uint8_t program[CTR_GX2_SHADER_MAX_BYTES];
    uint32_t size,gprs,pica_instructions,alu_instructions; /* size is padded to 256. */
    uint16_t input_mask;
    bool texcoord0; /* Complete optional PICA semantics 12 and 13. */
    uint8_t uniform_used[CTR_SHADER_UNIFORMS]; /* Read lane masks. */
} ctr_gx2_shader;

/* Compile MOV/ADD/MUL/NOP/END, including masks, independent source swizzles,
 * negation and temporary dependencies. Geometry/relative addressing fail.
 * output_maps is the packed PICA semantic register table (up to seven words):
 * output_mask selects output registers; output_count must match its popcount.
 * Position xyzw and primary-color rgba must be defined and mapped. Texcoord0
 * semantics 12/13 are optional, but both must be defined when either is mapped.
 * Inputs v0..15 are fetched into GPR1..16; temporary/output storage is private.
 * Uniforms use constant-file vectors 0..95 in UniformRegister mode. Upload each
 * uniform_used vector with GX2SetVertexUniformReg(index*4,4,bits).
 * The result needs a VS GPR budget of at least gprs (53, or 54 with texcoord0),
 * not the default 48. PARAM0 is color; optional PARAM1 exports texcoord0.xy01.
 * Every actual vertex must also pass ctr_shader_execute before GPU submission:
 * the encoder does not expand the existing exact float24 numeric contract.
 * On any failure output remains unchanged; objects must be stable for the call. */
ctr_gx2_shader_result ctr_gx2_shader_compile(const ctr_shader_program *program,
                                            uint16_t output_mask,
                                            const uint32_t *output_maps,
                                            unsigned output_count,
                                            ctr_gx2_shader *output);
const char *ctr_gx2_shader_result_name(ctr_gx2_shader_result result);
#endif
