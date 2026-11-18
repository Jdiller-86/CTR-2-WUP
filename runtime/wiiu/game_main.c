/* Experimental per-title runner using the shared runtime/platform owners. */
#include "game_image.h"
#include "ctr_apt.h"
#include "ctr_dsp.h"
#include "ctr_dsp_stream.h"
#include "ctr_fs_snapshot.h"
#include "ctr_gsp.h"
#include "ctr_hid.h"
#include "ctr_recomp.h"
#include "ctr_romfs_file.h"
#include "ctr_sha256.h"
#include "diagnostic_audio.h"
#include "diagnostic_display.h"
#include "guest_slice.h"
#include "native_audio_stream.h"
#include "report_storage.h"
#include "test_lifecycle_wiiu.h"
#include "test_report.h"
#include <coreinit/debug.h>
#include <coreinit/foreground.h>
#include <coreinit/title.h>
#include <vpad/input.h>
#include <errno.h>
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static ctr_guest guest;
static ctr_fs_volume savedata,sdmc;
static ctr_romfs_file romfs;
static ctr_test_report report;
static ctr_report_storage storage={.client=-1};
static ctr_test_lifecycle lifecycle;
static ctr_diagnostic_audio audio;
static ctr_native_stream stream;
static ctr_dsp_stream converter;
static uint32_t top[400u*240u],bottom[320u*240u];
static int16_t dsp_pcm[CTR_DSP_FRAME_SAMPLES*2],pcm[CTR_NATIVE_STREAM_FRAMES*2];
static size_t pcm_frames;
static uint64_t session,clock_ns,last_status_us,frames;
static bool opened,save_ready,sd_ready,foreground=true,closed_io,stream_started;
static const char *failure;
static atomic_bool exit_requested;

static bool directory(const char *path) {
    struct stat info;
    if(mkdir(path,0777)==0)return true;
    return errno==EEXIST && stat(path,&info)==0 && S_ISDIR(info.st_mode);
}
static bool status_report(const char *stage) {
    if(closed_io || !report.file || report.failed)return false;
    char line[1536],service[sizeof(guest.ipc_service)];
    for(size_t i=0;i<sizeof(service);++i) {
        unsigned char c=(unsigned char)guest.ipc_service[i];
        service[i]=(c=='"' || c=='\\' || (c && (c<32 || c>126)))?'?':(char)c;
    }
    service[sizeof(service)-1]=0;
    snprintf(line,sizeof(line),
        "GAME_JSON {\"schema_version\":1,\"stage\":\"%s\",\"source\":\"%s\","
        "\"program_id\":\"%s\",\"revision\":\"%s\",\"state\":\"%s\","
        "\"instructions\":%llu,\"fallback_instructions\":%llu,\"frames\":%llu,"
        "\"pc\":%u,\"fault\":%u,\"detail\":%u,\"svc\":%u,"
        "\"ipc_service\":\"%s\",\"ipc_header\":%u,\"ipc_result\":%u,"
        "\"pica_result\":%u,\"scene_result\":%u,\"renderer\":\"software\",\"gameplay_verified\":false}",
        stage,CTR_SOURCE_COMMIT,CTR_GAME_PROGRAM_ID,CTR_GAME_REVISION,ctr_guest_state_name(guest.state),
        (unsigned long long)guest.cpu.instructions,(unsigned long long)guest.cpu.fallback_instructions,
        (unsigned long long)frames,guest.cpu.r[15],guest.fault,guest.fault_detail,guest.cpu.svc_number,
        service,guest.ipc_header,guest.ipc_result,guest.gsp.last_pica_result,guest.gsp.last_scene_result);
    return ctr_test_report_line(&report,line) && ctr_test_report_commit(&report);
}
static void fail(const char *message) {
    if(!failure) {
        failure=message;
        char line[256];snprintf(line,sizeof(line),"STOP %s errno=%d",message,errno);
        OSReport("CTR2WUP game: %s\n",line);
        if(!closed_io && report.file && !report.failed)ctr_test_report_line(&report,line);
        status_report("stopped");
    }
}
static void trace(void *context,const char *event,const char *module,uint32_t pc,bool thumb,uint32_t detail) {
    (void)context;(void)module;
    /* Fallback remains counted, without logging every recovered instruction. */
    if(!strcmp(event,"missing-block") || !strcmp(event,"code-invalidated"))return;
    char line[256];snprintf(line,sizeof(line),"CPU event=%s pc=%08x thumb=%u detail=%08x",event,pc,thumb,detail);
    if(report.file && !ctr_test_report_line(&report,line))fail("Private runtime log failed");
}
static void debug(void *context,const char *message) {
    (void)context;
    char line[256];snprintf(line,sizeof(line),"GUEST %.240s",message);
    if(report.file && !ctr_test_report_line(&report,line))fail("Private runtime log failed");
}
static void *load_code(void) {
    const size_t size=ctr_game_image.code_size;
    if(!size || size>CTR_GUEST_MAX_BYTES) { errno=EINVAL;return NULL; }
    FILE *file=fopen("fs:/vol/content/guest-code.bin","rb");if(!file)return NULL;
    uint8_t *bytes=malloc(size),digest[32];ctr_sha256 hash;ctr_sha256_init(&hash);
    bool ok=bytes && fread(bytes,1,size,file)==size && fgetc(file)==EOF && !ferror(file);
    if(ok)ok=ctr_sha256_update(&hash,bytes,size) && ctr_sha256_final(&hash,digest) && !memcmp(digest,ctr_game_code_sha256,32);
    if(fclose(file)!=0)ok=false;
    if(!ok) { free(bytes);errno=EIO;return NULL; }return bytes;
}
static bool start_guest(void) {
    void *code=load_code();if(!code)return false;
    ctr_guest_image image=ctr_game_image;image.code=code;
    opened=ctr_guest_open(&guest,&image,NULL,0);free(code);
    if(!opened || !ctr_recomp_attach(&guest,true))return false;
    guest.cpu.log=trace;guest.debug=debug;
    if(!ctr_apt_configure(&guest))return false;
    save_ready=ctr_fs_volume_init(&savedata,4,true);
    if(!save_ready || ctr_fs_snapshot_load(&savedata,CTR_GAME_DATA_ROOT "/save.snapshot")==CTR_FS_SNAPSHOT_ERROR)return false;
    savedata.persist=ctr_fs_snapshot_save;savedata.persist_context=CTR_GAME_DATA_ROOT "/save.snapshot";
    sd_ready=ctr_fs_volume_init(&sdmc,9,false);
    if(!sd_ready || !ctr_fs_mount(&guest,&savedata) || !ctr_fs_mount(&guest,&sdmc))return false;
#if CTR_GAME_HAS_ROMFS
    ctr_diagnostic_display_row(0,"Verifying installed RomFS...");ctr_diagnostic_display_draw();
    if(!ctr_romfs_file_open(&romfs,"fs:/vol/content/romfs.bin",ctr_game_romfs_sha256) ||
        !ctr_fs_mount_romfs(&guest,&romfs.view))return false;
#endif
    return true;
}
static bool open_audio(void) {
    ctr_dsp_stream_init(&converter);pcm_frames=0;stream_started=false;
    return ctr_diagnostic_audio_begin(&audio,true) && ctr_native_stream_open(&stream);
}
static bool pump_audio(void) {
    for(unsigned n=0;n<16;++n) {
        bool progress=false;
        if(ctr_dsp_stream_space(&converter)>=CTR_DSP_FRAME_SAMPLES && ctr_dsp_read(&guest,dsp_pcm,CTR_DSP_FRAME_SAMPLES*2)) {
            if(!ctr_dsp_stream_push(&converter,dsp_pcm,CTR_DSP_FRAME_SAMPLES))return false;
            progress=true;
        }
        if(ctr_native_stream_space(&stream)) {
            size_t count;
            if(!ctr_dsp_stream_pull(&converter,pcm+pcm_frames*2,CTR_NATIVE_STREAM_FRAMES-pcm_frames,&count))return false;
            pcm_frames+=count;progress=progress || count!=0;
            if(pcm_frames==CTR_NATIVE_STREAM_FRAMES) {
                if(!ctr_native_stream_push(&stream,pcm,pcm_frames))return false;
                pcm_frames=0;
                if(!stream_started && ctr_native_stream_space(&stream)<=CTR_NATIVE_STREAM_SLOTS-2) {
                    if(!ctr_native_stream_start(&stream))return false;
                    stream_started=true;
                }
            }
        }
        if(!progress)break;
    }
    return true;
}
static int axis(float value) {
    if(!isfinite(value))return 0;
    if(value>1)value=1;
    if(value< -1)value=-1;
    return (int)lroundf(value*156.0f);
}
static bool update_input(const VPADStatus *pad) {
    static const uint32_t buttons[]={VPAD_BUTTON_A,VPAD_BUTTON_B,VPAD_BUTTON_MINUS,VPAD_BUTTON_PLUS,
        VPAD_BUTTON_RIGHT,VPAD_BUTTON_LEFT,VPAD_BUTTON_UP,VPAD_BUTTON_DOWN,VPAD_BUTTON_R,VPAD_BUTTON_L,VPAD_BUTTON_X,VPAD_BUTTON_Y};
    uint32_t held=0;for(unsigned i=0;i<12;++i)if(pad->hold&buttons[i])held|=1u<<i;
    VPADTouchData touch={0};uint16_t x=0,y=0;
    VPADGetTPCalibratedPointEx(VPAD_CHAN_0,VPAD_TP_1280X720,&touch,&pad->tpNormal);
    bool inside=touch.touched && touch.validity==VPAD_VALID && ctr_diagnostic_display_touch(touch.x,touch.y,&x,&y);
    return ctr_hid_update(&guest,held,axis(pad->leftStick.x),axis(pad->leftStick.y),inside,x,y,clock_ns);
}
static uint64_t slice_clock(void *unused) { (void)unused;return OSTicksToMicroseconds(OSGetTime()); }
static ctr_slice_step execute(void *context,unsigned budget) {
    (void)context;uint64_t before=guest.cpu.instructions,serial=guest.gsp.swaps[0];
    ctr_guest_state state=ctr_guest_run(&guest,budget,clock_ns,true);
    return (ctr_slice_step){guest.cpu.instructions-before,
        state==CTR_GUEST_READY?CTR_SLICE_RUN:state==CTR_GUEST_YIELDED?CTR_SLICE_YIELD:
        state==CTR_GUEST_WAITING || state==CTR_GUEST_SLEEPING?CTR_SLICE_WAIT:CTR_SLICE_STOP,
        guest.gsp.swaps[0]>serial};
}
static uint32_t save_callback(void *unused) { (void)unused;OSSavesDone_ReadyToRelease();return 0; }
static uint32_t home_callback(void *unused) { (void)unused;exit_requested=true;return 0; }
static void checkpoint(void *unused,const char *message) {
    (void)unused;OSReport("%s\n",message);
    if(!closed_io && report.file && !report.failed)ctr_test_report_checkpoint(&report,message);
    ctr_report_storage_close(&storage);
}
static void clear_callbacks(void *unused) {
    (void)unused;ProcUIClearCallbacks();ProcUISetSaveCallback(save_callback,NULL);
    ctr_test_report_set_volume_sync(&report,NULL,NULL);ctr_report_storage_close(&storage);
}
static void finalize(void *unused) { (void)unused;closed_io=true; }
static void cleanup(void *unused) {
    (void)unused;
    status_report("closing");
    if(!ctr_diagnostic_audio_end(&audio,ctr_native_stream_release,&stream))fail("Audio cleanup failed");
    if(opened) { ctr_guest_close(&guest);opened=false; }
    if(save_ready) {
        if(savedata.dirty && !ctr_fs_snapshot_save(CTR_GAME_DATA_ROOT "/save.snapshot",&savedata))fail("Save publication failed");
        ctr_fs_volume_close(&savedata);
    }
    if(sd_ready)ctr_fs_volume_close(&sdmc);
    ctr_romfs_file_close(&romfs);ctr_diagnostic_display_close(foreground);
    ctr_test_report_close(&report);
}
static const ctr_exit_ops exit_ops={cleanup,clear_callbacks,ctr_procui_request_menu,ctr_procui_messages,
    ctr_procui_release,ctr_procui_wait,ctr_procui_shutdown,ctr_procui_shutdown_whb,checkpoint,finalize};

int main(void) {
    WHBProcInit();session=OSGetTime();lifecycle.title_id=OSGetTitleID();
    if(ctr_test_from_hbl(lifecycle.title_id))ProcUIRegisterCallback(PROCUI_CALLBACK_HOME_BUTTON_DENIED,home_callback,NULL,99);
    char path[512];
    bool logging=directory("fs:/vol/external01/ctr2wup") && directory("fs:/vol/external01/ctr2wup/titles") &&
        ctr_test_report_directory(CTR_GAME_DATA_ROOT,false,path,sizeof(path)) &&
        ctr_test_report_open(&report,path,"game",CTR_APP_VERSION,session);
    ctr_test_report_set_volume_sync(&report,ctr_report_storage_sync,&storage);
    if(!logging)fail("Unable to open private SD log");
    ctr_diagnostic_display_labels(CTR_GAME_NAME,"HOME OR L+R+MINUS: EXIT");
    if(!open_audio())fail("Audio initialization failed");
    if(!ctr_diagnostic_display_init())fail("Display initialization failed");
    ctr_diagnostic_display_row(0,"Loading installed guest executable...");ctr_diagnostic_display_draw();
    if(!failure && !start_guest())fail("Guest executable, assets, save or AOT load failed");
    if(!failure && !status_report("running"))fail("Private runtime log failed");
    uint64_t previous_us=slice_clock(NULL);clock_ns=1;bool exit_observed=false;
    VPADStatus pad={0};
    while(!exit_requested) {
        ctr_proc_status status=ctr_procui_messages(NULL);
        if(status==CTR_PROC_EXITING) { exit_observed=true;break; }
        if(status==CTR_PROC_RELEASE) {
            ctr_diagnostic_audio_end(&audio,ctr_native_stream_release,&stream);
            if(save_ready && savedata.dirty && !ctr_fs_snapshot_save(CTR_GAME_DATA_ROOT "/save.snapshot",&savedata))fail("Save publication failed");
            status_report("background");ctr_report_storage_close(&storage);ctr_procui_release(NULL);
        }
        bool previous_foreground=foreground;foreground=status==CTR_PROC_FOREGROUND;
        uint64_t now=slice_clock(NULL),delta=now-previous_us;previous_us=now;
        if(!foreground) { ctr_procui_wait(NULL);continue; }
        if(!previous_foreground) {
            delta=0;ctr_diagnostic_display_invalidate();
            if(!failure && !open_audio())fail("Audio reacquisition failed");
        }
        clock_ns+=delta*1000u;
        VPADStatus sample={0};VPADReadError error;
        pad.trigger=pad.release=0;
        int count=VPADRead(VPAD_CHAN_0,&sample,1,&error);
        if(count>0 && error==VPAD_READ_SUCCESS)pad=sample;
        else if(error!=VPAD_READ_NO_SAMPLES)memset(&pad,0,sizeof(pad));
        if(count>0 && error==VPAD_READ_SUCCESS && (pad.trigger&VPAD_BUTTON_MINUS) &&
            (pad.hold&(VPAD_BUTTON_L|VPAD_BUTTON_R))==(VPAD_BUTTON_L|VPAD_BUTTON_R))break;
        if(!failure && guest.state!=CTR_GUEST_FAULTED && guest.state!=CTR_GUEST_EXITED) {
            if(!update_input(&pad))fail("Guest input publication failed");
            else {
                const ctr_slice_policy policy={10000,32,200000,12000};
                ctr_guest_slice(&policy,slice_clock,execute,NULL);
                if(guest.state==CTR_GUEST_FAULTED) { status_report("guest_fault");failure="Guest stopped - unsupported runtime behavior"; }
                else if(guest.state==CTR_GUEST_EXITED)status_report("guest_exit");
                if(!failure && !pump_audio())fail("Guest audio delivery failed");
            }
        }
        bool picture=!failure && opened && !guest.gsp.black && guest.gsp.active[0] && guest.gsp.active[1] &&
            ctr_gsp_pixels(&guest,0,top,400u*240u)==CTR_MEM_OK && ctr_gsp_pixels(&guest,1,bottom,320u*240u)==CTR_MEM_OK;
        if(picture) {
            if(!ctr_diagnostic_display_guest(top,bottom))fail("Guest display presentation failed");
            else ++frames;
        } else {
            char line[128];ctr_diagnostic_display_row(0,failure?failure:guest.state==CTR_GUEST_EXITED?"Guest exited":"Executing guest - waiting for framebuffers");
            snprintf(line,sizeof(line),"ARM %llu / fallback %llu / PC %08x",(unsigned long long)guest.cpu.instructions,
                (unsigned long long)guest.cpu.fallback_instructions,guest.cpu.r[15]);ctr_diagnostic_display_row(2,line);
            snprintf(line,sizeof(line),"%s / IPC %s %08x / fault %s",ctr_guest_state_name(guest.state),guest.ipc_service,
                guest.ipc_header,ctr_cpu_result_name(guest.fault));ctr_diagnostic_display_row(3,line);
            ctr_diagnostic_display_row(5,"Private logs and saves:");ctr_diagnostic_display_row(6,CTR_GAME_SD_FOLDER);
            if(!ctr_diagnostic_display_draw()) { fail("Status display presentation failed");break; }
        }
        if(now-last_status_us>=5000000u) {
            last_status_us=now;if(!status_report(failure?"stopped":"progress"))fail("Private runtime log failed");
        }
        OSSleepTicks(OSMillisecondsToTicks(1));
    }
    if(!ctr_test_complete_exit(&lifecycle,exit_observed,&exit_ops,NULL))failure="Exit transition failed";
    ctr_report_storage_close(&storage);return failure?1:0;
}
