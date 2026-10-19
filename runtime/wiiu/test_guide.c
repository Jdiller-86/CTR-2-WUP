#include "test_guide.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

enum step_type { VIEW_NATIVE, BUTTONS, LEFT_STICK, RIGHT_STICK, TOUCH_POINTS,
    TOUCH_BORDER, TOUCH_CONFIRM, AUDIO, STABILITY, STABILITY_CONFIRM,
    VIEW_GUEST, COLOR_A, GUEST_EXIT, GUEST_RESTART, SAVE_FIRST, SAVE_RELOAD };
typedef struct { enum step_type type; const char *name,*line1,*line2; } step;
static const step native_steps[]={
    {VIEW_NATIVE,"Picture check","Check TV bands / GamePad checkerboard.","R: looks correct   L: picture problem"},
    {BUTTONS,"Buttons","Press A B X Y L R and all D-pad directions.","Each press is recorded; completes automatically."},
    {LEFT_STICK,"Left stick","Move the LEFT stick fully in all 4 directions.","Up, down, left and right are recorded."},
    {RIGHT_STICK,"Right stick","Move the RIGHT stick fully in all 4 directions.","Up, down, left and right are recorded."},
    {TOUCH_POINTS,"Touch positions","Touch 4 image corners and the center.","Touch inside the GamePad image, near each corner."},
    {TOUCH_BORDER,"Touch border rejection","Touch a BLACK SIDE BORDER on the GamePad.","The border touch must be outside the image."},
    {TOUCH_CONFIRM,"Touch alignment","Does the marker follow your finger correctly?","R: correct   L: offset or other problem"},
    {AUDIO,"Sound check","Press A for a tone, then check TV and GamePad.","R: heard on BOTH   L: missing or wrong sound"},
    {STABILITY,"Ten-minute stability","Leave the test running; countdown is automatic.","Report a problem with ZR, or skip with ZL."},
    {STABILITY_CONFIRM,"Stability observation","Any freezes, flicker or other visible problems?","R: no problems observed   L: problem observed"}
};
static const step interactive_steps[]={
    {VIEW_GUEST,"Guest picture","Wait for RED top and BLUE bottom images.","R: correct   L: wrong or missing picture"},
    {COLOR_A,"Guest button response","Hold A and watch the top image turn GREEN.","After trying A: R correct / L wrong"},
    {TOUCH_POINTS,"Guest touch positions","Touch 4 image corners and the center.","Touch inside the GamePad image, near each corner."},
    {TOUCH_CONFIRM,"Guest touch marker","Does the white marker match your touch?","R: correct   L: offset or missing marker"},
    {GUEST_EXIT,"Guest exit","Press B and wait for the guest to exit.","Its own PASS message and exit are checked."},
    {GUEST_RESTART,"Guest restart","Press PLUS to restart, then B to exit again.","Restart and the new guest PASS/exit are checked."},
    {STABILITY,"Ten-minute guest stability","Guest restarts automatically for this check.","Leave running; ZR reports a problem, ZL skips."},
    {STABILITY_CONFIRM,"Guest stability observation","Any freezes, flicker or other visible problems?","R: no problems observed   L: problem observed"}
};
static const step save_steps[]={
    {SAVE_FIRST,"Initial save / reload","Wait for the guest's save or reload PASS.","An existing snapshot is allowed and preserved."},
    {SAVE_RELOAD,"Snapshot reload","Press PLUS to restart from the saved snapshot.","The new guest PASS and exit are checked."}
};
static const step asset_steps[]={
    {GUEST_EXIT,"RomFS asset reads","Checking the bundled sample files automatically.","Checks folders, Japanese names and write rejection."}
};
static const step audio_steps[]={
    {VIEW_NATIVE,"Launch music stopped","Has the Wii U launch music stopped?","R: quiet now   L: launch music still playing"},
    {AUDIO,"Native test tone","Press A: a short tone should play on both screens.","R: heard on BOTH   L: missing or wrong sound"}
};
static const step scene_steps[]={
    {VIEW_GUEST,"Arithmetic triangle picture","Both: RED lower-left / GREEN upper-right square.","R: correct on BOTH   L: wrong or missing picture"},
    {VIEW_GUEST,"Shared diagonal","GamePad: check the full square's diagonal.","R: solid square   L: gap, flicker or other problem"}
};
static const step pica_steps[]={
    {VIEW_GUEST,"Guest triangle pictures","BOTH: RED lower-left / GREEN upper-right rectangle.","R: solid diagonal on BOTH   L: wrong or missing"},
    {GUEST_EXIT,"Guest draws / readback / exit","Waiting for the compiled guest's pixel checks.","Its own PICA draw PASS and exit are checked."},
    {GUEST_RESTART,"Repeat draw run","Press PLUS to run the draw test again.","Wait for the new guest's automatic PASS and exit."}
};
static const step gx2_steps[]={
    {VIEW_GUEST,"GX2 guest triangle pictures","BOTH: RED lower-left / GREEN upper-right rectangle.","R: correct on BOTH   L: wrong or missing picture"},
    {GUEST_EXIT,"GPU readback and guest exit","Checking GPU pixels against the software reference.","Wait for automatic guest PASS, then MINUS to exit."}
};
static const step gx_steps[]={
    {VIEW_GUEST,"GX transfer picture","TV: GREEN / RED / GREEN. GamePad: BLUE.","R: observed correctly   L: wrong/missing picture"},
    {GUEST_EXIT,"GX operations and exit","Wait for fill/copy/conversion checks and exit.","The original guest's own PASS and exit are checked."}
};
static const step textured_steps[]={
    {VIEW_GUEST,"Textured guest pictures","BOTH: colored pattern and pale-green triangle.","R: correct on BOTH   L: wrong or missing picture"},
    {GUEST_EXIT,"Texture readback and guest exit","Checking texture, TEV and GPU pixels automatically.","Wait for automatic guest PASS, then MINUS to exit."}
};

static const step *steps(const ctr_test_guide *g, unsigned *count) {
    if(g->kind==CTR_GUIDE_TEXTURED) { *count=sizeof(textured_steps)/sizeof(*textured_steps);return textured_steps; }
    if(g->kind==CTR_GUIDE_GX2) { *count=sizeof(gx2_steps)/sizeof(*gx2_steps);return gx2_steps; }
    if(g->kind==CTR_GUIDE_SCENE) { *count=sizeof(scene_steps)/sizeof(*scene_steps);return scene_steps; }
    if(g->kind==CTR_GUIDE_PICA) { *count=sizeof(pica_steps)/sizeof(*pica_steps);return pica_steps; }
    if(g->kind==CTR_GUIDE_AUDIO) { *count=sizeof(audio_steps)/sizeof(audio_steps[0]);return audio_steps; }
    if(g->kind==CTR_GUIDE_GX) { *count=sizeof(gx_steps)/sizeof(*gx_steps);return gx_steps; }
    if(g->kind==CTR_GUIDE_ASSETS) { *count=sizeof(asset_steps)/sizeof(*asset_steps);return asset_steps; }
    if(g->kind==CTR_GUIDE_SAVE) { *count=sizeof(save_steps)/sizeof(*save_steps);return save_steps; }
    if(g->kind==CTR_GUIDE_INTERACTIVE) { *count=sizeof(interactive_steps)/sizeof(*interactive_steps);return interactive_steps; }
    *count=sizeof(native_steps)/sizeof(*native_steps);return native_steps;
}
static void log_line(const ctr_test_guide *g,const char *format,...) {
    char text[384];va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);
    if(g->log)g->log(g->context,text);
}
static void enter_step(ctr_test_guide *g) {
    unsigned count;const step *list=steps(g,&count);
    g->observed=0;g->restart_waiting=false;g->step_started=g->last_time;
    if(g->step==count) {
        g->done=true;
        log_line(g,"GUIDE COMPLETE: passed=%u failed=%u skipped=%u. Press Minus to close the report.",g->passed,g->failed,g->skipped);
        log_line(g,"DELIVERY: ZIP sd:/ctr2wup/results/ and return the folder. Photos are optional for unexplained visual failures.");
        if(g->kind==CTR_GUIDE_SAVE)log_line(g,g->fresh_reload_confirmed?
            "Fresh-launch reload confirmed. Upload this report and the preceding launch report if available.":
            "NEXT: exit and relaunch this app ONCE for fresh-process reload; upload both launch reports.");
        return;
    }
    log_line(g,"GUIDE STEP %u/%u: %s | %s | %s",g->step+1,count,list[g->step].name,list[g->step].line1,list[g->step].line2);
    if(list[g->step].type==STABILITY && g->kind==CTR_GUIDE_INTERACTIVE)g->restart_requested=true;
}
void ctr_test_guide_init(ctr_test_guide *g,ctr_guide_kind kind,ctr_guide_log log,void *context) {
    memset(g,0,sizeof(*g));g->kind=kind;g->log=log;g->context=context;
}
static void finish_step(ctr_test_guide *g,const char *result,const char *evidence) {
    unsigned count;const step *list=steps(g,&count);
    g->outcomes[g->step]=strcmp(result,"PASS")==0?'P':strcmp(result,"FAIL")==0?'F':'S';
    g->evidence[g->step]=evidence;
    log_line(g,"GUIDE RESULT: step=%u name=%s result=%s elapsed_ms=%llu observed=%08x evidence=%s",
        g->step+1,list[g->step].name,result,(unsigned long long)(g->last_time-g->step_started),g->observed,evidence);
    if(strcmp(result,"PASS")==0)++g->passed;
    else if(strcmp(result,"FAIL")==0)++g->failed;
    else ++g->skipped;
    ++g->step;enter_step(g);
}
void ctr_test_guide_update(ctr_test_guide *g,const ctr_guide_input *in) {
    if(g->cancelled)return;
    if(in->milliseconds>g->last_time)g->last_time=in->milliseconds;
    if(!g->started) { g->started=true;enter_step(g); }
    if(g->done)return;
    if(in->pressed&CTR_GUIDE_MINUS) {
        g->cancelled=true;log_line(g,"GUIDE CANCELLED during step %u. Remaining steps are NOT TESTED.",g->step+1);return;
    }
    if(in->pressed&CTR_GUIDE_SKIP) { finish_step(g,"NOT TESTED","User pressed ZL to skip");return; }
    if(in->pressed&CTR_GUIDE_FAIL) { finish_step(g,"FAIL","User pressed ZR to report a problem");return; }
    if(in->guest_fault && g->kind!=CTR_GUIDE_NATIVE) {
        finish_step(g,"FAIL",g->kind==CTR_GUIDE_SCENE?"Automatic scene test failed; inspect diagnostic log":"Guest fault observed; inspect diagnostic log");return;
    }
    unsigned count;const step *list=steps(g,&count);enum step_type type=list[g->step].type;
    if(type==BUTTONS) {
        const uint32_t required=CTR_GUIDE_A|CTR_GUIDE_B|CTR_GUIDE_X|CTR_GUIDE_Y|CTR_GUIDE_L|CTR_GUIDE_R|
            CTR_GUIDE_LEFT|CTR_GUIDE_RIGHT|CTR_GUIDE_UP|CTR_GUIDE_DOWN;
        g->observed|=in->pressed&required;
        if((g->observed&required)==required)finish_step(g,"PASS","All ten requested buttons observed");
    } else if(type==LEFT_STICK || type==RIGHT_STICK) {
        float x=type==LEFT_STICK?in->left_x:in->right_x,y=type==LEFT_STICK?in->left_y:in->right_y;
        if(x<-.5f)g->observed|=1;
        if(x>.5f)g->observed|=2;
        if(y<-.5f)g->observed|=4;
        if(y>.5f)g->observed|=8;
        if(g->observed==15)finish_step(g,"PASS","All four stick directions observed beyond 0.5");
    } else if(type==TOUCH_POINTS) {
        if(in->touch_inside && in->touch_x<320 && in->touch_y<240) {
            unsigned x=in->touch_x,y=in->touch_y;
            if(x<=31 && y<=23)g->observed|=1;
            if(x>=288 && y<=23)g->observed|=2;
            if(x<=31 && y>=216)g->observed|=4;
            if(x>=288 && y>=216)g->observed|=8;
            if(x>=128 && x<=191 && y>=96 && y<=143)g->observed|=16;
        }
        if(g->observed==31)finish_step(g,"PASS","Four image corner regions and center observed; alignment needs user confirmation");
    } else if(type==TOUCH_BORDER) {
        if(in->touch_border)finish_step(g,"PASS","Valid physical touch rejected outside viewport");
    } else if(type==STABILITY) {
        if(in->milliseconds>=g->step_started && in->milliseconds-g->step_started>=600000)
            finish_step(g,"PASS","Ten-minute observation interval completed; see counters and user confirmation");
    } else if(type==GUEST_EXIT || type==SAVE_FIRST) {
        if(type==SAVE_FIRST && in->fresh_save_reload && in->guest_exited && in->guest_pass)g->fresh_reload_confirmed=true;
        if(in->guest_exited)finish_step(g,in->guest_pass?"PASS":"FAIL",in->guest_pass?"Guest PASS message and exited state observed":"Guest exited without its expected PASS message");
    } else if(type==GUEST_RESTART || type==SAVE_RELOAD) {
        if(!g->restart_waiting && (in->pressed&CTR_GUIDE_PLUS)) {
            g->restart_requested=true;g->restart_waiting=true;g->expected_session=in->guest_sessions+1;
            log_line(g,"GUIDE restart requested; waiting for guest session %u",g->expected_session);
        } else if(g->restart_waiting && in->guest_sessions>=g->expected_session && in->guest_exited &&
                  ((g->kind!=CTR_GUIDE_PICA && g->kind!=CTR_GUIDE_GX2 && g->kind!=CTR_GUIDE_TEXTURED) || in->guest_drawn_frame_available)) {
            finish_step(g,in->guest_pass?"PASS":"FAIL",in->guest_pass?"New guest session PASS and exited state observed":"New guest exited without PASS");
        }
    } else {
        bool frame_ready=(g->kind==CTR_GUIDE_PICA || g->kind==CTR_GUIDE_GX2 || g->kind==CTR_GUIDE_TEXTURED)?in->guest_drawn_frame_available:in->guest_frame_available;
        if(type==VIEW_GUEST && frame_ready && !g->observed) {
            g->observed=1;
            log_line(g,"GUIDE visual frame ready: kind=%u serial=%llu elapsed_ms=%llu; fresh R press required",
                (unsigned)g->kind,(unsigned long long)in->guest_frame_serial,(unsigned long long)in->milliseconds);
        }
        if((type==COLOR_A || type==AUDIO) && ((in->pressed|in->held)&CTR_GUIDE_A) && !(g->observed&1)) {
            g->observed=1;g->input_frame_serial=in->input_frame_serial;g->button_started=in->milliseconds;
            if(type==COLOR_A)log_line(g,"GUIDE A input recorded: frame_before=%llu; waiting for a new green guest frame",
                (unsigned long long)g->input_frame_serial);
        }
        if((type==COLOR_A || type==AUDIO) && !g->observed)return;
        if(type==COLOR_A && in->guest_green_frame && in->guest_frame_serial>g->input_frame_serial && !(g->observed&2)) {
            g->observed|=2;
            log_line(g,"GUIDE green guest frame detected: serial=%llu A_to_green_ms=%llu; visual confirmation still required",
                (unsigned long long)in->guest_frame_serial,
                (unsigned long long)(in->milliseconds>=g->button_started?in->milliseconds-g->button_started:0));
        }
        if(in->pressed&CTR_GUIDE_L)finish_step(g,"FAIL","User reported a picture/touch/sound/stability problem");
        else if(type==COLOR_A && !(g->observed&2))return;
        else if((in->pressed&CTR_GUIDE_R) && (type!=VIEW_GUEST || frame_ready))
            finish_step(g,"PASS","User confirmed the displayed observation prompt");
    }
}
bool ctr_test_guide_take_restart(ctr_test_guide *g) {
    bool requested=g->restart_requested;g->restart_requested=false;return requested;
}
static void remaining(char row[72],uint32_t observed,const uint32_t *bits,const char *const *names,unsigned count) {
    size_t used=5;memcpy(row,"Need:",6);
    for(unsigned i=0;i<count;++i)if(!(observed&bits[i])) {
        int n=snprintf(row+used,72-used," %s",names[i]);
        if(n<0 || (size_t)n>=72-used)return;
        used+=(size_t)n;
    }
}
void ctr_test_guide_view(const ctr_test_guide *g,uint64_t milliseconds,char rows[7][72]) {
    memset(rows,0,7*72);unsigned count;const step *list=steps(g,&count);
    if(g->done) {
        snprintf(rows[0],72,"GUIDED TEST COMPLETE");
        snprintf(rows[1],72,"PASS %u / FAIL %u / SKIPPED %u",g->passed,g->failed,g->skipped);
        snprintf(rows[2],72,"Press MINUS to close; report is already saved.");
        snprintf(rows[3],72,"Results: SD:/CTR2WUP/RESULTS/");
        snprintf(rows[4],72,"ZIP and return the whole RESULTS folder.");
        snprintf(rows[5],72,"Filename and SD status are shown below.");
        if(g->kind==CTR_GUIDE_SAVE && !g->fresh_reload_confirmed && !g->failed && !g->skipped) {
            snprintf(rows[0],72,"SAVE CHECK: ONE FRESH LAUNCH STILL NEEDED");
            snprintf(rows[2],72,"MINUS: exit. Then open this SAME app again.");
            snprintf(rows[4],72,"Keep the SD snapshot; PLUS is only an in-app restart.");
            snprintf(rows[6],72,"Relaunch ONCE, repeat; upload BOTH reports.");
        } else if(g->kind==CTR_GUIDE_SAVE)snprintf(rows[6],72,g->fresh_reload_confirmed?
            "Fresh-launch reload confirmed; testing complete.":"Save checks failed or skipped; send the saved report.");
        return;
    }
    snprintf(rows[0],72,"GUIDED TEST %u/%u",g->step+1,count);
    snprintf(rows[1],72,"%s",list[g->step].name);
    snprintf(rows[2],72,"%s",list[g->step].line1);
    snprintf(rows[3],72,"%s",list[g->step].line2);
    if(list[g->step].type==STABILITY) {
        uint64_t elapsed=milliseconds>=g->step_started?milliseconds-g->step_started:0;
        unsigned remaining=elapsed>=600000?0:(unsigned)((600000-elapsed+999)/1000);
        snprintf(rows[4],72,"Time remaining: %u:%02u",remaining/60,remaining%60);
    } else if(list[g->step].type==BUTTONS) {
        static const uint32_t bits[]={CTR_GUIDE_A,CTR_GUIDE_B,CTR_GUIDE_X,CTR_GUIDE_Y,CTR_GUIDE_L,CTR_GUIDE_R,
            CTR_GUIDE_LEFT,CTR_GUIDE_RIGHT,CTR_GUIDE_UP,CTR_GUIDE_DOWN};
        static const char *const names[]={"A","B","X","Y","L","R","LEFT","RIGHT","UP","DOWN"};
        remaining(rows[4],g->observed,bits,names,10);
    } else if(list[g->step].type==LEFT_STICK || list[g->step].type==RIGHT_STICK) {
        static const uint32_t bits[]={1,2,4,8};
        static const char *const names[]={"LEFT","RIGHT","DOWN","UP"};
        remaining(rows[4],g->observed,bits,names,4);
    } else if(list[g->step].type==TOUCH_POINTS) {
        static const uint32_t bits[]={1,2,4,8,16};
        static const char *const names[]={"TOP-LEFT","TOP-RIGHT","BOTTOM-LEFT","BOTTOM-RIGHT","CENTER"};
        remaining(rows[4],g->observed,bits,names,5);
    } else if(list[g->step].type==COLOR_A) {
        if(!(g->observed&1))snprintf(rows[4],72,"Hold A; wait for the guest to finish its frame.");
        else if(!(g->observed&2))snprintf(rows[4],72,"A recorded. WAITING FOR GREEN; keep holding A.");
        else snprintf(rows[4],72,"NEW GREEN FRAME DETECTED. R: correct / L: problem.");
    } else if(list[g->step].type==AUDIO) {
        snprintf(rows[4],72,g->observed?"A recorded. Now confirm with R or L.":"Try A first; then confirm the observed result.");
    } else if(list[g->step].type==GUEST_RESTART || list[g->step].type==SAVE_RELOAD) {
        snprintf(rows[4],72,g->restart_waiting?"Restart recorded. Waiting for new guest PASS.":"Waiting for your PLUS press.");
    } else if(list[g->step].type==VIEW_GUEST) {
        snprintf(rows[4],72,g->observed?"Drawn picture ready. Confirm with R or L.":
            (g->kind==CTR_GUIDE_PICA || g->kind==CTR_GUIDE_GX2 || g->kind==CTR_GUIDE_TEXTURED)?"WAIT: startup image is NOT the drawn scene.":"Waiting for the first displayed scene.");
    } else snprintf(rows[4],72,"Your result will be written to the TXT report.");
    snprintf(rows[5],72,"ZL: skip this step   ZR: report a problem");
    snprintf(rows[6],72,"MINUS: finish early and preserve the report");
}
void ctr_test_guide_summary(const ctr_test_guide *g) {
    unsigned count;steps(g,&count);
    log_line(g,"GUIDE SUMMARY: state=%s passed=%u failed=%u skipped=%u not_finished=%u",
        g->done?"COMPLETE":g->cancelled?"CANCELLED":"INCOMPLETE",g->passed,g->failed,g->skipped,count-g->step);
    if(g->kind==CTR_GUIDE_SAVE)log_line(g,"Fresh-launch save reload confirmed: %s",g->fresh_reload_confirmed?"YES":"NO / NOT YET OBSERVED");
}
bool ctr_test_guide_results(const ctr_test_guide *g,char *output,size_t capacity) {
    if(!g || !output || !capacity)return false;
    unsigned count;const step *list=steps(g,&count);size_t used=0;output[0]=0;
    for(unsigned i=0;i<count;++i) {
        char outcome=g->outcomes[i];
        const char *result=outcome=='P'?"PASS":outcome=='F'?"FAIL":"NOT TESTED";
        const char *evidence=g->evidence[i]?g->evidence[i]:"Step not completed";
        int n=snprintf(output+used,capacity-used,"%u. %s: %s | %s\n",i+1,list[i].name,result,evidence);
        if(n<0 || (size_t)n>=capacity-used)return false;
        used+=(size_t)n;
    }
    return true;
}
