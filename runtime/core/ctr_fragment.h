#ifndef CTR_FRAGMENT_H
#define CTR_FRAGMENT_H
#include <stdbool.h>
#include <stdint.h>

/* Owned byte-domain output merger, evaluated after the final TEV stage.
 * All eight alpha comparisons, sixteen logic operations and RGBA masks are
 * supported. Color reads/writes use only the explicit gates 0 or 15; operations
 * reading destination bytes require the full read gate when writes are active.
 * Separate RGB/alpha blending supports five equations and fifteen factors.
 * Products remain exact integers until one truncating division by 255, then
 * clamp to a byte. MIN/MAX apply the factors. These are explicit software
 * rules; physical PICA/GX2 rounding remains unverified.
 * Descriptor-aware scenes perform D16 depth test/write between alpha acceptance
 * and color output. Stencil and other fragment operation modes are unsupported. */
typedef struct {
    uint8_t logic_op,write_mask,alpha_function,alpha_reference;
    bool alpha_enabled;
    bool blend_enabled;
    uint8_t blend_equation[2],blend_source[2],blend_destination[2];
    uint8_t blend_constant[4];
} ctr_fragment_state;

bool ctr_fragment_register(unsigned reg);
bool ctr_fragment_register_value(unsigned reg,uint32_t value);
/* Arrays cover PICA registers through 0x113. No output changes on failure.
 * required demands all six registers completely defined. Otherwise no defined
 * merger registers selects legacy COPY/RGBA/disabled alpha; any definition
 * requires the entire explicit configuration, including full byte masks.
 * Blend enable additionally requires 0x101 completely defined; selected
 * constant factors require full 0x103 even when writes are masked. Disabled
 * blending retains partial configuration but fully defined 0x101 still validates. */
bool ctr_fragment_decode(const uint32_t registers[],const uint8_t defined[],
                         bool required,ctr_fragment_state *out);
/* Descriptor-aware scene entry. Depth bits in 0x107 are admitted only when the
 * caller owns and validates a corresponding depth surface. The ordinary
 * decoder keeps rejecting depth for color-only/native legacy callers. */
bool ctr_fragment_decode_depth(const uint32_t registers[],const uint8_t defined[],
                               bool required,bool depth_allowed,ctr_fragment_state *out);
bool ctr_fragment_alpha_passes(const ctr_fragment_state *state,uint8_t alpha);
/* Internal evaluation for a successfully decoded state. Sources and the old
 * destination are captured before writes. Alpha failure preserves all bytes;
 * the return value reports alpha acceptance, independently of write masks. */
bool ctr_fragment_apply(const ctr_fragment_state *state,const uint8_t source[4],
                        uint8_t destination[4]);
#endif
