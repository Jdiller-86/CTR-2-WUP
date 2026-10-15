#include "audio_stream_app.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const char *const names[CTR_STREAM_APP_STEPS]={
    "Launch silence","Left channel","Right channel","Two-voice mix",
    "Source rate change","Pause and resume","Seek to second phrase","New playback session"
};
static const char *const prompts[CTR_STREAM_APP_STEPS]={
    "R: launch music stopped / L: still playing",
    "A: rising notes LEFT only on TV and GamePad",
    "A: falling notes RIGHT only on TV and GamePad",
    "A: both motifs together on BOTH outputs",
    "A: faster/higher LEFT motif (48k source)",
    "A: play. X DURING NOTE: pause, wait, X resume",
    "A: start at note 2; first note must be absent",
    "PLUS: new session. Both motifs must repeat"
};
static void log_line(const ctr_stream_app *app,const char *format,...) {
    char text[384];va_list args;va_start(args,format);
    vsnprintf(text,sizeof(text),format,args);va_end(args);
    if(app->ops.log)app->ops.log(app->ops.context,text);
}
static void enter(ctr_stream_app *app) {
    app->played=app->complete=app->paused=app->resumed=false;
    app->step_started=app->milliseconds;
    if(app->step==CTR_STREAM_APP_STEPS) {
        app->done=true;
        log_line(app,"STREAM GUIDE COMPLETE passed=%u failed=%u skipped=%u sessions=%u; Minus exits; manual hearing only, no guest DSP acceptance",
            app->passed,app->failed,app->skipped,app->session);
    } else log_line(app,"STREAM GUIDE STEP %u/%u name=%s | %s",app->step+1,CTR_STREAM_APP_STEPS,names[app->step],prompts[app->step]);
}
static void finish(ctr_stream_app *app,char outcome,const char *evidence) {
    if(app->ops.stop)app->ops.stop(app->ops.context);
    app->outcomes[app->step]=outcome;
    if(outcome=='P')++app->passed;else if(outcome=='F')++app->failed;else ++app->skipped;
    log_line(app,"STREAM GUIDE RESULT step=%u name=%s result=%s session=%u played=%u native_complete=%u pause=%u resume=%u elapsed_ms=%llu evidence=%s",
        app->step+1,names[app->step],outcome=='P'?"PASS":outcome=='F'?"FAIL":"NOT TESTED",app->session,
        app->played,app->complete,app->paused,app->resumed,
        (unsigned long long)(app->milliseconds-app->step_started),evidence);
    ++app->step;enter(app);
}
void ctr_stream_app_init(ctr_stream_app *app,const ctr_stream_app_ops *ops) {
    memset(app,0,sizeof(*app));if(ops)app->ops=*ops;app->session=1;
}
void ctr_stream_app_update(ctr_stream_app *app,const ctr_stream_app_input *in) {
    if(!app || !in || app->cancelled)return;
    if(in->milliseconds>app->milliseconds)app->milliseconds=in->milliseconds;
    if(!app->started) { app->started=true;enter(app); }
    if(in->pressed&CTR_GUIDE_MINUS) {
        if(app->ops.stop)app->ops.stop(app->ops.context);
        app->cancelled=!app->done;
        log_line(app,"STREAM GUIDE EXIT session=%u finished=%u remaining=%u",app->session,app->done,CTR_STREAM_APP_STEPS-app->step);return;
    }
    if(app->done)return;
    if(in->failed) { finish(app,'F',"Native preparation/playback failure; inspect numeric native evidence");return; }
    if(in->pressed&(CTR_GUIDE_FAIL|CTR_GUIDE_L)) { finish(app,'F',"User reported missing/wrong sound");return; }
    if(in->pressed&CTR_GUIDE_SKIP) { finish(app,'S',"User skipped with ZL");return; }
    if(app->step==0) {
        if(in->pressed&CTR_GUIDE_R)finish(app,'P',"User confirmed launch music stopped; no hearing measurement");
        return;
    }
    if(in->pressed&(app->step==7?CTR_GUIDE_PLUS:CTR_GUIDE_A)) {
        if(app->step==7)++app->session;
        ++app->attempts;app->complete=app->paused=app->resumed=false;
        app->played=app->ops.start && app->ops.start(app->ops.context,app->step,app->session);
        log_line(app,"STREAM GUIDE PLAY step=%u session=%u attempt=%u accepted=%u",app->step+1,app->session,app->attempts,app->played);
        if(!app->played)finish(app,'F',"Explicit playback action failed");
        return;
    }
    if(app->step==5 && app->played && !app->complete && (in->pressed&CTR_GUIDE_X)) {
        if(!app->paused && !app->resumed) {
            if(!app->ops.pause || !app->ops.pause(app->ops.context,true)) { finish(app,'F',"Native pause failed");return; }
            app->paused=true;app->pause_started=app->milliseconds;
            log_line(app,"STREAM GUIDE PAUSE session=%u at_ms=%llu",app->session,(unsigned long long)app->milliseconds);
        } else if(app->paused && !app->resumed) {
            if(app->milliseconds-app->pause_started<250u) {
                log_line(app,"STREAM GUIDE RESUME blocked minimum_pause_ms=250");return;
            }
            if(!app->ops.pause || !app->ops.pause(app->ops.context,false)) { finish(app,'F',"Native resume failed");return; }
            app->resumed=true;
            log_line(app,"STREAM GUIDE RESUME session=%u pause_ms=%llu",app->session,(unsigned long long)(app->milliseconds-app->pause_started));
        }
        return;
    }
    if(app->played && in->complete && !app->complete) {
        app->complete=true;
        log_line(app,"STREAM GUIDE NATIVE COMPLETE step=%u session=%u; fresh R required for manual hearing",app->step+1,app->session);
        return;
    }
    if(in->pressed&CTR_GUIDE_R) {
        if(app->played && app->complete && (app->step!=5 || (app->paused && app->resumed)))
            finish(app,'P',app->step==5?"Native pause/resume and completion observed; user confirmed silence during pause and resumed sound on TV and GamePad":"Native finite playback completed; user confirmed expected sound on TV and GamePad");
        else log_line(app,"STREAM GUIDE CONFIRM blocked step=%u played=%u complete=%u pause=%u resume=%u",app->step+1,app->played,app->complete,app->paused,app->resumed);
    }
}
void ctr_stream_app_suspend(ctr_stream_app *app) {
    if(!app)return;
    if(app->ops.stop)app->ops.stop(app->ops.context);
    ++app->foreground_losses;
    app->played=app->complete=app->paused=app->resumed=false;
    log_line(app,"STREAM GUIDE FOREGROUND RELEASE losses=%u step=%u session=%u; current attempt invalidated; explicit replay required",
        app->foreground_losses,app->step+1,app->session);
}
void ctr_stream_app_view(const ctr_stream_app *app,char rows[7][72]) {
    memset(rows,0,7u*72u);
    snprintf(rows[0],72,"AUDIO STREAMS %s / SESSION %u",app->done?"COMPLETE":"GUIDED TEST",app->session);
    if(app->done) {
        snprintf(rows[1],72,"PASS %u / FAIL %u / SKIP %u",app->passed,app->failed,app->skipped);
        snprintf(rows[2],72,"MINUS: EXIT AND CLOSE BOTH TXT FILES");
        snprintf(rows[3],72,"Send result + trace from SD:/CTR2WUP");
        snprintf(rows[4],72,"Human hearing; original finite native clips");
        snprintf(rows[5],72,"Guest DSP / game audio are not tested");return;
    }
    snprintf(rows[1],72,"%u/%u %s",app->step+1,CTR_STREAM_APP_STEPS,names[app->step]);
    snprintf(rows[2],72,"%s",prompts[app->step]);
    snprintf(rows[3],72,app->step==0?"Check TV AND GamePad before confirming":
        app->complete?"Playback ended. R: correct on BOTH / L: wrong":
        app->played?(app->paused&&!app->resumed?"PAUSED: wait at least 0.25 sec then X":"PLAYING: wait for native completion"):
        "Playback required before R can confirm");
    if(app->step==5 && app->complete && app->resumed)snprintf(rows[3],72,"R: quiet WHILE PAUSED and sound resumed on BOTH");
    if(app->step==5 && app->complete && !app->resumed)snprintf(rows[3],72,"Repeat A, then X pause / wait / X resume");
    snprintf(rows[4],72,"L / ZR: FAIL   ZL: SKIP   MINUS: EXIT");
    snprintf(rows[5],72,"Use stereo TV or headphones for LEFT/RIGHT");
    snprintf(rows[6],72,"Keep TV and GamePad sound audible");
}
bool ctr_stream_app_results(const ctr_stream_app *app,char *out,size_t capacity) {
    if(!app || !out || !capacity)return false;
    size_t used=0;
    for(unsigned i=0;i<CTR_STREAM_APP_STEPS;++i) {
        const char *result=app->outcomes[i]=='P'?"PASS":app->outcomes[i]=='F'?"FAIL":app->outcomes[i]=='S'?"NOT TESTED (skipped)":"NOT FINISHED";
        int n=snprintf(out+used,capacity-used,"%u. %s: %s\n",i+1,names[i],result);
        if(n<0 || (size_t)n>=capacity-used)return false;
        used+=(size_t)n;
    }
    return true;
}
void ctr_stream_app_summary(const ctr_stream_app *app) {
    log_line(app,"STREAM GUIDE SUMMARY state=%s passed=%u failed=%u skipped=%u remaining=%u sessions=%u attempts=%u foreground_losses=%u scope=human_hearing_original_native_clips_not_guest_DSP",
        app->done?"COMPLETE":app->cancelled?"CANCELLED":"RUNNING",app->passed,app->failed,app->skipped,
        CTR_STREAM_APP_STEPS-app->step,app->session,app->attempts,app->foreground_losses);
}
