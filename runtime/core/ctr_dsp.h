#ifndef CTR_DSP_H
#define CTR_DSP_H
#include "ctr_guest.h"

#define CTR_DSP_RAM UINT32_C(0x1ff40000)
#define CTR_DSP_RAM_BYTES UINT32_C(0x40000)
#define CTR_DSP_CHANNELS 24u
#define CTR_DSP_FRAME_SAMPLES 160u
#define CTR_DSP_OUTPUT_FRAMES 8u
/* Original explicit HLE selection token; never treats arbitrary firmware as
 * implemented. Standard DSP IPC and NDSP shared-buffer ABI follow libctru. */
extern const uint8_t ctr_dsp_component[16];
typedef struct {
    uint64_t frames, samples, queued, completed, loops, underruns;
    uint64_t rejected, output_reads;
    uint32_t detail;
    uint32_t guest_pc, guest_ipc_header, guest_fault, guest_fault_detail;
    bool loaded, running, pending;
} ctr_dsp_statistics;
bool ctr_dsp_request(ctr_guest *g,unsigned session,const uint32_t *words,
                     uint32_t command,uint32_t *out,unsigned *count);
bool ctr_dsp_poll(ctr_guest *g,uint64_t now_ns);
/* Signal the retained DSP semaphore event, including through duplicated
 * handles. False means an ordinary event; handled=true distinguishes errors. */
bool ctr_dsp_signal(ctr_guest *g,unsigned event,bool *handled);
void ctr_dsp_session_closed(ctr_guest *g,unsigned session);
void ctr_dsp_close(ctr_guest *g);
/* One complete 160-frame stereo PCM16 DSP-rate block. Copy into independent
 * host storage; unavailable/aliased/bad capacity leaves queue and output alone.
 * Rate is exactly 16756991/512 Hz. Host playback must resample explicitly. */
bool ctr_dsp_read(ctr_guest *g,int16_t *stereo,size_t capacity);
bool ctr_dsp_stats(const ctr_guest *g,ctr_dsp_statistics *out);
/* Bounded finite DSP-rate -> 48k PCM16 conversion. Integer linear interpolation,
 * nearest/ties positive, last-sample hold for the fractional tail. Independent
 * immutable input/output buffers; failure preserves output and written. */
bool ctr_dsp_pcm48k(const int16_t *input,size_t frames,int16_t *output,
                    size_t capacity,size_t *written);
#endif
