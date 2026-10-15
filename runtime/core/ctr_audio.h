#ifndef CTR_AUDIO_H
#define CTR_AUDIO_H
#include "ctr_cstm.h"
#include <stdbool.h>

#define CTR_AUDIO_VOICES 2u
#define CTR_AUDIO_OUTPUT_RATE 48000u
#define CTR_AUDIO_MAX_FRAMES 4096u
#define CTR_AUDIO_GAIN_ONE 32768u

typedef enum {
    CTR_AUDIO_OK=0, CTR_AUDIO_ARGUMENT, CTR_AUDIO_BOUNDS, CTR_AUDIO_FORMAT,
    CTR_AUDIO_UNSUPPORTED, CTR_AUDIO_OVERLAP, CTR_AUDIO_PREDICTOR, CTR_AUDIO_STATE
} CtrAudioResult;
/* Public storage for allocation-free ownership and diagnostic counters. Treat
 * every field as read-only; only these functions may change it. history[c][0/1]
 * precedes decoded, and pair[c][0/1] holds pair_position and its successor. */
typedef struct {
    const uint8_t *data;
    size_t bytes;
    CtrCstmInfo info;
    uint32_t position, phase, decoded, pair_position;
    int16_t history[2][2], pair[2][2];
    uint32_t gain[2];
    bool bound, playing, pair_valid;
    uint64_t rendered_frames, decoded_frames, clipped_samples;
} CtrAudioVoice;
typedef struct {
    uint32_t guard;
    CtrAudioVoice voices[CTR_AUDIO_VOICES];
    uint64_t rendered_frames, clipped_samples;
} CtrAudioEngine;

/* Checked non-looping CSTM v2 DSP ADPCM, mono/stereo, 8..96 kHz. Caller keeps
 * source bytes immutable and alive until unbound; no concurrent calls/mutation.
 * Bind resets that voice, unity gains and paused; NULL/0 unbinds. Play toggles
 * consumption without rewinding; EOF becomes paused. Seek resets phase to zero
 * and decodes from the containing block's supplied SEEK checkpoint; continuous
 * playback carries decoded histories across blocks, ignoring later checkpoints.
 * Mono duplicates to both outputs, stereo preserves channels. At each 48 kHz
 * output, interpolate source floor(position)+phase/48000, holding the final
 * sample for the fractional tail. Linear interpolation rounds nearest with exact
 * ties toward +infinity, then independent Q15 gain uses the same rounding.
 * Sum both voices in wide integers and saturate once to PCM16. This is a simple
 * deterministic resampler, not bandlimited or a Nintendo DSP fidelity claim.
 * rendered_frames includes silence at engine level and only consumed output
 * frames per voice. decoded_frames counts source frames decoded, including seek
 * work. clipped_samples counts final L/R saturation; per voice it counts those
 * clipped outputs to which that voice contributed in the clipped direction.
 * Counters saturate at UINT64_MAX. A render request is at most MAX_FRAMES stereo
 * frames; capacity is in frames, output interleaved L,R. Zero frames permits NULL.
 * State/output/source aliases, invalid late predictors and all validation errors
 * preserve the complete engine and requested output. All routines are bounded,
 * allocation-free; render first validates a dry simulation, then commits. */
CtrAudioResult ctr_audio_init(CtrAudioEngine *engine);
CtrAudioResult ctr_audio_bind(CtrAudioEngine *engine,unsigned voice,
    const uint8_t *data,size_t bytes);
CtrAudioResult ctr_audio_gain(CtrAudioEngine *engine,unsigned voice,
    uint32_t left_q15,uint32_t right_q15);
CtrAudioResult ctr_audio_play(CtrAudioEngine *engine,unsigned voice,bool playing);
CtrAudioResult ctr_audio_seek(CtrAudioEngine *engine,unsigned voice,uint32_t sample);
CtrAudioResult ctr_audio_render(CtrAudioEngine *engine,int16_t *output,
    size_t capacity_frames,size_t frames);
const char *ctr_audio_result_name(CtrAudioResult result);
#endif
