#ifndef CTR_AUDIO_STREAM_DIAGNOSTIC_H
#define CTR_AUDIO_STREAM_DIAGNOSTIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* This finite diagnostic adapter owns one immutable, copied stereo clip. It
 * does not implement an asynchronous stream queue or a guest DSP protocol. */
#define CTR_NATIVE_PCM_MAX_FRAMES 96000u

typedef enum {
    CTR_NATIVE_PCM_EMPTY, CTR_NATIVE_PCM_READY, CTR_NATIVE_PCM_PLAYING,
    CTR_NATIVE_PCM_PAUSED, CTR_NATIVE_PCM_STOPPED, CTR_NATIVE_PCM_COMPLETE,
    CTR_NATIVE_PCM_FAILED
} ctr_native_pcm_status;

typedef enum {
    CTR_NATIVE_PCM_OK, CTR_NATIVE_PCM_ARGUMENT, CTR_NATIVE_PCM_BOUNDS,
    CTR_NATIVE_PCM_BUSY, CTR_NATIVE_PCM_AX_UNAVAILABLE,
    CTR_NATIVE_PCM_VOICE_FAILED, CTR_NATIVE_PCM_CONFIG_FAILED,
    CTR_NATIVE_PCM_INVALID_STATE
} ctr_native_pcm_result;

typedef struct {
    ctr_native_pcm_status status;
    ctr_native_pcm_result result;
    bool prepared, running[2];
    uint32_t frames, pcm_fnv1a, cursor[2];
    uint32_t prepares, plays, pauses, resumes, stops, releases;
    uint32_t completions, failures, ownership_losses;
} ctr_native_pcm_report;

struct AXVoice;
typedef struct {
    struct AXVoice *voices[2];
    bool observed_running[2];
    ctr_native_pcm_report report;
} ctr_native_pcm;

/* Zero-initialize state and keep it alive until release. diagnostic_audio owns
 * AX initialization and shutdown; this adapter never initializes/quits AX. Only
 * one state may own the module's static 64-byte-aligned planar buffers at once.
 * Calls belong on the foreground/main thread; there are no adapter callbacks.
 * Prepare replaces an owned clip only after stopping/freeing its old voices.
 * The caller may release/reuse interleaved PCM as soon as prepare returns. */
bool ctr_native_pcm_prepare(ctr_native_pcm *state, const int16_t *interleaved,
                            size_t frames);
/* Play starts the clip at zero, including after COMPLETE. Pause/resume retain
 * independently observed channel cursors at native AX frame granularity. */
bool ctr_native_pcm_play(ctr_native_pcm *state);
bool ctr_native_pcm_pause(ctr_native_pcm *state);
bool ctr_native_pcm_resume(ctr_native_pcm *state);
/* Stop retains the prepared clip and rewinds both channel cursors. */
bool ctr_native_pcm_stop(ctr_native_pcm *state);
/* Poll native running/cursor state and copy public numeric evidence. COMPLETE
 * requires both channels to have been observed running and subsequently stopped;
 * an extremely short clip may finish between polls without that confirmation.
 * Native completion is not a substitute for a human audible observation. Clip frame
 * count/hash and cumulative counters survive release for final reporting. */
bool ctr_native_pcm_snapshot(ctr_native_pcm *state, ctr_native_pcm_report *out);
/* Suitable for diagnostic_audio_end's release_voices callback. If AX has gone,
 * discard stale voice pointers without dereferencing or freeing them. */
void ctr_native_pcm_release(void *state);
const char *ctr_native_pcm_status_name(ctr_native_pcm_status status);
const char *ctr_native_pcm_result_name(ctr_native_pcm_result result);

#endif
