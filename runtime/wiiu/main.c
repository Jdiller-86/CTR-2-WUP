#include <coreinit/cache.h>
#include <coreinit/debug.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/surface.h>
#include <sndcore2/core.h>
#include <sndcore2/voice.h>
#include <vpad/input.h>
#include <whb/gfx.h>
#include <whb/log.h>
#include <whb/log_udp.h>
#include <whb/proc.h>
#include <whb/sdcard.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "diagnostic_font.h"
#include "viewport.h"
#include "ctr_cpu.h"
#include "ctr_guest_fixture.h"
#include "ctr_service_fixture.h"

enum { TOP_W=400, BOTTOM_W=320, GUEST_H=240, AUDIO_SAMPLES=12000 };
static uint32_t top_image[TOP_W*GUEST_H], bottom_image[BOTTOM_W*GUEST_H];
static int16_t audio_samples[AUDIO_SAMPLES] __attribute__((aligned(64)));
static FILE *log_file;
static AXVoice *tone_voice;
static unsigned tone_count;
static int audio_ready;
static int cpu_test_result;
static int service_test_result;
static ctr_guest frame_guest;
static int guest_ready,guest_mode=CTR_GUEST_DEFAULT;
static uint32_t presented_frames;
static OSTime guest_origin;

static void guest_debug(void *context,const char *message) {
    (void)context; WHBLogPrintf("guest_debug %s",message);
}
static void guest_trace(void *context,const char *event,const char *hash,uint32_t pc,bool thumb,uint32_t detail) {
    (void)context;
    WHBLogPrintf("guest_event=%s module=%s pc=%08x thumb=%u detail=%08x",event,hash,pc,thumb,detail);
}
static void restart_guest(void) {
    ctr_guest_close(&frame_guest);
    guest_ready=ctr_frame_open(&frame_guest);
    frame_guest.debug=guest_debug; frame_guest.cpu.log=guest_trace;
    guest_origin=OSGetTime(); presented_frames=0;
    WHBLogPrintf("guest_open ok=%u profile=ctr2wup-framebuffer-fixture-v1 mode=AOT HID_GSP_PICA=unavailable",guest_ready);
}

static void file_log(const char *message) {
    if (log_file) { fprintf(log_file, "%s\n", message); fflush(log_file); }
}
static void system_log(const char *message) { OSReport("%s\n", message); }

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
    const uint32_t bytes=65536;
    unsigned char *buffer=MEMAllocFromDefaultHeapEx(bytes, 64);
    if (!buffer) { WHBLogPrintf("allocation_test FAIL bytes=%u", bytes); return 0; }
    int ok=1;
    for (uint32_t i=0;i<bytes;++i) buffer[i]=(unsigned char)(i^(i>>8)^0xa5);
    for (uint32_t i=0;i<bytes;++i)
        if (buffer[i]!=(unsigned char)(i^(i>>8)^0xa5)) { ok=0; break; }
    WHBLogPrintf("allocation_test %s bytes=%u aligned64=%u", ok ? "PASS":"FAIL",
                  bytes, (unsigned)(((uintptr_t)buffer&63)==0));
    MEMFreeToDefaultHeap(buffer);
    return ok;
}

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
        top_image[y*TOP_W+x]=y<83 ? 0x162d4fff :
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
    image_text(top_image,TOP_W,6,"Y: ARM GUEST / PLUS: RESTART GUEST");
    snprintf(text,sizeof(text),"TLS/SRV/CFG STARTUP %s CODE %d",service_test_result?"FAIL":"PASS",service_test_result);
    image_text(top_image,TOP_W,7,text);
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
    if (touch_inside) for (int d=-7;d<=7;++d) {
        int xx=(int)tx+d, yy=(int)ty+d;
        if (xx>=0 && xx<BOTTOM_W) bottom_image[(unsigned)ty*BOTTOM_W+(unsigned)xx]=0xffffffff;
        if (yy>=0 && yy<GUEST_H) bottom_image[(unsigned)yy*BOTTOM_W+tx]=0xffffffff;
    }
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    int result=1, gfx_ready=0, sd_ready=0, udp_ready=0;
    GX2Surface tv_source={0}, drc_source={0};
    CtrViewport tv_view, drc_view;
    WHBProcInit();
    WHBAddLogHandler(system_log);
    udp_ready=WHBLogUdpInit();
    sd_ready=WHBMountSdCard();
    if (sd_ready) {
        char directory[320], path[384];
        snprintf(directory,sizeof(directory),"%s/ctr2wup",WHBGetSdCardMountPath());
        mkdir(directory,0777);
        snprintf(path,sizeof(path),"%s/native-%llx.log",directory,(unsigned long long)OSGetTime());
        log_file=fopen(path,"w");
        if (log_file) WHBAddLogHandler(file_log);
    }
    WHBLogPrintf("CTR2WUP native diagnostic version=0.6.0 pointer_bits=%u endian=big UDP=%u SD_log=%u",
                  (unsigned)(8*sizeof(void *)), udp_ready, log_file!=NULL);
    OSTime cpu_start=OSGetTime();
    cpu_test_result=ctr_cpu_selftest();
    WHBLogPrintf("CPU_selftest %s result=%d elapsed_us=%llu guest_instructions_per_path=40 fallback=0 fixture=original_arm_thumb",
                  cpu_test_result?"FAIL":"PASS",cpu_test_result,
                  (unsigned long long)OSTicksToMicroseconds(OSGetTime()-cpu_start));
    service_test_result=ctr_service_selftest();
    WHBLogPrintf("service_selftest %s result=%d fixture=original_arm_tls_srv_cfg console_result_required=1",
                  service_test_result?"FAIL":"PASS",service_test_result);
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
    audio_init();
    restart_guest();
    VPADStatus pad={0}; VPADReadError error=VPAD_READ_NO_SAMPLES;
    unsigned frame=0; uint32_t frame_us=0;
    OSTime log_time=OSGetTime();
    while (WHBProcIsRunning()) {
        OSTime frame_start=OSGetTime();
        VPADStatus sample={0};
        int32_t count=VPADRead(VPAD_CHAN_0,&sample,1,&error);
        pad.trigger=pad.release=0;
        if (count>0 && error==VPAD_READ_SUCCESS) pad=sample;
        else if (error!=VPAD_READ_NO_SAMPLES) memset(&pad,0,sizeof(pad));
        if (pad.trigger&VPAD_BUTTON_MINUS) { WHBProcStopRunning(); continue; }
        if (pad.trigger&VPAD_BUTTON_A) play_tone();
        if (pad.trigger&VPAD_BUTTON_X) alloc_ok=allocation_test();
        if (pad.trigger&VPAD_BUTTON_Y) {
            guest_mode=!guest_mode; presented_frames=0;
            WHBLogPrintf("display_mode=%s",guest_mode?"guest-fixture":"native");
        }
        if (pad.trigger&VPAD_BUTTON_PLUS) restart_guest();
        VPADTouchData touch={0}; uint16_t tx=0, ty=0;
        VPADGetTPCalibratedPointEx(VPAD_CHAN_0,VPAD_TP_1280X720,&touch,&pad.tpNormal);
        int inside=touch.touched && touch.validity==VPAD_VALID &&
            ctr_viewport_touch(&drc_view,touch.x,touch.y,1280,720,&tx,&ty);
        if (!guest_mode) diagnostic_images(frame,&pad,error,inside,&touch,tx,ty,frame_us,alloc_ok);
        else {
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
        /* Synchronize previous GPU reads before rewriting the CPU surfaces. */
        GX2DrawDone();
        scale_image(&tv_source,&tv_view,top_image);
        scale_image(&drc_source,&drc_view,bottom_image);
        WHBGfxBeginRender();
        WHBGfxBeginRenderTV();
        GX2CopySurface(&tv_source,0,0,&WHBGfxGetTVColourBuffer()->surface,0,0);
        WHBGfxFinishRenderTV();
        WHBGfxBeginRenderDRC();
        GX2CopySurface(&drc_source,0,0,&WHBGfxGetDRCColourBuffer()->surface,0,0);
        WHBGfxFinishRenderDRC();
        WHBGfxFinishRender();
        frame_us=(uint32_t)OSTicksToMicroseconds(OSGetTime()-frame_start);
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
            log_time=OSGetTime();
        }
        ++frame;
    }
    result=0;
cleanup:
    WHBLogPrintf("shutdown begin result=%d",result);
    ctr_guest_close(&frame_guest);
    if (tone_voice) { AXSetVoiceState(tone_voice,AX_VOICE_STATE_STOPPED); AXFreeVoice(tone_voice); }
    if (AXIsInit()) AXQuit();
    if (gfx_ready) GX2DrawDone();
    if (tv_source.image) MEMFreeToDefaultHeap(tv_source.image);
    if (drc_source.image) MEMFreeToDefaultHeap(drc_source.image);
    if (gfx_ready) WHBGfxShutdown();
    WHBLogPrintf("shutdown complete result=%d",result);
    if (log_file) { WHBRemoveLogHandler(file_log); fclose(log_file); log_file=NULL; }
    if (sd_ready) WHBUnmountSdCard();
    if (udp_ready) WHBLogUdpDeinit();
    WHBRemoveLogHandler(system_log);
    WHBProcShutdown();
    return result;
}
