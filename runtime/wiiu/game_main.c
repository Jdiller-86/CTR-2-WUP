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
#include "ctr_guest_pica.h"
#include "gx2_scene.h"
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
#include <stddef.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static ctr_guest guest;
static ctr_wiiu_gx2_scene gx2_scene;
static ctr_scene_backend gx2_backend;
static ctr_fs_volume savedata,sdmc;
static ctr_romfs_file romfs;
#if CTR_GAME_HAS_MII_ROMFS
static ctr_romfs_file mii_romfs;
#endif
static ctr_test_report report;
static ctr_report_storage storage={.client=-1};
static ctr_test_lifecycle lifecycle;
static ctr_diagnostic_audio audio;
static ctr_native_stream stream;
static ctr_dsp_stream converter;
static uint32_t top[400u*240u],bottom[320u*240u];
static ctr_gsp_pixel_cache screen_cache[2];
static int16_t dsp_pcm[CTR_DSP_FRAME_SAMPLES*2],pcm[CTR_NATIVE_STREAM_FRAMES*2];
static size_t pcm_frames;
static uint64_t session,clock_ns,last_status_us,frames;
static uint64_t guest_exec_us,presentation_us;
static bool opened,save_ready,sd_ready,foreground=true,closed_io,stream_started;
static const char *failure;
static atomic_bool exit_requested;
static uint64_t slice_clock(void *unused);

static void startup_checkpoint(const char *stage) {
    int error=errno;
    OSReport("CTR2WUP_STARTUP protocol=1 stage=%s errno=%d\n",stage,error);
    errno=error;
}

static bool directory(const char *path) {
    struct stat info;
    if(mkdir(path,0777)==0)return true;
    return errno==EEXIST && stat(path,&info)==0 && S_ISDIR(info.st_mode);
}
static bool status_report(const char *stage) {
    if(closed_io || !report.file || report.failed)return false;
#ifdef CTR_GAME_CEMU_TEST
    if(frames) {
        unsigned top_changes=0,bottom_changes=0;
        for(unsigned i=257u;i<400u*240u;i+=257u)top_changes+=top[i]!=top[0];
        for(unsigned i=257u;i<320u*240u;i+=257u)bottom_changes+=bottom[i]!=bottom[0];
        OSReport("CTR2WUP_CEMU_SCREENS top_varied_samples=%u bottom_varied_samples=%u\n",top_changes,bottom_changes);
    }
#endif
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
        "\"host_us\":%llu,\"guest_ns\":%llu,\"top_swaps\":%llu,"
        "\"guest_exec_us\":%llu,\"presentation_us\":%llu,"
#ifdef CTR_GAME_CEMU_TEST
        "\"gx_us\":%llu,\"pica_us\":%llu,\"pica_fallbacks\":%llu,"
        "\"gx2_draws\":%llu,\"gx2_readbacks\":%llu,\"gx2_waits\":%llu,\"gx2_depth_skips\":%llu,"
#endif
        "\"vblanks\":%llu,\"coalesced_vblanks\":%llu,\"dropped_gsp_irqs\":%llu,"
        "\"pc\":%u,\"fault\":%u,\"detail\":%u,\"svc\":%u,"
        "\"ipc_service\":\"%s\",\"ipc_header\":%u,\"ipc_result\":%u,"
        "\"pica_result\":%u,\"pica_register\":%u,\"pica_offset\":%u,\"scene_result\":%u,"
#ifdef CTR_GAME_CEMU_TEST
        "\"pica_draws\":%llu,\"gx_transfers\":%llu,\"renderer\":\"gx2-direct\",\"gameplay_verified\":false}",
#else
        "\"pica_draws\":%llu,\"gx_transfers\":%llu,\"renderer\":\"software\",\"gameplay_verified\":false}",
#endif
        stage,CTR_SOURCE_COMMIT,CTR_GAME_PROGRAM_ID,CTR_GAME_REVISION,ctr_guest_state_name(guest.state),
        (unsigned long long)guest.cpu.instructions,(unsigned long long)guest.cpu.fallback_instructions,
        (unsigned long long)frames,(unsigned long long)OSTicksToMicroseconds(OSGetTime()),
        (unsigned long long)clock_ns,(unsigned long long)guest.gsp.swaps[0],
        (unsigned long long)guest_exec_us,(unsigned long long)presentation_us,
#ifdef CTR_GAME_CEMU_TEST
        (unsigned long long)guest.gsp.gx_us,(unsigned long long)guest.gsp.pica_us,
        (unsigned long long)guest.gsp.pica_fallback_lists,
        (unsigned long long)gx2_scene.draws,(unsigned long long)gx2_scene.readbacks,
        (unsigned long long)gx2_scene.waits,(unsigned long long)gx2_scene.direct_depth_skips,
#endif
        (unsigned long long)guest.gsp.vblanks,(unsigned long long)guest.gsp.coalesced_vblanks,
        (unsigned long long)guest.gsp.dropped_interrupts,
        guest.cpu.r[15],guest.fault,guest.fault_detail,guest.cpu.svc_number,
        service,guest.ipc_header,guest.ipc_result,guest.gsp.last_pica_result,guest.gsp.last_pica_register,
        guest.gsp.last_pica_offset,guest.gsp.last_scene_result,(unsigned long long)guest.gsp.pica_draws,
        (unsigned long long)guest.gsp.gx_transfers);
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
    ctr_recomp_enable_runfast(&guest);
#ifdef CTR_GAME_CEMU_TEST
    guest.gsp.clock_us=slice_clock;
#endif
    if(CTR_GAME_READONLY_VRAM && !ctr_gsp_open_vram(&guest))return false;
    guest.gsp.pica_round_f24=true;
    ctr_wiiu_gx2_scene_init(&gx2_scene,ctr_diagnostic_display_context());
#ifdef CTR_GAME_CEMU_TEST
    gx2_scene.cemu_readback=true;
#endif
    gx2_scene.batch_transformed=true;
    gx2_backend=ctr_wiiu_gx2_scene_backend_direct(&gx2_scene);
    ctr_guest_set_scene_backend(&guest,&gx2_backend);
    guest.gsp.pica_depth_hint=ctr_wiiu_gx2_scene_set_depth_keep;
    guest.dsp_firmware=&ctr_game_dsp_firmware;
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
#if CTR_GAME_HAS_MII_ROMFS
    if(!ctr_romfs_file_open(&mii_romfs,"fs:/vol/content/mii.romfs",ctr_game_mii_sha256) ||
        !ctr_fs_mount_mii_romfs(&guest,&mii_romfs.view))return false;
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
        else if(ctr_dsp_stream_space(&converter)<CTR_DSP_FRAME_SAMPLES && ctr_dsp_read(&guest,dsp_pcm,CTR_DSP_FRAME_SAMPLES*2))
            progress=true;
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
static int16_t motion_axis(float value,float scale) {
    if(!isfinite(value))return 0;
    return (int16_t)lroundf(fmaxf(-32768.0f,fminf(32767.0f,value*scale)));
}
static bool update_input(const VPADStatus *pad) {
    static const uint32_t buttons[]={VPAD_BUTTON_A,VPAD_BUTTON_B,VPAD_BUTTON_MINUS,VPAD_BUTTON_PLUS,
        VPAD_BUTTON_RIGHT,VPAD_BUTTON_LEFT,VPAD_BUTTON_UP,VPAD_BUTTON_DOWN,VPAD_BUTTON_R,VPAD_BUTTON_L,VPAD_BUTTON_X,VPAD_BUTTON_Y};
    uint32_t held=0;for(unsigned i=0;i<12;++i)if(pad->hold&buttons[i])held|=1u<<i;
    VPADTouchData touch={0};uint16_t x=0,y=0;
    VPADGetTPCalibratedPointEx(VPAD_CHAN_0,VPAD_TP_1280X720,&touch,&pad->tpNormal);
    bool inside=touch.touched && touch.validity==VPAD_VALID && ctr_diagnostic_display_touch(touch.x,touch.y,&x,&y);
#ifdef CTR_GAME_CEMU_TEST
    static uint32_t previous_buttons;
    static bool previous_touch;
    if(held!=previous_buttons || inside!=previous_touch) {
        OSReport("CTR2WUP_CEMU_INPUT buttons=%03x touch=%u guest_ms=%u\n",held,(unsigned)inside,
                 (unsigned)(clock_ns/UINT64_C(1000000)));
        previous_buttons=held;previous_touch=inside;
    }
#endif
    guest.dsp_headphones=pad->usingHeadphones!=0;
    if(guest.hid.accelerometer_clients) {
        VPADAccStatus motion;
        /* WUT renamed this field. Its ABI slot is immediately before gyro. */
        memcpy(&motion,(const uint8_t *)pad+offsetof(VPADStatus,gyro)-sizeof(motion),sizeof(motion));
        const VPADVec3D *acc=&motion.acc;
        /* A stationary upright controller supplies gravity when motion input is absent. */
        if(motion.magnitude>0.0f)
            ctr_hid_acceleration(&guest,motion_axis(acc->x,512.0f),motion_axis(acc->y,512.0f),motion_axis(acc->z,512.0f));
        else
            ctr_hid_acceleration(&guest,0,-512,0);
    }
    if(guest.hid.gyroscope_clients) {
        const float scale=360.0f*CTR_HID_GYRO_COUNTS_PER_DPS;
        ctr_hid_angular_velocity(&guest,motion_axis(pad->gyro.x,scale),motion_axis(pad->gyro.y,scale),
                                motion_axis(pad->gyro.z,scale));
    }
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
#if CTR_GAME_HAS_MII_ROMFS
    ctr_romfs_file_close(&mii_romfs);
#endif
    ctr_romfs_file_close(&romfs);
    ctr_wiiu_gx2_scene_abort(&gx2_scene);
    ctr_diagnostic_display_close(foreground);
    ctr_wiiu_gx2_scene_after_shutdown(&gx2_scene);
    ctr_test_report_close(&report);
}
static const ctr_exit_ops exit_ops={cleanup,clear_callbacks,ctr_procui_request_menu,ctr_procui_messages,
    ctr_procui_release,ctr_procui_wait,ctr_procui_shutdown,ctr_procui_shutdown_whb,checkpoint,finalize};

int main(void) {
    startup_checkpoint("main_enter");
    WHBProcInit();startup_checkpoint("procui_initialized");
    session=OSGetTime();lifecycle.title_id=OSGetTitleID();
    startup_checkpoint("title_identity_ready");
    if(ctr_test_from_hbl(lifecycle.title_id))ProcUIRegisterCallback(PROCUI_CALLBACK_HOME_BUTTON_DENIED,home_callback,NULL,99);
    char path[512];
    startup_checkpoint("sd_root_begin");
    bool logging=directory("fs:/vol/external01/ctr2wup");
    startup_checkpoint("sd_root_end");
    if(logging) {
        logging=directory("fs:/vol/external01/ctr2wup/titles");
        startup_checkpoint("sd_titles_end");
    }
    if(logging) {
        logging=ctr_test_report_directory(CTR_GAME_DATA_ROOT,false,path,sizeof(path));
        startup_checkpoint("sd_results_end");
    }
    if(logging) {
        startup_checkpoint("report_open_begin");
        logging=ctr_test_report_open(&report,path,"game",CTR_APP_VERSION,session);
        startup_checkpoint("report_open_end");
    }
#ifdef CTR_GAME_CEMU_TEST
    /* Keep checked file sync and close on Cemu's virtual SD filesystem. */
    startup_checkpoint("cemu_file_sync");
#else
    ctr_test_report_set_volume_sync(&report,ctr_report_storage_sync,&storage);
#endif
    if(!logging)fail("Unable to open private SD log");
    ctr_diagnostic_display_labels(CTR_GAME_NAME,"HOME OR L+R+MINUS: EXIT");
    startup_checkpoint("audio_begin");
    if(!open_audio())fail("Audio initialization failed");
    startup_checkpoint("audio_end");
    startup_checkpoint("display_begin");
    if(!ctr_diagnostic_display_init())fail("Display initialization failed");
    startup_checkpoint("display_end");
    startup_checkpoint("initial_draw_begin");
    ctr_diagnostic_display_row(0,"Loading installed guest executable...");ctr_diagnostic_display_draw();
    startup_checkpoint("initial_draw_end");
    startup_checkpoint("guest_load_begin");
    if(!failure && !start_guest())fail("Guest executable, assets, save or AOT load failed");
    startup_checkpoint("guest_load_end");
    if(!failure && !status_report("running"))fail("Private runtime log failed");
    uint64_t previous_us=slice_clock(NULL),last_status_draw_us=0;clock_ns=1;
    uint64_t presented_vblank=0,presented_swaps[2]={0,0};
    bool exit_observed=false,previous_picture=false;
    const char *shown_failure=NULL;
    VPADStatus pad={0};
#ifdef CTR_GAME_CEMU_TEST
    uint32_t pending_buttons=0;
#endif
    while(!exit_requested) {
        ctr_proc_status status=ctr_procui_messages(NULL);
        if(status==CTR_PROC_EXITING) { exit_observed=true;break; }
        if(status==CTR_PROC_RELEASE) {
            ctr_diagnostic_audio_end(&audio,ctr_native_stream_release,&stream);
            if(save_ready && savedata.dirty && !ctr_fs_snapshot_save(CTR_GAME_DATA_ROOT "/save.snapshot",&savedata))fail("Save publication failed");
            if(opened && (!ctr_guest_pica_flush_color(&guest) || !ctr_guest_scanout_flush(&guest)))
                fail("Pending GPU color publication failed");
            ctr_wiiu_gx2_scene_abort(&gx2_scene);
            status_report("background");ctr_report_storage_close(&storage);ctr_procui_release(NULL);
        }
        bool previous_foreground=foreground;foreground=status==CTR_PROC_FOREGROUND;
        uint64_t now=slice_clock(NULL),delta=now-previous_us;previous_us=now;
        if(!foreground) { ctr_procui_wait(NULL);continue; }
        if(!previous_foreground) {
            delta=0;last_status_draw_us=0;previous_picture=false;ctr_diagnostic_display_invalidate();
            gx2_scene.restore_context=ctr_diagnostic_display_context();
            if(!failure && !open_audio())fail("Audio reacquisition failed");
        }
        /* Service each pending display deadline before crossing it. Limit
         * catch-up after a host stall to one guest frame per pass. */
        uint64_t elapsed_ns=delta>CTR_GSP_FRAME_NS/1000u?CTR_GSP_FRAME_NS:delta*1000u;
        if(guest.gsp.registered && guest.gsp.right) {
            uint64_t deadline=guest.gsp.next_vblank_ns;
            uint64_t until=deadline>clock_ns?deadline-clock_ns:0u;
            if(elapsed_ns>until)elapsed_ns=until;
        }
        clock_ns+=elapsed_ns;
        VPADStatus sample={0};VPADReadError error;
        pad.trigger=pad.release=0;
        int count=VPADRead(VPAD_CHAN_0,&sample,1,&error);
        if(count>0 && error==VPAD_READ_SUCCESS)pad=sample;
        /* Cemu can return a successful empty poll before the next sample. */
        else if(error!=VPAD_READ_SUCCESS && error!=VPAD_READ_NO_SAMPLES)memset(&pad,0,sizeof(pad));
        if(count>0 && error==VPAD_READ_SUCCESS && (pad.trigger&VPAD_BUTTON_MINUS) &&
            (pad.hold&(VPAD_BUTTON_L|VPAD_BUTTON_R))==(VPAD_BUTTON_L|VPAD_BUTTON_R))break;
        if(!failure && guest.state!=CTR_GUEST_FAULTED && guest.state!=CTR_GUEST_EXITED) {
#ifdef CTR_GAME_CEMU_TEST
            /* Match the host replay's guest-time input cadence and retain
             * short controller presses between those samples. */
            pending_buttons|=pad.trigger;
            if(!guest.hid.published || clock_ns-guest.hid.last_ns>=UINT64_C(17000000)) {
                VPADStatus guest_pad=pad;
                guest_pad.hold|=pending_buttons;
                pending_buttons=0;
                if(!update_input(&guest_pad))fail("Guest input publication failed");
            }
#else
            if(!update_input(&pad))fail("Guest input publication failed");
#endif
            if(!failure) {
                const ctr_slice_policy policy={10000,32,200000,12000};
                ctr_slice_report slice=ctr_guest_slice(&policy,slice_clock,execute,NULL);
                guest_exec_us+=slice.elapsed_us;
                if(guest.state==CTR_GUEST_FAULTED) {
                    OSReport("CTR2WUP_GUEST_FAULT pc=%08x fault=%u detail=%08x svc=%02x ipc=%s header=%08x\n",
                             guest.cpu.r[15],(unsigned)guest.fault,guest.fault_detail,
                             guest.cpu.svc_number,guest.ipc_service,guest.ipc_header);
                    status_report("guest_fault");failure="Guest stopped - unsupported runtime behavior";
                }
                else if(guest.state==CTR_GUEST_EXITED)status_report("guest_exit");
                if(!failure && !pump_audio())fail("Guest audio delivery failed");
            }
        }
        bool picture=!failure && opened && !guest.gsp.black && guest.gsp.active[0] && guest.gsp.active[1];
        if(picture && (!previous_picture || presented_vblank!=guest.gsp.vblanks ||
           presented_swaps[0]!=guest.gsp.swaps[0] || presented_swaps[1]!=guest.gsp.swaps[1])) {
            uint64_t presentation_start=slice_clock(NULL);
            const void *top_scanout=ctr_wiiu_gx2_scene_scanout_texture(&gx2_scene,ctr_guest_scanout_screen(&guest,0));
            const void *bottom_scanout=ctr_wiiu_gx2_scene_scanout_texture(&gx2_scene,ctr_guest_scanout_screen(&guest,1));
            bool drawn=(top_scanout || bottom_scanout) &&
                (top_scanout || ctr_gsp_pixels_cached(&guest,0,top,400u*240u,&screen_cache[0])==CTR_MEM_OK) &&
                (bottom_scanout || ctr_gsp_pixels_cached(&guest,1,bottom,320u*240u,&screen_cache[1])==CTR_MEM_OK) &&
                ctr_diagnostic_display_scanout(top_scanout,bottom_scanout,top,bottom);
            if(!drawn && (ctr_gsp_pixels_cached(&guest,0,top,400u*240u,&screen_cache[0])!=CTR_MEM_OK ||
               ctr_gsp_pixels_cached(&guest,1,bottom,320u*240u,&screen_cache[1])!=CTR_MEM_OK ||
               !ctr_diagnostic_display_guest(top,bottom))) {
                fail("Guest display presentation failed");picture=false;
            } else {
                presented_vblank=guest.gsp.vblanks;
                presented_swaps[0]=guest.gsp.swaps[0];presented_swaps[1]=guest.gsp.swaps[1];++frames;
            }
            presentation_us+=slice_clock(NULL)-presentation_start;
        }
        if(!picture && (previous_picture || failure!=shown_failure || now-last_status_draw_us>=250000u)) {
            /* Startup telemetry shares CPU time with the running guest. */
            char line[128];ctr_diagnostic_display_row(0,failure?failure:guest.state==CTR_GUEST_EXITED?"Guest exited":"Executing guest - waiting for framebuffers");
            snprintf(line,sizeof(line),"ARM %llu / fallback %llu / PC %08x",(unsigned long long)guest.cpu.instructions,
                (unsigned long long)guest.cpu.fallback_instructions,guest.cpu.r[15]);ctr_diagnostic_display_row(2,line);
            snprintf(line,sizeof(line),"%s / IPC %s %08x / fault %s",ctr_guest_state_name(guest.state),guest.ipc_service,
                guest.ipc_header,ctr_cpu_result_name(guest.fault));ctr_diagnostic_display_row(3,line);
            ctr_diagnostic_display_row(5,"Private logs and saves:");ctr_diagnostic_display_row(6,CTR_GAME_SD_FOLDER);
            if(!ctr_diagnostic_display_draw()) { fail("Status display presentation failed");break; }
            last_status_draw_us=now;shown_failure=failure;
        }
        previous_picture=picture;
        if(now-last_status_us>=5000000u) {
            last_status_us=now;if(!status_report(failure?"stopped":"progress"))fail("Private runtime log failed");
        }
        if(guest.state==CTR_GUEST_WAITING || guest.state==CTR_GUEST_SLEEPING)
            OSSleepTicks(OSMillisecondsToTicks(1));
    }
    if(!ctr_test_complete_exit(&lifecycle,exit_observed,&exit_ops,NULL))failure="Exit transition failed";
    ctr_report_storage_close(&storage);return failure?1:0;
}
