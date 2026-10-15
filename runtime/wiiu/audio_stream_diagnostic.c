#include "audio_stream_diagnostic.h"

#include <coreinit/cache.h>
#include <sndcore2/core.h>
#include <sndcore2/voice.h>
#include <string.h>

static int16_t pcm[2][CTR_NATIVE_PCM_MAX_FRAMES] __attribute__((aligned(64)));
static ctr_native_pcm *owner;

static void count(uint32_t *value) {
    if (*value != UINT32_MAX) ++*value;
}

static bool overlap(const void *a, size_t a_size, const void *b, size_t b_size) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x<=y ? y-x<a_size : x-y<b_size;
}

static bool fail(ctr_native_pcm *state, ctr_native_pcm_result result, bool terminal) {
    if (state) {
        state->report.result=result;
        if (terminal) state->report.status=CTR_NATIVE_PCM_FAILED;
        count(&state->report.failures);
    }
    return false;
}

static void lost(ctr_native_pcm *state) {
    state->voices[0]=state->voices[1]=NULL;
    state->report.prepared=false;
    state->report.running[0]=state->report.running[1]=false;
    if (owner==state) owner=NULL;
    count(&state->report.ownership_losses);
    fail(state,CTR_NATIVE_PCM_AX_UNAVAILABLE,true);
}

static bool available(ctr_native_pcm *state) {
    if (!state) return false;
    if (owner!=state || !state->report.prepared ||
        !state->voices[0] || !state->voices[1])
        return fail(state,CTR_NATIVE_PCM_INVALID_STATE,false);
    if (!AXIsInit()) {
        lost(state);
        return false;
    }
    return true;
}

static void free_voices(ctr_native_pcm *state) {
    bool initialized=AXIsInit()!=0;
    for (unsigned channel=0;channel<2;++channel) {
        if (state->voices[channel] && initialized) {
            AXSetVoiceState(state->voices[channel],AX_VOICE_STATE_STOPPED);
            AXFreeVoice(state->voices[channel]);
        }
        state->voices[channel]=NULL;
        state->report.running[channel]=false;
    }
}

static bool configure(AXVoice *voice, unsigned channel, uint32_t frames) {
    AXSetVoiceState(voice,AX_VOICE_STATE_STOPPED);
    AXVoiceOffsets offsets={0};
    offsets.dataType=AX_VOICE_FORMAT_LPCM16;
    offsets.loopingEnabled=AX_VOICE_LOOP_DISABLED;
    offsets.endOffset=frames-1;
    offsets.data=pcm[channel];
    if (!AXCheckVoiceOffsets(&offsets)) return false;
    AXSetVoiceOffsets(voice,&offsets);
    AXSetVoiceSrcType(voice,AX_VOICE_SRC_TYPE_NONE);
    if (AXSetVoiceSrcRatio(voice,1.0f)!=AX_VOICE_RATIO_RESULT_SUCCESS) return false;
    AXVoiceVeData volume={0x8000,0};
    AXSetVoiceVe(voice,&volume);
    AXVoiceDeviceMixData tv_mix[6]={0},drc_mix[4]={0};
    tv_mix[channel].bus[0].volume=0x8000;
    drc_mix[channel].bus[0].volume=0x8000;
    return AXSetVoiceDeviceMix(voice,AX_DEVICE_TYPE_TV,0,tv_mix)==AX_RESULT_SUCCESS &&
           AXSetVoiceDeviceMix(voice,AX_DEVICE_TYPE_DRC,0,drc_mix)==AX_RESULT_SUCCESS;
}

bool ctr_native_pcm_prepare(ctr_native_pcm *state, const int16_t *interleaved,
                            size_t frames) {
    if (!state) return false;
    if (!interleaved || !frames || (uintptr_t)interleaved%_Alignof(int16_t))
        return fail(state,CTR_NATIVE_PCM_ARGUMENT,false);
    if (frames>CTR_NATIVE_PCM_MAX_FRAMES ||
        (uintptr_t)interleaved>UINTPTR_MAX-frames*2*sizeof(*interleaved))
        return fail(state,CTR_NATIVE_PCM_BOUNDS,false);
    size_t bytes=frames*2*sizeof(*interleaved);
    if (overlap(interleaved,bytes,state,sizeof(*state)) ||
        overlap(interleaved,bytes,pcm,sizeof(pcm)) ||
        overlap(state,sizeof(*state),pcm,sizeof(pcm)))
        return fail(state,CTR_NATIVE_PCM_ARGUMENT,false);
    if (owner && owner!=state) return fail(state,CTR_NATIVE_PCM_BUSY,false);
    if (!AXIsInit()) {
        if (owner==state) lost(state);
        else fail(state,CTR_NATIVE_PCM_AX_UNAVAILABLE,true);
        return false;
    }
    if (owner==state) ctr_native_pcm_release(state);
    state->report.prepared=false;
    state->report.cursor[0]=state->report.cursor[1]=0;
    state->voices[0]=AXAcquireVoice(31,NULL,NULL);
    if (state->voices[0]) state->voices[1]=AXAcquireVoice(31,NULL,NULL);
    if (!state->voices[0] || !state->voices[1]) {
        free_voices(state);
        return fail(state,CTR_NATIVE_PCM_VOICE_FAILED,true);
    }
    uint32_t hash=UINT32_C(2166136261);
    for (size_t frame=0;frame<frames;++frame) {
        for (unsigned channel=0;channel<2;++channel) {
            int16_t sample=interleaved[frame*2+channel];
            pcm[channel][frame]=sample;
            uint16_t bits=(uint16_t)sample;
            hash=(hash^(uint8_t)bits)*UINT32_C(16777619);
            hash=(hash^(uint8_t)(bits>>8))*UINT32_C(16777619);
        }
    }
    /* The two independently aligned planes are immutable until both voices
       have been stopped/freed. Flush exactly the copied PCM, before AX sees it. */
    DCFlushRange(pcm[0],(uint32_t)(frames*sizeof(pcm[0][0])));
    DCFlushRange(pcm[1],(uint32_t)(frames*sizeof(pcm[1][0])));
    if (AXGetInputSamplesPerSec()!=48000 ||
        !configure(state->voices[0],0,(uint32_t)frames) ||
        !configure(state->voices[1],1,(uint32_t)frames)) {
        free_voices(state);
        return fail(state,CTR_NATIVE_PCM_CONFIG_FAILED,true);
    }
    owner=state;
    state->report.prepared=true;
    state->report.frames=(uint32_t)frames;
    state->report.pcm_fnv1a=hash;
    state->report.status=CTR_NATIVE_PCM_READY;
    state->report.result=CTR_NATIVE_PCM_OK;
    count(&state->report.prepares);
    return true;
}

bool ctr_native_pcm_play(ctr_native_pcm *state) {
    if (!available(state)) return false;
    for (unsigned channel=0;channel<2;++channel) {
        AXSetVoiceState(state->voices[channel],AX_VOICE_STATE_STOPPED);
        AXSetVoiceCurrentOffset(state->voices[channel],0);
        state->report.cursor[channel]=0;
        state->observed_running[channel]=false;
    }
    for (unsigned channel=0;channel<2;++channel) {
        AXSetVoiceState(state->voices[channel],AX_VOICE_STATE_PLAYING);
        state->report.running[channel]=true;
    }
    state->report.status=CTR_NATIVE_PCM_PLAYING;
    state->report.result=CTR_NATIVE_PCM_OK;
    count(&state->report.plays);
    return true;
}

static void poll(ctr_native_pcm *state) {
    for (unsigned channel=0;channel<2;++channel) {
        AXVoiceOffsets offsets;
        AXGetVoiceOffsets(state->voices[channel],&offsets);
        state->report.cursor[channel]=offsets.currentOffset<state->report.frames ?
            offsets.currentOffset:state->report.frames;
        state->report.running[channel]=AXIsVoiceRunning(state->voices[channel])!=0;
        if (state->report.running[channel]) state->observed_running[channel]=true;
    }
    if (state->report.status==CTR_NATIVE_PCM_PLAYING &&
        !state->report.running[0] && !state->report.running[1] &&
        state->observed_running[0] && state->observed_running[1]) {
        state->report.status=CTR_NATIVE_PCM_COMPLETE;
        /* A completed finite voice consumed its final inclusive end sample. */
        state->report.cursor[0]=state->report.cursor[1]=state->report.frames;
        count(&state->report.completions);
    }
}

bool ctr_native_pcm_pause(ctr_native_pcm *state) {
    if (!available(state)) return false;
    poll(state);
    if (state->report.status!=CTR_NATIVE_PCM_PLAYING)
        return fail(state,CTR_NATIVE_PCM_INVALID_STATE,false);
    for (unsigned channel=0;channel<2;++channel)
        AXSetVoiceState(state->voices[channel],AX_VOICE_STATE_STOPPED);
    for (unsigned channel=0;channel<2;++channel) {
        AXVoiceOffsets offsets;
        AXGetVoiceOffsets(state->voices[channel],&offsets);
        state->report.cursor[channel]=offsets.currentOffset<state->report.frames ?
            offsets.currentOffset:state->report.frames-1;
        state->report.running[channel]=false;
    }
    state->report.status=CTR_NATIVE_PCM_PAUSED;
    state->report.result=CTR_NATIVE_PCM_OK;
    count(&state->report.pauses);
    return true;
}

bool ctr_native_pcm_resume(ctr_native_pcm *state) {
    if (!available(state)) return false;
    if (state->report.status!=CTR_NATIVE_PCM_PAUSED)
        return fail(state,CTR_NATIVE_PCM_INVALID_STATE,false);
    for (unsigned channel=0;channel<2;++channel) {
        AXSetVoiceCurrentOffset(state->voices[channel],state->report.cursor[channel]);
        AXSetVoiceState(state->voices[channel],AX_VOICE_STATE_PLAYING);
        state->report.running[channel]=true;
    }
    state->report.status=CTR_NATIVE_PCM_PLAYING;
    state->report.result=CTR_NATIVE_PCM_OK;
    count(&state->report.resumes);
    return true;
}

bool ctr_native_pcm_stop(ctr_native_pcm *state) {
    if (!available(state)) return false;
    for (unsigned channel=0;channel<2;++channel) {
        AXSetVoiceState(state->voices[channel],AX_VOICE_STATE_STOPPED);
        AXSetVoiceCurrentOffset(state->voices[channel],0);
        state->report.cursor[channel]=0;
        state->report.running[channel]=false;
    }
    state->report.status=CTR_NATIVE_PCM_STOPPED;
    state->report.result=CTR_NATIVE_PCM_OK;
    count(&state->report.stops);
    return true;
}

bool ctr_native_pcm_snapshot(ctr_native_pcm *state, ctr_native_pcm_report *out) {
    if (!state || !out ||
        (out!=&state->report && overlap(out,sizeof(*out),state,sizeof(*state))))
        return false;
    if (owner==state && state->report.prepared) {
        if (!AXIsInit()) lost(state);
        else if (state->report.status==CTR_NATIVE_PCM_PLAYING) poll(state);
    }
    *out=state->report;
    return true;
}

void ctr_native_pcm_release(void *context) {
    ctr_native_pcm *state=context;
    if (!state || owner!=state) return;
    if (!AXIsInit()) lost(state);
    else free_voices(state);
    state->report.prepared=false;
    state->report.running[0]=state->report.running[1]=false;
    state->report.status=CTR_NATIVE_PCM_EMPTY;
    if (owner==state) owner=NULL;
    count(&state->report.releases);
}

const char *ctr_native_pcm_status_name(ctr_native_pcm_status status) {
    switch (status) {
        case CTR_NATIVE_PCM_EMPTY:return "EMPTY";
        case CTR_NATIVE_PCM_READY:return "READY";
        case CTR_NATIVE_PCM_PLAYING:return "PLAYING";
        case CTR_NATIVE_PCM_PAUSED:return "PAUSED";
        case CTR_NATIVE_PCM_STOPPED:return "STOPPED";
        case CTR_NATIVE_PCM_COMPLETE:return "COMPLETE";
        case CTR_NATIVE_PCM_FAILED:return "FAILED";
        default:return "INVALID";
    }
}

const char *ctr_native_pcm_result_name(ctr_native_pcm_result result) {
    switch (result) {
        case CTR_NATIVE_PCM_OK:return "OK";
        case CTR_NATIVE_PCM_ARGUMENT:return "ARGUMENT";
        case CTR_NATIVE_PCM_BOUNDS:return "BOUNDS";
        case CTR_NATIVE_PCM_BUSY:return "BUSY";
        case CTR_NATIVE_PCM_AX_UNAVAILABLE:return "AX_UNAVAILABLE";
        case CTR_NATIVE_PCM_VOICE_FAILED:return "VOICE_FAILED";
        case CTR_NATIVE_PCM_CONFIG_FAILED:return "CONFIG_FAILED";
        case CTR_NATIVE_PCM_INVALID_STATE:return "INVALID_STATE";
        default:return "INVALID";
    }
}
