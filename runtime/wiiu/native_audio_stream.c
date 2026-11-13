/* Original queued AX adapter. Interface/loop handoff facts were checked against
 * pinned WUT and SDL's Wii U audio driver; no SDL implementation is imported. */
#include "native_audio_stream.h"
#include <coreinit/cache.h>
#include <sndcore2/core.h>
#include <sndcore2/voice.h>
#include <string.h>

_Static_assert(ATOMIC_INT_LOCK_FREE==2 && ATOMIC_POINTER_LOCK_FREE==2,"AX callback requires lock-free publication");
#define SILENCE CTR_NATIVE_STREAM_SLOTS
#define PLANE ((CTR_NATIVE_STREAM_SLOTS+1)*CTR_NATIVE_STREAM_FRAMES)
static int16_t pcm[2][PLANE] __attribute__((aligned(64)));
static _Atomic(ctr_native_stream *) owner;
static atomic_uint in_callback;
enum { IDLE,PLAY,PAUSE };
static bool overlap(const void *a,size_t n,const void *b,size_t m) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return n && m && (x<=y?y-x<n:x-y<m);
}
static bool fail(ctr_native_stream *s,ctr_native_stream_result result) {
    atomic_store(&s->result,result);return false;
}
static void increment(atomic_uint *p,unsigned n) {
    unsigned value=atomic_load_explicit(p,memory_order_relaxed);
    atomic_store_explicit(p,UINT32_MAX-value<n?UINT32_MAX:value+n,memory_order_relaxed);
}
static void stop(ctr_native_stream *s) {
    for(unsigned c=0;c<2;++c)AXSetVoiceState(s->voices[c],AX_VOICE_STATE_STOPPED);
}
static bool available(ctr_native_stream *s) {
    return s && atomic_load(&owner)==s && s->voices[0] && s->voices[1] &&
        AXIsInit() && atomic_load(&s->result)==CTR_NATIVE_STREAM_OK;
}
static void frame(void) {
    atomic_fetch_add(&in_callback,1);
    ctr_native_stream *s=atomic_load(&owner);
    if(!s)goto done;
    increment(&s->callbacks,1);
    if(atomic_load(&s->result)!=CTR_NATIVE_STREAM_OK) { stop(s);goto done; }
    unsigned request=atomic_load_explicit(&s->requested,memory_order_acquire);
    if(request==IDLE || atomic_load(&s->complete))goto done;
    if(request==PAUSE) {
        if(!atomic_load(&s->paused)) { stop(s);atomic_store(&s->paused,true); }
        goto done;
    }
    if(!atomic_load(&s->started) || atomic_load(&s->paused)) {
        for(unsigned c=0;c<2;++c)AXSetVoiceState(s->voices[c],AX_VOICE_STATE_PLAYING);
        atomic_store(&s->started,true);atomic_store(&s->paused,false);
    }
    AXVoiceOffsets offsets[2];
    for(unsigned c=0;c<2;++c)AXGetVoiceOffsets(s->voices[c],&offsets[c]);
    if(offsets[0].currentOffset>=PLANE || offsets[0].currentOffset!=offsets[1].currentOffset ||
       offsets[0].data!=pcm[0] || offsets[1].data!=pcm[1] ||
       !AXIsVoiceRunning(s->voices[0]) || !AXIsVoiceRunning(s->voices[1])) {
        fail(s,CTR_NATIVE_STREAM_OFFSETS);stop(s);goto done;
    }
    unsigned active=offsets[0].currentOffset/CTR_NATIVE_STREAM_FRAMES;
    unsigned read=atomic_load_explicit(&s->read,memory_order_relaxed);
    bool finished=atomic_load_explicit(&s->finished,memory_order_acquire);
    unsigned write=atomic_load_explicit(&s->write,memory_order_acquire);
    if(active!=s->active) {
        if(s->active!=SILENCE) {
            if(s->active!=read%CTR_NATIVE_STREAM_SLOTS || read==write) {
                fail(s,CTR_NATIVE_STREAM_STATE);stop(s);goto done;
            }
            increment(&s->played,s->lengths[read%CTR_NATIVE_STREAM_SLOTS]);
            ++read;atomic_store_explicit(&s->read,read,memory_order_release);
        }
        if(active!=SILENCE && (read==write || active!=read%CTR_NATIVE_STREAM_SLOTS)) {
            fail(s,CTR_NATIVE_STREAM_STATE);stop(s);goto done;
        }
        if(active==SILENCE && !finished)increment(&s->underruns,1);
        s->active=active;
    }
    unsigned next=active==SILENCE?read:read+1;
    if(active==SILENCE && read==write && finished) {
        stop(s);atomic_store(&s->complete,true);goto done;
    }
    unsigned slot=next!=write?next%CTR_NATIVE_STREAM_SLOTS:SILENCE;
    for(unsigned c=0;c<2;++c) {
        AXSetVoiceEndOffset(s->voices[c],(active+1)*CTR_NATIVE_STREAM_FRAMES-1);
        AXSetVoiceLoopOffset(s->voices[c],slot*CTR_NATIVE_STREAM_FRAMES);
    }
done:
    atomic_fetch_sub(&in_callback,1);
}
bool ctr_native_stream_open(ctr_native_stream *s) {
    if(!s || overlap(s,sizeof(*s),pcm,sizeof(pcm)) || atomic_load(&owner))return false;
    memset(s,0,sizeof(*s));
    atomic_init(&s->read,0);atomic_init(&s->write,0);atomic_init(&s->requested,IDLE);
    atomic_init(&s->result,CTR_NATIVE_STREAM_OK);atomic_init(&s->played,0);
    atomic_init(&s->underruns,0);atomic_init(&s->callbacks,0);
    atomic_init(&s->finished,false);atomic_init(&s->started,false);
    atomic_init(&s->paused,false);atomic_init(&s->complete,false);
    if(!AXIsInit() || AXGetInputSamplesPerSec()!=48000 || AXGetInputSamplesPerFrame()>CTR_NATIVE_STREAM_FRAMES)
        return fail(s,CTR_NATIVE_STREAM_AX);
    memset(pcm,0,sizeof(pcm));DCFlushRange(pcm,sizeof(pcm));
    atomic_store_explicit(&owner,s,memory_order_release);
    for(unsigned c=0;c<2;++c) {
        s->voices[c]=AXAcquireVoice(31,NULL,NULL);
        if(!s->voices[c])goto failed;
        AXVoice *v=s->voices[c];AXSetVoiceState(v,AX_VOICE_STATE_STOPPED);
        AXVoiceOffsets offsets={0};offsets.dataType=AX_VOICE_FORMAT_LPCM16;
        offsets.loopingEnabled=AX_VOICE_LOOP_ENABLED;offsets.data=pcm[c];
        offsets.endOffset=CTR_NATIVE_STREAM_FRAMES-1;offsets.loopOffset=CTR_NATIVE_STREAM_FRAMES;
        if(!AXCheckVoiceOffsets(&offsets))goto failed;
        AXSetVoiceOffsets(v,&offsets);AXSetVoiceSrcType(v,AX_VOICE_SRC_TYPE_NONE);
        if(AXSetVoiceSrcRatio(v,1.0f)!=AX_VOICE_RATIO_RESULT_SUCCESS)goto failed;
        AXVoiceVeData volume={0x8000,0};AXSetVoiceVe(v,&volume);
        AXVoiceDeviceMixData tv[6]={0},drc[4]={0};
        tv[c].bus[0].volume=drc[c].bus[0].volume=0x8000;
        if(AXSetVoiceDeviceMix(v,AX_DEVICE_TYPE_TV,0,tv)!=AX_RESULT_SUCCESS ||
           AXSetVoiceDeviceMix(v,AX_DEVICE_TYPE_DRC,0,drc)!=AX_RESULT_SUCCESS)goto failed;
    }
    if(AXRegisterAppFrameCallback(frame)!=AX_RESULT_SUCCESS)goto failed;
    s->registered=true;return true;
failed:
    fail(s,CTR_NATIVE_STREAM_CONFIG);ctr_native_stream_release(s);return false;
}
size_t ctr_native_stream_space(const ctr_native_stream *s) {
    if(!s || atomic_load(&owner)!=s || atomic_load(&s->finished) ||
       atomic_load(&s->result)!=CTR_NATIVE_STREAM_OK)return 0;
    unsigned count=atomic_load_explicit(&s->write,memory_order_relaxed)-atomic_load_explicit(&s->read,memory_order_acquire);
    return count<=CTR_NATIVE_STREAM_SLOTS?CTR_NATIVE_STREAM_SLOTS-count:0;
}
bool ctr_native_stream_push(ctr_native_stream *s,const int16_t *input,size_t frames) {
    if(!available(s) || s->partial || !input || !frames || frames>CTR_NATIVE_STREAM_FRAMES ||
       (uintptr_t)input%_Alignof(int16_t) || (uintptr_t)input>UINTPTR_MAX-frames*4 ||
       overlap(input,frames*4,s,sizeof(*s)) || overlap(input,frames*4,pcm,sizeof(pcm)) || !ctr_native_stream_space(s))return false;
    unsigned write=atomic_load_explicit(&s->write,memory_order_relaxed),slot=write%CTR_NATIVE_STREAM_SLOTS;
    for(unsigned c=0;c<2;++c) {
        int16_t *dst=pcm[c]+slot*CTR_NATIVE_STREAM_FRAMES;
        for(size_t i=0;i<frames;++i)dst[i]=input[i*2+c];
        memset(dst+frames,0,(CTR_NATIVE_STREAM_FRAMES-frames)*sizeof(*dst));
        DCFlushRange(dst,CTR_NATIVE_STREAM_FRAMES*sizeof(*dst));
    }
    s->lengths[slot]=(uint32_t)frames;
    s->partial=frames<CTR_NATIVE_STREAM_FRAMES;
    s->submitted=UINT32_MAX-s->submitted<frames?UINT32_MAX:s->submitted+(uint32_t)frames;
    atomic_store_explicit(&s->write,write+1,memory_order_release);return true;
}
bool ctr_native_stream_start(ctr_native_stream *s) {
    if(!available(s) || atomic_load(&s->requested)!=IDLE ||
       atomic_load(&s->write)-atomic_load(&s->read)<2)return false;
    atomic_store_explicit(&s->requested,PLAY,memory_order_release);return true;
}
bool ctr_native_stream_pause(ctr_native_stream *s,bool pause) {
    if(!available(s) || atomic_load(&s->requested)==IDLE)return false;
    if(atomic_load(&s->complete))return true;
    atomic_store_explicit(&s->requested,pause?PAUSE:PLAY,memory_order_release);return true;
}
bool ctr_native_stream_finish(ctr_native_stream *s) {
    if(!available(s))return false;
    atomic_store_explicit(&s->finished,true,memory_order_release);return true;
}
bool ctr_native_stream_snapshot(ctr_native_stream *s,ctr_native_stream_report *out) {
    if(!s || !out || overlap(out,sizeof(*out),s,sizeof(*s)))return false;
    if(atomic_load(&owner)==s && !AXIsInit())fail(s,CTR_NATIVE_STREAM_AX);
    *out=(ctr_native_stream_report){
        .queued_blocks=atomic_load(&s->write)-atomic_load(&s->read),.submitted_frames=s->submitted,
        .played_frames=atomic_load(&s->played),.underruns=atomic_load(&s->underruns),
        .callbacks=atomic_load(&s->callbacks),.started=atomic_load(&s->started),
        .paused=atomic_load(&s->paused),.complete=atomic_load(&s->complete),.result=atomic_load(&s->result)};
    return out->result==CTR_NATIVE_STREAM_OK;
}
void ctr_native_stream_release(void *context) {
    ctr_native_stream *s=context;if(!s || atomic_load(&owner)!=s)return;
    atomic_store(&owner,NULL);
    bool initialized=AXIsInit()!=0;
    if(initialized && s->registered && AXDeregisterAppFrameCallback(frame)!=AX_RESULT_SUCCESS)
        fail(s,CTR_NATIVE_STREAM_CALLBACK);
    while(atomic_load(&in_callback)) { }
    s->registered=false;
    for(unsigned c=0;c<2;++c) {
        if(initialized && s->voices[c]) {
            AXSetVoiceState(s->voices[c],AX_VOICE_STATE_STOPPED);AXFreeVoice(s->voices[c]);
        }
        s->voices[c]=NULL;
    }
}
