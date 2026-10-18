/* Installed-title frontend. Share Capture's platform/display implementation;
 * do not initialize the prototype, guest runtime, tone voice or display cache. */
#include "diagnostic_audio.h"
#include "diagnostic_display.h"
#include "m1_snapshot_probe.h"
#include "report_storage.h"
#include "test_lifecycle_wiiu.h"
#include "test_report.h"
#include "test_result.h"
#include "ctr_sha256.h"
#include <coreinit/debug.h>
#include <coreinit/foreground.h>
#include <coreinit/systeminfo.h>
#include <coreinit/title.h>
#include <vpad/input.h>
#include <errno.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>

#define TITLE_ID UINT64_C(0x0005000043543257)
#ifndef DATA_ROOT
#define DATA_ROOT "fs:/vol/external01/ctr2wup"
#endif
static ctr_test_report report;
static ctr_test_result result;
static ctr_report_storage storage={.client=-1};
static ctr_diagnostic_audio audio;
static ctr_test_lifecycle lifecycle;
static CtrM1SnapshotEvidence snapshot={.storage={.result=CTR_M1_STORAGE_ARGUMENT,
    .error_stage="not_run"},.phase="NONE"};
static atomic_bool exit_requested;
static bool foreground=true,exit_observed,io_closed,platform_failed,sd_verified;
static uint64_t session;
static uint32_t frames,input_samples,input_errors,minus_presses;
static int exit_result=-1;

static bool trace(const char *format,...) {
    char text[1024];va_list args;va_start(args,format);
    int n=vsnprintf(text,sizeof(text),format,args);va_end(args);
    if(n<0 || (size_t)n>=sizeof(text)) {
        platform_failed=true;return false;
    }
    OSReport("%s\n",text);
    return report.file && ctr_test_report_line(&report,text);
}

static void storage_line(char *line,size_t capacity) {
    const CtrM1StorageEvidence *s=&snapshot.storage;
    snprintf(line,capacity,
        "M1_STORAGE protocol=snapshot-v2 phase=%s producer_session=%016llx recovery_session=%016llx current_session=%016llx production_adapter=%u interruption_prepared=%u sentinel_written=%u sentinel_readback=%u fresh_reload=%u previous_recovered=%u pending_rejected=%u invalid_path_rejected=%u final_readback=%u result=%s errno=%d error_stage=%s writes=%u readbacks=%u recoveries=%u; software_recovery_not_power_loss=1",
        snapshot.phase,(unsigned long long)snapshot.producer_session,
        (unsigned long long)snapshot.recovery_session,(unsigned long long)snapshot.current_session,
        snapshot.production_adapter,snapshot.interruption_prepared,s->sentinel_written,
        s->sentinel_readback,s->fresh_reload,s->previous_recovered,s->pending_rejected,
        s->invalid_path_rejected,s->final_readback,ctr_m1_storage_result_name(s->result),
        s->error_code,s->error_stage?s->error_stage:"none",s->writes,s->readbacks,s->recoveries);
}

static bool write_result(const char *stage) {
    char body[CTR_TEST_RESULT_BYTES],receipt[768];storage_line(receipt,sizeof(receipt));
    int n=snprintf(body,sizeof(body),
        "Source: %s dirty=%u | Evidence schema: 2\n"
        "Stage: %s\nHost exit result: %d (-1 means still running)\n"
        "Install Check title: CTR2WUP Install Check\n"
        "Actual title ID: %016llx | Expected title ID: %016llx | Match: %u\n"
        "Session: %016llx | Protocol: install-v3\n"
        "Frames: host=%u | Input: samples=%u errors=%u Minus_presses=%u\n%s\n"
        "Native audio context: result=%s owned=%u attempts=%u successes=%u failures=%u releases=%u\n"
        "Full trace: %s | file=%s errno=%d operation=%s FSA=%d commits=%llu\n"
        "Previous result writes verified: %llu\n"
        "Independent physical SD result readback: %s\n"
        "Frontend: shared Capture display, silent AX and live ProcUI adapter; no guest execution.\n"
        "Scope: installed Menu route and production SD recovery; actual Menu return requires observation.\n",
        CTR_SOURCE_COMMIT,CTR_SOURCE_DIRTY,stage,exit_result,
        (unsigned long long)lifecycle.title_id,(unsigned long long)TITLE_ID,lifecycle.title_id==TITLE_ID,
        (unsigned long long)session,frames,input_samples,input_errors,minus_presses,receipt,
        ctr_diagnostic_audio_result_name(audio.result),audio.owned,audio.attempts,
        audio.successes,audio.failures,audio.releases,
        report.path[0]&&!report.failed?"SAVING":"FAILED",report.filename,report.error_code,
        report.error_stage?report.error_stage:"none",storage.last_status,
        (unsigned long long)report.commits,(unsigned long long)result.verified_writes,
        sd_verified?"VERIFIED":"UNCONFIRMED");
    if(n<0 || (size_t)n>=sizeof(body)) { platform_failed=true;return false; }
    return ctr_test_result_update(&result,body);
}

static bool commit(const char *stage) {
    bool ok=trace("REPORT COMMIT stage=%s sequence=%llu policy=file_sync_close_volume_sync",
        stage,(unsigned long long)(report.commits+1)) && ctr_test_report_commit(&report);
    if(!write_result(stage))ok=false;
    return ok;
}

static bool verify_physical_result(void) {
    uint8_t data[CTR_TEST_RESULT_BYTES],digest[32];
    errno=0;FILE *file=fopen(result.path,"rb");bool ok=false;int error=0;
    if(file) {
        size_t got=fread(data,1,sizeof(data),file);int end=fgetc(file);
        ok=got==sizeof(data) && end==EOF && !ferror(file);
        if(!ok)error=errno?errno:EIO;
        if(fclose(file)!=0) { if(!error)error=errno?errno:EIO;ok=false; }
    } else error=errno?errno:EIO;
    if(ok) {
        ctr_sha256 hash;ctr_sha256_init(&hash);
        ok=ctr_sha256_update(&hash,data,sizeof(data)) && ctr_sha256_final(&hash,digest);
        if(ok) {
            errno=0;
            ok=ctr_report_storage_verify(&storage,result.path,sizeof(data),digest);
            if(!ok)error=errno?errno:EIO;
        } else error=EIO;
    }
    if(!ok && !result.failed) {
        result.failed=true;result.error_code=error?error:EIO;result.error_stage="physical_readback";
    }
    trace("SD physical_result_readback verified=%u bytes=%u fsa=0x%08x errno=%d",
        ok,CTR_TEST_RESULT_BYTES,(unsigned)(uint32_t)storage.last_status,ok?0:result.error_code);
    return ok;
}

static uint32_t home_exit(void *unused) { (void)unused;exit_requested=true;return 0; }
static uint32_t save_ready(void *unused) { (void)unused;OSSavesDone_ReadyToRelease();return 0; }
static bool audio_begin(void) {
    bool ok=ctr_diagnostic_audio_begin(&audio,foreground);
    trace("AUDIO result=%s owned=%u inherited=%u resets=%u",
        ctr_diagnostic_audio_result_name(audio.result),audio.owned,audio.last_inherited,audio.inherited_resets);
    return ok;
}

static void checkpoint(void *unused,const char *message) {
    (void)unused;OSReport("%s\n",message);
    if(!io_closed && report.path[0] && !report.failed)
        ctr_test_report_checkpoint(&report,message);
    ctr_report_storage_close(&storage);
}
static void clear_callbacks(void *unused) {
    (void)unused;ProcUIClearCallbacks();
    ctr_test_report_set_volume_sync(&report,NULL,NULL);
    ctr_report_storage_close(&storage);
    ProcUISetSaveCallback(save_ready,NULL);
}
static void finish_logging(void *unused) { (void)unused;io_closed=true; }
static void show_log_error(void) {
    const char *stage=report.failed?report.error_stage:result.failed?result.error_stage:"format";
    int error=report.failed?report.error_code:result.error_code;
    char text[84];snprintf(text,sizeof(text),"ERROR %s / errno %d / FSA %ld",
        stage?stage:"unknown",error,(long)storage.last_status);
    ctr_diagnostic_display_row(6,"LOGGING ERROR - PHOTOGRAPH THIS SCREEN");
    ctr_diagnostic_display_row(7,text);
}

static void cleanup(void *unused) {
    (void)unused;
    trace("EXIT stage=guest_cleanup_begin; no guest runtime was initialized");
    trace("EXIT stage=guest_cleanup_done");
    trace("EXIT stage=audio_cleanup_begin");
    if(!ctr_diagnostic_audio_end(&audio,NULL,NULL)) { platform_failed=true;exit_result=1; }
    trace("EXIT stage=audio_cleanup_done");
    trace("EXIT stage=graphics_cleanup_begin");
    ctr_diagnostic_display_close(foreground);
    trace("EXIT stage=graphics_cleanup_done");
    trace("INSTALL SUMMARY title_id=%016llx expected_title_id=%016llx title_match=%u frames=%u elapsed_ms=%llu input_errors=%u Minus_presses=%u result=%d",
        (unsigned long long)lifecycle.title_id,(unsigned long long)TITLE_ID,lifecycle.title_id==TITLE_ID,
        frames,(unsigned long long)OSTicksToMilliseconds(OSGetTime()-session),input_errors,minus_presses,exit_result);
    if(!write_result("resources_closed_before_system_transition"))exit_result=1;
    trace("===== END OF REPORT: application resource cleanup reached; result=%d =====",exit_result);
    trace("EXIT stage=report_close_begin; launcher return is not yet established");
    if(!ctr_test_report_close(&report))exit_result=1;
    checkpoint(NULL,"EXIT stage=report_close_done; SD mount cleanup belongs to WUT");
}

static const ctr_exit_ops exit_ops={cleanup,clear_callbacks,ctr_procui_request_menu,
    ctr_procui_messages,ctr_procui_release,ctr_procui_wait,ctr_procui_shutdown,
    ctr_procui_shutdown_whb,checkpoint,finish_logging};

int main(void) {
    WHBProcInit();lifecycle.title_id=OSGetTitleID();session=OSGetTime();
    if(ctr_test_from_hbl(lifecycle.title_id))
        ProcUIRegisterCallback(PROCUI_CALLBACK_HOME_BUTTON_DENIED,home_exit,NULL,99);
    char directory[512];
    bool ready=ctr_test_report_source_directory(DATA_ROOT,CTR_SOURCE_COMMIT,directory,sizeof(directory));
    if(ready)ready=ctr_test_result_open(&result,directory,"native-install",CTR_APP_VERSION,session);
    if(ready)ready=ctr_test_report_open(&report,directory,"native-install",CTR_APP_VERSION,session);
    if(!ready && !report.failed) {
        report.failed=true;report.error_code=result.failed?result.error_code:(errno?errno:EIO);
        report.error_stage=result.failed?result.error_stage:"directory";
    }
    if(ready) {
        ctr_test_report_set_volume_sync(&report,ctr_report_storage_sync,&storage);
        ready=trace("BUILD source_commit=%s source_dirty=%u evidence_schema=2 result_bytes=%u",
            CTR_SOURCE_COMMIT,CTR_SOURCE_DIRTY,CTR_TEST_RESULT_BYTES) &&
            trace("INSTALL_SESSION ticks=%016llx source_commit=%s version=%s protocol=install-v3 title_id=%016llx expected_title_id=%016llx title_match=%u",
                (unsigned long long)session,CTR_SOURCE_COMMIT,CTR_APP_VERSION,
                (unsigned long long)lifecycle.title_id,(unsigned long long)TITLE_ID,lifecycle.title_id==TITLE_ID) &&
            trace("Platform title_id=%016llx exit_route=%s SD_owner=WUT_devoptab",
                (unsigned long long)lifecycle.title_id,ctr_test_from_hbl(lifecycle.title_id)?"HBL_relaunch":"system_menu") &&
            trace("FRONTEND shared_capture_display=1 shared_silent_AX=1 shared_ProcUI=1 prototype_guest_cache_tone=0 process_sdk=%u",
                __OSGetProcessSDKVersion());
        if(ready)ready=commit("startup");
        if(ready) {
            sd_verified=verify_physical_result();
            ready=sd_verified && commit("physical_sd_verified");
        }
    }
    bool audio_ready=audio_begin();
    ctr_diagnostic_display_labels("CTR2WUP INSTALL CHECK","MINUS: MENU / HOME: WII U MENU");
    bool display_ready=ctr_diagnostic_display_init();
    platform_failed=!audio_ready || !display_ready || !ready;
    if(ready && audio_ready && display_ready) {
        ctr_diagnostic_display_row(0,"Checking original SD recovery...");
        if(!ctr_diagnostic_display_draw())platform_failed=true;
        else ++frames;
        if(!platform_failed)ctr_m1_snapshot_probe(directory,CTR_SOURCE_COMMIT,session,&snapshot);
        char receipt[768];storage_line(receipt,sizeof(receipt));trace("%s",receipt);
        if(!commit("probe_complete"))platform_failed=true;
    }
    ctr_diagnostic_display_row(0,"INSTALLED MENU RETURN AND SD RECOVERY");
    char status_line[84];snprintf(status_line,sizeof(status_line),"TITLE %016llX / %s",
        (unsigned long long)lifecycle.title_id,lifecycle.title_id==TITLE_ID?"MATCH":"WRONG ROUTE");
    ctr_diagnostic_display_row(2,status_line);
    snprintf(status_line,sizeof(status_line),"SD %s / %s",
        ctr_m1_storage_result_name(snapshot.storage.result),snapshot.phase);
    ctr_diagnostic_display_row(4,status_line);
    ctr_diagnostic_display_row(6,!ready || !sd_verified || report.failed || result.failed?"LOGGING ERROR - PHOTOGRAPH THIS SCREEN":
        "FTP return: sd:/ctr2wup/install-check/");
    ctr_diagnostic_display_row(8,"PREPARED: MINUS / RECOVERED: HOME THEN MENU");
    if(!ready || report.failed || result.failed)show_log_error();
    else if(snapshot.storage.result!=CTR_M1_STORAGE_PASS) {
        snprintf(status_line,sizeof(status_line),"ERROR %s / errno %d / FSA %ld",
            snapshot.storage.error_stage?snapshot.storage.error_stage:"unknown",
            snapshot.storage.error_code,(long)storage.last_status);
        ctr_diagnostic_display_row(7,status_line);
    }
    uint64_t last_report=OSGetTime();ctr_proc_status previous=(ctr_proc_status)-1;
    if(!display_ready)exit_requested=true;
    else if(ctr_diagnostic_display_draw())++frames;
    else { platform_failed=true;exit_requested=true; }
    while(!exit_requested) {
        ctr_proc_status state=ctr_procui_messages(NULL);
        if(state!=previous) { trace("APP ProcUI status=%d",(int)state);previous=state; }
        bool was_foreground=foreground;
        if(state==CTR_PROC_EXITING) {
            trace("EXIT trigger=system_exit; ProcUI EXITING observed before cleanup");
            ctr_test_report_set_volume_sync(&report,NULL,NULL);ctr_report_storage_close(&storage);
            exit_observed=true;break;
        }
        if(state==CTR_PROC_RELEASE) {
            if(!ctr_diagnostic_audio_end(&audio,NULL,NULL))platform_failed=true;
            ctr_test_report_set_volume_sync(&report,NULL,NULL);ctr_report_storage_close(&storage);
            ctr_procui_release(NULL);
        }
        foreground=state==CTR_PROC_FOREGROUND;
        if(!foreground) { ctr_procui_wait(NULL);continue; }
        if(!was_foreground) {
            ctr_test_report_set_volume_sync(&report,ctr_report_storage_sync,&storage);
            ctr_diagnostic_display_invalidate();if(!audio_begin())platform_failed=true;
        }
        VPADStatus pad;VPADReadError error;
        int count=VPADRead(VPAD_CHAN_0,&pad,1,&error);
        if(count>0 && error==VPAD_READ_SUCCESS) {
            ++input_samples;
            if(pad.trigger&VPAD_BUTTON_MINUS) {
                ++minus_presses;trace("EXIT trigger=Minus; cleaning resources before system transition");break;
            }
        } else if(error!=VPAD_READ_NO_SAMPLES)++input_errors;
        if(!ctr_diagnostic_display_draw()) { platform_failed=true;break; }
        ++frames;
        if(OSTicksToMilliseconds(OSGetTime()-last_report)>=1000) {
            snprintf(status_line,sizeof(status_line),"LIVE FRAMES %u / INPUT ERRORS %u",frames,input_errors);
            ctr_diagnostic_display_row(10,status_line);
            if(ready && !commit("periodic")) { platform_failed=true;show_log_error(); }
            last_report=OSGetTime();
        }
        OSSleepTicks(OSMillisecondsToTicks(20));
    }
    exit_result=!platform_failed && ready && sd_verified && lifecycle.title_id==TITLE_ID &&
        snapshot.storage.result==CTR_M1_STORAGE_PASS && frames && input_samples && !input_errors &&
        !report.failed && !result.failed?0:1;
    if(!ctr_test_complete_exit(&lifecycle,exit_observed,&exit_ops,NULL))exit_result=1;
    ctr_report_storage_close(&storage);
    return exit_result;
}
