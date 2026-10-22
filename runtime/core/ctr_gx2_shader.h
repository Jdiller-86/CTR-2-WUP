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
    bool texcoord0; /* Legacy mirror of texcoord_mask bit 0. */
    uint8_t texcoord_mask; /* Complete UV0=12/13, UV1=14/15, UV2=22/23 pairs. */
    uint8_t uniform_used[CTR_SHADER_UNIFORMS]; /* Read lane masks. */
    uint16_t uniform_order[CTR_SHADER_UNIFORMS*4]; /* Unique lanes, first-read order. */
    uint16_t uniform_count;
} ctr_gx2_shader;

/* Compile MOV/ADD/MUL/NOP/END, including masks, independent source swizzles,
 * negation and temporary dependencies. Geometry/relative addressing fail.
 * output_maps is the packed PICA semantic register table (up to seven words):
 * output_mask selects output registers; output_count must match its popcount.
 * Position xyzw and primary-color rgba must be defined and mapped. UV pairs
 * 12/13, 14/15 and 22/23 are optional; both lanes must be defined when either is mapped.
 * Inputs v0..15 are fetched into GPR1..16; temporary/output storage is private.
 * Uniforms use constant-file vectors 0..95 in UniformRegister mode. Upload each
 * uniform_used vector with GX2SetVertexUniformReg(index*4,4,bits).
 * The result needs a VS GPR budget of at least gprs (53..56), not the default 48.
 * UV unit n uses GPR53+n. PARAM0 is color; complete UV sets export densely into
 * PARAM1..3 in unit order. Native VS_OUT_ID maps those slots to UV semantics1..3.
 * Every actual vertex must also pass ctr_shader_execute before GPU submission:
 * the encoder does not expand the existing exact float24 numeric contract.
 * On any failure output remains unchanged; objects must be stable for the call. */
ctr_gx2_shader_result ctr_gx2_shader_compile(const ctr_shader_program *program,
                                            uint16_t output_mask,
                                            const uint32_t *output_maps,
                                            unsigned output_count,
                                            ctr_gx2_shader *output);
/* Recheck current uniform definitions and exact float24 values for a previously
 * compiled program with unchanged code/entry/descriptors and output semantics. The
 * recorded first-read order preserves compile diagnostics. Invalid lane-order
 * metadata returns ARGUMENT. Neither object is changed; both must be stable. */
ctr_gx2_shader_result ctr_gx2_shader_validate_uniforms(const ctr_shader_program *program,
                                                       const ctr_gx2_shader *compiled);
const char *ctr_gx2_shader_result_name(ctr_gx2_shader_result result);
#endif
