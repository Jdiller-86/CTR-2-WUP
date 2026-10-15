#ifndef CTR_AUDIO_FIXTURE_H
#define CTR_AUDIO_FIXTURE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CTR_AUDIO_FIXTURE_CAPACITY 56000u
#define CTR_AUDIO_FIXTURE_RATE 32000u
#define CTR_AUDIO_FIXTURE_SAMPLES 48000u
typedef struct {
    uint8_t data[CTR_AUDIO_FIXTURE_CAPACITY];
    size_t bytes;
} CtrAudioFixture;
/* Original finite 1.5-second DSP ADPCM CSTM. Mono is a rising three-note
 * triangle motif; stereo adds a falling right-channel motif. Each half-second
 * phrase ends in 0.1 seconds of silence. No firmware, game data or recording.
 * Build once into owned storage, then keep it immutable while a voice uses it.
 * A channels value other than 1 or 2 preserves the complete output object. */
bool ctr_audio_fixture_build(CtrAudioFixture *fixture,unsigned channels);
/* Same original encoded motif, with a checked declared source rate. At 48 kHz
 * the clip is one second and pitches rise by 3/2 compared with the default.
 * Rates 8000..96000 are accepted; finite output capacity is the caller's job. */
bool ctr_audio_fixture_build_rate(CtrAudioFixture *fixture,unsigned channels,uint32_t rate);
/* Mono motif 0 rises, motif 1 falls. Source remains 48000 frames. */
bool ctr_audio_fixture_build_mono(CtrAudioFixture *fixture,uint32_t rate,unsigned motif);
#endif
