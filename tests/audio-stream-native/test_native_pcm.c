#include "audio_stream_diagnostic.h"
#include <coreinit/cache.h>
#include <sndcore2/core.h>
#include <sndcore2/voice.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks;if (!(x)) { fprintf(stderr,"FAIL %u line %d: %s\n",checks,__LINE__,#x);exit(1); } } while (0)

static bool initialized,automatic_start,offsets_valid;
static unsigned acquire_calls,free_calls,fail_acquire,flush_calls,offset_calls;
static unsigned ratio_fail,mix_fail_device,rate;
static AXVoice voices[2];
static const void *flushed[2];
static uint32_t flushed_bytes[2];
static const int16_t input[]={0x1234,-2,32767,-32768,1,-1};

int AXIsInit(void) { return initialized; }
uint32_t AXGetInputSamplesPerSec(void) { CHECK(initialized);return rate; }
static void valid(AXVoice *voice) {
    CHECK(initialized && (voice==&voices[0] || voice==&voices[1]) && voice->live);
}
AXVoice *AXAcquireVoice(uint32_t priority,void (*callback)(void *),void *context) {
    CHECK(initialized && priority==31 && !callback && !context);
    ++acquire_calls;
    if (acquire_calls==fail_acquire) return NULL;
    for (unsigned i=0;i<2;++i) if (!voices[i].live) {
        voices[i]=(AXVoice){.slot=i,.live=true};return &voices[i];
    }
    CHECK(false);return NULL;
}
void AXFreeVoice(AXVoice *voice) {
    valid(voice);CHECK(!voice->running);voice->live=false;++free_calls;
}
void AXSetVoiceState(AXVoice *voice,uint32_t state) {
    valid(voice);CHECK(state==AX_VOICE_STATE_STOPPED || state==AX_VOICE_STATE_PLAYING);
    voice->running=state==AX_VOICE_STATE_PLAYING && automatic_start;
}
void DCFlushRange(const void *data,uint32_t bytes) {
    CHECK(initialized && !((uintptr_t)data&63) && bytes && bytes<=CTR_NATIVE_PCM_MAX_FRAMES*2);
    CHECK(!voices[0].running && !voices[1].running);
    flushed[flush_calls%2]=data;flushed_bytes[flush_calls%2]=bytes;++flush_calls;
}
bool AXCheckVoiceOffsets(AXVoiceOffsets *offsets) {
    CHECK(offsets && offsets->dataType==AX_VOICE_FORMAT_LPCM16 &&
        offsets->loopingEnabled==AX_VOICE_LOOP_DISABLED && !offsets->currentOffset &&
        !offsets->loopOffset && offsets->endOffset<CTR_NATIVE_PCM_MAX_FRAMES);
    return offsets_valid;
}
void AXSetVoiceOffsets(AXVoice *voice,AXVoiceOffsets *offsets) {
    valid(voice);CHECK(!voice->running && flush_calls>=2);
    CHECK(offsets->data==flushed[voice->slot] && flushed_bytes[voice->slot]==(offsets->endOffset+1)*2);
    voice->offsets=*offsets;++offset_calls;
}
void AXSetVoiceSrcType(AXVoice *voice,uint32_t type) { valid(voice);CHECK(type==AX_VOICE_SRC_TYPE_NONE); }
int AXSetVoiceSrcRatio(AXVoice *voice,float ratio) {
    valid(voice);CHECK(ratio==1.0f);return ratio_fail ? -1:AX_VOICE_RATIO_RESULT_SUCCESS;
}
void AXSetVoiceVe(AXVoice *voice,AXVoiceVeData *volume) {
    valid(voice);CHECK(volume && volume->volume==0x8000 && !volume->delta);
}
AXResult AXSetVoiceDeviceMix(AXVoice *voice,uint32_t device,uint32_t id,AXVoiceDeviceMixData *mix) {
    valid(voice);CHECK((device==AX_DEVICE_TYPE_TV || device==AX_DEVICE_TYPE_DRC) && !id && mix);
    unsigned channels=device==AX_DEVICE_TYPE_TV ? 6:4;
    for (unsigned i=0;i<channels;++i)
        for (unsigned bus=0;bus<4;++bus)
            CHECK(mix[i].bus[bus].volume==((i==voice->slot && bus==0)?0x8000:0) && !mix[i].bus[bus].delta);
    return mix_fail_device==device+1 ? -1:AX_RESULT_SUCCESS;
}
void AXSetVoiceCurrentOffset(AXVoice *voice,uint32_t offset) {
    valid(voice);CHECK(!voice->running && offset<=voice->offsets.endOffset);
    voice->offsets.currentOffset=offset;
}
void AXGetVoiceOffsets(AXVoice *voice,AXVoiceOffsets *out) { valid(voice);CHECK(out);*out=voice->offsets; }
bool AXIsVoiceRunning(AXVoice *voice) { valid(voice);return voice->running; }

static void reset(void) {
    CHECK(!voices[0].live && !voices[1].live);
    initialized=automatic_start=offsets_valid=true;
    acquire_calls=free_calls=fail_acquire=flush_calls=offset_calls=ratio_fail=mix_fail_device=0;
    rate=48000;memset(voices,0,sizeof(voices));memset(flushed,0,sizeof(flushed));
    memset(flushed_bytes,0,sizeof(flushed_bytes));
}

static void clip_copy_routing_and_lifetime(void) {
    reset();ctr_native_pcm state={0};ctr_native_pcm_report report;
    int16_t mutable_input[6];memcpy(mutable_input,input,sizeof(input));
    CHECK(ctr_native_pcm_prepare(&state,mutable_input,3));
    CHECK(state.report.status==CTR_NATIVE_PCM_READY && state.report.prepared && state.report.frames==3);
    CHECK(state.report.pcm_fnv1a==UINT32_C(0x769c4b95) && state.report.prepares==1);
    CHECK(flush_calls==2 && offset_calls==2 && acquire_calls==2 && !free_calls);
    const int16_t *left=voices[0].offsets.data,*right=voices[1].offsets.data;
    CHECK(left[0]==0x1234 && left[1]==32767 && left[2]==1);
    CHECK(right[0]==-2 && right[1]==-32768 && right[2]==-1);
    memset(mutable_input,0,sizeof(mutable_input));
    CHECK(left[0]==0x1234 && right[2]==-1);
    CHECK(ctr_native_pcm_play(&state));
    CHECK(ctr_native_pcm_snapshot(&state,&report));
    CHECK(report.status==CTR_NATIVE_PCM_PLAYING && report.running[0] && report.running[1] && report.plays==1);
    voices[0].offsets.currentOffset=1;voices[1].offsets.currentOffset=2;
    CHECK(ctr_native_pcm_pause(&state));
    CHECK(ctr_native_pcm_snapshot(&state,&report));
    CHECK(report.status==CTR_NATIVE_PCM_PAUSED && report.cursor[0]==1 && report.cursor[1]==2 && !report.running[0] && !report.running[1]);
    CHECK(!ctr_native_pcm_pause(&state) && state.report.result==CTR_NATIVE_PCM_INVALID_STATE);
    CHECK(ctr_native_pcm_resume(&state));
    CHECK(voices[0].offsets.currentOffset==1 && voices[1].offsets.currentOffset==2);
    CHECK(ctr_native_pcm_snapshot(&state,&report));
    CHECK(report.pauses==1 && report.resumes==1 && report.result==CTR_NATIVE_PCM_OK);
    voices[0].running=false;
    CHECK(ctr_native_pcm_snapshot(&state,&report) && report.status==CTR_NATIVE_PCM_PLAYING);
    voices[1].running=false;
    CHECK(ctr_native_pcm_snapshot(&state,&report));
    CHECK(report.status==CTR_NATIVE_PCM_COMPLETE && report.completions==1 && report.cursor[0]==3 && report.cursor[1]==3);
    CHECK(ctr_native_pcm_snapshot(&state,&report) && report.completions==1);
    CHECK(!ctr_native_pcm_resume(&state));
    CHECK(ctr_native_pcm_play(&state));
    CHECK(voices[0].offsets.currentOffset==0 && voices[1].offsets.currentOffset==0);
    CHECK(ctr_native_pcm_stop(&state));
    CHECK(state.report.status==CTR_NATIVE_PCM_STOPPED && state.report.stops==1 && state.report.prepared);
    ctr_native_pcm_release(&state);
    CHECK(!state.report.prepared && state.report.status==CTR_NATIVE_PCM_EMPTY && free_calls==2);
    CHECK(state.report.frames==3 && state.report.pcm_fnv1a==UINT32_C(0x769c4b95) && state.report.releases==1);
    ctr_native_pcm_release(&state);CHECK(free_calls==2 && state.report.releases==1);
}

static void invalid_requests_and_busy_owner(void) {
    reset();ctr_native_pcm state={0},other={0};ctr_native_pcm_report report;
    CHECK(!ctr_native_pcm_prepare(NULL,input,3));
    CHECK(!ctr_native_pcm_prepare(&state,NULL,3));
    CHECK(!ctr_native_pcm_prepare(&state,input,0));
    CHECK(!ctr_native_pcm_prepare(&state,input,CTR_NATIVE_PCM_MAX_FRAMES+1u));
    CHECK(state.report.result==CTR_NATIVE_PCM_BOUNDS && !acquire_calls);
    CHECK(!ctr_native_pcm_prepare(&state,(const int16_t *)(uintptr_t)(UINTPTR_MAX-1),3));
    CHECK(!ctr_native_pcm_prepare(&state,(const int16_t *)((const unsigned char *)input+1),2));
    CHECK(!ctr_native_pcm_prepare(&state,(const int16_t *)&state,2));
    CHECK(!ctr_native_pcm_play(&state) && !ctr_native_pcm_pause(&state) && !ctr_native_pcm_resume(&state) && !ctr_native_pcm_stop(&state));
    CHECK(!ctr_native_pcm_play(NULL) && !ctr_native_pcm_pause(NULL) && !ctr_native_pcm_resume(NULL) && !ctr_native_pcm_stop(NULL));
    CHECK(!ctr_native_pcm_snapshot(NULL,&report) && !ctr_native_pcm_snapshot(&state,NULL));
    CHECK(!ctr_native_pcm_snapshot(&state,(ctr_native_pcm_report *)&state));
    CHECK(ctr_native_pcm_prepare(&state,input,3));
    CHECK(!ctr_native_pcm_prepare(&other,input,3) && other.report.result==CTR_NATIVE_PCM_BUSY && acquire_calls==2);
    ctr_native_pcm_release(&other);CHECK(!free_calls && state.report.prepared);
    CHECK(!ctr_native_pcm_prepare(&state,voices[0].offsets.data,2));
    CHECK(state.report.prepared && !free_calls);
    CHECK(ctr_native_pcm_snapshot(&state,&state.report));
    ctr_native_pcm_release(&state);ctr_native_pcm_release(NULL);
}

static void pending_start_and_replacement(void) {
    reset();ctr_native_pcm state={0};ctr_native_pcm_report report;
    automatic_start=false;
    CHECK(ctr_native_pcm_prepare(&state,input,3) && ctr_native_pcm_play(&state));
    CHECK(ctr_native_pcm_snapshot(&state,&report));
    CHECK(report.status==CTR_NATIVE_PCM_PLAYING && !report.completions && !report.running[0]);
    voices[0].running=voices[1].running=true;
    CHECK(ctr_native_pcm_snapshot(&state,&report) && report.running[0]);
    voices[0].running=voices[1].running=false;
    CHECK(ctr_native_pcm_snapshot(&state,&report) && report.status==CTR_NATIVE_PCM_COMPLETE);
    automatic_start=true;
    CHECK(ctr_native_pcm_play(&state));
    CHECK(ctr_native_pcm_prepare(&state,input,2));
    CHECK(free_calls==2 && acquire_calls==4 && state.report.prepares==2 && state.report.releases==1);
    CHECK(state.report.status==CTR_NATIVE_PCM_READY && state.report.frames==2);
    ctr_native_pcm_release(&state);CHECK(free_calls==4);
}

static void rollback_failures(void) {
    for (unsigned failure=1;failure<=7;++failure) {
        reset();ctr_native_pcm state={0};
        if (failure<=2) fail_acquire=failure;
        if (failure==3) offsets_valid=false;
        if (failure==4) ratio_fail=1;
        if (failure==5) mix_fail_device=1;
        if (failure==6) mix_fail_device=2;
        if (failure==7) rate=32000;
        CHECK(!ctr_native_pcm_prepare(&state,input,3));
        CHECK(!state.report.prepared && state.report.status==CTR_NATIVE_PCM_FAILED && state.report.failures==1);
        CHECK(!voices[0].live && !voices[1].live && !state.voices[0] && !state.voices[1]);
        CHECK(free_calls==(failure==1?0:failure==2?1:2));
        CHECK(state.report.result==(failure<=2?CTR_NATIVE_PCM_VOICE_FAILED:CTR_NATIVE_PCM_CONFIG_FAILED));
        ctr_native_pcm_release(&state);
        fail_acquire=ratio_fail=mix_fail_device=0;offsets_valid=true;rate=48000;
        CHECK(ctr_native_pcm_prepare(&state,input,3));
        ctr_native_pcm_release(&state);
    }
}

static void lost_context_and_saturation(void) {
    reset();ctr_native_pcm state={0};ctr_native_pcm_report report;
    initialized=false;
    CHECK(!ctr_native_pcm_prepare(&state,input,3) && !acquire_calls && state.report.result==CTR_NATIVE_PCM_AX_UNAVAILABLE);
    initialized=true;
    CHECK(ctr_native_pcm_prepare(&state,input,3) && ctr_native_pcm_play(&state));
    initialized=false;
    /* Simulate AX freeing its own resources. Any AX call on these pointers is
       rejected by valid(), including an attempted double free at release. */
    voices[0].live=voices[1].live=false;
    CHECK(ctr_native_pcm_snapshot(&state,&report));
    CHECK(report.status==CTR_NATIVE_PCM_FAILED && report.ownership_losses==1 && !report.prepared && !free_calls);
    CHECK(!state.voices[0] && !state.voices[1]);
    ctr_native_pcm_release(&state);CHECK(!free_calls);
    initialized=true;
    CHECK(ctr_native_pcm_prepare(&state,input,3));
    initialized=false;voices[0].live=voices[1].live=false;
    ctr_native_pcm_release(&state);
    CHECK(state.report.status==CTR_NATIVE_PCM_EMPTY && state.report.ownership_losses==2 && state.report.releases==1 && !free_calls);
    initialized=true;
    state.report.prepares=state.report.plays=state.report.stops=state.report.releases=UINT32_MAX;
    CHECK(ctr_native_pcm_prepare(&state,input,3) && ctr_native_pcm_play(&state) && ctr_native_pcm_stop(&state));
    ctr_native_pcm_release(&state);
    CHECK(state.report.prepares==UINT32_MAX && state.report.plays==UINT32_MAX && state.report.stops==UINT32_MAX && state.report.releases==UINT32_MAX);
}

static void maximum_clip_and_names(void) {
    reset();ctr_native_pcm state={0};
    int16_t *samples=malloc(CTR_NATIVE_PCM_MAX_FRAMES*2*sizeof(*samples));CHECK(samples);
    for (size_t i=0;i<CTR_NATIVE_PCM_MAX_FRAMES;++i) { samples[i*2]=123;samples[i*2+1]=-456; }
    CHECK(ctr_native_pcm_prepare(&state,samples,CTR_NATIVE_PCM_MAX_FRAMES));
    CHECK(flushed_bytes[0]==192000 && flushed_bytes[1]==192000);
    CHECK(((const int16_t *)voices[0].offsets.data)[CTR_NATIVE_PCM_MAX_FRAMES-1]==123);
    CHECK(((const int16_t *)voices[1].offsets.data)[CTR_NATIVE_PCM_MAX_FRAMES-1]==-456);
    free(samples);CHECK(ctr_native_pcm_play(&state));ctr_native_pcm_release(&state);
    static const char *const statuses[]={"EMPTY","READY","PLAYING","PAUSED","STOPPED","COMPLETE","FAILED"};
    static const char *const results[]={"OK","ARGUMENT","BOUNDS","BUSY","AX_UNAVAILABLE","VOICE_FAILED","CONFIG_FAILED","INVALID_STATE"};
    for (unsigned i=0;i<sizeof(statuses)/sizeof(*statuses);++i) CHECK(!strcmp(statuses[i],ctr_native_pcm_status_name((ctr_native_pcm_status)i)));
    for (unsigned i=0;i<sizeof(results)/sizeof(*results);++i) CHECK(!strcmp(results[i],ctr_native_pcm_result_name((ctr_native_pcm_result)i)));
    CHECK(!strcmp("INVALID",ctr_native_pcm_status_name((ctr_native_pcm_status)999)));
    CHECK(!strcmp("INVALID",ctr_native_pcm_result_name((ctr_native_pcm_result)999)));
}

int main(void) {
    clip_copy_routing_and_lifetime();invalid_requests_and_busy_owner();
    pending_start_and_replacement();rollback_failures();lost_context_and_saturation();
    maximum_clip_and_names();
    printf("finite native PCM adapter PASS checks=%u (mock AX, not console audio)\n",checks);
    return 0;
}
