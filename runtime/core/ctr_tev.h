#ifndef CTR_TEV_H
#define CTR_TEV_H
#include <stdbool.h>
#include <stdint.h>

#define CTR_TEV_STAGES 6u
typedef struct {
    uint8_t source[2][3],operand[2][3],operation[2],scale[2],constant[4];
} ctr_tev_stage;
typedef struct {
    ctr_tev_stage stages[CTR_TEV_STAGES];
    uint8_t buffer_color[4],buffer_update[2];
} ctr_tev_program;
extern const unsigned ctr_tev_registers[CTR_TEV_STAGES];

/* Internal owned-state boundary: arrays contain the complete PICA register
 * file. Decode rejects unsupported fields and leaves output unchanged on error.
 * Active sources: primary, Texture0, PreviousBuffer, stage constant, and
 * Previous after stage 0. Buffer RGB/alpha update masks select stages 0..3.
 * Eight scalar operations, RGB/alpha operands and 1/2/4 scaling are supported.
 * Lighting, other textures, fog and DOT3 are not implemented. */
bool ctr_tev_register_value(unsigned reg,uint32_t value);
bool ctr_tev_decode(const uint32_t *registers,const uint8_t *defined,bool textured,
                    ctr_tev_program *out);
bool ctr_tev_passthrough(const ctr_tev_stage *stage);
/* Successfully decoded program uses a buffer source, update or initial color. */
bool ctr_tev_buffered(const ctr_tev_program *program);
unsigned ctr_tev_arguments(unsigned operation);
/* Byte-domain contract: product/interpolation division truncates by 255;
 * signed add subtracts 128. Clamp before scaling, then saturate to 255.
 * Every stage snapshots all RGB/alpha sources before publishing its result.
 * PreviousBuffer starts at zero. After each stage, including passthrough,
 * the pending buffer becomes visible before that stage's selected output
 * updates pending RGB/alpha. Pending starts at buffer_color; stage 1 sees it.
 * Evaluation and passthrough inspection require a successfully decoded program. */
void ctr_tev_evaluate(const ctr_tev_program *program,const uint8_t primary[4],
                      const uint8_t texture[4],uint8_t output[4]);
#endif
