#include "ctr_dsp_stream.h"
#include <string.h>
#define GUARD UINT32_C(0x44535053)
#define DENOMINATOR UINT32_C(24576000)
#define STEP UINT32_C(16756991)
static bool valid(const ctr_dsp_stream *s) {
    return s && s->guard==GUARD && s->first<CTR_DSP_STREAM_CAPACITY &&
        s->count<=CTR_DSP_STREAM_CAPACITY && s->phase<DENOMINATOR;
}
static bool overlap(const void *a,size_t n,const void *b,size_t m) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return n && m && (x<=y?y-x<n:x-y<m);
}
static void add(uint64_t *v,size_t n) { *v=UINT64_MAX-*v<n?UINT64_MAX:*v+n; }
void ctr_dsp_stream_init(ctr_dsp_stream *s) { if(s) { memset(s,0,sizeof(*s));s->guard=GUARD; } }
size_t ctr_dsp_stream_space(const ctr_dsp_stream *s) {
    return valid(s) && !s->finished?CTR_DSP_STREAM_CAPACITY-s->count:0;
}
bool ctr_dsp_stream_push(ctr_dsp_stream *s,const int16_t *pcm,size_t frames) {
    if(!valid(s) || s->finished || frames>CTR_DSP_STREAM_CAPACITY-s->count ||
       (frames && (!pcm || (uintptr_t)pcm%_Alignof(int16_t))) ||
       frames>(UINTPTR_MAX-(uintptr_t)pcm)/4 || overlap(s,sizeof(*s),pcm,frames*4))return false;
    for(size_t i=0;i<frames;++i) {
        unsigned at=(s->first+s->count+(unsigned)i)%CTR_DSP_STREAM_CAPACITY;
        s->pcm[at][0]=pcm[i*2];s->pcm[at][1]=pcm[i*2+1];
    }
    s->count+=(uint32_t)frames;add(&s->input_frames,frames);return true;
}
bool ctr_dsp_stream_finish(ctr_dsp_stream *s) { if(!valid(s))return false;s->finished=true;return true; }
bool ctr_dsp_stream_done(const ctr_dsp_stream *s) { return valid(s) && s->finished && !s->count; }
bool ctr_dsp_stream_pull(ctr_dsp_stream *s,int16_t *pcm,size_t capacity,size_t *written) {
    if(!valid(s) || !written || (uintptr_t)written%_Alignof(size_t) ||
       capacity>CTR_DSP_STREAM_PULL_MAX || (capacity && (!pcm || (uintptr_t)pcm%_Alignof(int16_t))) ||
       capacity>(UINTPTR_MAX-(uintptr_t)pcm)/4 || overlap(s,sizeof(*s),pcm,capacity*4) ||
       overlap(s,sizeof(*s),written,sizeof(*written)) || overlap(pcm,capacity*4,written,sizeof(*written)))return false;
    size_t count=0;
    while(count<capacity && s->count && (s->count>1 || s->finished)) {
        unsigned next=s->count>1?(s->first+1)%CTR_DSP_STREAM_CAPACITY:s->first;
        for(unsigned lane=0;lane<2;++lane) {
            int64_t value=(int64_t)s->pcm[s->first][lane]*(DENOMINATOR-s->phase)+
                          (int64_t)s->pcm[next][lane]*s->phase+DENOMINATOR/2;
            pcm[count*2+lane]=(int16_t)(value>=0?value/DENOMINATOR:
                -((-value+DENOMINATOR-1)/DENOMINATOR));
        }
        ++count;s->phase+=STEP;
        if(s->phase>=DENOMINATOR) {
            s->phase-=DENOMINATOR;s->first=(s->first+1)%CTR_DSP_STREAM_CAPACITY;--s->count;
        }
    }
    add(&s->output_frames,count);*written=count;return true;
}
