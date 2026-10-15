#ifndef CTR_CSTM_H
#define CTR_CSTM_H
#include "ctr_dsp_adpcm.h"

#define CTR_CSTM_MAX_BYTES (256u*1024u*1024u)
#define CTR_CSTM_MAX_BLOCKS 65536u
typedef enum {
    CTR_CSTM_OK=0, CTR_CSTM_ARGUMENT, CTR_CSTM_BOUNDS,
    CTR_CSTM_FORMAT, CTR_CSTM_UNSUPPORTED, CTR_CSTM_OVERLAP
} CtrCstmResult;
typedef struct {
    uint32_t sample_rate, samples, blocks, block_samples, last_samples;
    uint32_t block_bytes, last_bytes, last_padded_bytes;
    unsigned channels;
} CtrCstmInfo;
typedef struct {
    const uint8_t *encoded;
    size_t bytes;
    uint32_t first_sample, samples;
    int16_t coefficients[16];
    CtrDspAdpcmState state;
} CtrCstmBlock;
/* Checked LE CSTM v2, non-looping mono/stereo DSP ADPCM only. SEEK entries
 * must be 4 bytes/channel at the regular block interval. No allocation, playback,
 * track mixing or predictor scan. Decode each returned block with the existing
 * codec, using its own encoded base and sample-zero state. Input stays immutable
 * and alive while a returned span is used. All failures preserve the whole output;
 * output must not alias input. Accessors revalidate bytes, never trust a retained
 * mutable descriptor. Container history is supplied metadata, not authenticated
 * by decoding preceding blocks. */
CtrCstmResult ctr_cstm_info(const uint8_t *data,size_t bytes,CtrCstmInfo *output);
CtrCstmResult ctr_cstm_block(const uint8_t *data,size_t bytes,
    uint32_t block,unsigned channel,CtrCstmBlock *output);
const char *ctr_cstm_result_name(CtrCstmResult result);
#endif
