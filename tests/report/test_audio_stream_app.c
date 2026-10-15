#include "audio_stream_app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks,starts,stops,pauses,resumes,last_step,last_session;
static bool start_ok=true,pause_ok=true;
static char transcript[8192];
#define CHECK(x) do { ++checks;if(!(x)) { fprintf(stderr,"stream guide FAIL line %d: %s\n",__LINE__,#x);exit(1); } } while(0)
static bool start(void *context,unsigned step,unsigned session) {
    CHECK(context==&starts);++starts;last_step=step;last_session=session;return start_ok;
}
static bool pause_playback(void *context,bool paused) {
    CHECK(context==&starts);if(paused)++pauses;else ++resumes;return pause_ok;
}
static void stop(void *context) { CHECK(context==&starts);++stops; }
static void log_message(void *context,const char *text) {
    CHECK(context==&starts);
    size_t used=strlen(transcript),n=strlen(text);
    if(used+n+2<sizeof(transcript)) { memcpy(transcript+used,text,n);transcript[used+n]='\n';transcript[used+n+1]=0; }
}
static void init(ctr_stream_app *app,unsigned step) {
    const ctr_stream_app_ops ops={start,pause_playback,stop,log_message,&starts};
    starts=stops=pauses=resumes=0;start_ok=pause_ok=true;transcript[0]=0;
    ctr_stream_app_init(app,&ops);app->step=step;
}
static void tick(ctr_stream_app *app,uint64_t time,uint32_t pressed,bool complete,bool failed) {
    ctr_stream_app_input input={time,pressed,complete,failed};ctr_stream_app_update(app,&input);
}
unsigned test_audio_stream_app(void) {
    ctr_stream_app app;char rows[7][72],result[1536];
    init(&app,0);tick(&app,0,CTR_GUIDE_R,false,false);
    CHECK(app.step==1 && app.passed==1 && !starts);
    tick(&app,10,CTR_GUIDE_R,true,false);
    CHECK(app.step==1 && !app.complete && !app.played);
    CHECK(strstr(transcript,"CONFIRM blocked")!=NULL);
    tick(&app,20,CTR_GUIDE_A|CTR_GUIDE_R,true,false);
    CHECK(app.step==1 && app.played && !app.complete && starts==1 && last_step==1 && last_session==1);
    tick(&app,30,CTR_GUIDE_R,false,false);CHECK(app.step==1);
    tick(&app,40,CTR_GUIDE_R,true,false);CHECK(app.step==1 && app.complete);
    tick(&app,41,CTR_GUIDE_R,true,false);CHECK(app.step==2 && app.passed==2 && !app.played && !app.complete);
    CHECK(strstr(transcript,"fresh R required") && strstr(transcript,"native_complete=1"));

    init(&app,5);tick(&app,0,CTR_GUIDE_X,false,false);CHECK(!pauses);
    tick(&app,10,CTR_GUIDE_A,false,false);
    tick(&app,100,CTR_GUIDE_X,false,false);CHECK(app.paused && pauses==1 && !resumes);
    tick(&app,349,CTR_GUIDE_X,false,false);CHECK(!resumes);
    tick(&app,0,CTR_GUIDE_X,false,false);CHECK(!resumes && app.milliseconds==349);
    tick(&app,350,CTR_GUIDE_X,false,false);CHECK(app.resumed && resumes==1);
    tick(&app,400,CTR_GUIDE_R,true,false);CHECK(app.step==5 && app.complete);
    tick(&app,401,CTR_GUIDE_R,true,false);CHECK(app.step==6 && app.passed==1);
    init(&app,5);tick(&app,1,CTR_GUIDE_A,false,false);tick(&app,1600,0,true,false);
    tick(&app,1700,CTR_GUIDE_R,true,false);CHECK(app.step==5 && !app.passed);
    ctr_stream_app_view(&app,rows);CHECK(strstr(rows[3],"Repeat A")!=NULL);
    tick(&app,1800,CTR_GUIDE_A,false,false);CHECK(!app.complete && starts==2);
    pause_ok=false;tick(&app,1900,CTR_GUIDE_X,false,false);CHECK(app.step==6 && app.failed==1);

    init(&app,6);tick(&app,1,CTR_GUIDE_A,false,false);tick(&app,1100,0,true,false);
    ctr_stream_app_suspend(&app);CHECK(app.foreground_losses==1 && !app.played && !app.complete);
    tick(&app,1200,CTR_GUIDE_R,true,false);CHECK(app.step==6 && !app.passed);
    tick(&app,1300,CTR_GUIDE_A,false,false);CHECK(starts==2 && last_step==6);
    tick(&app,2400,0,true,false);tick(&app,2401,CTR_GUIDE_R,true,false);CHECK(app.step==7);

    init(&app,7);tick(&app,1,CTR_GUIDE_A,true,false);CHECK(!starts && app.session==1 && !app.played);
    tick(&app,2,CTR_GUIDE_PLUS|CTR_GUIDE_R,true,false);CHECK(starts==1 && last_session==2 && app.session==2 && !app.complete);
    tick(&app,3,CTR_GUIDE_R,false,false);CHECK(!app.done);
    tick(&app,1500,CTR_GUIDE_R,true,false);CHECK(app.complete && !app.done);
    tick(&app,1501,CTR_GUIDE_R,true,false);CHECK(app.done && app.passed==1 && app.outcomes[7]=='P');
    tick(&app,1600,CTR_GUIDE_MINUS,false,false);CHECK(app.done && !app.cancelled);
    ctr_stream_app_view(&app,rows);CHECK(strstr(rows[0],"COMPLETE") && strstr(rows[2],"MINUS"));

    init(&app,1);start_ok=false;tick(&app,1,CTR_GUIDE_A,false,false);
    CHECK(app.failed==1 && app.step==2 && app.outcomes[1]=='F');
    tick(&app,2,0,false,false);CHECK(app.failed==1);
    tick(&app,3,CTR_GUIDE_SKIP,false,false);CHECK(app.skipped==1 && app.step==3);
    tick(&app,4,CTR_GUIDE_L,false,false);CHECK(app.failed==2 && app.step==4);
    tick(&app,5,CTR_GUIDE_MINUS,false,false);CHECK(app.cancelled && !app.done);
    CHECK(ctr_stream_app_results(&app,result,sizeof(result)));
    CHECK(strstr(result,"2. Left channel: FAIL") && strstr(result,"3. Right channel: NOT TESTED (skipped)") && strstr(result,"5. Source rate change: NOT FINISHED"));
    CHECK(!ctr_stream_app_results(&app,result,1));
    ctr_stream_app_summary(&app);CHECK(strstr(transcript,"scope=human_hearing_original_native_clips_not_guest_DSP"));
    init(&app,1);tick(&app,0,CTR_GUIDE_A,false,false);tick(&app,1,0,false,true);
    CHECK(app.failed==1 && app.step==2);
    return checks;
}
