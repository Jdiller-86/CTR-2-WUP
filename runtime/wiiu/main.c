#include <coreinit/cache.h>
#include <coreinit/debug.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <coreinit/title.h>
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/surface.h>
#include <sndcore2/core.h>
#include <sndcore2/voice.h>
#include <vpad/input.h>
#include <whb/gfx.h>
#include <whb/log.h>
#if CTR_DIAGNOSTIC_UDP
#include <whb/log_udp.h>
#endif
#include <whb/proc.h>
#include <sysapp/launch.h>
#include <math.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "diagnostic_font.h"
#include "viewport.h"
#include "test_report.h"
#include "test_guide.h"
#include "test_lifecycle.h"
#include "ctr_cpu.h"
#include "ctr_guest_fixture.h"
#include "ctr_service_fixture.h"
#include "ctr_heap_fixture.h"
#include "ctr_kernel_fixture.h"
#include "ctr_hid_fixture.h"
#include "ctr_gsp_fixture.h"
#include "ctr_relay_fixture.h"
#include "ctr_stack_fixture.h"
#include "ctr_thread_fixture.h"
#ifdef CTR_FILESYSTEM_HOMEBREW
#include "ctr_fs_snapshot.h"
static ctr_fs_volume diagnostic_save;
static bool diagnostic_save_ready;
static char diagnostic_save_path[384];
static char filesystem_message[64];
#endif
#ifdef CTR_COMPILED_HOMEBREW
#include "boot-image.h"
#include "ctr_recomp.h"
#include "ctr_apt.h"
#include "ctr_fs.h"
static ctr_fs_volume compiled_sd;
static bool compiled_sd_ready;
#endif

enum { TOP_W=400, BOTTOM_W=320, GUEST_H=240, AUDIO_SAMPLES=12000 };
static uint32_t top_image[TOP_W*GUEST_H], bottom_image[BOTTOM_W*GUEST_H];
#ifndef CTR_COMPILED_HOMEBREW
static int16_t audio_samples[AUDIO_SAMPLES] __attribute__((aligned(64)));
#endif
static ctr_test_report test_report;
static ctr_test_guide test_guide;
static ctr_test_lifecycle lifecycle;
static unsigned freeze_trace_frames;
static bool gsp_pass_seen,fresh_save_reload_seen;
static bool report_handler_added,report_error_announced;
static OSTime session_started;
static unsigned guest_sessions,completed_frames,input_samples,input_errors;
static unsigned button_presses[12],touch_samples,border_touch_samples;
static uint32_t frame_min_us=UINT32_MAX,frame_max_us;
static uint64_t frame_total_us;
static bool left_stick_seen,right_stick_seen,guest_fault_seen,allocation_checked;
static bool allocation_failure_seen;
static int alloc_ok_for_report;
static uint16_t touch_min_x=UINT16_MAX,touch_min_y=UINT16_MAX,touch_max_x,touch_max_y;
#ifdef CTR_FILESYSTEM_HOMEBREW
#define REPORT_APPLICATION "compiled-save"
#elif defined(CTR_COMPILED_HOMEBREW)
#define REPORT_APPLICATION "compiled-interactive"
#elif defined(CTR_EXTERNAL_AOT)
#define REPORT_APPLICATION "upstream-guest"
#elif CTR_GUEST_DEFAULT == 1
#define REPORT_APPLICATION "original-guest"
#else
#define REPORT_APPLICATION "native"
#endif
static const uint32_t input_masks[]={VPAD_BUTTON_A,VPAD_BUTTON_B,VPAD_BUTTON_MINUS,VPAD_BUTTON_PLUS,
    VPAD_BUTTON_RIGHT,VPAD_BUTTON_LEFT,VPAD_BUTTON_UP,VPAD_BUTTON_DOWN,VPAD_BUTTON_R,VPAD_BUTTON_L,
    VPAD_BUTTON_X,VPAD_BUTTON_Y};
static const char *const input_names[]={"A","B","Minus","Plus","Right","Left","Up","Down","R","L","X","Y"};
#ifndef CTR_COMPILED_HOMEBREW
static AXVoice *tone_voice;
#endif
static unsigned tone_count;
static int audio_ready;
static int cpu_test_result;
static int service_test_result;
static int heap_test_result;
static int kernel_test_result;
static int hid_test_result,hid_ready;
static ctr_guest hid_guest;
static uint32_t hid_snapshot[5];
static OSTime hid_origin;
static int gsp_test_result,gsp_ready;
static int relay_test_result;
static int stack_test_result,thread_test_result;
static ctr_guest gsp_guest;
static uint64_t gsp_presented;
static OSTime gsp_origin;
static ctr_guest frame_guest;
static int guest_ready,guest_mode=CTR_GUEST_DEFAULT;
static uint32_t presented_frames;
static OSTime guest_origin;

static void request_app_exit(void) {
    ctr_exit_action action=ctr_test_request_exit(&lifecycle);
    if(action==CTR_EXIT_NONE)return;
    WHBLogPrintf("EXIT stage=request_begin route=%s title_id=%016llx",
        action==CTR_EXIT_HBL?"HBL_relaunch":"system_menu",
        (unsigned long long)lifecycle.title_id);
    if(action==CTR_EXIT_HBL)WHBProcStopRunning();
    else SYSLaunchMenu();
    WHBLogPrintf("EXIT stage=request_returned; waiting for ProcUI exit");
}

static void freeze_checkpoint(const char *stage) {
    if(!freeze_trace_frames)return;
    WHBLogPrintf("INPUT CHECKPOINT stage=%s remaining=%u state=%s pc=%08x instructions=%llu swaps=%llu",
        stage,freeze_trace_frames,ctr_guest_state_name(gsp_guest.state),gsp_guest.cpu.r[15],
        (unsigned long long)gsp_guest.cpu.instructions,(unsigned long long)gsp_guest.gsp.swaps[0]);
}

static void guest_debug(void *context,const char *message) {
    (void)context; WHBLogPrintf("guest_debug %s",message);
    if(strstr(message,"CTR2WUP compiled interactive PASS") ||
       strstr(message,"CTR2WUP compiled filesystem SAVE 1 PASS") ||
       strstr(message,"CTR2WUP compiled filesystem RELOAD PASS"))gsp_pass_seen=true;
    if(guest_sessions==1 && strstr(message,"CTR2WUP compiled filesystem RELOAD PASS"))fresh_save_reload_seen=true;
#ifdef CTR_FILESYSTEM_HOMEBREW
    snprintf(filesystem_message,sizeof(filesystem_message),"%s",message);
#endif
}
static void guest_trace(void *context,const char *event,const char *hash,uint32_t pc,bool thumb,uint32_t detail) {
    (void)context;
    WHBLogPrintf("guest_event=%s module=%s pc=%08x thumb=%u detail=%08x",event,hash,pc,thumb,detail);
}
static void restart_guest(void) {
    if(guest_sessions) {
        WHBLogPrintf("Restart requested: prior framebuffer=%s prior GSP=%s prior GSP fallback=%llu fault=%s detail=%08x",
            ctr_guest_state_name(frame_guest.state),ctr_guest_state_name(gsp_guest.state),
            (unsigned long long)gsp_guest.cpu.fallback_instructions,ctr_cpu_result_name(gsp_guest.fault),gsp_guest.fault_detail);
        if(frame_guest.state==CTR_GUEST_FAULTED || gsp_guest.state==CTR_GUEST_FAULTED)guest_fault_seen=true;
    }
    ++guest_sessions;
    gsp_pass_seen=false;
    WHBLogPrintf("Guest session %u starting",guest_sessions);
    ctr_guest_close(&frame_guest);
#ifdef CTR_COMPILED_HOMEBREW
    guest_ready=0;
#else
    guest_ready=ctr_frame_open(&frame_guest);
#endif
    frame_guest.debug=guest_debug; frame_guest.cpu.log=guest_trace;
    guest_origin=OSGetTime(); presented_frames=0;
    WHBLogPrintf("guest_open ok=%u profile=ctr2wup-framebuffer-fixture-v1 mode=AOT custom_frame_io=1 GSP_PICA=unavailable",guest_ready);
    ctr_guest_close(&gsp_guest);
#ifdef CTR_FILESYSTEM_HOMEBREW
    if(diagnostic_save_ready)ctr_fs_volume_close(&diagnostic_save);
    diagnostic_save_ready=ctr_fs_volume_init(&diagnostic_save,4,true);
    filesystem_message[0]=0;
    ctr_fs_snapshot_result loaded=ctr_fs_snapshot_load(&diagnostic_save,diagnostic_save_path);
    diagnostic_save.persist=ctr_fs_snapshot_save;diagnostic_save.persist_context=diagnostic_save_path;
#endif
#ifdef CTR_COMPILED_HOMEBREW
    gsp_ready=ctr_guest_open(&gsp_guest,&boot_image,NULL,0) && ctr_recomp_attach(&gsp_guest,false);
    if(compiled_sd_ready)ctr_fs_volume_close(&compiled_sd);
    compiled_sd_ready=ctr_fs_volume_init(&compiled_sd,9,false);
    gsp_ready=gsp_ready && compiled_sd_ready && ctr_apt_configure(&gsp_guest) && ctr_fs_mount(&gsp_guest,&compiled_sd);
#else
    gsp_ready=ctr_relay_fixture_open(&gsp_guest);
#endif
    gsp_guest.debug=guest_debug;gsp_guest.cpu.log=guest_trace;
#ifdef CTR_FILESYSTEM_HOMEBREW
    gsp_ready=gsp_ready && diagnostic_save_ready && loaded!=CTR_FS_SNAPSHOT_ERROR && ctr_fs_mount(&gsp_guest,&diagnostic_save);
    if(!gsp_ready) { gsp_guest.state=CTR_GUEST_FAULTED;snprintf(filesystem_message,sizeof(filesystem_message),"SAVE LOAD / MOUNT FAIL"); }
    WHBLogPrintf("filesystem_load result=%d ok=%u path=%s console_result_required=1",loaded,gsp_ready,diagnostic_save_path);
#endif
    gsp_origin=OSGetTime();gsp_presented=0;
#ifdef CTR_COMPILED_HOMEBREW
#ifdef CTR_FILESYSTEM_HOMEBREW
    WHBLogPrintf("gsp_guest_open ok=%u profile=original_compiled_libctru_filesystem mode=external_AOT PICA=unavailable",gsp_ready);
#else
    WHBLogPrintf("gsp_guest_open ok=%u profile=original_compiled_libctru_interactive mode=external_AOT PICA=unavailable",gsp_ready);
#endif
#else
    WHBLogPrintf("gsp_guest_open ok=%u profile=original_hid_gsp_shared_relay_rgb565 mode=original_AOT PICA=unavailable",gsp_ready);
#endif
}

static int circle_axis(float axis) {
    if (!isfinite(axis)) return 0;
    if (axis>1) axis=1;
    if (axis<-1) axis=-1;
    return (int)lroundf(axis*156.0f);
}
static void update_input_guest(ctr_guest *guest,int ready,OSTime origin,const VPADStatus *pad,bool inside,uint16_t x,uint16_t y,unsigned budget) {
    static const uint32_t masks[]={VPAD_BUTTON_A,VPAD_BUTTON_B,VPAD_BUTTON_MINUS,VPAD_BUTTON_PLUS,
        VPAD_BUTTON_RIGHT,VPAD_BUTTON_LEFT,VPAD_BUTTON_UP,VPAD_BUTTON_DOWN,VPAD_BUTTON_R,VPAD_BUTTON_L,
        VPAD_BUTTON_X,VPAD_BUTTON_Y};
    if (!ready || guest->state==CTR_GUEST_FAULTED || guest->state==CTR_GUEST_EXITED) return;
    uint32_t buttons=0;for(unsigned i=0;i<12;++i)if(pad->hold&masks[i])buttons|=1u<<i;
    uint64_t now=1+(uint64_t)OSTicksToMicroseconds(OSGetTime()-origin)*1000u;
    if (!ctr_hid_update(guest,buttons,circle_axis(pad->leftStick.x),circle_axis(pad->leftStick.y),inside,x,y,now)) {
        guest->state=CTR_GUEST_FAULTED;guest->fault=CTR_CPU_UNSUPPORTED;
        WHBLogPrintf("HID live publication FAIL time_ns=%llu",(unsigned long long)now);return;
    }
    ctr_guest_run(guest,budget,now,true);
}

static void file_log(const char *message) {
    if(!ctr_test_report_line(&test_report,message) && !report_error_announced) {
        report_error_announced=true;
        /* Calling WHBLogPrintf here would recurse through this handler. */
        OSReport("CTR2WUP TXT report write FAILED errno=%d path=%s\n",test_report.error_code,test_report.path);
    }
}
static void system_log(const char *message) { OSReport("%s\n", message); }
static void guide_log(void *context,const char *message) {
    (void)context;WHBLogPrintf("%s",message);
}

static void session_summary(int result) {
    WHBLogPrintf("===== SESSION SUMMARY =====");
    ctr_test_guide_summary(&test_guide);
    WHBLogPrintf("Application: %s | Build: 0.14.2 | Entering cleanup: result=%d",REPORT_APPLICATION,result);
    WHBLogPrintf("Elapsed milliseconds: %llu | Completed display frames: %u | Guest sessions: %u",
        (unsigned long long)(OSTicksToMicroseconds(OSGetTime()-session_started)/1000),completed_frames,guest_sessions);
    WHBLogPrintf("Display frame microseconds: minimum=%u average=%llu maximum=%u (diagnostic workload, not game FPS)",
        completed_frames?frame_min_us:0,(unsigned long long)(completed_frames?frame_total_us/completed_frames:0),frame_max_us);
    WHBLogPrintf("Self-tests (0=PASS): CPU=%d services=%d heap=%d objects=%d HID=%d GSP=%d relay=%d stack=%d threads=%d",
        cpu_test_result,service_test_result,heap_test_result,kernel_test_result,hid_test_result,gsp_test_result,
        relay_test_result,stack_test_result,thread_test_result);
    WHBLogPrintf("Allocation latest: %s | Allocation failure ever seen: %s | Audio configured: %s | Tone requests: %u",
        !allocation_checked?"NOT TESTED":alloc_ok_for_report?"PASS":"FAIL",allocation_failure_seen?"YES":"NO",audio_ready?"YES":"NO",tone_count);
    WHBLogPrintf("Input samples: %u | Input errors: %u | Left stick moved: %s | Right stick moved: %s",
        input_samples,input_errors,left_stick_seen?"YES":"NO",right_stick_seen?"YES":"NO");
    for(unsigned i=0;i<12;++i)WHBLogPrintf("Button %s presses: %u",input_names[i],button_presses[i]);
    WHBLogPrintf("Accepted touch samples: %u | Rejected border touch samples: %u | Accepted range: %u,%u to %u,%u",
        touch_samples,border_touch_samples,touch_samples?touch_min_x:0,touch_samples?touch_min_y:0,touch_max_x,touch_max_y);
    WHBLogPrintf("Guest fault observed during session: %s",guest_fault_seen || frame_guest.state==CTR_GUEST_FAULTED ||
        hid_guest.state==CTR_GUEST_FAULTED || gsp_guest.state==CTR_GUEST_FAULTED?"YES":"NO");
    WHBLogPrintf("Framebuffer guest: %s | GSP guest: %s | GSP instructions: %llu | GSP fallback: %llu",
        ctr_guest_state_name(frame_guest.state),ctr_guest_state_name(gsp_guest.state),
        (unsigned long long)gsp_guest.cpu.instructions,(unsigned long long)gsp_guest.cpu.fallback_instructions);
    WHBLogPrintf("GSP fault: %s detail=%08x | Presentations: %llu | Device failures: %llu | Dropped IRQs: %llu",
        ctr_cpu_result_name(gsp_guest.fault),gsp_guest.fault_detail,(unsigned long long)gsp_presented,
        (unsigned long long)gsp_guest.gsp.failures,(unsigned long long)gsp_guest.gsp.dropped_interrupts);
    WHBLogPrintf("Human observations: note incorrect picture/touch alignment, missing sound, or failure to return to launcher.");
    WHBLogPrintf("No overall hardware/game compatibility PASS is inferred from these counters.");
}

static int surface_create(GX2Surface *s, const GX2Surface *target) {
    memset(s, 0, sizeof(*s));
    s->dim=GX2_SURFACE_DIM_TEXTURE_2D;
    s->width=target->width; s->height=target->height;
    s->depth=1; s->mipLevels=1;
    s->format=GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
    s->aa=GX2_AA_MODE1X;
    s->use=GX2_SURFACE_USE_TEXTURE;
    s->tileMode=GX2_TILE_MODE_LINEAR_ALIGNED;
    GX2CalcSurfaceSizeAndAlignment(s);
    if (target->format != s->format || !s->imageSize || !s->alignment) {
        WHBLogPrintf("UNSUPPORTED native output format=%u size=%u alignment=%u",
                      (unsigned)target->format, s->imageSize, s->alignment);
        return 0;
    }
    s->image=MEMAllocFromDefaultHeapEx(s->imageSize, s->alignment);
    if (!s->image) return 0;
    memset(s->image, 0, s->imageSize);
    return 1;
}

/* A CPU diagnostic image copied to GX2 is intentionally separate from any
 * guest PICA emulation. No proprietary shader compiler or assets are used. */
static void scale_image(GX2Surface *s, const CtrViewport *v, const uint32_t *image) {
    unsigned char *pixels=s->image;
    memset(pixels, 0, s->imageSize);
    for (uint32_t y=0; y<v->height; ++y) {
        uint32_t sy=(uint32_t)((uint64_t)y*v->guest_height/v->height);
        unsigned char *row=pixels+((size_t)(v->y+y)*s->pitch+v->x)*4;
        for (uint32_t x=0; x<v->width; ++x) {
            uint32_t sx=(uint32_t)((uint64_t)x*v->guest_width/v->width);
            uint32_t rgba=image[sy*v->guest_width+sx];
            row[x*4]=(unsigned char)(rgba>>24);
            row[x*4+1]=(unsigned char)(rgba>>16);
            row[x*4+2]=(unsigned char)(rgba>>8);
            row[x*4+3]=(unsigned char)rgba;
        }
    }
    DCFlushRange(s->image, s->imageSize);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, s->image, s->imageSize);
}

static int allocation_test(void) {
    allocation_checked=true;alloc_ok_for_report=0;
    const uint32_t bytes=65536;
    unsigned char *buffer=MEMAllocFromDefaultHeapEx(bytes, 64);
    if (!buffer) { allocation_failure_seen=true;WHBLogPrintf("allocation_test FAIL bytes=%u", bytes); return 0; }
    int ok=1;
    for (uint32_t i=0;i<bytes;++i) buffer[i]=(unsigned char)(i^(i>>8)^0xa5);
    for (uint32_t i=0;i<bytes;++i)
        if (buffer[i]!=(unsigned char)(i^(i>>8)^0xa5)) { ok=0; break; }
    WHBLogPrintf("allocation_test %s bytes=%u aligned64=%u", ok ? "PASS":"FAIL",
                  bytes, (unsigned)(((uintptr_t)buffer&63)==0));
    MEMFreeToDefaultHeap(buffer);
    alloc_ok_for_report=ok;
    if(!ok)allocation_failure_seen=true;
    return ok;
}

#ifndef CTR_COMPILED_HOMEBREW
static void audio_init(void) {
    AXInitParams params={0};
    params.renderer=AX_INIT_RENDERER_48KHZ;
    params.pipeline=AX_INIT_PIPELINE_SINGLE;
    AXInitWithParams(&params);
    if (!AXIsInit()) { WHBLogPrintf("audio FAIL AX initialization"); return; }
    tone_voice=AXAcquireVoice(31, NULL, NULL);
    if (!tone_voice) { WHBLogPrintf("audio FAIL no AX voice"); return; }
    for (unsigned i=0;i<AUDIO_SAMPLES;++i) {
        /* 250ms, 440Hz, 10% amplitude with a 10ms fade at both ends. */
        float gain=(i<480 ? i/480.0f : i>AUDIO_SAMPLES-480 ? (AUDIO_SAMPLES-i)/480.0f:1.0f);
        audio_samples[i]=(int16_t)(3276.0f*gain*sinf(6.28318530718f*440.0f*i/48000.0f));
    }
    DCFlushRange(audio_samples, sizeof(audio_samples));
    AXVoiceOffsets offsets={0};
    offsets.dataType=AX_VOICE_FORMAT_LPCM16;
    offsets.loopingEnabled=AX_VOICE_LOOP_DISABLED;
    offsets.endOffset=AUDIO_SAMPLES-1; offsets.data=audio_samples;
    AXSetVoiceOffsets(tone_voice, &offsets);
    AXSetVoiceSrcType(tone_voice, AX_VOICE_SRC_TYPE_NONE);
    if (AXSetVoiceSrcRatio(tone_voice, 1.0f)!=AX_VOICE_RATIO_RESULT_SUCCESS) {
        WHBLogPrintf("audio FAIL sample rate ratio"); return;
    }
    AXVoiceVeData volume={0x8000,0};
    AXSetVoiceVe(tone_voice, &volume);
    AXVoiceDeviceMixData tv_mix[6]={0}, drc_mix[4]={0};
    tv_mix[0].bus[0].volume=tv_mix[1].bus[0].volume=0x8000;
    drc_mix[0].bus[0].volume=drc_mix[1].bus[0].volume=0x8000;
    AXResult tv=AXSetVoiceDeviceMix(tone_voice, AX_DEVICE_TYPE_TV, 0, tv_mix);
    AXResult drc=AXSetVoiceDeviceMix(tone_voice, AX_DEVICE_TYPE_DRC, 0, drc_mix);
    audio_ready=(tv==AX_RESULT_SUCCESS && drc==AX_RESULT_SUCCESS);
    WHBLogPrintf("audio configured=%u TV_result=%d DRC_result=%d rate=%u",
                  audio_ready, (int)tv, (int)drc, AXGetInputSamplesPerSec());
}

static void play_tone(void) {
    if (!audio_ready) return;
    AXSetVoiceState(tone_voice, AX_VOICE_STATE_STOPPED);
    AXSetVoiceCurrentOffset(tone_voice, 0);
    AXSetVoiceState(tone_voice, AX_VOICE_STATE_PLAYING);
    WHBLogPrintf("tone request=%u frequency_hz=440 duration_ms=250", ++tone_count);
}
#endif

static void image_text(uint32_t *image, unsigned w, unsigned row, const char *text) {
    ctr_draw_text(image,w,GUEST_H,6,row*10+6,text,0xffffffff);
}

static void diagnostic_images(unsigned frame, const VPADStatus *pad,
                              VPADReadError input_error, int touch_inside,
                              const VPADTouchData *touch, uint16_t tx, uint16_t ty,
                              uint32_t frame_us, int alloc_ok) {
    char text[90];
    for (unsigned y=0;y<GUEST_H;++y) for (unsigned x=0;x<TOP_W;++x) {
        unsigned band=((x+frame/2)/25)%3;
        top_image[y*TOP_W+x]=y<123 ? 0x162d4fff :
            (x%40==0 || y%24==0) ? 0xffffffff :
            band==0 ? 0x195baaff : band==1 ? 0x25a980ff : 0xb46b20ff;
    }
    image_text(top_image,TOP_W,0,"CTR2WUP TV / TOP 400X240");
    image_text(top_image,TOP_W,1,"NATIVE DIAGNOSTIC / NO GAME LOADED");
    snprintf(text,sizeof(text),"FRAME %u / PREVIOUS FRAME US %u",frame,frame_us);
    image_text(top_image,TOP_W,2,text);
    snprintf(text,sizeof(text),"ALLOCATION %s / AUDIO %s",alloc_ok?"PASS":"FAIL",audio_ready?"READY":"FAIL");
    image_text(top_image,TOP_W,3,text);
    image_text(top_image,TOP_W,4,"A: TONE / X: ALLOC TEST / MINUS: EXIT");
    snprintf(text,sizeof(text),"CPU ARM/THUMB AOT SELFTEST %s CODE %d",cpu_test_result?"FAIL":"PASS",cpu_test_result);
    image_text(top_image,TOP_W,5,text);
    image_text(top_image,TOP_W,6,"Y: CYCLE GUESTS / PLUS: RESTART");
    snprintf(text,sizeof(text),"TLS/SRV/CFG STARTUP %s CODE %d",service_test_result?"FAIL":"PASS",service_test_result);
    image_text(top_image,TOP_W,7,text);
    snprintf(text,sizeof(text),"GUEST HEAP LIFECYCLE %s CODE %d",heap_test_result?"FAIL":"PASS",heap_test_result);
    image_text(top_image,TOP_W,8,text);
    snprintf(text,sizeof(text),"SHARED/SYNC LIFECYCLE %s CODE %d",kernel_test_result?"FAIL":"PASS",kernel_test_result);
    image_text(top_image,TOP_W,9,text);
    snprintf(text,sizeof(text),"HID ARM CONSUMER %s CODE %d",hid_test_result?"FAIL":"PASS",hid_test_result);
    image_text(top_image,TOP_W,10,text);
    snprintf(text,sizeof(text),"HID/GSP RGB565 DISPLAY %s CODE %d",gsp_test_result?"FAIL":"PASS",gsp_test_result);
    image_text(top_image,TOP_W,11,text);
    snprintf(text,sizeof(text),"GSP QUEUE/VBLANK %s CODE %d",relay_test_result?"FAIL":"PASS",relay_test_result);
    image_text(top_image,TOP_W,12,text);
    snprintf(text,sizeof(text),"ARM/THUMB STACK CALLS %s CODE %d",stack_test_result?"FAIL":"PASS",stack_test_result);
    image_text(top_image,TOP_W,13,text);
    snprintf(text,sizeof(text),"GUEST THREAD SCHEDULER %s CODE %d",thread_test_result?"FAIL":"PASS",thread_test_result);
    image_text(top_image,TOP_W,14,text);
    for (unsigned y=0;y<GUEST_H;++y) for (unsigned x=0;x<BOTTOM_W;++x)
        bottom_image[y*BOTTOM_W+x]=y<92 ? 0x35194fff :
            (((x/20)^(y/20))&1) ? 0x783598ff:0x108797ff;
    image_text(bottom_image,BOTTOM_W,0,"CTR2WUP GAMEPAD / BOTTOM 320X240");
    snprintf(text,sizeof(text),"BUTTONS %08X / VPAD ERROR %d",(unsigned)pad->hold,(int)input_error);
    image_text(bottom_image,BOTTOM_W,1,text);
    snprintf(text,sizeof(text),"LEFT %+.2f %+.2f",(double)pad->leftStick.x,(double)pad->leftStick.y);
    image_text(bottom_image,BOTTOM_W,2,text);
    snprintf(text,sizeof(text),"RIGHT %+.2f %+.2f",(double)pad->rightStick.x,(double)pad->rightStick.y);
    image_text(bottom_image,BOTTOM_W,3,text);
    snprintf(text,sizeof(text),"TOUCH %s X %u Y %u",touch_inside?"INSIDE":touch->touched?"BORDER/INVALID":"UP",(unsigned)tx,(unsigned)ty);
    image_text(bottom_image,BOTTOM_W,4,text);
    snprintf(text,sizeof(text),"CALIBRATED %u %u / VALIDITY %u",touch->x,touch->y,touch->validity);
    image_text(bottom_image,BOTTOM_W,5,text);
    snprintf(text,sizeof(text),"TONE COUNT %u / A: PLAY",tone_count);
    image_text(bottom_image,BOTTOM_W,6,text);
    image_text(bottom_image,BOTTOM_W,7,"TOUCH BORDERS MUST BE IGNORED");
    snprintf(text,sizeof(text),"ARM HID %08X / SAMPLES %u",hid_snapshot[0],hid_snapshot[4]);
    image_text(bottom_image,BOTTOM_W,8,text);
    snprintf(text,sizeof(text),"ARM CIRCLE %08X / TOUCH %08X V%u",hid_snapshot[1],hid_snapshot[2],hid_snapshot[3]);
    image_text(bottom_image,BOTTOM_W,9,text);
    snprintf(text,sizeof(text),"HID STATE %s",hid_ready?ctr_guest_state_name(hid_guest.state):"LOADER FAILED");
    image_text(bottom_image,BOTTOM_W,10,text);
    if (touch_inside) for (int d=-7;d<=7;++d) {
        int xx=(int)tx+d, yy=(int)ty+d;
        if (xx>=0 && xx<BOTTOM_W) bottom_image[(unsigned)ty*BOTTOM_W+(unsigned)xx]=0xffffffff;
        if (yy>=0 && yy<GUEST_H) bottom_image[(unsigned)yy*BOTTOM_W+tx]=0xffffffff;
    }
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    int result=1, gfx_ready=0;
    bool proc_stopped=false;
    GX2Surface tv_source={0}, drc_source={0};
    CtrViewport tv_view, drc_view;
    WHBProcInit();
    lifecycle.title_id=OSGetTitleID();
    session_started=OSGetTime();
    WHBAddLogHandler(system_log);
#if CTR_DIAGNOSTIC_UDP
    int udp_ready=WHBLogUdpInit();
#else
    int udp_ready=0;
#endif
    /* WUT's newlib devoptab already owns the SD mount. A second WHB mount
       followed by FSUnmount conflicts with that process-wide ownership. */
    {
        const char *directory="fs:/vol/external01/ctr2wup";
        bool directory_ok=mkdir(directory,0777)==0 || errno==EEXIST;
#ifdef CTR_FILESYSTEM_HOMEBREW
        if(directory_ok)snprintf(diagnostic_save_path,sizeof(diagnostic_save_path),"%s/filesystem-test.snapshot",directory);
#endif
        if(directory_ok && ctr_test_report_open(&test_report,directory,REPORT_APPLICATION,"0.14.2",(uint64_t)session_started)) {
            WHBAddLogHandler(file_log);report_handler_added=true;
        } else if(!directory_ok) {
            test_report.failed=true;test_report.error_code=errno;
        }
    }
    WHBLogPrintf("CTR2WUP native diagnostic version=0.14.2 application=%s pointer_bits=%u endian=big UDP=%u TXT_report=%u",
                  REPORT_APPLICATION,(unsigned)(8*sizeof(void *)),udp_ready,report_handler_added);
    WHBLogPrintf("Platform title_id=%016llx exit_route=%s SD_owner=WUT_devoptab",
        (unsigned long long)lifecycle.title_id,ctr_test_from_hbl(lifecycle.title_id)?"HBL_relaunch":"system_menu");
    WHBLogPrintf("TXT report path=%s status=%s errno=%d",test_report.path[0]?test_report.path:"unavailable",
        report_handler_added?"SAVING":"FAILED",test_report.error_code);
#ifdef CTR_FILESYSTEM_HOMEBREW
    ctr_test_guide_init(&test_guide,CTR_GUIDE_SAVE,guide_log,NULL);
#elif defined(CTR_COMPILED_HOMEBREW)
    ctr_test_guide_init(&test_guide,CTR_GUIDE_INTERACTIVE,guide_log,NULL);
#else
    ctr_test_guide_init(&test_guide,CTR_GUIDE_NATIVE,guide_log,NULL);
#endif
    OSTime cpu_start=OSGetTime();
    thread_test_result=ctr_thread_selftest();
    WHBLogPrintf("thread_selftest %s result=%d fixture=original_arm_three_threads context_switches=8 instructions_per_path=345 console_result_required=1",
                 thread_test_result?"FAIL":"PASS",thread_test_result);
    cpu_test_result=ctr_cpu_selftest();
    WHBLogPrintf("CPU_selftest %s result=%d elapsed_us=%llu guest_instructions_per_path=40 fallback=0 fixture=original_arm_thumb",
                  cpu_test_result?"FAIL":"PASS",cpu_test_result,
                  (unsigned long long)OSTicksToMicroseconds(OSGetTime()-cpu_start));
    service_test_result=ctr_service_selftest();
    WHBLogPrintf("service_selftest %s result=%d fixture=original_arm_tls_srv_cfg console_result_required=1",
                  service_test_result?"FAIL":"PASS",service_test_result);
    heap_test_result=ctr_heap_selftest();
    WHBLogPrintf("heap_selftest %s result=%d fixture=original_arm_heap_lifecycle console_result_required=1",
                  heap_test_result?"FAIL":"PASS",heap_test_result);
    kernel_test_result=ctr_kernel_selftest();
    WHBLogPrintf("kernel_selftest %s result=%d fixture=original_arm_shared_sync console_result_required=1",
                  kernel_test_result?"FAIL":"PASS",kernel_test_result);
    hid_test_result=ctr_hid_selftest();
    WHBLogPrintf("hid_selftest %s result=%d fixture=original_arm_standard_hid console_result_required=1",
                  hid_test_result?"FAIL":"PASS",hid_test_result);
    hid_ready=ctr_hid_fixture_open(&hid_guest);hid_origin=OSGetTime();
    hid_guest.debug=guest_debug;hid_guest.cpu.log=guest_trace;
    WHBLogPrintf("hid_live_open ok=%u service=hid:USER guest_mapping=readonly motion=unsupported",hid_ready);
    OSTime gsp_start=OSGetTime();gsp_test_result=ctr_gsp_selftest();
    WHBLogPrintf("gsp_selftest %s result=%d fixture=original_arm_hid_gsp_rgb565 elapsed_us=%llu console_result_required=1",
        gsp_test_result?"FAIL":"PASS",gsp_test_result,(unsigned long long)OSTicksToMicroseconds(OSGetTime()-gsp_start));
    relay_test_result=ctr_relay_selftest();
    WHBLogPrintf("gsp_relay_selftest %s result=%d fixture=original_arm_shared_gsp_vblank console_result_required=1",
        relay_test_result?"FAIL":"PASS",relay_test_result);
    stack_test_result=ctr_stack_selftest();
    WHBLogPrintf("stack_selftest %s result=%d fixture=original_arm_thumb_nested_calls instructions_per_path=47 console_result_required=1",
        stack_test_result?"FAIL":"PASS",stack_test_result);
    if (!WHBGfxInit()) { WHBLogPrintf("FAIL WHBGfxInit"); goto cleanup; }
    gfx_ready=1;
    GX2Surface *tv_target=&WHBGfxGetTVColourBuffer()->surface;
    GX2Surface *drc_target=&WHBGfxGetDRCColourBuffer()->surface;
    if (!surface_create(&tv_source,tv_target) || !surface_create(&drc_source,drc_target) ||
        !ctr_viewport_fit(&tv_view,tv_target->width,tv_target->height,TOP_W,GUEST_H) ||
        !ctr_viewport_fit(&drc_view,drc_target->width,drc_target->height,BOTTOM_W,GUEST_H)) {
        WHBLogPrintf("FAIL native surface allocation or viewport"); goto cleanup;
    }
    WHBLogPrintf("TV surface=%ux%u viewport=%u,%u %ux%u allocation_bytes=%u pitch=%u",
                  tv_view.output_width,tv_view.output_height,tv_view.x,tv_view.y,
                  tv_view.width,tv_view.height,tv_source.imageSize,tv_source.pitch);
    WHBLogPrintf("DRC surface=%ux%u viewport=%u,%u %ux%u calibrated_space=1280x720 allocation_bytes=%u pitch=%u",
                  drc_view.output_width,drc_view.output_height,drc_view.x,drc_view.y,
                  drc_view.width,drc_view.height,drc_source.imageSize,drc_source.pitch);
    int alloc_ok=allocation_test();
#ifdef CTR_COMPILED_HOMEBREW
    WHBLogPrintf("Compiled test: native AX/tone disabled; A is guest input only; guest audio unavailable");
#else
    audio_init();
#endif
    restart_guest();
#ifndef CTR_COMPILED_HOMEBREW
    guest_mode=0;
#endif
    VPADStatus pad={0}; VPADReadError error=VPAD_READ_NO_SAMPLES;
    unsigned frame=0; uint32_t frame_us=0;
    OSTime log_time=OSGetTime();
    while (WHBProcIsRunning()) {
        if(lifecycle.requested) { OSSleepTicks(OSMillisecondsToTicks(5));continue; }
        OSTime frame_start=OSGetTime();
        VPADStatus sample={0};
        int32_t count=VPADRead(VPAD_CHAN_0,&sample,1,&error);
        pad.trigger=pad.release=0;
        if (count>0 && error==VPAD_READ_SUCCESS) pad=sample;
        else if (error!=VPAD_READ_NO_SAMPLES) memset(&pad,0,sizeof(pad));
        if(count>0 && error==VPAD_READ_SUCCESS)++input_samples;
        else if(error!=VPAD_READ_NO_SAMPLES)++input_errors;
        for(unsigned i=0;i<12;++i)if(pad.trigger&input_masks[i])++button_presses[i];
        if(pad.trigger || pad.release)WHBLogPrintf("Input event: pressed=%08x released=%08x held=%08x",
            (unsigned)pad.trigger,(unsigned)pad.release,(unsigned)pad.hold);
        if(fabsf(pad.leftStick.x)>0.15f || fabsf(pad.leftStick.y)>0.15f)left_stick_seen=true;
        if(fabsf(pad.rightStick.x)>0.15f || fabsf(pad.rightStick.y)>0.15f)right_stick_seen=true;
        if (pad.trigger&VPAD_BUTTON_MINUS) {
            ctr_guide_input stop={0};stop.milliseconds=OSTicksToMicroseconds(OSGetTime()-session_started)/1000;stop.pressed=CTR_GUIDE_MINUS;
            ctr_test_guide_update(&test_guide,&stop);
            request_app_exit();continue;
        }
#ifdef CTR_COMPILED_HOMEBREW
        if(pad.trigger&(VPAD_BUTTON_A|VPAD_BUTTON_B|VPAD_BUTTON_PLUS))freeze_trace_frames=64;
        freeze_checkpoint("input_read");
#else
        if (pad.trigger&VPAD_BUTTON_A) play_tone();
#endif
        if (pad.trigger&VPAD_BUTTON_X) alloc_ok=allocation_test();
        if (test_guide.done && (pad.trigger&VPAD_BUTTON_Y)) {
#ifdef CTR_COMPILED_HOMEBREW
            guest_mode=guest_mode==2?0:2;
#else
            guest_mode=(guest_mode+1)%3;presented_frames=0;
#endif
            WHBLogPrintf("display_mode=%s",guest_mode==2?"standard-hid-gsp":guest_mode==1?"guest-fixture":"native");
        }
        if (test_guide.done && (pad.trigger&VPAD_BUTTON_PLUS)) restart_guest();
        VPADTouchData touch={0}; uint16_t tx=0, ty=0;
        VPADGetTPCalibratedPointEx(VPAD_CHAN_0,VPAD_TP_1280X720,&touch,&pad.tpNormal);
        int inside=touch.touched && touch.validity==VPAD_VALID &&
            ctr_viewport_touch(&drc_view,touch.x,touch.y,1280,720,&tx,&ty);
        if(inside) {
            ++touch_samples;
            if(tx<touch_min_x)touch_min_x=tx;
            if(ty<touch_min_y)touch_min_y=ty;
            if(tx>touch_max_x)touch_max_x=tx;
            if(ty>touch_max_y)touch_max_y=ty;
        } else if(touch.touched && touch.validity==VPAD_VALID)++border_touch_samples;
        freeze_checkpoint("hid_guest_begin");
        update_input_guest(&hid_guest,hid_ready,hid_origin,&pad,inside!=0,tx,ty,1000);
        freeze_checkpoint("hid_guest_done");
        if(hid_ready)ctr_hid_fixture_snapshot(&hid_guest,hid_snapshot);
        if (!guest_mode) diagnostic_images(frame,&pad,error,inside,&touch,tx,ty,frame_us,alloc_ok);
        else if(guest_mode==2) {
#ifndef CTR_COMPILED_HOMEBREW
            if(pad.trigger&VPAD_BUTTON_B)ctr_relay_fixture_exit(&gsp_guest);
#endif
            freeze_checkpoint("compiled_guest_begin");
            update_input_guest(&gsp_guest,gsp_ready,gsp_origin,&pad,inside!=0,tx,ty,50000);
            freeze_checkpoint("compiled_guest_done");
            if(!gsp_presented) { memset(top_image,0,sizeof(top_image));memset(bottom_image,0,sizeof(bottom_image)); }
            if(gsp_ready && !gsp_guest.gsp.black && gsp_guest.gsp.swaps[0] && gsp_guest.gsp.swaps[0]==gsp_guest.gsp.swaps[1] && gsp_guest.gsp.swaps[0]!=gsp_presented) {
                freeze_checkpoint("guest_pixels_begin");
                ctr_mem_result top=ctr_gsp_pixels(&gsp_guest,0,top_image,TOP_W*GUEST_H);
                ctr_mem_result bottom=ctr_gsp_pixels(&gsp_guest,1,bottom_image,BOTTOM_W*GUEST_H);
                if(top!=CTR_MEM_OK || bottom!=CTR_MEM_OK) {
                    gsp_guest.state=CTR_GUEST_FAULTED;gsp_guest.fault=CTR_CPU_MEMORY_FAULT;
                    WHBLogPrintf("GSP presentation FAIL top=%s bottom=%s",ctr_mem_result_name(top),ctr_mem_result_name(bottom));
                }
                gsp_presented=gsp_guest.gsp.swaps[0];
                freeze_checkpoint("guest_pixels_done");
            }
            for(unsigned i=TOP_W*102;i<TOP_W*144;++i)top_image[i]=0x162d4fff;
            char text[90];
#ifdef CTR_COMPILED_HOMEBREW
#ifdef CTR_FILESYSTEM_HOMEBREW
            image_text(top_image,TOP_W,10,"CTR2WUP COMPILED SAVE / RELOAD");
#else
            image_text(top_image,TOP_W,10,"CTR2WUP COMPILED LIBCTRU / GSP");
#endif
#else
            image_text(top_image,TOP_W,10,"CTR2WUP HID / GSP QUEUE / VBLANK");
#endif
            snprintf(text,sizeof(text),"FRAME %llu / STATE %s",(unsigned long long)gsp_presented,ctr_guest_state_name(gsp_guest.state));
            image_text(top_image,TOP_W,11,text);image_text(top_image,TOP_W,12,"A: GREEN / TOUCH: WHITE / B: EXIT");
#ifdef CTR_FILESYSTEM_HOMEBREW
            image_text(top_image,TOP_W,12,filesystem_message);
            image_text(top_image,TOP_W,13,"PLUS: RELOAD SAVE / MINUS: EXIT");
#else
            image_text(top_image,TOP_W,13,"Y: CYCLE / PLUS: RESET / PICA ABSENT");
#endif
        } else {
            if (guest_ready && frame_guest.state!=CTR_GUEST_FAULTED && frame_guest.state!=CTR_GUEST_EXITED) {
                uint32_t buttons=(pad.hold&VPAD_BUTTON_A)?1u:0u;
                ctr_frame_input(&frame_guest,buttons,inside!=0,tx,ty);
                if (pad.trigger&VPAD_BUTTON_B) ctr_frame_exit(&frame_guest);
                uint64_t now=(uint64_t)OSTicksToMicroseconds(OSGetTime()-guest_origin)*1000u;
                ctr_guest_run(&frame_guest,50000,now,true);
            }
            uint32_t completed=0;
            if (guest_ready && ctr_frame_count(&frame_guest,&completed) && completed && completed!=presented_frames) {
                if (!ctr_frame_pixels(&frame_guest,true,top_image,TOP_W*GUEST_H) ||
                    !ctr_frame_pixels(&frame_guest,false,bottom_image,BOTTOM_W*GUEST_H)) {
                    frame_guest.state=CTR_GUEST_FAULTED; frame_guest.fault=CTR_CPU_MEMORY_FAULT;
                }
                presented_frames=completed;
            }
            for (unsigned i=0;i<TOP_W*42;++i) top_image[i]=0x162d4fff;
            char text[90];
            image_text(top_image,TOP_W,0,"CTR2WUP ARM GUEST / RAW FRAMEBUFFERS");
            snprintf(text,sizeof(text),"GUEST FRAME %u / STATE %s",completed,guest_ready?ctr_guest_state_name(frame_guest.state):"LOADER FAILED");
            image_text(top_image,TOP_W,1,text);
            image_text(top_image,TOP_W,2,"A: COLOR / TOUCH: PIXEL / B: GUEST EXIT");
            snprintf(text,sizeof(text),"Y: NATIVE / PLUS: RESET / FAULT %u",(unsigned)frame_guest.fault);
            image_text(top_image,TOP_W,3,text);
        }
        ctr_guide_input guide_input={0};
        guide_input.milliseconds=OSTicksToMicroseconds(OSGetTime()-session_started)/1000;
        for(unsigned i=0;i<12;++i) {
            if(pad.trigger&input_masks[i])guide_input.pressed|=1u<<i;
            if(pad.hold&input_masks[i])guide_input.held|=1u<<i;
        }
        if(pad.trigger&VPAD_BUTTON_ZL)guide_input.pressed|=CTR_GUIDE_SKIP;
        if(pad.trigger&VPAD_BUTTON_ZR)guide_input.pressed|=CTR_GUIDE_FAIL;
        guide_input.left_x=pad.leftStick.x;guide_input.left_y=pad.leftStick.y;
        guide_input.right_x=pad.rightStick.x;guide_input.right_y=pad.rightStick.y;
        guide_input.touch_inside=inside!=0;guide_input.touch_border=!inside && touch.touched && touch.validity==VPAD_VALID;
        guide_input.touch_x=tx;guide_input.touch_y=ty;
        guide_input.guest_exited=gsp_guest.state==CTR_GUEST_EXITED;
        guide_input.guest_fault=gsp_guest.state==CTR_GUEST_FAULTED;
        guide_input.guest_pass=gsp_pass_seen;guide_input.guest_sessions=guest_sessions;
        guide_input.guest_frame_available=gsp_presented>0;
        guide_input.fresh_save_reload=fresh_save_reload_seen;
        ctr_test_guide_update(&test_guide,&guide_input);
        if(ctr_test_guide_take_restart(&test_guide))restart_guest();
        char guide_rows[7][72];ctr_test_guide_view(&test_guide,guide_input.milliseconds,guide_rows);
        for(unsigned i=0;i<TOP_W*83;++i)top_image[i]=0x162d4fff;
        if(test_report.failed && test_guide.done) {
            snprintf(guide_rows[3],72,"SD WRITE FAILED; report may be missing/partial.");
            snprintf(guide_rows[4],72,"Send the error below and any available TXT.");
        }
        for(unsigned i=0;i<7;++i)image_text(top_image,TOP_W,i,guide_rows[i]);
        for(unsigned i=TOP_W*154;i<TOP_W*178;++i)top_image[i]=0x162d4fff;
        image_text(top_image,TOP_W,15,test_report.file && !test_report.failed?"TXT REPORT SAVING / UPLOAD SD:/CTR2WUP":"TXT REPORT FAILED / CHECK SD CARD");
        char report_line[100];
        if(!test_report.failed)snprintf(report_line,sizeof(report_line),"%s",test_report.filename);
        else snprintf(report_line,sizeof(report_line),"SD REPORT ERROR %d",test_report.error_code);
        image_text(top_image,TOP_W,16,report_line);
#ifdef CTR_COMPILED_HOMEBREW
        for(unsigned i=TOP_W*184;i<TOP_W*206;++i)top_image[i]=0x162d4fff;
        snprintf(report_line,sizeof(report_line),"GUEST INSTRUCTIONS %llu",(unsigned long long)gsp_guest.cpu.instructions);
        image_text(top_image,TOP_W,18,report_line);
        snprintf(report_line,sizeof(report_line),"HOST FRAME %u / GUEST PC %08X",frame,gsp_guest.cpu.r[15]);
        image_text(top_image,TOP_W,19,report_line);
#endif
        /* Draw prompts before scaling so they reach the actual outputs. */
        freeze_checkpoint("gx2_wait_begin");
        GX2DrawDone();
        freeze_checkpoint("scale_begin");
        scale_image(&tv_source,&tv_view,top_image);
        scale_image(&drc_source,&drc_view,bottom_image);
        freeze_checkpoint("render_begin");
        WHBGfxBeginRender();
        WHBGfxBeginRenderTV();
        GX2CopySurface(&tv_source,0,0,&WHBGfxGetTVColourBuffer()->surface,0,0);
        WHBGfxFinishRenderTV();
        WHBGfxBeginRenderDRC();
        GX2CopySurface(&drc_source,0,0,&WHBGfxGetDRCColourBuffer()->surface,0,0);
        WHBGfxFinishRenderDRC();
        WHBGfxFinishRender();
        freeze_checkpoint("render_done");
        if(freeze_trace_frames)--freeze_trace_frames;
        frame_us=(uint32_t)OSTicksToMicroseconds(OSGetTime()-frame_start);
        ++completed_frames;frame_total_us+=frame_us;
        if(frame_us<frame_min_us)frame_min_us=frame_us;
        if(frame_us>frame_max_us)frame_max_us=frame_us;
        if(frame_guest.state==CTR_GUEST_FAULTED || hid_guest.state==CTR_GUEST_FAULTED || gsp_guest.state==CTR_GUEST_FAULTED)guest_fault_seen=true;
        if ((uint64_t)(OSGetTime()-log_time)>=OSSecondsToTicks(1)) {
            WHBLogPrintf("frame=%u frame_us=%u buttons=%08x input_error=%d left=%.3f,%.3f right=%.3f,%.3f touch=%u,%u inside=%u tone=%u",
                frame,frame_us,(unsigned)pad.hold,(int)error,(double)pad.leftStick.x,
                (double)pad.leftStick.y,(double)pad.rightStick.x,(double)pad.rightStick.y,
                touch.x,touch.y,inside,tone_count);
            WHBLogPrintf("guest state=%s presented=%u instructions=%llu fallback=%llu svc=%llu fault=%s detail=%08x",
                ctr_guest_state_name(frame_guest.state),presented_frames,(unsigned long long)frame_guest.cpu.instructions,
                (unsigned long long)frame_guest.cpu.fallback_instructions,(unsigned long long)frame_guest.svc_count,
                ctr_cpu_result_name(frame_guest.fault),frame_guest.fault_detail);
            WHBLogPrintf("IPC requests=%llu failures=%llu service=%s target=%s header=%08x result=%08x",
                (unsigned long long)frame_guest.ipc_requests,(unsigned long long)frame_guest.ipc_failures,
                frame_guest.ipc_service[0]?frame_guest.ipc_service:"none",frame_guest.ipc_target[0]?frame_guest.ipc_target:"none",
                frame_guest.ipc_header,frame_guest.ipc_result);
            WHBLogPrintf("HID live state=%s buttons=%08x circle=%08x touch=%08x valid=%u snapshots=%u published=%llu instructions=%llu fallback=%llu fault=%s detail=%08x",
                ctr_guest_state_name(hid_guest.state),hid_snapshot[0],hid_snapshot[1],hid_snapshot[2],hid_snapshot[3],hid_snapshot[4],
                (unsigned long long)hid_guest.hid.samples,(unsigned long long)hid_guest.cpu.instructions,
                (unsigned long long)hid_guest.cpu.fallback_instructions,ctr_cpu_result_name(hid_guest.fault),hid_guest.fault_detail);
            WHBLogPrintf("GSP guest state=%s presented=%llu top_swaps=%llu bottom_swaps=%llu instructions=%llu fallback=%llu fault=%s detail=%08x",
                ctr_guest_state_name(gsp_guest.state),(unsigned long long)gsp_presented,(unsigned long long)gsp_guest.gsp.swaps[0],
                (unsigned long long)gsp_guest.gsp.swaps[1],(unsigned long long)gsp_guest.cpu.instructions,
                (unsigned long long)gsp_guest.cpu.fallback_instructions,ctr_cpu_result_name(gsp_guest.fault),gsp_guest.fault_detail);
            WHBLogPrintf("GSP relay registered=%u vblanks=%llu coalesced=%llu dropped_irqs=%llu device_failures=%llu",
                gsp_guest.gsp.registered,(unsigned long long)gsp_guest.gsp.vblanks,
                (unsigned long long)gsp_guest.gsp.coalesced_vblanks,(unsigned long long)gsp_guest.gsp.dropped_interrupts,
                (unsigned long long)gsp_guest.gsp.failures);
            WHBLogPrintf("GSP IPC requests=%llu failures=%llu service=%s target=%s header=%08x result=%08x",
                (unsigned long long)gsp_guest.ipc_requests,(unsigned long long)gsp_guest.ipc_failures,
                gsp_guest.ipc_service[0]?gsp_guest.ipc_service:"none",gsp_guest.ipc_target[0]?gsp_guest.ipc_target:"none",
                gsp_guest.ipc_header,gsp_guest.ipc_result);
            log_time=OSGetTime();
        }
        ++frame;
    }
    proc_stopped=true;
    result=0;
cleanup:
    WHBLogPrintf("shutdown begin result=%d",result);
    session_summary(result);
    if(!proc_stopped) {
        request_app_exit();
        while(WHBProcIsRunning())OSSleepTicks(OSMillisecondsToTicks(5));
    }
    WHBLogPrintf("EXIT stage=procui_stopped");
    WHBLogPrintf("EXIT stage=guest_cleanup_begin");
    ctr_guest_close(&frame_guest);
    if (hid_ready && hid_guest.state!=CTR_GUEST_FAULTED && ctr_hid_fixture_exit(&hid_guest)) {
        ctr_guest_run(&hid_guest,1000,hid_guest.now_ns,true);
        WHBLogPrintf("hid_live_shutdown state=%s fallback=%llu",ctr_guest_state_name(hid_guest.state),
            (unsigned long long)hid_guest.cpu.fallback_instructions);
    }
    ctr_guest_close(&hid_guest);
#ifndef CTR_COMPILED_HOMEBREW
    if(gsp_ready && gsp_guest.state!=CTR_GUEST_FAULTED && gsp_guest.state!=CTR_GUEST_EXITED && ctr_relay_fixture_exit(&gsp_guest)) {
        ctr_guest_run(&gsp_guest,1000000,gsp_guest.now_ns,true);
        WHBLogPrintf("gsp_guest_shutdown state=%s fallback=%llu",ctr_guest_state_name(gsp_guest.state),
            (unsigned long long)gsp_guest.cpu.fallback_instructions);
    }
#endif
    ctr_guest_close(&gsp_guest);
#ifdef CTR_FILESYSTEM_HOMEBREW
    if(diagnostic_save_ready)ctr_fs_volume_close(&diagnostic_save);
#endif
    WHBLogPrintf("EXIT stage=guest_cleanup_done");
    WHBLogPrintf("EXIT stage=audio_cleanup_begin");
#ifndef CTR_COMPILED_HOMEBREW
    if (tone_voice) { AXSetVoiceState(tone_voice,AX_VOICE_STATE_STOPPED); AXFreeVoice(tone_voice); }
#endif
#ifdef CTR_COMPILED_HOMEBREW
    if(compiled_sd_ready)ctr_fs_volume_close(&compiled_sd);
#endif
#ifndef CTR_COMPILED_HOMEBREW
    if (AXIsInit()) AXQuit();
#endif
    WHBLogPrintf("EXIT stage=audio_cleanup_done");
    WHBLogPrintf("EXIT stage=graphics_cleanup_begin");
    if (gfx_ready) GX2DrawDone();
    if (tv_source.image) MEMFreeToDefaultHeap(tv_source.image);
    if (drc_source.image) MEMFreeToDefaultHeap(drc_source.image);
    if (gfx_ready) WHBGfxShutdown();
    WHBLogPrintf("EXIT stage=graphics_cleanup_done");
#if CTR_DIAGNOSTIC_UDP
    WHBLogPrintf("EXIT stage=udp_cleanup_begin");
    if (udp_ready) WHBLogUdpDeinit();
    WHBLogPrintf("EXIT stage=udp_cleanup_done");
#endif
    WHBLogPrintf("shutdown complete result=%d",result);
    WHBLogPrintf("===== END OF REPORT: application resource cleanup reached; result=%d =====",result);
    WHBLogPrintf("EXIT stage=report_close_begin; launcher return is not yet established");
    if(report_handler_added)WHBRemoveLogHandler(file_log);
    if(!ctr_test_report_close(&test_report))OSReport("CTR2WUP TXT report close FAILED errno=%d\n",test_report.error_code);
    ctr_test_report_checkpoint(&test_report,"EXIT stage=report_close_done; SD mount cleanup belongs to WUT");
    ctr_test_report_checkpoint(&test_report,"EXIT stage=whb_proc_shutdown_begin");
    WHBProcShutdown();
    ctr_test_report_checkpoint(&test_report,"EXIT stage=whb_proc_shutdown_done");
    ctr_test_report_checkpoint(&test_report,"EXIT stage=main_return; runtime finalizers and launcher return still require observation");
    WHBRemoveLogHandler(system_log);
    return result;
}
