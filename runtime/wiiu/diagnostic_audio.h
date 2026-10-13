#ifndef CTR_DIAGNOSTIC_AUDIO_H
#define CTR_DIAGNOSTIC_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    CTR_AUDIO_NOT_STARTED,
    CTR_AUDIO_READY,
    CTR_AUDIO_NOT_FOREGROUND,
    CTR_AUDIO_RESET_FAILED,
    CTR_AUDIO_INIT_FAILED,
    CTR_AUDIO_QUIT_FAILED
} ctr_diagnostic_audio_result;

typedef struct {
    bool owned, last_inherited;
    ctr_diagnostic_audio_result result;
    uint32_t attempts, successes, failures, inherited_resets, releases;
} ctr_diagnostic_audio;

/* The standalone diagnostic is the sole AX owner within this process. Call
   only while in the foreground, before creating any diagnostic-owned voices.
   Taking a fresh AX context ends launch transition audio without playing tone
   samples or implementing guest sound. Repeated calls while owned are inert. */
bool ctr_diagnostic_audio_begin(ctr_diagnostic_audio *audio, bool foreground);

/* Release this application's voices before quitting its AX context. The
   optional callback must also tolerate AX having already been shut down and
   discard any now-stale voice pointer without calling AX on it. */
bool ctr_diagnostic_audio_end(ctr_diagnostic_audio *audio,
                              void (*release_voices)(void *), void *context);
const char *ctr_diagnostic_audio_result_name(ctr_diagnostic_audio_result result);

#endif
