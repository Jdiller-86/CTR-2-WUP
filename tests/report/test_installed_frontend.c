#define _POSIX_C_SOURCE 200809L
#include "../test_platform.h"
#include "native_platform.h"
#include <stdarg.h>

/* Real report/result I/O and real shared exit sequencing, with SDK/device doubles.
 * Each child is a fresh app process. No console return is inferred from a pass. */
static char native_root[512];
static const char *scenario;
static unsigned native_checks,polls,menu_calls,releases,shutdowns,draws,audio_ends;
static bool display_alive,callbacks_cleared,proc_alive,logging_error_shown;
static uint64_t ticks;
#define NATIVE_CHECK(c) do { ++native_checks;if(!(c)) { fprintf(stderr,"native frontend line %d: %s\n",__LINE__,#c);exit(1); } } while(0)
#define DATA_ROOT native_root
#define CTR_SOURCE_COMMIT "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define CTR_SOURCE_DIRTY 0
#define CTR_APP_VERSION "0.27.0"
#define ctr_report_storage_sync native_storage_sync
#define ctr_report_storage_close native_storage_close
#define ctr_report_storage_verify native_storage_verify
#define ctr_m1_snapshot_probe native_snapshot_probe
#define ctr_m1_storage_result_name native_storage_result_name
#define main native_app_entry
#include "../../runtime/wiiu/install_check.c"
#undef main

void OSReport(const char *format,...) { (void)format; }
uint64_t OSGetTime(void) { ticks+=100;return ticks; }
uint64_t OSGetTitleID(void) { return !strcmp(scenario,"wrong-route")?1:TITLE_ID; }
uint32_t __OSGetProcessSDKVersion(void) { return 0; }
uint64_t OSTicksToMilliseconds(uint64_t value) { return value; }
uint64_t OSMillisecondsToTicks(uint64_t value) { return value; }
void OSSleepTicks(uint64_t value) { ticks+=value; }
void OSSavesDone_ReadyToRelease(void) {}
void WHBProcInit(void) { NATIVE_CHECK(!proc_alive);proc_alive=true; }
void WHBProcShutdown(void) { NATIVE_CHECK(!proc_alive && shutdowns==1); }
void SYSLaunchMenu(void) {
    NATIVE_CHECK(!display_alive && !audio.owned && callbacks_cleared && storage.client==-1 && io_closed);
    ++menu_calls;
}
ProcUIStatus ProcUIProcessMessages(int blocking) {
    NATIVE_CHECK(proc_alive && blocking);++polls;
    if(menu_calls) {
        if(releases==0)return PROCUI_STATUS_RELEASE_FOREGROUND;
        return PROCUI_STATUS_EXITING;
    }
    if(!strcmp(scenario,"home")) {
        if(polls==1)return PROCUI_STATUS_IN_FOREGROUND;
        if(polls==2)return PROCUI_STATUS_RELEASE_FOREGROUND;
        if(polls==3)return PROCUI_STATUS_IN_BACKGROUND;
        return PROCUI_STATUS_EXITING;
    }
    if(!strcmp(scenario,"reacquire") && (polls==2 || polls==3))
        return polls==2?PROCUI_STATUS_RELEASE_FOREGROUND:PROCUI_STATUS_IN_BACKGROUND;
    return PROCUI_STATUS_IN_FOREGROUND;
}
void ProcUIDrawDoneRelease(void) { NATIVE_CHECK(!audio.owned);++releases; }
void ProcUIShutdown(void) {
    NATIVE_CHECK(proc_alive && !display_alive && !audio.owned && callbacks_cleared && !report.file && io_closed);
    NATIVE_CHECK(storage.client==-1);proc_alive=false;++shutdowns;
}
void ProcUIClearCallbacks(void) { NATIVE_CHECK(!display_alive);callbacks_cleared=true; }
void ProcUISetSaveCallback(uint32_t (*callback)(void *),void *context) { NATIVE_CHECK(callback(context)==0); }
void ProcUIRegisterCallback(int event,uint32_t (*callback)(void *),void *context,int priority) {
    (void)event;(void)callback;(void)context;(void)priority;
}
int VPADRead(int channel,VPADStatus *pad,int count,VPADReadError *error) {
    NATIVE_CHECK(channel==0 && count==1 && foreground);
    if(!strcmp(scenario,"input-error")) { *error=-2;exit_requested=true;return -1; }
    *error=VPAD_READ_SUCCESS;pad->trigger=0;
    if(strcmp(scenario,"home") && (strcmp(scenario,"reacquire") || polls>=4))pad->trigger=VPAD_BUTTON_MINUS;
    return 1;
}
bool ctr_diagnostic_audio_begin(ctr_diagnostic_audio *a,bool fg) {
    NATIVE_CHECK(fg && proc_alive);a->owned=true;a->result=CTR_AUDIO_READY;++a->attempts;++a->successes;return true;
}
bool ctr_diagnostic_audio_end(ctr_diagnostic_audio *a,void (*voices)(void *),void *context) {
    NATIVE_CHECK(voices==NULL && context==NULL);
    if(a->owned) { a->owned=false;++a->releases;++audio_ends; }return true;
}
const char *ctr_diagnostic_audio_result_name(ctr_diagnostic_audio_result state) { (void)state;return "READY"; }
void ctr_diagnostic_display_labels(const char *title,const char *hint) { NATIVE_CHECK(title && hint); }
bool ctr_diagnostic_display_init(void) { NATIVE_CHECK(audio.owned && !display_alive);display_alive=true;return true; }
bool ctr_diagnostic_display_row(unsigned row,const char *text) {
    NATIVE_CHECK(row<14 && text);
    if(row==6 && strstr(text,"LOGGING ERROR"))logging_error_shown=true;
    return true;
}
bool ctr_diagnostic_display_draw(void) { NATIVE_CHECK(display_alive && audio.owned && foreground);++draws;return true; }
void ctr_diagnostic_display_invalidate(void) { NATIVE_CHECK(display_alive); }
void ctr_diagnostic_display_close(bool fg) { (void)fg;NATIVE_CHECK(!audio.owned);display_alive=false; }
bool native_storage_sync(void *context) {
    ctr_report_storage *s=context;
    NATIVE_CHECK(!io_closed && foreground && proc_alive);s->client=7;
    if(!strcmp(scenario,"commit-error")) { errno=EIO;s->last_status=-9;return false; }
    return true;
}
void native_storage_close(ctr_report_storage *s) { s->client=-1; }
bool native_storage_verify(void *context,const char *path,uint64_t bytes,const uint8_t digest[32]) {
    ctr_report_storage *s=context;uint8_t data[CTR_TEST_RESULT_BYTES],actual[32];
    NATIVE_CHECK(!io_closed && foreground && bytes==sizeof(data));
    FILE *f=fopen(path,"rb");NATIVE_CHECK(f!=NULL);
    NATIVE_CHECK(fread(data,1,sizeof(data),f)==sizeof(data) && fclose(f)==0);
    ctr_sha256 hash;ctr_sha256_init(&hash);
    NATIVE_CHECK(ctr_sha256_update(&hash,data,sizeof(data)) && ctr_sha256_final(&hash,actual));
    NATIVE_CHECK(!memcmp(actual,digest,32));
    if(!strcmp(scenario,"physical-error")) { s->last_status=-6;errno=EIO;return false; }
    return true;
}
const char *native_storage_result_name(CtrM1StorageResult state) { return state==CTR_M1_STORAGE_PASS?"PASS":"ARGUMENT"; }
bool native_snapshot_probe(const char *directory,const char *source,uint64_t id,CtrM1SnapshotEvidence *out) {
    NATIVE_CHECK(directory && strlen(source)==40 && display_alive && audio.owned && frames==1);
    *out=(CtrM1SnapshotEvidence){.phase="PREPARED",.current_session=id,.producer_session=id,
        .production_adapter=true,.interruption_prepared=true,
        .storage={.result=CTR_M1_STORAGE_PASS,.sentinel_written=true,.sentinel_readback=true,
            .invalid_path_rejected=true,.error_stage="none",.writes=2,.readbacks=5}};
    if(!strcmp(scenario,"home")) {
        out->phase="RECOVERED";out->producer_session=100;out->recovery_session=id;
        out->storage.fresh_reload=true;out->storage.previous_recovered=true;
        out->storage.pending_rejected=true;out->storage.final_readback=true;
        out->storage.writes=1;out->storage.recoveries=1;
    }
    if(!strcmp(scenario,"probe-error")) { out->storage.result=CTR_M1_STORAGE_PATH;return false; }
    return true;
}

int test_installed_frontend_child(const char *mode,const char *directory) {
    scenario=mode+7;NATIVE_CHECK(!strncmp(mode,"native-",7));
    if(!strcmp(scenario,"home"))ticks=100;
    NATIVE_CHECK(snprintf(native_root,sizeof(native_root),"%s/data",directory)>0);
    if(!strcmp(scenario,"logging-error")) { FILE *f=fopen(native_root,"wb");NATIVE_CHECK(f!=NULL);fclose(f); }
    int returned=native_app_entry();
    bool expected_failure=!strcmp(scenario,"input-error") || !strcmp(scenario,"probe-error") || !strcmp(scenario,"commit-error") || !strcmp(scenario,"physical-error") ||
        !strcmp(scenario,"logging-error") || !strcmp(scenario,"wrong-route");
    NATIVE_CHECK(returned==(expected_failure?1:0));
    NATIVE_CHECK(shutdowns==1 && !proc_alive && !display_alive && !audio.owned && audio_ends>=1);
    NATIVE_CHECK(io_closed && storage.client==-1 && !report.file && draws>=1);
    NATIVE_CHECK(menu_calls==(!strcmp(scenario,"home")?0u:1u));
    if(strcmp(scenario,"logging-error")) {
        FILE *f=fopen(result.path,"rb");NATIVE_CHECK(f!=NULL);
        char text[CTR_TEST_RESULT_BYTES+1];size_t bytes=fread(text,1,sizeof(text)-1,f);text[bytes]=0;
        NATIVE_CHECK(bytes==CTR_TEST_RESULT_BYTES && fclose(f)==0);
        if(strcmp(scenario,"physical-error")) {
            NATIVE_CHECK(strstr(text,"Stage: resources_closed_before_system_transition\n"));
            NATIVE_CHECK(strstr(text,expected_failure?"Host exit result: 1 ":"Host exit result: 0 "));
        } else {
            NATIVE_CHECK(result.failed && !sd_verified && platform_failed && logging_error_shown);
            NATIVE_CHECK(strstr(text,"Host exit result: -1 ") && !strstr(text,"readback: VERIFIED"));
        }
        NATIVE_CHECK(result.verified_writes>=(!strcmp(scenario,"physical-error")?2u:3u));
        f=fopen(report.path,"rb");NATIVE_CHECK(f!=NULL);
        char trace_text[12000];bytes=fread(trace_text,1,sizeof(trace_text)-1,f);trace_text[bytes]=0;NATIVE_CHECK(fclose(f)==0);
        char *barrier=strstr(trace_text,"EXIT stage=runtime_io_finalize_begin");
        if(!strcmp(scenario,"commit-error")) {
            NATIVE_CHECK(!barrier && report.failed && report.error_code==EIO && storage.last_status==-9);
            NATIVE_CHECK(strstr(text,"Full trace: FAILED"));
        } else {
            NATIVE_CHECK(barrier);
            NATIVE_CHECK(!strstr(barrier,"EXIT stage=request_menu_begin") && !strstr(barrier,"EXIT stage=procui_shutdown_begin"));
        }
        if(!strcmp(scenario,"home")) {
            NATIVE_CHECK(strstr(trace_text,"APP ProcUI status=3") && strstr(trace_text,"EXIT trigger=system_exit"));
            NATIVE_CHECK(strstr(text,"phase=RECOVERED") && strstr(text,"fresh_reload=1"));
        }
        NATIVE_CHECK(ctr_test_unlink(result.path)==0 && ctr_test_unlink(report.path)==0);
        char source_dir[512],parent[512];NATIVE_CHECK(ctr_test_report_source_directory(native_root,CTR_SOURCE_COMMIT,source_dir,sizeof(source_dir)));
        NATIVE_CHECK(ctr_test_remove_directory(source_dir)==0);
        NATIVE_CHECK(snprintf(parent,sizeof(parent),"%s/install-check",native_root)>0);
        NATIVE_CHECK(ctr_test_remove_directory(parent)==0 && ctr_test_remove_directory(native_root)==0);
    } else NATIVE_CHECK(ctr_test_unlink(native_root)==0 && report.failed && report.error_code!=0);
    printf("Native frontend scenario=%s checks=%u (SDK doubles, not console acceptance)\n",scenario,native_checks);
    return 0;
}

unsigned test_installed_frontend(void) {
    const char *modes[]={"native-minus","native-home","native-reacquire","native-input-error", "native-probe-error","native-logging-error","native-commit-error","native-physical-error","native-wrong-route"};
    unsigned checks=0;
    for(unsigned i=0;i<sizeof(modes)/sizeof(modes[0]);++i) {
        char directory[512];if(!ctr_test_temp_directory(directory,sizeof(directory),"ctr-native"))abort();
        ctr_test_child child;unsigned status;
        if(!ctr_test_child_start(&child,modes[i],directory,test_installed_frontend_child) ||
           !ctr_test_child_wait(&child,&status) || status!=0)abort();
        if(ctr_test_remove_directory(directory)!=0)abort();
        ++checks;
    }
    return checks;
}
