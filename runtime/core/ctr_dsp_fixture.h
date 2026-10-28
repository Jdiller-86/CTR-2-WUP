#ifndef CTR_DSP_FIXTURE_H
#define CTR_DSP_FIXTURE_H
#include "ctr_dsp.h"
#define CTR_DSP_FIXTURE_BLOCKS 300u
extern const ctr_guest_image ctr_dsp_image;
extern const ctr_block ctr_dsp_fixture_blocks[];
extern const size_t ctr_dsp_fixture_block_count;
/* Produces 48,000 DSP-rate stereo frames through actual ARM DSP requests.
 * Caller owns at least 96,000 int16 samples; no native playback occurs here. */
bool ctr_dsp_fixture_render(bool translated,int16_t *pcm,size_t samples,
                            ctr_dsp_statistics *statistics,uint64_t *instructions);
#endif
