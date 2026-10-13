#include "diagnostic_audio.h"
#include <sndcore2/core.h>

static void count(uint32_t *value) {
    if (*value != UINT32_MAX) ++*value;
}

bool ctr_diagnostic_audio_begin(ctr_diagnostic_audio *audio, bool foreground) {
    if (!audio) return false;
    if (!foreground) {
        audio->result=CTR_AUDIO_NOT_FOREGROUND;
        return false;
    }
    if (audio->owned) {
        if (audio->result==CTR_AUDIO_QUIT_FAILED) return false;
        audio->result=CTR_AUDIO_READY;
        return true;
    }

    count(&audio->attempts);
    audio->last_inherited=AXIsInit()!=0;
    if (audio->last_inherited) {
        /* AXInitWithParams may be inert when AX is already initialized. This
           application has not allocated voices yet; replace that same-process
           inherited context instead of retaining its sound/callback state. */
        count(&audio->inherited_resets);
        AXQuit();
        if (AXIsInit()) {
            count(&audio->failures);
            audio->result=CTR_AUDIO_RESET_FAILED;
            return false;
        }
    }
    AXInitParams params={0};
    params.renderer=AX_INIT_RENDERER_48KHZ;
    params.pipeline=AX_INIT_PIPELINE_SINGLE;
    AXInitWithParams(&params);
    if (!AXIsInit()) {
        /* This was our initialization attempt, so release any partial native
           state as well. No diagnostic voice has been created at this point. */
        AXQuit();
        count(&audio->failures);
        audio->result=CTR_AUDIO_INIT_FAILED;
        return false;
    }
    audio->owned=true;
    audio->result=CTR_AUDIO_READY;
    count(&audio->successes);
    return true;
}

bool ctr_diagnostic_audio_end(ctr_diagnostic_audio *audio,
                              void (*release_voices)(void *), void *context) {
    if (!audio || !audio->owned) return true;
    if (release_voices) release_voices(context);
    if (AXIsInit()) AXQuit();
    if (AXIsInit()) {
        count(&audio->failures);
        audio->result=CTR_AUDIO_QUIT_FAILED;
        return false;
    }
    audio->owned=false;
    count(&audio->releases);
    return true;
}

const char *ctr_diagnostic_audio_result_name(ctr_diagnostic_audio_result result) {
    switch (result) {
        case CTR_AUDIO_NOT_STARTED:return "NOT_STARTED";
        case CTR_AUDIO_READY:return "READY";
        case CTR_AUDIO_NOT_FOREGROUND:return "NOT_FOREGROUND";
        case CTR_AUDIO_RESET_FAILED:return "RESET_FAILED";
        case CTR_AUDIO_INIT_FAILED:return "INIT_FAILED";
        case CTR_AUDIO_QUIT_FAILED:return "QUIT_FAILED";
        default:return "INVALID";
    }
}
