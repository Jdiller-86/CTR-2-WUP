/* Focused original ARM DSP round. Reuses Capture's display/AX/ProcUI/storage. */
#include "ctr_dsp_fixture.h"
#include "ctr_sha256.h"
#include "retained_logs.h"
#include "audio_stream_diagnostic.h"
#include "diagnostic_audio.h"
#include "diagnostic_display.h"
#include "report_storage.h"
#include "test_lifecycle_wiiu.h"
#include "test_report.h"
#include "test_result.h"
#include <coreinit/debug.h>
#include <coreinit/foreground.h>
#include <coreinit/title.h>
#include <vpad/input.h>
#include <stdatomic.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#ifndef DATA_ROOT
#define DATA_ROOT "fs:/vol/external01/ctr2wup/dsp-check"
#endif
static ctr_test_report report;
static ctr_test_result result;
static ctr_report_storage storage={.client=-1};
static ctr_diagnostic_audio audio;
static ctr_native_pcm native;
static ctr_native_pcm_report playback;
static ctr_test_lifecycle lifecycle;
static ctr_dsp_statistics dsp;
static ctr_retained_logs retained;
static int16_t original[96000],translated[96000],pcm[CTR_NATIVE_PCM_MAX_FRAMES*2];
static uint64_t session,instructions,started,pause_started;
static size_t frames;
static uint32_t held[2],sequence;
static char hash[65];
static const char *hearing="pending";
static const char *probe_phase="not_run";
static const char *logging_stage;
static int logging_errno;
static int32_t logging_fsa;
static bool sd_verified;
static atomic_bool exit_requested;
static bool foreground=true,closed_io,exit_observed,failed,core_ok,paused,resumed,held_ok,complete,failure_saved;

static void logging_failed(const char *stage,int error) {
    failed=true;sd_verified=false;
    if(!logging_stage) { logging_stage=stage;logging_errno=error?error:EIO;logging_fsa=storage.last_status; }
    OSReport("DSP logging failed stage=%s errno=%d FSA=%ld\n",logging_stage,logging_errno,(long)logging_fsa);
}

static bool save(const char *stage) {
    char body[3072];++sequence;
    if(native.report.prepared && !ctr_native_pcm_snapshot(&native,&playback))failed=true;
    int n=snprintf(body,sizeof(body),
        "DSP_JSON {\"schema\":1,\"kind\":\"ctr2wup-guest-dsp\",\"source_commit\":\"%s\",\"source_dirty\":%u,"
        "\"session\":\"%016llx\",\"sequence\":%u,\"stage\":\"%s\",\"core_ok\":%s,\"failed\":%s,"
        "\"arm_instructions\":%llu,\"dsp_blocks\":%llu,\"dsp_samples\":%llu,\"queued\":%llu,\"loops\":%llu,"
        "\"probe_phase\":\"%s\",\"guest_pc\":%u,\"guest_ipc_header\":%u,\"guest_fault\":%u,\"guest_fault_detail\":%u,"
        "\"output_frames\":%u,\"pcm_sha256\":\"%s\",\"pause\":%s,\"resume\":%s,\"pause_cursor_held\":%s,"
        "\"native_complete\":%s,\"hearing\":\"%s\",\"native_failures\":%u,\"native_releases\":%u,\"ax_owned\":%s,"
        "\"native_status\":%u,\"native_result\":%u,\"cursor\":[%u,%u],\"logging_stage\":\"%s\",\"logging_errno\":%d,\"logging_fsa\":%ld,\"finite_prerender\":true,\"installed_exit_tested\":false,\"system_logs_present\":%s,\"system_logs_complete\":%s,\"system_log_files\":%u,\"system_log_bytes\":%llu,\"system_logs_error\":%d}",
        CTR_SOURCE_COMMIT,CTR_SOURCE_DIRTY,(unsigned long long)session,sequence,stage,core_ok?"true":"false",failed?"true":"false",
        (unsigned long long)instructions,(unsigned long long)dsp.frames,(unsigned long long)dsp.samples,
        (unsigned long long)dsp.queued,(unsigned long long)dsp.loops,probe_phase,dsp.guest_pc,dsp.guest_ipc_header,dsp.guest_fault,dsp.guest_fault_detail,(unsigned)frames,hash,
        paused?"true":"false",resumed?"true":"false",held_ok?"true":"false",complete?"true":"false",hearing,
        native.report.failures,native.report.releases,audio.owned?"true":"false",playback.status,playback.result,
        playback.cursor[0],playback.cursor[1],logging_stage?logging_stage:"none",logging_errno,(long)logging_fsa,
        retained.present?"true":"false",retained.complete?"true":"false",retained.files,(unsigned long long)retained.bytes,retained.error);
    bool ok=n>0 && (size_t)n<sizeof(body) && ctr_test_report_line(&report,body) &&
        ctr_test_result_update(&result,body) && ctr_test_report_commit(&report);
    if(ok) {
        uint8_t bytes[CTR_TEST_RESULT_BYTES],digest[32];FILE *file=fopen(result.path,"rb");
        ok=file!=NULL;
        if(file) { ok=fread(bytes,1,sizeof(bytes),file)==sizeof(bytes) && fgetc(file)==EOF && !ferror(file);if(fclose(file)!=0)ok=false; }
        if(ok) { ctr_sha256 sha;ctr_sha256_init(&sha);ok=ctr_sha256_update(&sha,bytes,sizeof(bytes)) && ctr_sha256_final(&sha,digest);ok=ok && ctr_report_storage_verify(&storage,result.path,sizeof(bytes),digest); }
    }
    if(!ok)logging_failed(report.failed?report.error_stage:result.failed?result.error_stage:
        n<0 || (size_t)n>=sizeof(body)?"format":"physical_readback",
        report.failed?report.error_code:result.failed?result.error_code:errno);
    else sd_verified=true;
    return ok;
}
static void show_storage(void) {
    if(logging_stage || !sd_verified) {
        char error_line[84];snprintf(error_line,sizeof(error_line),"%s / errno %d / FSA %ld",
            logging_stage?logging_stage:"unconfirmed",logging_errno,(long)logging_fsa);
        ctr_diagnostic_display_row(6,"LOGGING ERROR - PHOTOGRAPH THIS SCREEN");
        ctr_diagnostic_display_row(7,error_line);
        ctr_diagnostic_display_row(8,"Return folder is UNCONFIRMED; keep any existing files");
    } else {
        ctr_diagnostic_display_row(6,"FTP return: sd:/ctr2wup/dsp-check/");
        ctr_diagnostic_display_row(7,result.filename);
        ctr_diagnostic_display_row(8,report.filename);
    }
}
static uint32_t save_ready(void *unused) { (void)unused;OSSavesDone_ReadyToRelease();return 0; }
static uint32_t home_exit(void *unused) { (void)unused;exit_requested=true;return 0; }
static void checkpoint(void *unused,const char *text) {
    (void)unused;OSReport("%s\n",text);
    if(!closed_io && report.path[0] && !report.failed)ctr_test_report_checkpoint(&report,text);
    ctr_report_storage_close(&storage);
}
static void clear_callbacks(void *unused) {
    (void)unused;ProcUIClearCallbacks();ProcUISetSaveCallback(save_ready,NULL);
    ctr_test_report_set_volume_sync(&report,NULL,NULL);ctr_report_storage_close(&storage);
}
static void finalize(void *unused) { (void)unused;closed_io=true; }
static void cleanup(void *unused) {
    (void)unused;
    if(!ctr_diagnostic_audio_end(&audio,ctr_native_pcm_release,&native))failed=true;
    ctr_diagnostic_display_close(foreground);
    if(!save("resources_closed"))failed=true;
    if(!ctr_test_report_close(&report))failed=true;
}
static const ctr_exit_ops exit_ops={cleanup,clear_callbacks,ctr_procui_request_menu,ctr_procui_messages,
    ctr_procui_release,ctr_procui_wait,ctr_procui_shutdown,ctr_procui_shutdown_whb,checkpoint,finalize};
static bool render(void) {
    ctr_dsp_statistics other={0};uint64_t count=0;
    probe_phase="interpreter";
    if(!ctr_dsp_fixture_render(false,original,96000,&dsp,&instructions))return false;
    probe_phase="aot";
    if(!ctr_dsp_fixture_render(true,translated,96000,&other,&count)) { dsp=other;instructions=count;return false; }
    probe_phase="comparison";
    if(count!=instructions ||
       memcmp(original,translated,sizeof(original)) || dsp.frames!=300 || dsp.samples!=48000 || dsp.queued!=2 || dsp.loops!=4)return false;
    for(unsigned i=0;i<48000;++i) {
        unsigned word=8192-(i%16384)/2;
        if(original[i*2]!=(i<16000 || i>=32000?((word&32)?-6000:6000):0) ||
           original[i*2+1]!=(i>=16000?((word&16)?-6000:6000):0))return false;
    }
    if(!ctr_dsp_pcm48k(original,48000,pcm,CTR_NATIVE_PCM_MAX_FRAMES,&frames))return false;
    /* Canonical little-endian hash, independent of Espresso's byte order. */
    ctr_sha256 sha;ctr_sha256_init(&sha);
    for(size_t i=0;i<frames*2;++i) { uint16_t v=(uint16_t)pcm[i];uint8_t bytes[2]={(uint8_t)v,(uint8_t)(v>>8)};ctr_sha256_update(&sha,bytes,2); }
    uint8_t digest[32];ctr_sha256_final(&sha,digest);
    for(unsigned i=0;i<32;++i)snprintf(hash+i*2,3,"%02x",digest[i]);
    probe_phase="complete";return true;
}
int main(void) {
    WHBProcInit();session=OSGetTime();lifecycle.title_id=OSGetTitleID();
    if(ctr_test_from_hbl(lifecycle.title_id))ProcUIRegisterCallback(PROCUI_CALLBACK_HOME_BUTTON_DENIED,home_exit,NULL,99);
    char directory[512];
    bool logging=ctr_test_report_directory(DATA_ROOT,false,directory,sizeof(directory)) &&
        ctr_test_result_open(&result,directory,"guest-dsp",CTR_APP_VERSION,session) &&
        ctr_test_report_open(&report,directory,"guest-dsp",CTR_APP_VERSION,session);
    ctr_test_report_set_volume_sync(&report,ctr_report_storage_sync,&storage);
    if(!logging)logging_failed(result.failed?result.error_stage:report.failed?report.error_stage:"directory",
        result.failed?result.error_code:report.failed?report.error_code:errno);
    else if(!save("startup"))failed=true;
    if(!ctr_diagnostic_audio_begin(&audio,true))failed=true;
    ctr_diagnostic_display_labels("CTR2WUP GUEST AUDIO","MINUS: EXIT / PLUS: REPLAY");
    if(!ctr_diagnostic_display_init())failed=true;
    ctr_diagnostic_display_row(0,"AUTOMATIC GUEST DSP / AUDIO CHECK");
    ctr_diagnostic_display_row(2,"Generating original audio through ARM DSP IPC...");
    ctr_diagnostic_display_draw();
    if(logging) {
        char destination[768];int n=snprintf(destination,sizeof(destination),"%s/system-logs-%016llx",directory,(unsigned long long)session);
        if(n>0 && (size_t)n<sizeof(destination))retained=ctr_retained_logs_copy("fs:/vol/external01/wiiu/crash_logs",destination);
        if(!save("retained_logs_collected"))failed=true;
    }
    if(!failed) {
        core_ok=render();if(!core_ok)failed=true;
        if(!save("guest_rendered"))failed=true;
    }
    if(!failed && !ctr_native_pcm_prepare(&native,pcm,frames))failed=true;
    /* Persist intent while AX is idle. SD commits may outlast a finite clip;
     * no successful-playback logging may delay the automatic pause or polling. */
    if(!failed && !save("play_requested"))failed=true;
    if(!failed && !ctr_native_pcm_play(&native))failed=true;
    started=OSGetTime();
    ctr_diagnostic_display_row(2,"Listen: LEFT low / RIGHT high / BOTH");
    ctr_diagnostic_display_row(3,"A short pause/resume happens automatically");
    show_storage();if(!ctr_diagnostic_display_draw())failed=true;
    while(!exit_requested) {
        ctr_proc_status state=ctr_procui_messages(NULL);
        if(state==CTR_PROC_EXITING) { exit_observed=true;break; }
        if(state==CTR_PROC_RELEASE) {
            failed=true;ctr_diagnostic_audio_end(&audio,ctr_native_pcm_release,&native);save("foreground_interrupted");
            ctr_report_storage_close(&storage);ctr_procui_release(NULL);
        }
        bool previous=foreground;foreground=state==CTR_PROC_FOREGROUND;
        if(!foreground) { ctr_procui_wait(NULL);continue; }
        if(!previous) { ctr_diagnostic_display_invalidate();if(!ctr_diagnostic_audio_begin(&audio,true))failed=true; }
        if(native.report.prepared && !ctr_native_pcm_snapshot(&native,&playback))failed=true;
        uint64_t now=OSGetTime();
        if(!failed && !paused && OSTicksToMilliseconds(now-started)>=300) {
            if(!ctr_native_pcm_pause(&native) || !ctr_native_pcm_snapshot(&native,&playback))failed=true;
            else { paused=true;pause_started=now;held[0]=playback.cursor[0];held[1]=playback.cursor[1]; }
            save("paused");
        }
        if(!failed && paused && !resumed && OSTicksToMilliseconds(now-pause_started)>=500) {
            held_ok=playback.cursor[0]==held[0] && playback.cursor[1]==held[1];
            if(!held_ok)failed=true;
            if(!save("resume_requested"))failed=true;
            if(!failed && !ctr_native_pcm_resume(&native))failed=true;
            else if(!failed) {
                resumed=true;now=OSGetTime();started+=now-pause_started;
            }
        }
        if(!failed && !complete && playback.status==CTR_NATIVE_PCM_COMPLETE) { complete=true;save("playback_complete"); }
        if(!complete && OSTicksToMilliseconds(now-started)>10000 && !failed) { failed=true;save("playback_timeout"); }
        VPADStatus pad;VPADReadError error;int n=VPADRead(VPAD_CHAN_0,&pad,1,&error);
        if(n>0 && error==VPAD_READ_SUCCESS) {
            if(pad.trigger&VPAD_BUTTON_MINUS)break;
            if(complete && (pad.trigger&(VPAD_BUTTON_R|VPAD_BUTTON_L))) { hearing=pad.trigger&VPAD_BUTTON_L?"fail":"pass";save("hearing_recorded");exit_requested=true; }
            if((complete || failed) && (pad.trigger&VPAD_BUTTON_PLUS) && core_ok && audio.owned) {
                /* A fresh immutable playback attempt; prior failures stay in
                 * the trace. Never auto-promote them to a successful run. */
                if(!failed) {
                    complete=paused=resumed=held_ok=false;hearing="pending";
                    if(!save("replay_requested") || !ctr_native_pcm_play(&native))failed=true;
                    started=OSGetTime();
                }
            }
        } else if(error!=VPAD_READ_NO_SAMPLES) { failed=true;save("input_failed"); }
        if(failed && !failure_saved) { failure_saved=true;save("failed"); }
        ctr_diagnostic_display_row(5,failed?"CHECK FAILED - return logs; no repeated test needed":
            complete?"R: correct on TV + GamePad / L: wrong or missing":"PLAYING - no button presses needed");
        show_storage();
        if(!ctr_diagnostic_display_draw()) { failed=true;break; }
        OSSleepTicks(OSMillisecondsToTicks(10));
    }
    if(!ctr_test_complete_exit(&lifecycle,exit_observed,&exit_ops,NULL))failed=true;
    ctr_report_storage_close(&storage);return failed?1:0;
}
