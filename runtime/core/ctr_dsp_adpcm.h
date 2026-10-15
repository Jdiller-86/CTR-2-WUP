#ifndef CTR_DSP_ADPCM_H
#define CTR_DSP_ADPCM_H
#include <stddef.h>
#include <stdint.h>

#define CTR_DSP_ADPCM_MAX_SAMPLES (1024u*1024u)
typedef struct { size_t sample; int16_t history1,history2; } CtrDspAdpcmState;
typedef enum {
    CTR_DSP_ADPCM_OK=0, CTR_DSP_ADPCM_ARGUMENT, CTR_DSP_ADPCM_BOUNDS,
    CTR_DSP_ADPCM_PREDICTOR, CTR_DSP_ADPCM_OVERLAP
} CtrDspAdpcmResult;
/* Decode signed PCM16 from 8-byte/14-sample DSP ADPCM frames. Header high nibble
 * selects one of eight coefficient pairs; low nibble is a scale exponent 0..15.
 * Samples use high nibble first, rounded prediction, saturation, then history.
 * Coefficients and histories are numeric signed 16-bit values, not packed bytes.
 * state.sample indexes the original encoded base; supplied histories must describe
 * the two samples before that position (including an explicit seek checkpoint).
 * Only requested bytes/headers are required, so a final partial frame is allowed.
 * At most MAX_SAMPLES are decoded per call. Zero samples is a validated no-op.
 * Input/coefficients remain immutable during the call. State and written output
 * must not alias each other or either input. Every validation failure preserves
 * all output/state; no allocation or hidden fallback is used. */
CtrDspAdpcmResult ctr_dsp_adpcm_decode(const uint8_t *encoded,size_t bytes,
    const int16_t coefficients[16],CtrDspAdpcmState *state,
    int16_t *output,size_t capacity,size_t samples);
const char *ctr_dsp_adpcm_result_name(CtrDspAdpcmResult result);
#endif
