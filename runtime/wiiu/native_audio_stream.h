#ifndef CTR_NATIVE_AUDIO_STREAM_H
#define CTR_NATIVE_AUDIO_STREAM_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

#define CTR_NATIVE_STREAM_FRAMES 960u
#define CTR_NATIVE_STREAM_SLOTS 8u
typedef enum {
    CTR_NATIVE_STREAM_OK,CTR_NATIVE_STREAM_ARGUMENT,CTR_NATIVE_STREAM_BUSY,
    CTR_NATIVE_STREAM_AX,CTR_NATIVE_STREAM_CONFIG,CTR_NATIVE_STREAM_CALLBACK,
    CTR_NATIVE_STREAM_OFFSETS,CTR_NATIVE_STREAM_STATE
} ctr_native_stream_result;
typedef struct {
    uint32_t queued_blocks,submitted_frames,played_frames,underruns,callbacks;
    bool started,paused,complete;
    ctr_native_stream_result result;
} ctr_native_stream_report;
struct AXVoice;
typedef struct {
    struct AXVoice *voices[2];
    atomic_uint read,write,requested,result,played,underruns,callbacks;
    atomic_bool finished,started,paused,complete;
    uint32_t lengths[CTR_NATIVE_STREAM_SLOTS],submitted;
    unsigned active;bool registered,partial;
} ctr_native_stream;
/* Zero initialize and retain state until release. The existing diagnostic_audio
 * owner must hold a 48k AX context. One producer on the main/AX-owning thread;
 * the callback switches immutable buffers only. Never execute a guest, decode,
 * allocate, log or access storage inside the AX callback. */
bool ctr_native_stream_open(ctr_native_stream *state);
size_t ctr_native_stream_space(const ctr_native_stream *state);
/* Full 960-frame blocks except an optional final partial block, padded with
 * silence. Copies/deinterleaves input before publishing; never blocks. */
bool ctr_native_stream_push(ctr_native_stream *state,const int16_t *stereo,size_t frames);
bool ctr_native_stream_start(ctr_native_stream *state);
bool ctr_native_stream_pause(ctr_native_stream *state,bool pause);
bool ctr_native_stream_finish(ctr_native_stream *state);
bool ctr_native_stream_snapshot(ctr_native_stream *state,ctr_native_stream_report *report);
/* Deregisters/drains callbacks before releasing voices; suitable for the
 * diagnostic_audio_end callback. Lost AX ownership discards stale pointers. */
void ctr_native_stream_release(void *state);
#endif
