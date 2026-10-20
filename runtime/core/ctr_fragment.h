#ifndef CTR_FRAGMENT_H
#define CTR_FRAGMENT_H
#include <stdbool.h>
#include <stdint.h>

/* Owned byte-domain output merger, evaluated after the final TEV stage.
 * All eight alpha comparisons, sixteen logic operations and RGBA masks are
 * supported. Color reads/writes use only the explicit gates 0 or 15; operations
 * reading destination bytes require the full read gate when writes are active.
 * Depth/stencil/blending and other fragment operation modes remain unsupported. */
typedef struct {
    uint8_t logic_op,write_mask,alpha_function,alpha_reference;
    bool alpha_enabled;
} ctr_fragment_state;

bool ctr_fragment_register(unsigned reg);
bool ctr_fragment_register_value(unsigned reg,uint32_t value);
/* Arrays cover PICA registers through 0x113. No output changes on failure.
 * required demands all six registers completely defined. Otherwise no defined
 * merger registers selects legacy COPY/RGBA/disabled alpha; any definition
 * requires the entire explicit configuration, including full byte masks. */
bool ctr_fragment_decode(const uint32_t registers[],const uint8_t defined[],
                         bool required,ctr_fragment_state *out);
/* Internal evaluation for a successfully decoded state. Sources and the old
 * destination are captured before writes. Alpha failure preserves all bytes;
 * the return value reports alpha acceptance, independently of write masks. */
bool ctr_fragment_apply(const ctr_fragment_state *state,const uint8_t source[4],
                        uint8_t destination[4]);
#endif
