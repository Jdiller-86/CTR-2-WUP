#include "native_audio_stream.h"
#include "ctr_dsp_stream.h"
#include <sndcore2/voice.h>
#include <sndcore2/core.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,acquires,frees,fail_acquire;
#define CHECK(x) do { ++checks;if(!(x)) { fprintf(stderr,"stream line %d: %s\n",__LINE__,#x);exit(1); } } while(0)
static bool initialized=true;
static AXVoice voices[2];
static void (*callback)(void);
static int16_t input[CTR_NATIVE_STREAM_FRAMES*2];
int AXIsInit(void) { return initialized; }
uint32_t AXGetInputSamplesPerSec(void) { return 48000; }
uint32_t AXGetInputSamplesPerFrame(void) { return 144; }
int AXRegisterAppFrameCallback(void (*f)(void)) { CHECK(!callback);callback=f;return 0; }
int AXDeregisterAppFrameCallback(void (*f)(void)) { CHECK(callback==f);callback=NULL;return 0; }
AXVoice *AXAcquireVoice(uint32_t priority,void (*f)(void *),void *context) {
    CHECK(initialized && priority==31 && !f && !context);++acquires;
    if(acquires==fail_acquire)return NULL;
    for(unsigned c=0;c<2;++c)if(!voices[c].live) { voices[c]=(AXVoice){.slot=c,.live=true};return &voices[c]; }
    CHECK(false);return NULL;
}
void AXFreeVoice(AXVoice *v) { CHECK(initialized && v->live && !v->running);v->live=false;++frees; }
void AXSetVoiceState(AXVoice *v,uint32_t state) { CHECK(initialized && v->live);v->running=state!=0; }
bool AXCheckVoiceOffsets(AXVoiceOffsets *o) { return o->dataType==AX_VOICE_FORMAT_LPCM16 && o->loopingEnabled==AX_VOICE_LOOP_ENABLED; }
void AXSetVoiceOffsets(AXVoice *v,AXVoiceOffsets *o) { CHECK(!v->running);v->offsets=*o; }
void AXSetVoiceSrcType(AXVoice *v,uint32_t type) { CHECK(v->live && type==AX_VOICE_SRC_TYPE_NONE); }
int AXSetVoiceSrcRatio(AXVoice *v,float ratio) { CHECK(v->live && ratio==1.0f);return 0; }
void AXSetVoiceVe(AXVoice *v,AXVoiceVeData *data) { CHECK(v->live && data->volume==0x8000); }
AXResult AXSetVoiceDeviceMix(AXVoice *v,uint32_t device,uint32_t id,AXVoiceDeviceMixData *mix) {
    CHECK(!id && mix[v->slot].bus[0].volume==0x8000 && (device==0 || device==1));return 0;
}
void AXGetVoiceOffsets(AXVoice *v,AXVoiceOffsets *o) { CHECK(v->live);*o=v->offsets; }
bool AXIsVoiceRunning(AXVoice *v) { return v->running; }
void AXSetVoiceEndOffset(AXVoice *v,uint32_t offset) { v->offsets.endOffset=offset; }
void AXSetVoiceLoopOffset(AXVoice *v,uint32_t offset) { v->offsets.loopOffset=offset; }
void DCFlushRange(const void *p,uint32_t bytes) {
    CHECK(!((uintptr_t)p&63));
    /* No producer flush may touch the playing or already scheduled next block. */
    for(unsigned c=0;c<2;++c)if(voices[c].running) {
        const int16_t *base=voices[c].offsets.data;
        uintptr_t first=(uintptr_t)p,last=first+bytes;
        uintptr_t current=(uintptr_t)(base+voices[c].offsets.currentOffset);
        uintptr_t next=(uintptr_t)(base+voices[c].offsets.loopOffset);
        CHECK((current<first || current>=last) && (next<first || next>=last));
    }
}
/* A small AX model consumes actual published PCM and follows inclusive ends
 * and loop offsets. It invokes the callback before each 144-sample render. */
static void tick(int expected,bool check_samples) {
    CHECK(callback!=NULL);callback();
    for(unsigned i=0;i<144;++i)for(unsigned c=0;c<2;++c)if(voices[c].running) {
        AXVoiceOffsets *o=&voices[c].offsets;
        int16_t sample=((const int16_t *)o->data)[o->currentOffset];
        if(check_samples)CHECK(sample==(c?-expected:expected));
        if(o->currentOffset==o->endOffset)o->currentOffset=o->loopOffset;else ++o->currentOffset;
    }
}
static void push(ctr_native_stream *s,int value,size_t frames) {
    for(unsigned i=0;i<CTR_NATIVE_STREAM_FRAMES;++i) { input[i*2]=(int16_t)value;input[i*2+1]=(int16_t)-value; }
    CHECK(ctr_native_stream_push(s,input,frames));memset(input,0,sizeof(input));
}
static void queued_playback(void) {
    ctr_native_stream s={0};ctr_native_stream_report report;
    CHECK(ctr_native_stream_open(&s));CHECK(!ctr_native_stream_start(&s));
    for(unsigned i=0;i<8;++i)push(&s,123,960);
    CHECK(!ctr_native_stream_push(&s,input,960));CHECK(ctr_native_stream_start(&s));
    for(unsigned i=0;i<20;++i)tick(123,true);
    CHECK(ctr_native_stream_pause(&s,true));tick(0,false);
    uint32_t offset=voices[0].offsets.currentOffset;
    for(unsigned i=0;i<5;++i)tick(0,false);
    CHECK(voices[0].offsets.currentOffset==offset);
    CHECK(ctr_native_stream_snapshot(&s,&report) && report.paused);
    CHECK(ctr_native_stream_pause(&s,false));
    /* Keep feeding well beyond the finite adapter's 96,000-frame limit. */
    for(unsigned i=0;i<1000;++i) {
        while(ctr_native_stream_space(&s))push(&s,123,960);
        tick(123,true);
    }
    CHECK(ctr_native_stream_finish(&s));
    for(unsigned i=0;i<70 && !atomic_load(&s.complete);++i)tick(0,false);
    CHECK(ctr_native_stream_snapshot(&s,&report) && report.complete && report.played_frames==report.submitted_frames);
    CHECK(report.played_frames>96000 && !report.underruns);
    ctr_native_stream_release(&s);CHECK(!callback && !voices[0].live && !voices[1].live);
}
static void underrun_recovery_and_failures(void) {
    ctr_native_stream s={0};ctr_native_stream_report report;
    CHECK(ctr_native_stream_open(&s));push(&s,1,960);push(&s,1,960);CHECK(ctr_native_stream_start(&s));
    for(unsigned i=0;i<28;++i)tick(0,false);
    CHECK(ctr_native_stream_snapshot(&s,&report) && report.underruns==1);
    tick(0,true); // A starved source uses permanent zero PCM, never stale audio.
    push(&s,2,960);push(&s,2,17);CHECK(!ctr_native_stream_push(&s,input,960));
    CHECK(ctr_native_stream_finish(&s));
    for(unsigned i=0;i<40 && !atomic_load(&s.complete);++i)tick(0,false);
    CHECK(ctr_native_stream_snapshot(&s,&report) && report.complete && report.played_frames==2897);
    ctr_native_stream_release(&s);
    fail_acquire=acquires+2;unsigned before=frees;
    CHECK(!ctr_native_stream_open(&s) && frees==before+1 && !callback);fail_acquire=0;
    CHECK(ctr_native_stream_open(&s));push(&s,1,960);push(&s,1,960);CHECK(ctr_native_stream_start(&s));
    tick(1,true);voices[1].offsets.currentOffset++;
    tick(0,false);CHECK(!ctr_native_stream_snapshot(&s,&report) && report.result==CTR_NATIVE_STREAM_OFFSETS);
    CHECK(!voices[0].running && !voices[1].running);ctr_native_stream_release(&s);
    CHECK(ctr_native_stream_open(&s));initialized=false;callback=NULL; // AX itself discarded its voices/callbacks.
    before=frees;ctr_native_stream_release(&s);CHECK(frees==before);
    memset(voices,0,sizeof(voices));initialized=true;
}
static void resampler_splits(void) {
    ctr_dsp_stream s;ctr_dsp_stream_init(&s);
    int16_t source[2000],out[4000];size_t produced=0;
    for(unsigned i=0;i<1000;++i)source[i*2]=source[i*2+1]=(int16_t)(i-500);
    for(unsigned at=0;at<1000;) {
        unsigned n=1000-at<17?1000-at:17;
        CHECK(ctr_dsp_stream_push(&s,source+at*2,n));at+=n;
        size_t written;CHECK(ctr_dsp_stream_pull(&s,out+produced*2,31,&written));produced+=written;
    }
    CHECK(ctr_dsp_stream_finish(&s));
    while(!ctr_dsp_stream_done(&s)) {
        size_t written;CHECK(ctr_dsp_stream_pull(&s,out+produced*2,31,&written));CHECK(written>0);produced+=written;
    }
    CHECK(produced==1467);
    for(size_t i=0;i<produced;++i) {
        uint64_t phase=i*16756991;size_t at=(size_t)(phase/24576000);
        /* A linear ramp has a closed-form reference until the held EOF tail. */
        int expected=at>=999?499:(int)((phase+12288000)/24576000)-500;
        CHECK(out[i*2]==expected && out[i*2+1]==expected);
    }
    size_t untouched=7;ctr_dsp_stream before=s;
    CHECK(!ctr_dsp_stream_push(&s,source,1));
    CHECK(!ctr_dsp_stream_pull(&s,(int16_t *)&s,1,&untouched) && untouched==7 && !memcmp(&s,&before,sizeof(s)));
}
int main(void) {
    resampler_splits();queued_playback();underrun_recovery_and_failures();
    printf("continuous audio: %u checks passed\n",checks);return 0;
}
