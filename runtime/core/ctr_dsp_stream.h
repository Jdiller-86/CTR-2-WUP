#ifndef CTR_DSP_STREAM_H
#define CTR_DSP_STREAM_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CTR_DSP_STREAM_CAPACITY 2048u
#define CTR_DSP_STREAM_PULL_MAX 4096u
/* Main-thread, allocation-free DSP-rate -> 48k stereo resampler. Input is
 * copied. Phase and the interpolation pair survive arbitrary push/pull splits.
 * finish permits the final fractional sample tail; an unfinished stream waits
 * for its next sample instead of inventing one. Fields are read-only to callers. */
typedef struct {
    uint32_t guard,first,count,phase;
    bool finished;
    uint64_t input_frames,output_frames;
    int16_t pcm[CTR_DSP_STREAM_CAPACITY][2];
} ctr_dsp_stream;
void ctr_dsp_stream_init(ctr_dsp_stream *stream);
size_t ctr_dsp_stream_space(const ctr_dsp_stream *stream);
bool ctr_dsp_stream_push(ctr_dsp_stream *stream,const int16_t *stereo,size_t frames);
bool ctr_dsp_stream_finish(ctr_dsp_stream *stream);
/* capacity and written are stereo frames. Valid calls can write zero frames
 * while waiting for input; false preserves state, output and written. */
bool ctr_dsp_stream_pull(ctr_dsp_stream *stream,int16_t *stereo,size_t capacity,size_t *written);
bool ctr_dsp_stream_done(const ctr_dsp_stream *stream);
#endif
