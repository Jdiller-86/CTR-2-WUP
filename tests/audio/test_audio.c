#include "diagnostic_audio.h"
#include <sndcore2/core.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"FAIL %u line %d: %s\n",checks,__LINE__,#x);exit(1); } } while(0)
static bool initialized, init_fails, quit_fails, voice_live;
static unsigned init_calls, quit_calls, voice_releases;
static char events[128];
static unsigned event_count;

static void event(char value) {
    CHECK(event_count+1<sizeof(events));
    events[event_count++]=value;events[event_count]=0;
}
int AXIsInit(void) { return initialized; }
void AXInitWithParams(AXInitParams *params) {
    CHECK(params && params->renderer==AX_INIT_RENDERER_48KHZ && !params->reserved &&
        params->pipeline==AX_INIT_PIPELINE_SINGLE);
    CHECK(!initialized); /* Model an already initialized call as invalid here. */
    event('I');++init_calls;initialized=!init_fails;
}
void AXQuit(void) {
    CHECK(!voice_live); /* No live app sample/voice may survive context quit. */
    event('Q');++quit_calls;if(!quit_fails)initialized=false;
}
static void free_voice(void *context) {
    CHECK(context==&voice_live);
    event(initialized?'V':'D');voice_live=false;++voice_releases;
}
static void reset(void) {
    initialized=init_fails=quit_fails=voice_live=false;
    init_calls=quit_calls=voice_releases=event_count=0;events[0]=0;
}
static void silent_context(void) {
    reset();ctr_diagnostic_audio audio={0};
    CHECK(!ctr_diagnostic_audio_begin(NULL,true));
    CHECK(!ctr_diagnostic_audio_begin(&audio,false));
    CHECK(audio.result==CTR_AUDIO_NOT_FOREGROUND && !audio.attempts && !event_count);
    CHECK(ctr_diagnostic_audio_begin(&audio,true));
    CHECK(audio.owned && !audio.last_inherited && audio.result==CTR_AUDIO_READY);
    CHECK(audio.attempts==1 && audio.successes==1 && !audio.failures && !voice_live);
    CHECK(!strcmp(events,"I") && init_calls==1 && !quit_calls);
    CHECK(ctr_diagnostic_audio_begin(&audio,true));
    CHECK(audio.attempts==1 && init_calls==1 && !quit_calls);
    ctr_diagnostic_audio_end(&audio,NULL,NULL);
    CHECK(!audio.owned && !initialized && audio.releases==1 && !strcmp(events,"IQ"));
    ctr_diagnostic_audio_end(&audio,NULL,NULL);ctr_diagnostic_audio_end(NULL,NULL,NULL);
    CHECK(quit_calls==1 && audio.releases==1 && !voice_releases);
    /* Fresh foreground starts once more, as after a ProcUI reacquire. */
    CHECK(!ctr_diagnostic_audio_begin(&audio,false));
    CHECK(init_calls==1 && !audio.owned);
    CHECK(ctr_diagnostic_audio_begin(&audio,true));
    CHECK(audio.attempts==2 && audio.successes==2 && !audio.last_inherited);
    ctr_diagnostic_audio_end(&audio,NULL,NULL);
    CHECK(!strcmp(events,"IQIQ") && audio.releases==2);
}
static void inherited_context(void) {
    reset();initialized=true;ctr_diagnostic_audio audio={0};
    CHECK(ctr_diagnostic_audio_begin(&audio,true));
    CHECK(audio.owned && audio.last_inherited && audio.inherited_resets==1);
    CHECK(init_calls==1 && quit_calls==1 && !strcmp(events,"QI"));
    CHECK(ctr_diagnostic_audio_begin(&audio,true));
    CHECK(!strcmp(events,"QI") && audio.inherited_resets==1);
    voice_live=true;
    ctr_diagnostic_audio_end(&audio,free_voice,&voice_live);
    CHECK(!voice_live && !audio.owned && !initialized && voice_releases==1);
    CHECK(!strcmp(events,"QIVQ"));
    CHECK(ctr_diagnostic_audio_begin(&audio,true));
    CHECK(!audio.last_inherited && audio.inherited_resets==1 && audio.successes==2);
    voice_live=true;ctr_diagnostic_audio_end(&audio,free_voice,&voice_live);
    CHECK(!strcmp(events,"QIVQIVQ") && audio.releases==2 && voice_releases==2);
}
static void failures(void) {
    reset();init_fails=true;ctr_diagnostic_audio audio={0};
    CHECK(!ctr_diagnostic_audio_begin(&audio,true));
    CHECK(audio.result==CTR_AUDIO_INIT_FAILED && !audio.owned && !audio.successes && audio.failures==1);
    CHECK(!strcmp(events,"IQ") && init_calls==1 && quit_calls==1);
    ctr_diagnostic_audio_end(&audio,free_voice,&voice_live);
    CHECK(!voice_releases && !audio.releases && quit_calls==1);
    init_fails=false;
    CHECK(ctr_diagnostic_audio_begin(&audio,true));
    CHECK(audio.result==CTR_AUDIO_READY && audio.attempts==2 && audio.successes==1 && audio.failures==1);
    ctr_diagnostic_audio_end(&audio,NULL,NULL);
    CHECK(!strcmp(events,"IQIQ"));

    reset();initialized=true;quit_fails=true;memset(&audio,0,sizeof(audio));
    CHECK(!ctr_diagnostic_audio_begin(&audio,true));
    CHECK(audio.result==CTR_AUDIO_RESET_FAILED && !audio.owned && audio.last_inherited);
    CHECK(audio.attempts==1 && audio.failures==1 && audio.inherited_resets==1 && !audio.successes);
    CHECK(!init_calls && quit_calls==1 && !strcmp(events,"Q"));
    ctr_diagnostic_audio_end(&audio,free_voice,&voice_live);CHECK(!voice_releases && quit_calls==1);
    quit_fails=false;
    CHECK(ctr_diagnostic_audio_begin(&audio,true));
    CHECK(audio.inherited_resets==2 && audio.attempts==2 && audio.failures==1 && audio.successes==1);
    ctr_diagnostic_audio_end(&audio,NULL,NULL);CHECK(!strcmp(events,"QQIQ"));
}
static void ownership_lost(void) {
    reset();ctr_diagnostic_audio audio={0};
    CHECK(ctr_diagnostic_audio_begin(&audio,true));
    voice_live=true;initialized=false;
    /* AX has already gone: release callback discards the stale pointer, with
       no second quit and no attempt to dereference a freed native voice. */
    ctr_diagnostic_audio_end(&audio,free_voice,&voice_live);
    CHECK(!voice_live && !audio.owned && !initialized && !quit_calls);
    CHECK(!strcmp(events,"ID") && voice_releases==1 && audio.releases==1);
    CHECK(ctr_diagnostic_audio_begin(&audio,true));
    ctr_diagnostic_audio_end(&audio,NULL,NULL);CHECK(!strcmp(events,"IDIQ"));
}
static void failed_shutdown(void) {
    reset();ctr_diagnostic_audio audio={0};
    CHECK(ctr_diagnostic_audio_begin(&audio,true));
    voice_live=true;quit_fails=true;
    CHECK(!ctr_diagnostic_audio_end(&audio,free_voice,&voice_live));
    CHECK(audio.owned && audio.result==CTR_AUDIO_QUIT_FAILED && !voice_live);
    CHECK(audio.failures==1 && !audio.releases && voice_releases==1 && !strcmp(events,"IVQ"));
    CHECK(!ctr_diagnostic_audio_begin(&audio,true));
    CHECK(init_calls==1 && audio.attempts==1 && audio.result==CTR_AUDIO_QUIT_FAILED);
    /* Cleanup retries the remaining context without touching a freed voice. */
    quit_fails=false;
    CHECK(ctr_diagnostic_audio_end(&audio,NULL,NULL));
    CHECK(!audio.owned && !initialized && audio.releases==1 && !strcmp(events,"IVQQ"));
    CHECK(ctr_diagnostic_audio_begin(&audio,true));
    CHECK(audio.result==CTR_AUDIO_READY && audio.successes==2);
    CHECK(ctr_diagnostic_audio_end(&audio,NULL,NULL));
}
static void counters_and_names(void) {
    reset();ctr_diagnostic_audio audio={.attempts=UINT32_MAX,.successes=UINT32_MAX,
        .failures=UINT32_MAX,.inherited_resets=UINT32_MAX,.releases=UINT32_MAX};
    initialized=true;CHECK(ctr_diagnostic_audio_begin(&audio,true));
    ctr_diagnostic_audio_end(&audio,NULL,NULL);
    CHECK(audio.attempts==UINT32_MAX && audio.successes==UINT32_MAX && audio.inherited_resets==UINT32_MAX && audio.releases==UINT32_MAX);
    init_fails=true;CHECK(!ctr_diagnostic_audio_begin(&audio,true));CHECK(audio.failures==UINT32_MAX);
    CHECK(!strcmp(ctr_diagnostic_audio_result_name(CTR_AUDIO_NOT_STARTED),"NOT_STARTED"));
    CHECK(!strcmp(ctr_diagnostic_audio_result_name(CTR_AUDIO_READY),"READY"));
    CHECK(!strcmp(ctr_diagnostic_audio_result_name(CTR_AUDIO_NOT_FOREGROUND),"NOT_FOREGROUND"));
    CHECK(!strcmp(ctr_diagnostic_audio_result_name(CTR_AUDIO_RESET_FAILED),"RESET_FAILED"));
    CHECK(!strcmp(ctr_diagnostic_audio_result_name(CTR_AUDIO_INIT_FAILED),"INIT_FAILED"));
    CHECK(!strcmp(ctr_diagnostic_audio_result_name(CTR_AUDIO_QUIT_FAILED),"QUIT_FAILED"));
    CHECK(!strcmp(ctr_diagnostic_audio_result_name((ctr_diagnostic_audio_result)999),"INVALID"));
}
int main(void) {
    silent_context();inherited_context();failures();ownership_lost();failed_shutdown();counters_and_names();
    printf("diagnostic audio lifecycle PASS checks=%u (mock AX, not console audio)\n",checks);return 0;
}
