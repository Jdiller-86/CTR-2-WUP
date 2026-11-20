#ifndef CTR_TEV_H
#define CTR_TEV_H
#include <stdbool.h>
#include <stdint.h>

#define CTR_TEV_STAGES 6u
#define CTR_TEV_TEXTURES 3u
typedef struct {
    uint8_t source[2][3],operand[2][3],operation[2],scale[2],constant[4];
} ctr_tev_stage;
typedef struct {
    ctr_tev_stage stages[CTR_TEV_STAGES];
    uint8_t buffer_color[4],buffer_update[2];
} ctr_tev_program;
/* Draw-local operand routing for a decoded, immutable combiner. */
typedef struct {
    const ctr_tev_program *program;
    uint8_t arguments[CTR_TEV_STAGES][2];
    uint8_t operands[CTR_TEV_STAGES][4][3];
    uint8_t active;
    bool reads_buffer;
} ctr_tev_plan;
extern const unsigned ctr_tev_registers[CTR_TEV_STAGES];

/* Internal owned-state boundary: arrays contain the complete PICA register
 * file. Decode rejects unsupported fields and leaves output unchanged on error.
 * Active sources: primary, enabled Texture0/1/2, PreviousBuffer, stage constant, and
 * Previous after stage 0. Buffer RGB/alpha update masks select stages 0..3.
 * Eight scalar operations, RGB/alpha operands and 1/2/4 scaling are supported.
 * texture_mask selects units 0..2. The game scene supplies fragment colors
 * and applies fog after the combiner. The game supports DOT3 RGB. */
bool ctr_tev_register_value(unsigned reg,uint32_t value);
bool ctr_tev_decode(const uint32_t *registers,const uint8_t *defined,unsigned texture_mask,
                    ctr_tev_program *out);
/* Diagnostic TEV operands with the depth-fog control bits admitted. */
bool ctr_tev_decode_fog(const uint32_t *registers,const uint8_t *defined,unsigned texture_mask,
                        ctr_tev_program *out);
/* The game combiner resolves stage zero's Previous through its third source. */
bool ctr_tev_game_register_value(unsigned reg,uint32_t value);
bool ctr_tev_decode_game(const uint32_t *registers,const uint8_t *defined,unsigned texture_mask,
                         ctr_tev_program *out);
bool ctr_tev_passthrough(const ctr_tev_stage *stage);
/* Successfully decoded program uses a buffer source, update or initial color. */
bool ctr_tev_buffered(const ctr_tev_program *program);
unsigned ctr_tev_arguments(unsigned operation);
/* Byte-domain contract: product/interpolation division truncates by 255;
 * signed add subtracts 128. DOT3 RGB sums signed normalized RGB products
 * and replicates the clamped scalar across color lanes. Alpha is independent.
 * Clamp before scaling, then saturate to 255.
 * Every stage snapshots all RGB/alpha sources before publishing its result.
 * PreviousBuffer starts at zero. After each stage, including passthrough,
 * the pending buffer becomes visible before that stage's selected output
 * updates pending RGB/alpha. Pending starts at buffer_color; stage 1 sees it.
 * Evaluation and passthrough inspection require a successfully decoded program. */
/* Legacy programs are decoded with mask 0 or 1; texture may be NULL when unused. */
void ctr_tev_evaluate(const ctr_tev_program *program,const uint8_t primary[4],
                      const uint8_t texture[4],uint8_t output[4]);
/* Distinct, already sampled RGBA values for the three enabled ordinary units.
 * The legacy evaluator above supplies only Texture0. */
void ctr_tev_evaluate_textures(const ctr_tev_program *program,const uint8_t primary[4],
                               const uint8_t textures[CTR_TEV_TEXTURES][4],uint8_t output[4]);
void ctr_tev_evaluate_game(const ctr_tev_program *program,const uint8_t primary[4],
                          const uint8_t textures[CTR_TEV_TEXTURES][4],const uint8_t lighting[2][4],
                          uint8_t output[4]);
void ctr_tev_prepare(const ctr_tev_program *program,ctr_tev_plan *plan);
void ctr_tev_evaluate_prepared(const ctr_tev_plan *plan,const uint8_t primary[4],
                               const uint8_t textures[CTR_TEV_TEXTURES][4],const uint8_t lighting[2][4],
                               uint8_t output[4]);
#endif
