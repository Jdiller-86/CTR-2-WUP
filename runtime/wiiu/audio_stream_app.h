#ifndef CTR_AUDIO_STREAM_APP_H
#define CTR_AUDIO_STREAM_APP_H
#include "test_guide.h"

#define CTR_STREAM_APP_STEPS 8u
typedef struct {
    bool (*start)(void *,unsigned step,unsigned session);
    bool (*pause)(void *,bool paused);
    void (*stop)(void *);
    ctr_guide_log log;
    void *context;
} ctr_stream_app_ops;
typedef struct {
    unsigned step,passed,failed,skipped,session,attempts,foreground_losses;
    uint64_t milliseconds,step_started,pause_started;
    bool started,done,cancelled,played,complete,paused,resumed;
    char outcomes[CTR_STREAM_APP_STEPS];
    ctr_stream_app_ops ops;
} ctr_stream_app;
typedef struct {
    uint64_t milliseconds;
    uint32_t pressed;
    bool complete,failed;
} ctr_stream_app_input;
void ctr_stream_app_init(ctr_stream_app *,const ctr_stream_app_ops *);
void ctr_stream_app_update(ctr_stream_app *,const ctr_stream_app_input *);
/* A foreground interruption invalidates the current playback attempt; a fresh
 * explicit A/Plus action must prepare and play it again after reacquisition. */
void ctr_stream_app_suspend(ctr_stream_app *);
void ctr_stream_app_view(const ctr_stream_app *,char rows[7][72]);
bool ctr_stream_app_results(const ctr_stream_app *,char *,size_t);
void ctr_stream_app_summary(const ctr_stream_app *);
#endif
