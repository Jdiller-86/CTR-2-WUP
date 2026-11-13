/* Continuous original guest execution; shares the accepted platform helpers. */
#include "ctr_dsp_fixture.h"
#include "ctr_dsp_stream.h"
#include "native_audio_stream.h"
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
#include <stdio.h>
#include <string.h>

#ifndef DATA_ROOT
#define DATA_ROOT "fs:/vol/external01/ctr2wup/guest-stream"
#endif
static ctr_guest guest;
static ctr_dsp_stream converter;
static ctr_native_stream stream;
static ctr_native_stream_report playback;
static ctr_diagnostic_audio audio;
static ctr_test_lifecycle lifecycle;
static ctr_test_report report;
static ctr_test_result result;
static ctr_report_storage storage={.client=-1};
static int16_t input[CTR_DSP_FRAME_SAMPLES*2],output[CTR_NATIVE_STREAM_FRAMES*2];
static uint64_t clock_ns,instructions,blocks,session;
static unsigned epochs,sequence;
static size_t output_frames;
static atomic_bool exit_requested;
static bool foreground=true,closed_io,failed,guest_open,source_done,drain_requested;
static bool paused,pause_saved,finished;
static const char *hearing="pending";

static bool save(const char *stage) {
    char text[2048];ctr_native_stream_snapshot(&stream,&playback);
    int n=snprintf(text,sizeof(text),
        "STREAM_JSON {\"schema\":1,\"source_commit\":\"%s\",\"source_dirty\":%u,"
        "\"session\":\"%016llx\",\"sequence\":%u,\"stage\":\"%s\",\"failed\":%s,"
        "\"guest_epochs\":%u,\"arm_instructions\":%llu,\"dsp_blocks\":%llu,"
        "\"resampled_frames\":%llu,\"submitted_frames\":%u,\"played_frames\":%u,"
        "\"queued_blocks\":%u,\"underruns\":%u,\"callbacks\":%u,\"paused\":%s,"
        "\"complete\":%s,\"native_result\":%u,\"hearing\":\"%s\",\"finite_prerender\":false,"
        "\"installed_exit_tested\":false}",CTR_SOURCE_COMMIT,CTR_SOURCE_DIRTY,
        (unsigned long long)session,++sequence,stage,failed?"true":"false",epochs,
        (unsigned long long)(instructions+(guest_open?guest.cpu.instructions:0)),
        (unsigned long long)blocks,(unsigned long long)converter.output_frames,
        playback.submitted_frames,playback.played_frames,playback.queued_blocks,playback.underruns,
        playback.callbacks,playback.paused?"true":"false",playback.complete?"true":"false",playback.result,hearing);
    bool ok=n>0 && (size_t)n<sizeof(text) && ctr_test_report_line(&report,text) &&
        ctr_test_result_update(&result,text) && ctr_test_report_commit(&report);
    if(!ok) { failed=true;OSReport("Guest stream logging failed; retain SD reports\n"); }
    return ok;
}
static bool open_guest(void) {
    guest_open=ctr_guest_open(&guest,&ctr_dsp_image,ctr_dsp_fixture_blocks,ctr_dsp_fixture_block_count);
    clock_ns=0;return guest_open;
}
static bool produce(void) {
    /* The audio queue controls production: at most four native blocks ahead.
     * Exact guest DSP deadlines drive virtual time, never display refreshes. */
    for(unsigned slice=0;slice<32 && !finished;++slice) {
        if(!ctr_native_stream_snapshot(&stream,&playback))return false;
        if(playback.queued_blocks>=4)return true;
        size_t count;
        if(!ctr_dsp_stream_pull(&converter,output+output_frames*2,
            CTR_NATIVE_STREAM_FRAMES-output_frames,&count))return false;
        output_frames+=count;
        if(output_frames==CTR_NATIVE_STREAM_FRAMES) {
            if(!ctr_native_stream_push(&stream,output,output_frames))return false;
            output_frames=0;continue;
        }
        if(source_done) {
            if(!ctr_dsp_stream_done(&converter))return false;
            if(output_frames && !ctr_native_stream_push(&stream,output,output_frames))return false;
            output_frames=0;finished=ctr_native_stream_finish(&stream);return finished;
        }
        ctr_guest_state state=ctr_guest_run(&guest,200000,clock_ns,true);
        if(state==CTR_GUEST_FAULTED)return false;
        while(ctr_dsp_stream_space(&converter)>=CTR_DSP_FRAME_SAMPLES && ctr_dsp_read(&guest,input,CTR_DSP_FRAME_SAMPLES*2)) {
            if(!ctr_dsp_stream_push(&converter,input,CTR_DSP_FRAME_SAMPLES))return false;
            ++blocks;
        }
        if(state==CTR_GUEST_EXITED) {
            instructions+=guest.cpu.instructions;++epochs;
            ctr_guest_close(&guest);guest_open=false;
            if(drain_requested) { source_done=true;if(!ctr_dsp_stream_finish(&converter))return false; }
            else if(!open_guest())return false;
        } else if(state==CTR_GUEST_WAITING || state==CTR_GUEST_SLEEPING) {
            uint64_t deadline;
            if(!ctr_dsp_next_deadline(&guest,&deadline) || deadline<clock_ns)return false;
            clock_ns=deadline;
        }
    }
    return true;
}
static bool begin(void) {
    ctr_native_stream_release(&stream);
    if(guest_open)ctr_guest_close(&guest);
    guest_open=false;source_done=drain_requested=paused=pause_saved=finished=false;
    output_frames=0;instructions=blocks=epochs=0;hearing="pending";
    ctr_dsp_stream_init(&converter);
    if(!open_guest() || !ctr_native_stream_open(&stream) || !produce())return false;
    if(!save("stream_requested") || !ctr_native_stream_start(&stream))return false;
    return true;
}
static uint32_t save_ready(void *unused) { (void)unused;OSSavesDone_ReadyToRelease();return 0; }
static uint32_t home_exit(void *unused) { (void)unused;exit_requested=true;return 0; }
static void checkpoint(void *unused,const char *text) {
    (void)unused;OSReport("%s\n",text);
    if(!closed_io && report.file && !report.failed)ctr_test_report_checkpoint(&report,text);
    ctr_report_storage_close(&storage);
}
static void clear_callbacks(void *unused) {
    (void)unused;ProcUIClearCallbacks();ProcUISetSaveCallback(save_ready,NULL);
    ctr_test_report_set_volume_sync(&report,NULL,NULL);ctr_report_storage_close(&storage);
}
static void finalize(void *unused) { (void)unused;closed_io=true; }
static void cleanup(void *unused) {
    (void)unused;
    if(!ctr_diagnostic_audio_end(&audio,ctr_native_stream_release,&stream))failed=true;
    if(guest_open) { instructions+=guest.cpu.instructions;ctr_guest_close(&guest);guest_open=false; }
    ctr_diagnostic_display_close(foreground);
    if(report.file && !save("resources_closed"))failed=true;
    if(!ctr_test_report_close(&report))failed=true;
}
static const ctr_exit_ops exit_ops={cleanup,clear_callbacks,ctr_procui_request_menu,ctr_procui_messages,
    ctr_procui_release,ctr_procui_wait,ctr_procui_shutdown,ctr_procui_shutdown_whb,checkpoint,finalize};
int main(void) {
    WHBProcInit();session=OSGetTime();lifecycle.title_id=OSGetTitleID();
    if(ctr_test_from_hbl(lifecycle.title_id))ProcUIRegisterCallback(PROCUI_CALLBACK_HOME_BUTTON_DENIED,home_exit,NULL,99);
    char directory[512];
    bool logging=ctr_test_report_directory(DATA_ROOT,false,directory,sizeof(directory)) &&
        ctr_test_result_open(&result,directory,"guest-stream",CTR_APP_VERSION,session) &&
        ctr_test_report_open(&report,directory,"guest-stream",CTR_APP_VERSION,session);
    ctr_test_report_set_volume_sync(&report,ctr_report_storage_sync,&storage);
    if(!logging)failed=true;
    else if(!save("startup"))failed=true;
    if(!ctr_diagnostic_audio_begin(&audio,true))failed=true;
    ctr_diagnostic_display_labels("CTR2WUP LIVE GUEST AUDIO","X: PAUSE / A: DRAIN / MINUS: EXIT");
    if(!ctr_diagnostic_display_init())failed=true;
    if(!failed && !begin())failed=true;
    bool exit_observed=false,reported_complete=false;
    while(!exit_requested) {
        ctr_proc_status status=ctr_procui_messages(NULL);
        if(status==CTR_PROC_EXITING) { exit_observed=true;break; }
        if(status==CTR_PROC_RELEASE) {
            failed=true;ctr_diagnostic_audio_end(&audio,ctr_native_stream_release,&stream);
            save("foreground_interrupted");ctr_report_storage_close(&storage);ctr_procui_release(NULL);
        }
        bool previous=foreground;foreground=status==CTR_PROC_FOREGROUND;
        if(!foreground) { ctr_procui_wait(NULL);continue; }
        if(!previous)ctr_diagnostic_display_invalidate();
        if(!failed && (!ctr_native_stream_snapshot(&stream,&playback) || (!paused && !produce())))failed=true;
        if(!failed && paused && playback.paused && !pause_saved) { pause_saved=true;save("paused"); }
        if(!failed && playback.complete && !reported_complete) { reported_complete=true;save("stream_drained"); }
        VPADStatus pad;VPADReadError error;int n=VPADRead(VPAD_CHAN_0,&pad,1,&error);
        if(n>0 && error==VPAD_READ_SUCCESS) {
            if(pad.trigger&VPAD_BUTTON_MINUS)break;
            if(!failed && !playback.complete && (pad.trigger&VPAD_BUTTON_A))drain_requested=true;
            if(!failed && !playback.complete && (pad.trigger&VPAD_BUTTON_X)) {
                if(!paused) { paused=true;pause_saved=false;if(!ctr_native_stream_pause(&stream,true))failed=true; }
                else if(playback.paused) {
                    if(!save("resume_requested") || !ctr_native_stream_pause(&stream,false))failed=true;
                    else paused=false;
                }
            }
            if(!failed && playback.complete && (pad.trigger&VPAD_BUTTON_PLUS)) {
                reported_complete=false;if(!begin())failed=true;
            }
            if(playback.complete && (pad.trigger&(VPAD_BUTTON_R|VPAD_BUTTON_L))) {
                hearing=pad.trigger&VPAD_BUTTON_L?"fail":"pass";save("hearing_recorded");exit_requested=true;
            }
        } else if(error!=VPAD_READ_NO_SAMPLES)failed=true;
        if(failed && audio.owned) {
            ctr_diagnostic_audio_end(&audio,ctr_native_stream_release,&stream);
            if(report.file)save("stream_failed");
        }
        char text[96];
        ctr_diagnostic_display_row(0,failed?"STREAM FAILED - retain reports; Minus exits":
            playback.complete?"DRAINED - PLUS replays / R correct / L wrong":paused?"PAUSED - X resumes":"LIVE ARM DSP AUDIO - A drains / X pauses");
        snprintf(text,sizeof(text),"Guest sessions %u / DSP blocks %llu / queued %u",epochs,(unsigned long long)blocks,playback.queued_blocks);
        ctr_diagnostic_display_row(2,text);
        snprintf(text,sizeof(text),"48k frames %u / underruns %u / callbacks %u",playback.played_frames,playback.underruns,playback.callbacks);
        ctr_diagnostic_display_row(3,text);
        ctr_diagnostic_display_row(5,"LEFT / RIGHT / BOTH motif repeats from live guest execution");
        ctr_diagnostic_display_row(6,report.failed || result.failed || !logging?"LOGGING ERROR - photograph this screen":"FTP return: sd:/ctr2wup/guest-stream/results/");
        ctr_diagnostic_display_row(7,result.filename);ctr_diagnostic_display_row(8,report.filename);
        if(!ctr_diagnostic_display_draw()) { failed=true;break; }
        OSSleepTicks(OSMillisecondsToTicks(2));
    }
    if(!ctr_test_complete_exit(&lifecycle,exit_observed,&exit_ops,NULL))failed=true;
    ctr_report_storage_close(&storage);return failed?1:0;
}
