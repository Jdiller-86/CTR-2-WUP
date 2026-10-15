#include <coreinit/cache.h>
#include <coreinit/debug.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <coreinit/title.h>
#include <coreinit/foreground.h>
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
#include <proc_ui/procui.h>
#include <math.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <stdatomic.h>
#include "diagnostic_font.h"
#include "viewport.h"
#include "test_report.h"
#include "test_result.h"
#include "report_storage.h"
#include "test_guide.h"
#include "test_lifecycle.h"
#include "diagnostic_audio.h"
#ifdef CTR_M1_ONLY
#include "m1_storage_probe.h"
#if defined(CTR_STREAM_AUDIO) || defined(CTR_AUDIO_ONLY) || defined(CTR_SCENE_ONLY) || defined(CTR_COMPILED_HOMEBREW) || CTR_GUEST_DEFAULT != 0
#error M1_Check_requires_the_full_native_platform_profile
#endif
static CtrM1StorageEvidence m1_storage={.result=CTR_M1_STORAGE_ARGUMENT,.error_stage="not_run"};
static void m1_storage_line(char *line,size_t capacity) {
    snprintf(line,capacity,"M1_STORAGE protocol=scratch-v1 sentinel_written=%u sentinel_readback=%u fresh_reload=%u previous_recovered=%u pending_rejected=%u invalid_path_rejected=%u final_readback=%u result=%s errno=%d error_stage=%s writes=%u readbacks=%u recoveries=%u; software_recovery_not_power_loss=1",
        m1_storage.sentinel_written,m1_storage.sentinel_readback,m1_storage.fresh_reload,
        m1_storage.previous_recovered,m1_storage.pending_rejected,m1_storage.invalid_path_rejected,
        m1_storage.final_readback,ctr_m1_storage_result_name(m1_storage.result),m1_storage.error_code,
        m1_storage.error_stage?m1_storage.error_stage:"none",m1_storage.writes,m1_storage.readbacks,m1_storage.recoveries);
}
#endif
#ifdef CTR_STREAM_AUDIO
#include "audio_stream_app.h"
#include "audio_stream_diagnostic.h"
#include "ctr_audio.h"
#include "ctr_audio_fixture.h"
static ctr_stream_app stream_app;
static ctr_native_pcm stream_pcm;
static ctr_native_pcm_report stream_native;
static CtrAudioFixture stream_fixtures[3];
static CtrAudioEngine stream_engine;
static int16_t stream_samples[72000u*2u];
static bool stream_fixtures_ready;
static uint32_t stream_render_frames,stream_render_us,stream_render_pulls;
static uint64_t stream_decoded_frames;
static CtrAudioResult stream_render_result;
static uint32_t stream_golden_expected,stream_golden_observed;
static bool stream_golden_checked,stream_golden_match;
#endif
#include "image_scale.h"
#include "guest_slice.h"
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
#include "ctr_fs_snapshot.h"
#include "ctr_romfs_file.h"
#include "ctr_sha256.h"
#ifdef CTR_SCENE_ONLY
#include "ctr_scene_fixture.h"
static uint8_t scene_pixels[2][CTR_SCENE_FIXTURE_BYTES];
static ctr_scene_report scene_reports[2];
static int scene_test_result=-1;
static ctr_scene_result scene_results[2]={CTR_SCENE_ARGUMENT,CTR_SCENE_ARGUMENT};
static uint64_t scene_us[2];
#endif
#ifdef CTR_FILESYSTEM_HOMEBREW
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
#ifdef CTR_ROMFS_HOMEBREW
#include "romfs-image.h"
static ctr_fs_romfs romfs_view;
static ctr_romfs_file romfs_file;
static bool romfs_view_ready;
#ifndef CTR_ROMFS_FILE
static bool read_romfs(void *context,uint64_t offset,void *output,uint32_t size) {
    (void)context;
    if(offset>sizeof(romfs_bytes) || size>sizeof(romfs_bytes)-offset)return false;
    memcpy(output,romfs_bytes+(size_t)offset,size);return true;
}
#endif
#endif
#endif

enum { TOP_W=400, BOTTOM_W=320, GUEST_H=240, AUDIO_SAMPLES=12000 };
static uint32_t top_image[TOP_W*GUEST_H], bottom_image[BOTTOM_W*GUEST_H];
#if !defined(CTR_COMPILED_HOMEBREW) && !defined(CTR_STREAM_AUDIO)
static int16_t audio_samples[AUDIO_SAMPLES] __attribute__((aligned(64)));
#endif
static ctr_test_report test_report;
static ctr_test_result test_result;
static bool selftests_done;
static int app_exit_result=-1,snapshot_errno;
static ctr_fs_snapshot_result snapshot_load_result=CTR_FS_SNAPSHOT_MISSING;
static char latest_guest_message[96];
static bool green_response_seen;
static uint64_t green_response_ms;
static struct {
    bool captured,opened;
    ctr_guest_state state;
    ctr_cpu_result fault;
    ctr_mem_result memory_fault;
    uint32_t pc,detail;
    uint64_t instructions,fallback;
    ctr_guest_gsp gx;
} final_guest;
static ctr_report_storage report_storage={.client=-1};
static uint64_t report_sync_max_us;
static ctr_test_guide test_guide;
static ctr_test_lifecycle lifecycle;
static atomic_uint home_exit_requested;
static unsigned freeze_trace_frames;
static ctr_scale_plan tv_scale,drc_scale;
static ctr_scale_cache tv_cache,drc_cache;
static struct {
    uint64_t updates,full,reused,scaled_pixels,publish_bytes,publish_ranges;
    size_t heap_bytes;
} publication[2];
static bool guest_green_frame;
#ifdef CTR_COMPILED_HOMEBREW
static OSTime a_request_time;
static uint64_t a_request_frame;
#endif
static bool a_response_pending;
static bool gsp_pass_seen,fresh_save_reload_seen;
static bool report_handler_added,report_error_announced;
static OSTime session_started;
static unsigned guest_sessions,completed_frames,input_samples,input_errors;
static unsigned button_presses[12],touch_samples,border_touch_samples;
static uint32_t frame_min_us=UINT32_MAX,frame_max_us;
static uint64_t frame_total_us;
enum { PH_GUEST,PH_DECODE,PH_SCALE,PH_PUBLISH,PH_WAIT,PH_PRESENT,PH_STORAGE,PH_COUNT };
static struct { uint64_t calls,total_us,max_us; } phases[PH_COUNT];
static const char *const phase_names[]={"guest","decode","scale","publish","gpu_wait","present","storage"};
static ctr_slice_report last_slice={.reason=CTR_SLICE_ARGUMENT};
static uint64_t slice_stops[11],drawn_latency_ms;
static bool drawn_frame_submitted,scene_submitted;
static uint64_t draw_ready_serial;
static void phase_add(unsigned phase,uint64_t elapsed) {
    ++phases[phase].calls;phases[phase].total_us+=elapsed;
    if(elapsed>phases[phase].max_us)phases[phase].max_us=elapsed;
}
static void phase_log(void) {
    for(unsigned i=0;i<PH_COUNT;++i)WHBLogPrintf("PHASE name=%s calls=%llu total_us=%llu max_us=%llu scope=diagnostic_host_not_game_fps",
        phase_names[i],(unsigned long long)phases[i].calls,(unsigned long long)phases[i].total_us,(unsigned long long)phases[i].max_us);
    for(unsigned i=0;i<2;++i)WHBLogPrintf("PUBLISH display=%s policy=changed_spans_rows updates=%llu full=%llu reused=%llu scaled_pixels=%llu requested_bytes=%llu ranges=%llu heap_bytes=%u; scale_plus_publish_compares_to_v0241_scale=1",
        i?"DRC":"TV",(unsigned long long)publication[i].updates,(unsigned long long)publication[i].full,
        (unsigned long long)publication[i].reused,(unsigned long long)publication[i].scaled_pixels,
        (unsigned long long)publication[i].publish_bytes,(unsigned long long)publication[i].publish_ranges,
        (unsigned)publication[i].heap_bytes);
    WHBLogPrintf("SLICE quantum=10000 max_instructions=500000 max_us=8000 max_calls=50 last_reason=%s last_calls=%u last_instructions=%llu last_us=%llu drawn_submitted=%u ready_serial=%llu drawn_ms=%llu guest_session=%u soft_time_bound=1",
        ctr_slice_reason_name(last_slice.reason),last_slice.calls,(unsigned long long)last_slice.instructions,
        (unsigned long long)last_slice.elapsed_us,drawn_frame_submitted,(unsigned long long)draw_ready_serial,
        (unsigned long long)drawn_latency_ms,guest_sessions);
    for(unsigned i=0;i<11;++i)if(slice_stops[i])WHBLogPrintf("SLICE STOP reason=%s count=%llu",
        ctr_slice_reason_name((ctr_slice_reason)i),(unsigned long long)slice_stops[i]);
}
static bool left_stick_seen,right_stick_seen,guest_fault_seen,allocation_checked;
static bool allocation_failure_seen;
static int alloc_ok_for_report;
static uint16_t touch_min_x=UINT16_MAX,touch_min_y=UINT16_MAX,touch_max_x,touch_max_y;
#ifdef CTR_PICA_HOMEBREW
#define REPORT_APPLICATION "compiled-pica-draws"
#elif defined(CTR_GX_HOMEBREW)
#define REPORT_APPLICATION "compiled-gx-transfer"
#elif defined(CTR_ROMFS_FILE)
#define REPORT_APPLICATION "compiled-romfs-file"
#elif defined(CTR_ROMFS_HOMEBREW)
#define REPORT_APPLICATION "compiled-romfs"
#elif defined(CTR_FILESYSTEM_HOMEBREW)
#define REPORT_APPLICATION "compiled-save"
#elif defined(CTR_SHALLOW_AOT)
#define REPORT_APPLICATION "compiled-guarded"
#elif defined(CTR_COMPILED_HOMEBREW)
#define REPORT_APPLICATION "compiled-interactive"
#elif defined(CTR_EXTERNAL_AOT)
#define REPORT_APPLICATION "upstream-guest"
#elif CTR_GUEST_DEFAULT == 1
#define REPORT_APPLICATION "original-guest"
#else
#ifdef CTR_M1_ONLY
#define REPORT_APPLICATION "native-m1"
#elif defined(CTR_STREAM_AUDIO)
#define REPORT_APPLICATION "native-audio-streams"
#elif defined(CTR_AUDIO_ONLY)
#define REPORT_APPLICATION "native-audio"
#elif defined(CTR_SCENE_ONLY)
#define REPORT_APPLICATION "native-software-scene"
#else
#define REPORT_APPLICATION "native"
#endif
#endif
static const uint32_t input_masks[]={VPAD_BUTTON_A,VPAD_BUTTON_B,VPAD_BUTTON_MINUS,VPAD_BUTTON_PLUS,
    VPAD_BUTTON_RIGHT,VPAD_BUTTON_LEFT,VPAD_BUTTON_UP,VPAD_BUTTON_DOWN,VPAD_BUTTON_R,VPAD_BUTTON_L,
    VPAD_BUTTON_X,VPAD_BUTTON_Y};
static const char *const input_names[]={"A","B","Minus","Plus","Right","Left","Up","Down","R","L","X","Y"};
#if !defined(CTR_COMPILED_HOMEBREW) && !defined(CTR_STREAM_AUDIO)
static AXVoice *tone_voice;
#endif
static unsigned tone_count;
static int audio_ready;
static ctr_diagnostic_audio diagnostic_audio;
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

static uint32_t home_button_exit(void *context) {
    (void)context;atomic_store(&home_exit_requested,1);return 0;
}

#ifdef CTR_STREAM_AUDIO
static void stream_log_native(const char *stage) {
    ctr_native_pcm_snapshot(&stream_pcm,&stream_native);
    WHBLogPrintf("STREAM NATIVE stage=%s status=%s result=%s prepared=%u running=%u/%u cursor=%u/%u frames=%u pcm_fnv1a=%08x prepares=%u plays=%u pauses=%u resumes=%u stops=%u releases=%u completions=%u failures=%u ownership_losses=%u",
        stage,ctr_native_pcm_status_name(stream_native.status),ctr_native_pcm_result_name(stream_native.result),
        stream_native.prepared,stream_native.running[0],stream_native.running[1],stream_native.cursor[0],stream_native.cursor[1],
        stream_native.frames,stream_native.pcm_fnv1a,stream_native.prepares,stream_native.plays,stream_native.pauses,
        stream_native.resumes,stream_native.stops,stream_native.releases,stream_native.completions,stream_native.failures,stream_native.ownership_losses);
}
static bool stream_result_snapshot(const char *stage) {
    char body[CTR_TEST_RESULT_BYTES],results[1536];
    ctr_native_pcm_snapshot(&stream_pcm,&stream_native);
    if(!ctr_stream_app_results(&stream_app,results,sizeof(results)))return false;
    int n=snprintf(body,sizeof(body),
        "Source: %s dirty=%u | Evidence schema: 2\n"
        "Stage: %s\nHost exit result: %d (-1 means still running)\n"
        "Guide: %s | passed=%u failed=%u skipped=%u current_step=%u\n"
        "Audio sessions: %u | attempts=%u foreground_losses=%u\n"
        "Native audio context: result=%s owned=%u inherited=%u attempts=%u successes=%u failures=%u resets=%u releases=%u\n"
        "Stream native: status=%s result=%s prepared=%u running=%u/%u cursor=%u/%u frames=%u pcm_fnv1a=%08x\n"
        "Stream counters: prepares=%u plays=%u pauses=%u resumes=%u stops=%u releases=%u completions=%u failures=%u ownership_losses=%u\n"
        "Portable render: fixtures_ready=%u pulls=%u frames=%u output_rate=48000 render_us=%u decoded_frames=%llu clipped_samples=%llu; fixed finite prerender, no callback mixing\n"
        "PCM golden: checked=%u expected_fnv1a=%08x observed_fnv1a=%08x match=%u portable_result=%s\n"
        "Input: samples=%u errors=%u A_presses=%u Minus_presses=%u\n"
        "Frames: host=%u | frame_us min=%u mean=%llu max=%u; diagnostic overhead, not game FPS\n"
        "Full trace: %s | file=%s errno=%d operation=%s FSA=%d commits=%llu\n"
        "Previous result writes verified: %llu\nNamed guide results:\n%s"
        "Human confirmations describe TV/GamePad hearing; native completion is not proof of audible output.\n"
        "Original CSTM/portable mixer/native finite PCM diagnostic only; guest DSP IPC, guest execution, continuous streaming and game compatibility unavailable.\n"
        "User must separately report launcher silence/return. No full milestone PASS is established.\n",
        CTR_SOURCE_COMMIT,CTR_SOURCE_DIRTY,stage,app_exit_result,
        !stream_app.started?"NOT_STARTED":stream_app.done?"COMPLETE":stream_app.cancelled?"CANCELLED":"RUNNING",
        stream_app.passed,stream_app.failed,stream_app.skipped,stream_app.step+1,
        stream_app.session,stream_app.attempts,stream_app.foreground_losses,
        ctr_diagnostic_audio_result_name(diagnostic_audio.result),diagnostic_audio.owned,diagnostic_audio.last_inherited,
        diagnostic_audio.attempts,diagnostic_audio.successes,diagnostic_audio.failures,diagnostic_audio.inherited_resets,diagnostic_audio.releases,
        ctr_native_pcm_status_name(stream_native.status),ctr_native_pcm_result_name(stream_native.result),stream_native.prepared,
        stream_native.running[0],stream_native.running[1],stream_native.cursor[0],stream_native.cursor[1],stream_native.frames,stream_native.pcm_fnv1a,
        stream_native.prepares,stream_native.plays,stream_native.pauses,stream_native.resumes,stream_native.stops,stream_native.releases,
        stream_native.completions,stream_native.failures,stream_native.ownership_losses,
        stream_fixtures_ready,stream_render_pulls,stream_render_frames,stream_render_us,(unsigned long long)stream_decoded_frames,
        (unsigned long long)stream_engine.clipped_samples,stream_golden_checked,stream_golden_expected,stream_golden_observed,
        stream_golden_match,ctr_audio_result_name(stream_render_result),input_samples,input_errors,button_presses[0],button_presses[2],
        completed_frames,completed_frames?frame_min_us:0,(unsigned long long)(completed_frames?frame_total_us/completed_frames:0),frame_max_us,
        report_handler_added&&!test_report.failed?"SAVING":"FAILED",test_report.filename,test_report.error_code,
        test_report.error_stage?test_report.error_stage:"none",report_storage.last_status,(unsigned long long)test_report.commits,
        (unsigned long long)test_result.verified_writes,results);
    if(n<0 || (size_t)n>=sizeof(body)) {
        test_result.failed=true;test_result.error_code=EMSGSIZE;test_result.error_stage="stream_format";return false;
    }
    return ctr_test_result_update(&test_result,body);
}
#endif

static bool result_snapshot(const char *stage) {
    if(!test_result.ready || test_result.failed)return false;
#ifdef CTR_STREAM_AUDIO
    return stream_result_snapshot(stage);
#endif
    char body[CTR_TEST_RESULT_BYTES],guide_results[1536],scene_summary[384]="",phase_summary[1024],publication_summary[768];
    size_t used=0;
    for(unsigned i=0;i<PH_COUNT;++i) {
        int length=snprintf(phase_summary+used,sizeof(phase_summary)-used,"Phase %s: calls=%llu total_us=%llu max_us=%llu\n",
            phase_names[i],(unsigned long long)phases[i].calls,(unsigned long long)phases[i].total_us,(unsigned long long)phases[i].max_us);
        if(length<0 || (size_t)length>=sizeof(phase_summary)-used) {
            test_result.failed=true;test_result.error_code=EMSGSIZE;test_result.error_stage="phase_format";return false;
        }
        used+=(size_t)length;
    }
    used=0;
    for(unsigned i=0;i<2;++i) {
        int length=snprintf(publication_summary+used,sizeof(publication_summary)-used,
            "Scale cache %s: updates=%llu full=%llu reused=%llu scaled_pixels=%llu publish_bytes=%llu ranges=%llu heap_bytes=%u\n",
            i?"DRC":"TV",(unsigned long long)publication[i].updates,(unsigned long long)publication[i].full,
            (unsigned long long)publication[i].reused,(unsigned long long)publication[i].scaled_pixels,
            (unsigned long long)publication[i].publish_bytes,(unsigned long long)publication[i].publish_ranges,(unsigned)publication[i].heap_bytes);
        if(length<0 || (size_t)length>=sizeof(publication_summary)-used) {
            test_result.failed=true;test_result.error_code=EMSGSIZE;test_result.error_stage="publication_format";return false;
        }
        used+=(size_t)length;
    }
#ifdef CTR_SCENE_ONLY
    snprintf(scene_summary,sizeof(scene_summary),
        "Owned software scenes: pixel_selftest=%d u8=%s u16=%s render_us=%llu/%llu draws=%u/%u triangles=%u/%u fragments=%u/%u shader_steps=%u/%u; CPU raster/GX2 copy, not guest GSP or GPU shader translation\n",
        scene_test_result,ctr_scene_result_name(scene_results[0]),ctr_scene_result_name(scene_results[1]),
        (unsigned long long)scene_us[0],(unsigned long long)scene_us[1],
        scene_reports[0].draws,scene_reports[1].draws,scene_reports[0].triangles,scene_reports[1].triangles,
        scene_reports[0].fragments,scene_reports[1].fragments,
        scene_reports[0].shader_instructions,scene_reports[1].shader_instructions);
#endif
    if(!ctr_test_guide_results(&test_guide,guide_results,sizeof(guide_results))) {
        test_result.failed=true;test_result.error_code=EMSGSIZE;test_result.error_stage="guide_format";return false;
    }
    int n=snprintf(body,sizeof(body),
        "Source: %s dirty=%u | Evidence schema: 2\n"
        "Stage: %s\nHost exit result: %d (-1 means still running)\nGuest sessions: %u\n"
        "Guide: %s | passed=%u failed=%u skipped=%u current_step=%u\n"
        "Fresh-launch save reload confirmed: %u\n"
        "Native audio context: result=%s owned=%u inherited=%u attempts=%u successes=%u failures=%u resets=%u releases=%u tone_ready=%u tone_requests=%u; guest audio unavailable\n"
        "Self-tests finished: %u | CPU=%d services=%d heap=%d objects=%d HID=%d GSP=%d relay=%d stack=%d threads=%d\n"
        "%s"
        "Input: samples=%u errors=%u A_presses=%u Minus_presses=%u\n"
        "Guest: state=%s pc=%08x instructions=%llu fallback=%llu fault=%s detail=%08x memory=%s\n"
        "Frames: host=%u guest=%llu | new_green_after_A=%u latency_ms=%llu\n"
        "GX: completed=%llu DMA=%llu fill=%llu display=%llu texture_copy=%llu cache=%llu last_header=%08x last_detail=%08x\n"
        "PICA: completed=%llu lists=%llu writes=%llu finalizers=%llu last_result=%u last_offset=%u last_register=%03x\n"
        "Guest software draws: draws=%llu triangles=%llu fragments=%llu shader_steps=%llu scene_error=%u vertex_error=%u shader_error=%u; no GPU shader translation\n"
        "Host frame us: min=%u mean=%llu max=%u (diagnostic overhead, not game performance)\n"
        "%s"
        "%s"
        "Draw submitted: %u serial=%llu latency_ms=%llu | Slice: reason=%s calls=%u instructions=%llu us=%llu; soft 8000us / 500000 instructions\n"
        "Latest guest message: %s\nSave: load_status=%d persist_errno=%d\n"
        "Full trace: %s | file=%s errno=%d operation=%s FSA=%d commits=%llu\n"
        "Previous result writes verified: %llu\n"
        "Named guide results:\n%s"
        "User must separately report picture accuracy and launcher return.\n"
        "No commercial-game compatibility or full milestone PASS is established.\n",
        CTR_SOURCE_COMMIT,CTR_SOURCE_DIRTY,stage,app_exit_result,guest_sessions,!test_guide.started?"NOT_STARTED":test_guide.done?"COMPLETE":test_guide.cancelled?"CANCELLED":"RUNNING",
        test_guide.passed,test_guide.failed,test_guide.skipped,test_guide.step+1,test_guide.fresh_reload_confirmed,
        ctr_diagnostic_audio_result_name(diagnostic_audio.result),diagnostic_audio.owned,diagnostic_audio.last_inherited,
        diagnostic_audio.attempts,diagnostic_audio.successes,diagnostic_audio.failures,
        diagnostic_audio.inherited_resets,diagnostic_audio.releases,audio_ready,tone_count,selftests_done,
        cpu_test_result,service_test_result,heap_test_result,kernel_test_result,hid_test_result,gsp_test_result,
        relay_test_result,stack_test_result,thread_test_result,scene_summary,input_samples,input_errors,button_presses[0],button_presses[2],
        (final_guest.captured?final_guest.opened:gsp_ready)?ctr_guest_state_name(final_guest.captured?final_guest.state:gsp_guest.state):"not-opened",
        final_guest.captured?final_guest.pc:gsp_guest.cpu.r[15],
        (unsigned long long)(final_guest.captured?final_guest.instructions:gsp_guest.cpu.instructions),
        (unsigned long long)(final_guest.captured?final_guest.fallback:gsp_guest.cpu.fallback_instructions),
        ctr_cpu_result_name(final_guest.captured?final_guest.fault:gsp_guest.fault),
        final_guest.captured?final_guest.detail:gsp_guest.fault_detail,
        ctr_mem_result_name(final_guest.captured?final_guest.memory_fault:gsp_guest.cpu.memory_fault),
        completed_frames,(unsigned long long)gsp_presented,green_response_seen,(unsigned long long)green_response_ms,
        (unsigned long long)(final_guest.captured?final_guest.gx.gx_commands:gsp_guest.gsp.gx_commands),
        (unsigned long long)(final_guest.captured?final_guest.gx.gx_dma:gsp_guest.gsp.gx_dma),
        (unsigned long long)(final_guest.captured?final_guest.gx.gx_fills:gsp_guest.gsp.gx_fills),
        (unsigned long long)(final_guest.captured?final_guest.gx.gx_transfers:gsp_guest.gsp.gx_transfers),
        (unsigned long long)(final_guest.captured?final_guest.gx.gx_copies:gsp_guest.gsp.gx_copies),
        (unsigned long long)(final_guest.captured?final_guest.gx.gx_cache:gsp_guest.gsp.gx_cache),
        final_guest.captured?final_guest.gx.last_gx_header:gsp_guest.gsp.last_gx_header,
        final_guest.captured?final_guest.gx.last_gx_detail:gsp_guest.gsp.last_gx_detail,
        (unsigned long long)(final_guest.captured?final_guest.gx.gx_pica:gsp_guest.gsp.gx_pica),
        (unsigned long long)(final_guest.captured?final_guest.gx.pica.committed_lists:gsp_guest.gsp.pica.committed_lists),
        (unsigned long long)(final_guest.captured?final_guest.gx.pica.register_writes:gsp_guest.gsp.pica.register_writes),
        (unsigned long long)(final_guest.captured?final_guest.gx.pica.finalizations:gsp_guest.gsp.pica.finalizations),
        final_guest.captured?final_guest.gx.last_pica_result:gsp_guest.gsp.last_pica_result,
        final_guest.captured?final_guest.gx.last_pica_offset:gsp_guest.gsp.last_pica_offset,
        final_guest.captured?final_guest.gx.last_pica_register:gsp_guest.gsp.last_pica_register,
        (unsigned long long)(final_guest.captured?final_guest.gx.pica_draws:gsp_guest.gsp.pica_draws),
        (unsigned long long)(final_guest.captured?final_guest.gx.pica_triangles:gsp_guest.gsp.pica_triangles),
        (unsigned long long)(final_guest.captured?final_guest.gx.pica_fragments:gsp_guest.gsp.pica_fragments),
        (unsigned long long)(final_guest.captured?final_guest.gx.pica_shader_instructions:gsp_guest.gsp.pica_shader_instructions),
        final_guest.captured?final_guest.gx.last_scene_result:gsp_guest.gsp.last_scene_result,
        final_guest.captured?final_guest.gx.last_vertex_result:gsp_guest.gsp.last_vertex_result,
        final_guest.captured?final_guest.gx.last_shader_result:gsp_guest.gsp.last_shader_result,
        completed_frames?frame_min_us:0,(unsigned long long)(completed_frames?frame_total_us/completed_frames:0),frame_max_us,
        phase_summary,publication_summary,drawn_frame_submitted,(unsigned long long)draw_ready_serial,(unsigned long long)drawn_latency_ms,
        ctr_slice_reason_name(last_slice.reason),last_slice.calls,(unsigned long long)last_slice.instructions,(unsigned long long)last_slice.elapsed_us,
        latest_guest_message[0]?latest_guest_message:"none",snapshot_load_result,snapshot_errno,
        report_handler_added && !test_report.failed?"SAVING":"FAILED",test_report.filename,test_report.error_code,
        test_report.error_stage?test_report.error_stage:"none",report_storage.last_status,
        (unsigned long long)test_report.commits,(unsigned long long)test_result.verified_writes,guide_results);
    if(n<0 || (size_t)n>=sizeof(body)) {
        test_result.failed=true;test_result.error_code=EMSGSIZE;test_result.error_stage="format";return false;
    }
    OSTime storage_started=OSGetTime();
#ifdef CTR_M1_ONLY
    char storage_line[768];m1_storage_line(storage_line,sizeof(storage_line));
    int extra=snprintf(body+n,sizeof(body)-(size_t)n,"%s\n",storage_line);
    if(extra<0 || (size_t)extra>=sizeof(body)-(size_t)n) {
        test_result.failed=true;test_result.error_code=EMSGSIZE;test_result.error_stage="m1_format";return false;
    }
#endif
    bool ok=ctr_test_result_update(&test_result,body);
    phase_add(PH_STORAGE,OSTicksToMicroseconds(OSGetTime()-storage_started));
    if(!ok)OSReport("CTR2WUP RESULT FAILED errno=%d operation=%s path=%s\n",test_result.error_code,
        test_result.error_stage?test_result.error_stage:"unknown",test_result.path);
    return ok;
}

static bool report_commit(const char *stage) {
    if (test_report.failed) return false;
    WHBLogPrintf("REPORT COMMIT stage=%s sequence=%llu policy=file_sync_close_volume_sync",
        stage,(unsigned long long)(test_report.commits+1));
    OSTime start=OSGetTime();
    bool ok=ctr_test_report_commit(&test_report);
    uint64_t elapsed=OSTicksToMicroseconds(OSGetTime()-start);
    phase_add(PH_STORAGE,elapsed);
    if(elapsed>report_sync_max_us)report_sync_max_us=elapsed;
    if(!ok)OSReport("CTR2WUP REPORT COMMIT FAILED stage=%s errno=%d operation=%s FSA=%d\n",
        stage,test_report.error_code,test_report.error_stage?test_report.error_stage:"unknown",report_storage.last_status);
    return ok;
}

static void freeze_checkpoint(const char *stage) {
    if(!freeze_trace_frames)return;
    WHBLogPrintf("INPUT CHECKPOINT stage=%s remaining=%u state=%s pc=%08x instructions=%llu swaps=%llu",
        stage,freeze_trace_frames,ctr_guest_state_name(gsp_guest.state),gsp_guest.cpu.r[15],
        (unsigned long long)gsp_guest.cpu.instructions,(unsigned long long)gsp_guest.gsp.swaps[0]);
#ifdef CTR_COMPILED_HOMEBREW
    unsigned depth=0;uint64_t unwinds=0;
    ctr_recomp_depth_stats(&gsp_guest,&depth,&unwinds);
    OSThread *thread=OSGetCurrentThread();uint32_t sp=OSGetStackPointer();
    uintptr_t end=thread?(uintptr_t)thread->stackEnd:0;
    WHBLogPrintf("AOT checkpoint max_depth=%u unwinds=%llu call_limit=%u host_stack_headroom=%lu",
        depth,(unsigned long long)unwinds,CTR_RECOMP_CALL_LIMIT,(unsigned long)(end && sp>=end?sp-end:0));
#endif
    /* Preserve the last completed stage before entering the next risky call.
       This is intentionally expensive diagnostic I/O, not a game timing metric. */
    report_commit(stage);
    if(!result_snapshot(stage))freeze_trace_frames=0;
}

static void guest_debug(void *context,const char *message) {
    (void)context; WHBLogPrintf("guest_debug %s",message);
    if(context==&gsp_guest) {
        snprintf(latest_guest_message,sizeof(latest_guest_message),"%s",message);
        for(unsigned i=0;latest_guest_message[i];++i)
            if(latest_guest_message[i]=='\n' || latest_guest_message[i]=='\r')latest_guest_message[i]=' ';
    }
    if(strstr(message,"CTR2WUP compiled interactive PASS") ||
       strstr(message,"CTR2WUP compiled PICA draws PASS") ||
       strstr(message,"CTR2WUP compiled GX transfers PASS") ||
       strstr(message,"CTR2WUP compiled RomFS PASS") ||
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
#ifdef CTR_FILESYSTEM_HOMEBREW
static bool save_diagnostic_snapshot(void *context,const ctr_fs_volume *volume) {
    errno=0;bool ok=ctr_fs_snapshot_save(context,volume);
    snapshot_errno=ok?0:errno?errno:EIO;
    WHBLogPrintf("snapshot_commit ok=%u errno=%d archive=%u",ok,snapshot_errno,volume->archive_id);
    result_snapshot("snapshot_commit");return ok;
}
#endif
static void restart_guest(void) {
    if(!test_result.ready || test_result.failed) return;
    if(guest_sessions) {
        WHBLogPrintf("Restart requested: prior framebuffer=%s prior GSP=%s prior GSP fallback=%llu fault=%s detail=%08x",
            ctr_guest_state_name(frame_guest.state),ctr_guest_state_name(gsp_guest.state),
            (unsigned long long)gsp_guest.cpu.fallback_instructions,ctr_cpu_result_name(gsp_guest.fault),gsp_guest.fault_detail);
        if(frame_guest.state==CTR_GUEST_FAULTED || gsp_guest.state==CTR_GUEST_FAULTED)guest_fault_seen=true;
    }
    ++guest_sessions;
    WHBLogPrintf("SCALE CACHE invalidate=guest_restart next_update=full");
    ctr_scale_cache_invalidate(&tv_cache);ctr_scale_cache_invalidate(&drc_cache);
    gsp_pass_seen=false;
    latest_guest_message[0]=0;
    WHBLogPrintf("Guest session %u starting",guest_sessions);
    report_commit("guest_start");
    if(!result_snapshot("guest_start"))return;
    ctr_guest_close(&frame_guest);
#ifdef CTR_COMPILED_HOMEBREW
    guest_ready=0;
#else
    guest_ready=ctr_frame_open(&frame_guest);
#endif
    frame_guest.debug=guest_debug;frame_guest.debug_context=&frame_guest;frame_guest.cpu.log=guest_trace;
    guest_origin=OSGetTime(); presented_frames=0;
    WHBLogPrintf("guest_open ok=%u profile=ctr2wup-framebuffer-fixture-v1 mode=AOT custom_frame_io=1 GSP_PICA=unavailable",guest_ready);
    ctr_guest_close(&gsp_guest);
#ifdef CTR_ROMFS_HOMEBREW
    if(romfs_view_ready)ctr_fs_romfs_close(&romfs_view);
    ctr_romfs_file_close(&romfs_file);
#ifdef CTR_ROMFS_FILE
    romfs_view_ready=ctr_romfs_file_open(&romfs_file,"fs:/vol/external01/ctr2wup/romfs-test.bin",romfs_sha256);
#else
    ctr_sha256 hash;uint8_t digest[32];ctr_sha256_init(&hash);
    romfs_view_ready=ctr_sha256_update(&hash,romfs_bytes,sizeof(romfs_bytes)) && ctr_sha256_final(&hash,digest) &&
        !memcmp(digest,romfs_sha256,32) && ctr_fs_romfs_init(&romfs_view,sizeof(romfs_bytes),read_romfs,NULL);
#endif
    WHBLogPrintf("romfs_provider open=%u errno=%d sha256_verified=%u",romfs_view_ready,romfs_view_ready?0:errno,romfs_view_ready);
#endif
#ifdef CTR_FILESYSTEM_HOMEBREW
    if(diagnostic_save_ready)ctr_fs_volume_close(&diagnostic_save);
    diagnostic_save_ready=ctr_fs_volume_init(&diagnostic_save,4,true);
    filesystem_message[0]=0;
    ctr_fs_snapshot_result loaded=ctr_fs_snapshot_load(&diagnostic_save,diagnostic_save_path);
    snapshot_load_result=loaded;snapshot_errno=0;
    diagnostic_save.persist=save_diagnostic_snapshot;diagnostic_save.persist_context=diagnostic_save_path;
#endif
#ifdef CTR_COMPILED_HOMEBREW
    gsp_ready=ctr_guest_open(&gsp_guest,&boot_image,NULL,0) && ctr_recomp_attach(&gsp_guest,false);
    if(compiled_sd_ready)ctr_fs_volume_close(&compiled_sd);
    compiled_sd_ready=ctr_fs_volume_init(&compiled_sd,9,false);
    gsp_ready=gsp_ready && compiled_sd_ready && ctr_apt_configure(&gsp_guest) && ctr_fs_mount(&gsp_guest,&compiled_sd);
#ifdef CTR_ROMFS_HOMEBREW
    gsp_ready=gsp_ready && romfs_view_ready && ctr_fs_mount_romfs(&gsp_guest,
#ifdef CTR_ROMFS_FILE
        &romfs_file.view
#else
        &romfs_view
#endif
    );
    if(!gsp_ready) {
        gsp_guest.state=CTR_GUEST_FAULTED;gsp_guest.fault=CTR_CPU_MEMORY_FAULT;
        snprintf(latest_guest_message,sizeof(latest_guest_message),"RomFS provider/load failed errno=%d",errno);
    }
#endif
#else
    gsp_ready=ctr_relay_fixture_open(&gsp_guest);
#endif
    gsp_guest.debug=guest_debug;gsp_guest.debug_context=&gsp_guest;gsp_guest.cpu.log=guest_trace;
#ifdef CTR_FILESYSTEM_HOMEBREW
    gsp_ready=gsp_ready && diagnostic_save_ready && loaded!=CTR_FS_SNAPSHOT_ERROR && ctr_fs_mount(&gsp_guest,&diagnostic_save);
    if(!gsp_ready) { gsp_guest.state=CTR_GUEST_FAULTED;snprintf(filesystem_message,sizeof(filesystem_message),"SAVE LOAD / MOUNT FAIL"); }
    WHBLogPrintf("filesystem_load result=%d ok=%u path=%s console_result_required=1",loaded,gsp_ready,diagnostic_save_path);
#endif
    gsp_origin=OSGetTime();gsp_presented=0;
    drawn_frame_submitted=false;draw_ready_serial=0;drawn_latency_ms=0;
    guest_green_frame=false;a_response_pending=false;green_response_seen=false;green_response_ms=0;
#ifdef CTR_COMPILED_HOMEBREW
#ifdef CTR_ROMFS_HOMEBREW
    WHBLogPrintf("gsp_guest_open ok=%u profile=original_compiled_libctru_romfs mode=external_AOT PICA=unavailable",gsp_ready);
#else
#ifdef CTR_FILESYSTEM_HOMEBREW
    WHBLogPrintf("gsp_guest_open ok=%u profile=original_compiled_libctru_filesystem mode=external_AOT PICA=unavailable",gsp_ready);
#else
    WHBLogPrintf("gsp_guest_open ok=%u profile=original_compiled_libctru_interactive mode=external_AOT PICA=unavailable",gsp_ready);
#endif
#endif
#else
    WHBLogPrintf("gsp_guest_open ok=%u profile=original_hid_gsp_shared_relay_rgb565 mode=original_AOT PICA=unavailable",gsp_ready);
#endif
    result_snapshot("guest_ready");
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
    if (test_result.failed || !ready || guest->state==CTR_GUEST_FAULTED || guest->state==CTR_GUEST_EXITED) return;
    uint32_t buttons=0;for(unsigned i=0;i<12;++i)if(pad->hold&masks[i])buttons|=1u<<i;
    uint64_t now=1+(uint64_t)OSTicksToMicroseconds(OSGetTime()-origin)*1000u;
    if (!ctr_hid_update(guest,buttons,circle_axis(pad->leftStick.x),circle_axis(pad->leftStick.y),inside,x,y,now)) {
        guest->state=CTR_GUEST_FAULTED;guest->fault=CTR_CPU_UNSUPPORTED;
        WHBLogPrintf("HID live publication FAIL time_ns=%llu",(unsigned long long)now);return;
    }
    ctr_guest_run(guest,budget,now,true);
}
#ifdef CTR_PICA_HOMEBREW
typedef struct { const VPADStatus *pad;bool inside;uint16_t x,y;uint64_t serial; } pica_slice_context;
static uint64_t slice_clock(void *context) { (void)context;return OSTicksToMicroseconds(OSGetTime()); }
static ctr_slice_step slice_execute(void *context,unsigned budget) {
    pica_slice_context *c=context;uint64_t before=gsp_guest.cpu.instructions;
    update_input_guest(&gsp_guest,gsp_ready,gsp_origin,c->pad,c->inside,c->x,c->y,budget);
    ctr_slice_state state=gsp_guest.state==CTR_GUEST_READY?CTR_SLICE_RUN:
        gsp_guest.state==CTR_GUEST_YIELDED?CTR_SLICE_YIELD:
        gsp_guest.state==CTR_GUEST_SLEEPING || gsp_guest.state==CTR_GUEST_WAITING?CTR_SLICE_WAIT:CTR_SLICE_STOP;
    ctr_slice_step step={gsp_guest.cpu.instructions-before,state,
        !gsp_guest.gsp.black && gsp_guest.gsp.swaps[0]>c->serial &&
        gsp_guest.gsp.swaps[0]==gsp_guest.gsp.swaps[1]};
    return step;
}
#endif

static void file_log(const char *message) {
    if(!ctr_test_report_line(&test_report,message) && !report_error_announced) {
        report_error_announced=true;
        /* Calling WHBLogPrintf here would recurse through this handler. */
        OSReport("CTR2WUP TXT report write FAILED errno=%d operation=%s path=%s\n",test_report.error_code,
            test_report.error_stage?test_report.error_stage:"unknown",test_report.path);
    }
}
static void system_log(const char *message) { OSReport("%s\n", message); }
static void guide_log(void *context,const char *message) {
    (void)context;WHBLogPrintf("%s",message);
}

#ifdef CTR_STREAM_AUDIO
static bool stream_start(void *context,unsigned step,unsigned session) {
    (void)context;
    if(!stream_fixtures_ready || !diagnostic_audio.owned)return false;
    ctr_native_pcm_release(&stream_pcm);
    stream_golden_checked=stream_golden_match=false;stream_golden_expected=stream_golden_observed=0;
    CtrAudioResult result=ctr_audio_init(&stream_engine);
    unsigned fixture=step==2?1u:step==4?2u:0u;
    unsigned voices=(step==3 || step==7)?2u:1u;
    uint32_t seek=step==6?16000u:0u;
    for(unsigned voice=0;voice<voices && result==CTR_AUDIO_OK;++voice) {
        unsigned selected=voices==2?voice:fixture;
        result=ctr_audio_bind(&stream_engine,voice,stream_fixtures[selected].data,stream_fixtures[selected].bytes);
        uint32_t left=step==2?0u:CTR_AUDIO_GAIN_ONE;
        uint32_t right=step==1 || step==4 || step==6?0u:CTR_AUDIO_GAIN_ONE;
        if(voices==2)left=right=CTR_AUDIO_GAIN_ONE/2u;
        if(result==CTR_AUDIO_OK)result=ctr_audio_gain(&stream_engine,voice,left,right);
        if(result==CTR_AUDIO_OK && seek)result=ctr_audio_seek(&stream_engine,voice,seek);
        if(result==CTR_AUDIO_OK)result=ctr_audio_play(&stream_engine,voice,true);
    }
    stream_render_frames=step==4?48000u:step==6?48000u:72000u;
    stream_render_pulls=0;OSTime started=OSGetTime();
    for(uint32_t at=0;at<stream_render_frames && result==CTR_AUDIO_OK;) {
        uint32_t frames=stream_render_frames-at;
        if(frames>CTR_AUDIO_MAX_FRAMES)frames=CTR_AUDIO_MAX_FRAMES;
        result=ctr_audio_render(&stream_engine,stream_samples+(size_t)at*2u,stream_render_frames-at,frames);
        at+=frames;++stream_render_pulls;
    }
    stream_render_us=(uint32_t)OSTicksToMicroseconds(OSGetTime()-started);stream_render_result=result;
    stream_decoded_frames=stream_engine.voices[0].decoded_frames+stream_engine.voices[1].decoded_frames;
    WHBLogPrintf("STREAM RENDER step=%u session=%u source_rate=%u seek_sample=%u voices=%u result=%s output_rate=48000 frames=%u pulls=%u render_us=%u source_decoded_frames=%llu clipped_samples=%llu; original finite prerender no callback mixing",
        step+1,session,step==4?48000u:32000u,seek,voices,ctr_audio_result_name(result),stream_render_frames,
        stream_render_pulls,stream_render_us,(unsigned long long)stream_decoded_frames,(unsigned long long)stream_engine.clipped_samples);
    /* Independently derived exact PCM goldens include resampling, channel
       routing, gain, mixing and the seek checkpoint. Numeric PCM is hashed in
       little-endian byte order on every host, including this big-endian PPC. */
    static const uint32_t expected[CTR_STREAM_APP_STEPS]={0,UINT32_C(0x103a2d65),UINT32_C(0x45023c95),
        UINT32_C(0x9b614705),UINT32_C(0x488615e5),UINT32_C(0x912406f5),UINT32_C(0x4348ffc1),UINT32_C(0x9b614705)};
    bool ok=result==CTR_AUDIO_OK && ctr_native_pcm_prepare(&stream_pcm,stream_samples,stream_render_frames);
    if(ok) {
        stream_golden_checked=true;stream_golden_expected=expected[step];stream_golden_observed=stream_pcm.report.pcm_fnv1a;
        bool match=stream_golden_match=stream_golden_observed==stream_golden_expected;
        WHBLogPrintf("STREAM GOLDEN step=%u expected_pcm_fnv1a=%08x observed_pcm_fnv1a=%08x match=%u scope=original_PCM_not_audibility",
            step+1,expected[step],stream_pcm.report.pcm_fnv1a,match);
        if(!match) { ctr_native_pcm_release(&stream_pcm);ok=false; }
    }
    if(ok)ok=ctr_native_pcm_play(&stream_pcm);
    stream_log_native("explicit_play");return ok;
}
static bool stream_pause(void *context,bool paused) {
    (void)context;
    bool ok=paused?ctr_native_pcm_pause(&stream_pcm):ctr_native_pcm_resume(&stream_pcm);
    stream_log_native(paused?"pause":"resume");return ok;
}
static void stream_stop(void *context) {
    (void)context;
    if(stream_pcm.report.prepared)ctr_native_pcm_stop(&stream_pcm);
    stream_log_native("guide_stop");
}
#endif

static void session_summary(int result) {
    phase_log();
    WHBLogPrintf("===== SESSION SUMMARY =====");
    WHBLogPrintf("SD commits=%llu maximum_sync_us=%llu policy=file_sync_close_volume_sync; crash_recovery_requires_console_evidence",
        (unsigned long long)test_report.commits,(unsigned long long)report_sync_max_us);
#ifdef CTR_STREAM_AUDIO
    ctr_stream_app_summary(&stream_app);stream_log_native("session_summary");
    WHBLogPrintf("Application: %s | Build: %s | Entering cleanup: result=%d | Audio sessions: %u | Human hearing remains external evidence",
        REPORT_APPLICATION,CTR_APP_VERSION,result,stream_app.session);
    return;
#else
    ctr_test_guide_summary(&test_guide);
#endif
    WHBLogPrintf("Application: %s | Build: %s | Entering cleanup: result=%d",REPORT_APPLICATION,CTR_APP_VERSION,result);
    WHBLogPrintf("Elapsed milliseconds: %llu | Completed display frames: %u | Guest sessions: %u",
        (unsigned long long)(OSTicksToMicroseconds(OSGetTime()-session_started)/1000),completed_frames,guest_sessions);
    WHBLogPrintf("Display frame microseconds: minimum=%u average=%llu maximum=%u (diagnostic workload, not game FPS)",
        completed_frames?frame_min_us:0,(unsigned long long)(completed_frames?frame_total_us/completed_frames:0),frame_max_us);
    WHBLogPrintf("Self-tests (0=PASS): CPU=%d services=%d heap=%d objects=%d HID=%d GSP=%d relay=%d stack=%d threads=%d",
        cpu_test_result,service_test_result,heap_test_result,kernel_test_result,hid_test_result,gsp_test_result,
        relay_test_result,stack_test_result,thread_test_result);
    WHBLogPrintf("Allocation latest: %s | Allocation failure ever seen: %s | Audio configured: %s | Tone requests: %u",
        !allocation_checked?"NOT TESTED":alloc_ok_for_report?"PASS":"FAIL",allocation_failure_seen?"YES":"NO",audio_ready?"YES":"NO",tone_count);
    WHBLogPrintf("Native audio context: result=%s owned=%u inherited=%u attempts=%u successes=%u failures=%u resets=%u releases=%u; guest audio unavailable",
        ctr_diagnostic_audio_result_name(diagnostic_audio.result),diagnostic_audio.owned,diagnostic_audio.last_inherited,
        diagnostic_audio.attempts,diagnostic_audio.successes,diagnostic_audio.failures,
        diagnostic_audio.inherited_resets,diagnostic_audio.releases);
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
static bool update_image(GX2Surface *s,ctr_scale_cache *cache,const uint32_t *image,size_t pixels,unsigned display) {
    ctr_scale_update update;
    if(!ctr_scale_cache_update(cache,image,pixels,s->image,s->imageSize,s->pitch,&update))return false;
    ++publication[display].updates;publication[display].full+=update.full;
    publication[display].reused+=cache->range_count==0;publication[display].scaled_pixels+=update.scaled_pixels;
    return true;
}
static void publish_image(GX2Surface *s,const ctr_scale_cache *cache,unsigned display) {
    for(size_t i=0;i<cache->range_count;++i) {
        const ctr_scale_range *range=&cache->ranges[i];
        void *start=(unsigned char *)s->image+range->offset;
        DCFlushRange(start,(uint32_t)range->bytes);
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE,start,(uint32_t)range->bytes);
        publication[display].publish_bytes+=range->bytes;++publication[display].publish_ranges;
    }
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

#if !defined(CTR_COMPILED_HOMEBREW) && !defined(CTR_STREAM_AUDIO)
static void tone_shutdown(void *context) {
    (void)context;
    if(tone_voice && AXIsInit()) {
        AXSetVoiceState(tone_voice,AX_VOICE_STATE_STOPPED);AXFreeVoice(tone_voice);
    }
    tone_voice=NULL;audio_ready=0;
}
static void tone_init(void) {
    tone_voice=AXAcquireVoice(31, NULL, NULL);
    if (!tone_voice) { WHBLogPrintf("audio FAIL no AX voice"); return; }
    AXSetVoiceState(tone_voice,AX_VOICE_STATE_STOPPED);
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
        WHBLogPrintf("audio FAIL sample rate ratio");tone_shutdown(NULL);return;
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
    if(!audio_ready)tone_shutdown(NULL);
}

static void play_tone(void) {
    if (!audio_ready) return;
    AXSetVoiceState(tone_voice, AX_VOICE_STATE_STOPPED);
    AXSetVoiceCurrentOffset(tone_voice, 0);
    AXSetVoiceState(tone_voice, AX_VOICE_STATE_PLAYING);
    WHBLogPrintf("tone request=%u frequency_hz=440 duration_ms=250", ++tone_count);
}
#endif

static bool audio_init(bool foreground) {
    bool already_owned=diagnostic_audio.owned;
    bool ready=ctr_diagnostic_audio_begin(&diagnostic_audio,foreground);
    WHBLogPrintf("Native audio begin: result=%s owned=%u inherited=%u attempts=%u failures=%u resets=%u; launcher silence requires human check",
        ctr_diagnostic_audio_result_name(diagnostic_audio.result),diagnostic_audio.owned,diagnostic_audio.last_inherited,
        diagnostic_audio.attempts,diagnostic_audio.failures,diagnostic_audio.inherited_resets);
#if !defined(CTR_COMPILED_HOMEBREW) && !defined(CTR_STREAM_AUDIO)
    if(ready && !already_owned)tone_init();
#else
    (void)already_owned;
#endif
    return ready;
}
static bool audio_shutdown(void) {
    bool released=ctr_diagnostic_audio_end(&diagnostic_audio,
#ifdef CTR_STREAM_AUDIO
        ctr_native_pcm_release,&stream_pcm);
#elif !defined(CTR_COMPILED_HOMEBREW)
        tone_shutdown,NULL);
#else
        NULL,NULL);
#endif
#ifdef CTR_STREAM_AUDIO
    stream_log_native("AX_context_release");
#endif
    WHBLogPrintf("Native audio end: result=%s owned=%u releases=%u failures=%u",
        ctr_diagnostic_audio_result_name(diagnostic_audio.result),diagnostic_audio.owned,
        diagnostic_audio.releases,diagnostic_audio.failures);
    return released;
}

static void image_text(uint32_t *image, unsigned w, unsigned row, const char *text) {
    ctr_draw_text(image,w,GUEST_H,6,row*10+6,text,0xffffffff);
}
#ifdef CTR_SCENE_ONLY
static void scene_images(void) {
    uint32_t *images[2]={top_image,bottom_image};unsigned widths[2]={TOP_W,BOTTOM_W};
    for(unsigned index=0;index<2;++index) {
        unsigned scale=index?2u:1u,side=64u*scale,left=(widths[index]-side)/2u,top=86u;
        for(unsigned i=0;i<widths[index]*GUEST_H;++i)images[index][i]=UINT32_C(0x102030ff);
        for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x) {
            const uint8_t *p=scene_pixels[index]+((63u-y/scale)*64u+x/scale)*4u;
            images[index][(top+y)*widths[index]+left+x]=
                (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];
        }
        if(index) {
            image_text(bottom_image,BOTTOM_W,0,"SCENE CHECK / SHORT INDICES");
            image_text(bottom_image,BOTTOM_W,1,scene_test_result || scene_results[0] || scene_results[1]?"AUTOMATIC PIXEL CHECK FAILED":"AUTOMATIC PIXEL CHECK PASSED");
            image_text(bottom_image,BOTTOM_W,2,"CONFIRM TV GUIDE WITH R OR L");
        }
    }
}
#endif

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

typedef struct {
    int result, gfx_ready, udp_ready;
    bool foreground;
    GX2Surface *tv,*drc;
} native_exit_context;

static void exit_checkpoint(void *context,const char *message) {
    (void)context;
    OSReport("%s\n",message);
    if(!ctr_test_report_checkpoint(&test_report,message))
        OSReport("CTR2WUP exit checkpoint FAILED errno=%d\n",test_report.error_code);
    /* No diagnostic-owned FSA client crosses a ProcUI/system transition. */
    ctr_report_storage_close(&report_storage);
}
static void cleanup_resources(void *context) {
    native_exit_context *exit=context;
    int result=exit->result;
    final_guest.captured=true;final_guest.opened=gsp_ready;
    final_guest.state=gsp_guest.state;final_guest.fault=gsp_guest.fault;
    final_guest.memory_fault=gsp_guest.cpu.memory_fault;final_guest.pc=gsp_guest.cpu.r[15];final_guest.detail=gsp_guest.fault_detail;
    final_guest.instructions=gsp_guest.cpu.instructions;final_guest.fallback=gsp_guest.cpu.fallback_instructions;
    final_guest.gx=gsp_guest.gsp;
    app_exit_result=result;result_snapshot("resource_cleanup_begin");
    WHBLogPrintf("shutdown begin result=%d",result);
    session_summary(result);
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
#ifdef CTR_ROMFS_HOMEBREW
    if(romfs_view_ready)ctr_fs_romfs_close(&romfs_view);
    ctr_romfs_file_close(&romfs_file);
#endif
#ifdef CTR_FILESYSTEM_HOMEBREW
    if(diagnostic_save_ready)ctr_fs_volume_close(&diagnostic_save);
#endif
    WHBLogPrintf("EXIT stage=guest_cleanup_done");
    WHBLogPrintf("EXIT stage=audio_cleanup_begin");
#ifdef CTR_COMPILED_HOMEBREW
    if(compiled_sd_ready)ctr_fs_volume_close(&compiled_sd);
#endif
    if(!audio_shutdown()) { result=1;exit->result=1;app_exit_result=1; }
    WHBLogPrintf("EXIT stage=audio_cleanup_done");
    WHBLogPrintf("EXIT stage=graphics_cleanup_begin");
    if (exit->gfx_ready && exit->foreground) GX2DrawDone();
    ctr_scale_cache_close(&tv_cache);ctr_scale_cache_close(&drc_cache);
    if (exit->tv->image) MEMFreeToDefaultHeap(exit->tv->image);
    if (exit->drc->image) MEMFreeToDefaultHeap(exit->drc->image);
    if (exit->gfx_ready) WHBGfxShutdown();
    ctr_scale_plan_close(&tv_scale);ctr_scale_plan_close(&drc_scale);
    WHBLogPrintf("EXIT stage=graphics_cleanup_done");
#if CTR_DIAGNOSTIC_UDP
    WHBLogPrintf("EXIT stage=udp_cleanup_begin");
    if (exit->udp_ready) WHBLogUdpDeinit();
    WHBLogPrintf("EXIT stage=udp_cleanup_done");
#endif
    WHBLogPrintf("shutdown complete result=%d",result);
    result_snapshot("resources_closed_before_system_transition");
    WHBLogPrintf("===== END OF REPORT: application resource cleanup reached; result=%d =====",result);
    WHBLogPrintf("EXIT stage=report_close_begin; launcher return is not yet established");
    if(report_handler_added)WHBRemoveLogHandler(file_log);
    if(!ctr_test_report_close(&test_report))OSReport("CTR2WUP TXT report close FAILED errno=%d\n",test_report.error_code);
    ctr_test_report_checkpoint(&test_report,"EXIT stage=report_close_done; SD mount cleanup belongs to WUT");
    ctr_report_storage_close(&report_storage);
}
static uint32_t exit_save_ready(void *context) {
    (void)context;OSSavesDone_ReadyToRelease();return 0;
}
static void clear_proc_callbacks(void *context) {
    (void)context;ProcUIClearCallbacks();
    /* Mounted-volume commits belong to active foreground diagnostics. Avoid
       opening new FSA clients after requesting a system transition. */
    ctr_test_report_set_volume_sync(&test_report,NULL,NULL);
    ctr_report_storage_close(&report_storage);
    /* Keep the save acknowledgement even after removing the GFX callbacks. */
    ProcUISetSaveCallback(exit_save_ready,NULL);
}
static void request_system_menu(void *context) { (void)context;SYSLaunchMenu(); }
static ctr_proc_status process_proc_messages(void *context) {
    (void)context;
    switch(ProcUIProcessMessages(TRUE)) {
        case PROCUI_STATUS_IN_FOREGROUND:return CTR_PROC_FOREGROUND;
        case PROCUI_STATUS_IN_BACKGROUND:return CTR_PROC_BACKGROUND;
        case PROCUI_STATUS_RELEASE_FOREGROUND:return CTR_PROC_RELEASE;
        case PROCUI_STATUS_EXITING:return CTR_PROC_EXITING;
        default:return (ctr_proc_status)-1;
    }
}
static void release_foreground(void *context) { (void)context;ProcUIDrawDoneRelease(); }
static void wait_proc_messages(void *context) { (void)context;OSSleepTicks(OSMillisecondsToTicks(5)); }
static void shutdown_procui(void *context) { (void)context;ProcUIShutdown(); }
static void shutdown_whb(void *context) { (void)context;WHBProcShutdown(); }
static const ctr_exit_ops exit_ops={cleanup_resources,clear_proc_callbacks,request_system_menu,
    process_proc_messages,release_foreground,wait_proc_messages,shutdown_procui,shutdown_whb,exit_checkpoint};

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    int result=1, gfx_ready=0;
    bool exit_observed=false, foreground=true;
    GX2Surface tv_source={0}, drc_source={0};
    CtrViewport tv_view, drc_view;
    WHBProcInit();
    lifecycle.title_id=OSGetTitleID();
    if(ctr_test_from_hbl(lifecycle.title_id))
        ProcUIRegisterCallback(PROCUI_CALLBACK_HOME_BUTTON_DENIED,home_button_exit,NULL,99);
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
        if(directory_ok)ctr_test_result_open(&test_result,directory,REPORT_APPLICATION,CTR_APP_VERSION,(uint64_t)session_started);
        else { test_result.failed=true;test_result.error_code=errno;test_result.error_stage="directory"; }
#ifdef CTR_FILESYSTEM_HOMEBREW
        if(directory_ok)snprintf(diagnostic_save_path,sizeof(diagnostic_save_path),"%s/filesystem-test.snapshot",directory);
#endif
        if(directory_ok && ctr_test_report_open(&test_report,directory,REPORT_APPLICATION,CTR_APP_VERSION,(uint64_t)session_started)) {
            ctr_test_report_set_volume_sync(&test_report,ctr_report_storage_sync,&report_storage);
            report_handler_added=WHBAddLogHandler(file_log)!=0;
            if(!report_handler_added) { test_report.failed=true;test_report.error_code=EIO;test_report.error_stage="handler_register"; }
        } else if(!directory_ok) {
            test_report.failed=true;test_report.error_code=errno;test_report.error_stage="directory";
        }
    }
    WHBLogPrintf("CTR2WUP native diagnostic version=%s application=%s pointer_bits=%u endian=big UDP=%u TXT_report=%u",
                  CTR_APP_VERSION,REPORT_APPLICATION,(unsigned)(8*sizeof(void *)),udp_ready,report_handler_added);
    WHBLogPrintf("BUILD source_commit=%s source_dirty=%u evidence_schema=2 result_bytes=%u",CTR_SOURCE_COMMIT,CTR_SOURCE_DIRTY,CTR_TEST_RESULT_BYTES);
#ifdef CTR_M1_ONLY
    WHBLogPrintf("M1_SESSION ticks=%016llx source_commit=%s version=%s protocol=m1-v1",
        (unsigned long long)session_started,CTR_SOURCE_COMMIT,CTR_APP_VERSION);
    ctr_m1_storage_probe("fs:/vol/external01/ctr2wup",CTR_SOURCE_COMMIT,&m1_storage);
    char storage_line[768];m1_storage_line(storage_line,sizeof(storage_line));
    WHBLogPrintf("%s",storage_line);
#endif
    WHBLogPrintf("Platform title_id=%016llx exit_route=%s SD_owner=WUT_devoptab",
        (unsigned long long)lifecycle.title_id,ctr_test_from_hbl(lifecycle.title_id)?"HBL_relaunch":"system_menu");
    WHBLogPrintf("TXT report path=%s status=%s errno=%d",test_report.path[0]?test_report.path:"unavailable",
        report_handler_added?"SAVING":"FAILED",test_report.error_code);
    if(report_handler_added)report_commit("startup");
    result_snapshot("startup");
    /* Launch transition music is native platform sound, separate from the
       optional diagnostic tone. Even compiled guests need a fresh, silent AX
       context. Do this before the long self-tests and any guest input. */
    if(!audio_init(foreground))goto cleanup;
    result_snapshot("native_audio_ready");
#ifdef CTR_SCENE_ONLY
    ctr_test_guide_init(&test_guide,CTR_GUIDE_SCENE,guide_log,NULL);
#elif defined(CTR_PICA_HOMEBREW)
    ctr_test_guide_init(&test_guide,CTR_GUIDE_PICA,guide_log,NULL);
#elif defined(CTR_AUDIO_ONLY)
    ctr_test_guide_init(&test_guide,CTR_GUIDE_AUDIO,guide_log,NULL);
#elif defined(CTR_GX_HOMEBREW)
    ctr_test_guide_init(&test_guide,CTR_GUIDE_GX,guide_log,NULL);
#elif defined(CTR_ROMFS_HOMEBREW)
    ctr_test_guide_init(&test_guide,CTR_GUIDE_ASSETS,guide_log,NULL);
#elif defined(CTR_FILESYSTEM_HOMEBREW)
    ctr_test_guide_init(&test_guide,CTR_GUIDE_SAVE,guide_log,NULL);
#elif defined(CTR_COMPILED_HOMEBREW)
    ctr_test_guide_init(&test_guide,CTR_GUIDE_INTERACTIVE,guide_log,NULL);
#else
    ctr_test_guide_init(&test_guide,CTR_GUIDE_NATIVE,guide_log,NULL);
#endif
#ifdef CTR_STREAM_AUDIO
    const ctr_stream_app_ops stream_ops={stream_start,stream_pause,stream_stop,guide_log,NULL};
    ctr_stream_app_init(&stream_app,&stream_ops);
    stream_fixtures_ready=ctr_audio_fixture_build_mono(&stream_fixtures[0],32000u,0) &&
        ctr_audio_fixture_build_mono(&stream_fixtures[1],32000u,1) &&
        ctr_audio_fixture_build_mono(&stream_fixtures[2],48000u,0);
    audio_ready=stream_fixtures_ready;
    if(!stream_fixtures_ready) { WHBLogPrintf("STREAM fixtures FAILED");goto cleanup; }
    bool run_guest_diagnostics=false;
    WHBLogPrintf("STREAM profile original_CSTM_to_portable_mixer_to_native_finite_PCM; guest CPU/service diagnostics skipped; guest DSP unavailable");
#else
    bool run_guest_diagnostics=true;
#endif
    if(run_guest_diagnostics) {
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
    hid_guest.debug=guest_debug;hid_guest.debug_context=&hid_guest;hid_guest.cpu.log=guest_trace;
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
#ifdef CTR_SCENE_ONLY
    scene_test_result=ctr_scene_arithmetic_selftest();
    for(unsigned i=0;i<2;++i) {
        ctr_scene_fixture_background(scene_pixels[i],sizeof(scene_pixels[i]));OSTime start=OSGetTime();
        scene_results[i]=ctr_scene_fixture_render_arithmetic(i!=0,scene_pixels[i],sizeof(scene_pixels[i]),&scene_reports[i]);
        scene_us[i]=OSTicksToMicroseconds(OSGetTime()-start);
        WHBLogPrintf("owned_scene arithmetic=ADD_MUL index_bytes=%u result=%s pixel_selftest=%d elapsed_us=%llu draws=%u triangles=%u fragments=%u shader_steps=%u console_picture_confirmation_required=1",
            i+1u,ctr_scene_result_name(scene_results[i]),scene_test_result,(unsigned long long)scene_us[i],
            scene_reports[i].draws,scene_reports[i].triangles,scene_reports[i].fragments,scene_reports[i].shader_instructions);
    }
#endif
    selftests_done=true;result_snapshot("selftests_done");
    }
    if (!WHBGfxInit()) { WHBLogPrintf("FAIL WHBGfxInit"); goto cleanup; }
    gfx_ready=1;
    GX2Surface *tv_target=&WHBGfxGetTVColourBuffer()->surface;
    GX2Surface *drc_target=&WHBGfxGetDRCColourBuffer()->surface;
    if (!surface_create(&tv_source,tv_target) || !surface_create(&drc_source,drc_target) ||
        !ctr_viewport_fit(&tv_view,tv_target->width,tv_target->height,TOP_W,GUEST_H) ||
        !ctr_viewport_fit(&drc_view,drc_target->width,drc_target->height,BOTTOM_W,GUEST_H) ||
        !ctr_scale_plan_init(&tv_scale,&tv_view) || !ctr_scale_plan_init(&drc_scale,&drc_view) ||
        !ctr_scale_cache_init(&tv_cache,&tv_scale) || !ctr_scale_cache_init(&drc_cache,&drc_scale)) {
        WHBLogPrintf("FAIL native surface allocation or viewport"); goto cleanup;
    }
    publication[0].heap_bytes=tv_cache.source_pixels*4+tv_cache.range_capacity*sizeof(*tv_cache.ranges);
    publication[1].heap_bytes=drc_cache.source_pixels*4+drc_cache.range_capacity*sizeof(*drc_cache.ranges);
    WHBLogPrintf("TV surface=%ux%u viewport=%u,%u %ux%u allocation_bytes=%u pitch=%u",
                  tv_view.output_width,tv_view.output_height,tv_view.x,tv_view.y,
                  tv_view.width,tv_view.height,tv_source.imageSize,tv_source.pitch);
    WHBLogPrintf("DRC surface=%ux%u viewport=%u,%u %ux%u calibrated_space=1280x720 allocation_bytes=%u pitch=%u",
                  drc_view.output_width,drc_view.output_height,drc_view.x,drc_view.y,
                  drc_view.width,drc_view.height,drc_source.imageSize,drc_source.pitch);
    int alloc_ok=allocation_test();
#ifdef CTR_COMPILED_HOMEBREW
    WHBLogPrintf("Compiled test: native AX context owned for launcher silence; tone disabled; A is guest input only; guest audio unavailable");
    WHBLogPrintf("AOT call_limit=%u variant=%s",CTR_RECOMP_CALL_LIMIT,REPORT_APPLICATION);
#endif
    if(run_guest_diagnostics)restart_guest();
#ifndef CTR_COMPILED_HOMEBREW
    guest_mode=0;
#endif
    VPADStatus pad={0}; VPADReadError error=VPAD_READ_NO_SAMPLES;
    unsigned frame=0; uint32_t frame_us=0;
    OSTime log_time=OSGetTime();
    ctr_proc_status prior_status=(ctr_proc_status)-1;
    for(;;) {
        /* WHBProcIsRunning shuts ProcUI down on EXITING, before our resource
           cleanup. Poll directly so cleanup can precede ProcUIShutdown. */
        ctr_proc_status status=process_proc_messages(NULL);
        if(status!=prior_status) {
            WHBLogPrintf("APP ProcUI status=%d",(int)status);prior_status=status;
        }
        if(status==CTR_PROC_EXITING) {
            ctr_test_report_set_volume_sync(&test_report,NULL,NULL);
            ctr_report_storage_close(&report_storage);
            exit_observed=true;break;
        }
        if(atomic_load(&home_exit_requested)) {
            WHBLogPrintf("EXIT trigger=HBL_home_button");break;
        }
        if(status==CTR_PROC_RELEASE) {
#ifdef CTR_STREAM_AUDIO
            ctr_stream_app_suspend(&stream_app);result_snapshot("foreground_release");report_commit("foreground_release");
#endif
            ctr_report_storage_close(&report_storage);
            if(!audio_shutdown())goto cleanup;
            foreground=false;ProcUIDrawDoneRelease();continue;
        }
        if(status!=CTR_PROC_FOREGROUND) {
            foreground=false;wait_proc_messages(NULL);continue;
        }
        bool reacquired=!foreground;
        foreground=true;
        if(reacquired) {
            WHBLogPrintf("SCALE CACHE invalidate=foreground_reacquire next_update=full");
            ctr_scale_cache_invalidate(&tv_cache);ctr_scale_cache_invalidate(&drc_cache);
            if(!audio_init(foreground))goto cleanup;
#ifdef CTR_STREAM_AUDIO
            WHBLogPrintf("STREAM foreground reacquired; native context fresh; A/Plus replay required");
            result_snapshot("foreground_reacquired");
#endif
        }
        OSTime frame_start=OSGetTime();
        bool draw_candidate=false;
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
#ifdef CTR_STREAM_AUDIO
            ctr_stream_app_input stream_stop_input={stop.milliseconds,CTR_GUIDE_MINUS,false,false};
            ctr_stream_app_update(&stream_app,&stream_stop_input);
#else
            ctr_test_guide_update(&test_guide,&stop);
#endif
            result_snapshot("Minus_exit_requested");
            WHBLogPrintf("EXIT trigger=Minus; cleaning resources before system transition");break;
        }
        uint64_t input_frame_serial=gsp_presented;
#ifdef CTR_COMPILED_HOMEBREW
#ifndef CTR_PICA_HOMEBREW
        if(pad.trigger&(VPAD_BUTTON_A|VPAD_BUTTON_B|VPAD_BUTTON_PLUS))freeze_trace_frames=64;
#endif
        if(test_guide.kind!=CTR_GUIDE_PICA && (pad.trigger&VPAD_BUTTON_A)) {
            a_request_time=OSGetTime();a_request_frame=gsp_presented;a_response_pending=true;
            WHBLogPrintf("A response pending: frame_before=%llu instructions_before=%llu",
                (unsigned long long)gsp_presented,(unsigned long long)gsp_guest.cpu.instructions);
        }
        freeze_checkpoint("input_read");
#elif !defined(CTR_STREAM_AUDIO)
        if (pad.trigger&VPAD_BUTTON_A) play_tone();
#endif
        if (run_guest_diagnostics && (pad.trigger&VPAD_BUTTON_X)) alloc_ok=allocation_test();
        if (run_guest_diagnostics && test_guide.done && test_guide.kind!=CTR_GUIDE_SCENE && test_guide.kind!=CTR_GUIDE_PICA && (pad.trigger&VPAD_BUTTON_Y)) {
#ifdef CTR_COMPILED_HOMEBREW
            guest_mode=guest_mode==2?0:2;
#else
            guest_mode=(guest_mode+1)%3;presented_frames=0;
#endif
            WHBLogPrintf("display_mode=%s",guest_mode==2?"standard-hid-gsp":guest_mode==1?"guest-fixture":"native");
        }
        if (run_guest_diagnostics && test_guide.done && test_guide.kind!=CTR_GUIDE_SCENE && test_guide.kind!=CTR_GUIDE_PICA && (pad.trigger&VPAD_BUTTON_PLUS)) restart_guest();
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
        if (!guest_mode) {
            if(run_guest_diagnostics)diagnostic_images(frame,&pad,error,inside,&touch,tx,ty,frame_us,alloc_ok);
#ifdef CTR_STREAM_AUDIO
            else {
                for(unsigned i=0;i<TOP_W*GUEST_H;++i)top_image[i]=0x162d4fff;
                image_text(top_image,TOP_W,9,"ORIGINAL CSTM / TWO PORTABLE MIXER VOICES");
                image_text(top_image,TOP_W,10,"FINITE NATIVE PCM / NO GUEST DSP IPC");
                image_text(top_image,TOP_W,11,"NATIVE COMPLETION DOES NOT PROVE HEARING");
            }
#endif
#ifdef CTR_SCENE_ONLY
            scene_images();
#endif
        }
        else if(guest_mode==2) {
#ifndef CTR_COMPILED_HOMEBREW
            if(pad.trigger&VPAD_BUTTON_B)ctr_relay_fixture_exit(&gsp_guest);
#endif
            freeze_checkpoint("compiled_guest_begin");
            OSTime guest_start=OSGetTime();
#ifdef CTR_PICA_HOMEBREW
            pica_slice_context context={&pad,inside!=0,tx,ty,gsp_presented};
            const ctr_slice_policy policy={10000,50,500000,8000};
            if(gsp_ready && !test_result.failed) {
                last_slice=ctr_guest_slice(&policy,slice_clock,slice_execute,&context);
                ++slice_stops[last_slice.reason];
                if(last_slice.reason==CTR_SLICE_CLOCK || last_slice.reason==CTR_SLICE_CONTRACT ||
                   last_slice.reason==CTR_SLICE_NO_PROGRESS || last_slice.reason==CTR_SLICE_ARGUMENT) {
                    gsp_guest.state=CTR_GUEST_FAULTED;gsp_guest.fault=CTR_CPU_MISSING_BLOCK;
                    WHBLogPrintf("SLICE FAIL reason=%s pc=%08x",ctr_slice_reason_name(last_slice.reason),gsp_guest.cpu.r[15]);
                }
            }
#else
            update_input_guest(&gsp_guest,gsp_ready,gsp_origin,&pad,inside!=0,tx,ty,50000);
#endif
            phase_add(PH_GUEST,OSTicksToMicroseconds(OSGetTime()-guest_start));
            freeze_checkpoint("compiled_guest_done");
            if(!gsp_presented) { memset(top_image,0,sizeof(top_image));memset(bottom_image,0,sizeof(bottom_image)); }
            if(gsp_ready && !gsp_guest.gsp.black && gsp_guest.gsp.swaps[0] && gsp_guest.gsp.swaps[0]==gsp_guest.gsp.swaps[1] && gsp_guest.gsp.swaps[0]!=gsp_presented) {
                freeze_checkpoint("guest_pixels_begin");
                OSTime decode_start=OSGetTime();
                ctr_mem_result top=ctr_gsp_pixels(&gsp_guest,0,top_image,TOP_W*GUEST_H);
                ctr_mem_result bottom=ctr_gsp_pixels(&gsp_guest,1,bottom_image,BOTTOM_W*GUEST_H);
                if(top!=CTR_MEM_OK || bottom!=CTR_MEM_OK) {
                    gsp_guest.state=CTR_GUEST_FAULTED;gsp_guest.fault=CTR_CPU_MEMORY_FAULT;
                    WHBLogPrintf("GSP presentation FAIL top=%s bottom=%s",ctr_mem_result_name(top),ctr_mem_result_name(bottom));
                }
                gsp_presented=gsp_guest.gsp.swaps[0];
                draw_candidate=ctr_draw_observation_ready(top==CTR_MEM_OK && bottom==CTR_MEM_OK,gsp_presented,
                    gsp_guest.gsp.pica_draws,gsp_guest.gsp.pica_triangles,gsp_guest.gsp.pica_fragments,gsp_guest.gsp.pica_shader_instructions);
                guest_green_frame=top==CTR_MEM_OK;
                for(unsigned i=0;guest_green_frame && i<TOP_W*GUEST_H;++i)
                    if(top_image[i]!=0x00ff00ff)guest_green_frame=false;
#ifdef CTR_COMPILED_HOMEBREW
                WHBLogPrintf("GUEST FRAME serial=%llu top_first=%08x green_pixels=%u host_elapsed_ms=%llu",
                    (unsigned long long)gsp_presented,top_image[0],guest_green_frame,
                    (unsigned long long)(OSTicksToMicroseconds(OSGetTime()-session_started)/1000));
                if(a_response_pending && guest_green_frame && gsp_presented>a_request_frame) {
                    green_response_seen=true;
                    green_response_ms=OSTicksToMicroseconds(OSGetTime()-a_request_time)/1000;
                    WHBLogPrintf("A RESPONSE new_green_serial=%llu latency_ms=%llu",
                        (unsigned long long)gsp_presented,
                        (unsigned long long)(OSTicksToMicroseconds(OSGetTime()-a_request_time)/1000));
                    a_response_pending=false;
                }
#endif
                freeze_checkpoint("guest_pixels_done");
                phase_add(PH_DECODE,OSTicksToMicroseconds(OSGetTime()-decode_start));
            }
            for(unsigned i=TOP_W*102;i<TOP_W*144;++i)top_image[i]=0x162d4fff;
            char text[90];
#ifdef CTR_COMPILED_HOMEBREW
#ifdef CTR_SHALLOW_AOT
            image_text(top_image,TOP_W,10,"CTR2WUP GUARDED AOT / LIMIT 2");
#else
#ifdef CTR_FILESYSTEM_HOMEBREW
            image_text(top_image,TOP_W,10,"CTR2WUP COMPILED SAVE / RELOAD");
#else
            image_text(top_image,TOP_W,10,"CTR2WUP COMPILED LIBCTRU / GSP");
#endif
#endif
#else
            image_text(top_image,TOP_W,10,"CTR2WUP HID / GSP QUEUE / VBLANK");
#endif
            snprintf(text,sizeof(text),"FRAME %llu / STATE %s",(unsigned long long)gsp_presented,ctr_guest_state_name(gsp_guest.state));
            image_text(top_image,TOP_W,11,text);
#ifdef CTR_PICA_HOMEBREW
            image_text(top_image,TOP_W,12,"GUEST TRIANGLES / EXACT PIXEL READBACK");
            snprintf(text,sizeof(text),"DRAWS %llu / SHADER STEPS %llu",(unsigned long long)gsp_guest.gsp.pica_draws,(unsigned long long)gsp_guest.gsp.pica_shader_instructions);
            image_text(top_image,TOP_W,13,text);
#elif defined(CTR_GX_HOMEBREW)
            image_text(top_image,TOP_W,12,"GX FILL / DMA / TILED TRANSFER / COPY");
            snprintf(text,sizeof(text),"GX %llu / PICA LISTS %llu / DRAWS %llu",(unsigned long long)gsp_guest.gsp.gx_commands,(unsigned long long)gsp_guest.gsp.gx_pica,(unsigned long long)gsp_guest.gsp.pica_draws);
            image_text(top_image,TOP_W,13,text);
#else
            image_text(top_image,TOP_W,12,"A: GREEN / TOUCH: WHITE / B: EXIT");
#ifdef CTR_FILESYSTEM_HOMEBREW
            image_text(top_image,TOP_W,12,filesystem_message);
            image_text(top_image,TOP_W,13,"PLUS: RELOAD SAVE / MINUS: EXIT");
#else
            image_text(top_image,TOP_W,13,"Y: CYCLE / PLUS: RESET / PICA ABSENT");
#endif
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
#ifdef CTR_SCENE_ONLY
        guide_input.guest_fault=guide_input.guest_fault || scene_test_result!=0 ||
            scene_results[0]!=CTR_SCENE_OK || scene_results[1]!=CTR_SCENE_OK;
#endif
        guide_input.guest_pass=gsp_pass_seen;guide_input.guest_sessions=guest_sessions;
        guide_input.guest_frame_available=gsp_presented>0;
        guide_input.guest_drawn_frame_available=drawn_frame_submitted;
#ifdef CTR_SCENE_ONLY
        guide_input.guest_frame_available=scene_submitted;
#endif
        guide_input.guest_green_frame=guest_green_frame;
        guide_input.guest_frame_serial=gsp_presented;guide_input.input_frame_serial=input_frame_serial;
#ifdef CTR_SCENE_ONLY
        guide_input.guest_frame_serial=completed_frames;
#endif
        guide_input.fresh_save_reload=fresh_save_reload_seen;
#ifdef CTR_STREAM_AUDIO
        unsigned guide_step_before=stream_app.step;
        ctr_native_pcm_status before_status=stream_native.status;
        ctr_native_pcm_snapshot(&stream_pcm,&stream_native);
        if(before_status!=stream_native.status)stream_log_native("status_change");
        if(!test_result.failed) {
            ctr_stream_app_input stream_input={guide_input.milliseconds,guide_input.pressed,
                stream_native.status==CTR_NATIVE_PCM_COMPLETE,stream_app.played && stream_native.status==CTR_NATIVE_PCM_FAILED};
            ctr_stream_app_update(&stream_app,&stream_input);
            if(stream_app.step!=guide_step_before || guide_input.pressed || before_status!=stream_native.status) {
                result_snapshot("stream_guide_update");report_commit("stream_guide_update");
            }
        } else if(stream_native.status==CTR_NATIVE_PCM_PLAYING || stream_native.status==CTR_NATIVE_PCM_PAUSED)stream_stop(NULL);
        char guide_rows[7][72];ctr_stream_app_view(&stream_app,guide_rows);
        for(unsigned i=0;i<BOTTOM_W*GUEST_H;++i)bottom_image[i]=0x162d4fff;
        image_text(bottom_image,BOTTOM_W,0,"AUDIO STREAMS / ORIGINAL CLIPS");
        for(unsigned i=1;i<7;++i)image_text(bottom_image,BOTTOM_W,i,guide_rows[i]);
        char stream_status_line[72];
        snprintf(stream_status_line,sizeof(stream_status_line),"NATIVE %s CURSOR %u/%u",ctr_native_pcm_status_name(stream_native.status),stream_native.cursor[0],stream_native.cursor[1]);
        image_text(bottom_image,BOTTOM_W,9,stream_status_line);
        image_text(bottom_image,BOTTOM_W,11,"TV GUIDE HAS FULL INSTRUCTIONS");
#else
        unsigned guide_step_before=test_guide.step;
        if(!test_result.failed) {
            ctr_test_guide_update(&test_guide,&guide_input);
            if(test_guide.step!=guide_step_before)result_snapshot("guide_update");
            if(ctr_test_guide_take_restart(&test_guide))restart_guest();
        }
        char guide_rows[7][72];ctr_test_guide_view(&test_guide,guide_input.milliseconds,guide_rows);
#ifdef CTR_M1_ONLY
        snprintf(guide_rows[6],72,"SD proof: %s / fresh reload: %s / MINUS exits",
            ctr_m1_storage_result_name(m1_storage.result),m1_storage.fresh_reload?"YES":"FIRST RUN");
        if(test_guide.done)snprintf(guide_rows[4],72,"Run complete. Full M1 still needs installed-WUP review.");
#endif
#endif
        for(unsigned i=0;i<TOP_W*83;++i)top_image[i]=0x162d4fff;
        if(test_result.failed) {
            snprintf(guide_rows[0],72,"RESULT STORAGE FAILED / TEST PAUSED");
            snprintf(guide_rows[1],72,"MINUS: EXIT. Do not continue this test.");
            snprintf(guide_rows[3],72,"SD file may be missing/partial after reboot.");
            snprintf(guide_rows[4],72,"Send the error below and any available TXT.");
        }
        for(unsigned i=0;i<7;++i)image_text(top_image,TOP_W,i,guide_rows[i]);
        for(unsigned i=TOP_W*154;i<TOP_W*184;++i)top_image[i]=0x162d4fff;
        image_text(top_image,TOP_W,15,test_result.ready && !test_result.failed?"RESULT VERIFIED / UPLOAD SD:/CTR2WUP":"RESULT FAILED / CHECK SD CARD");
        char report_line[100];
        if(!test_result.failed)snprintf(report_line,sizeof(report_line),"%.63s",test_result.filename);
        else snprintf(report_line,sizeof(report_line),"SD ERROR %d %s",test_result.error_code,
            test_result.error_stage?test_result.error_stage:"unknown");
        image_text(top_image,TOP_W,16,report_line);
        if(!test_result.failed && strlen(test_result.filename)>63)image_text(top_image,TOP_W,17,test_result.filename+63);
        for(unsigned i=TOP_W*214;i<TOP_W*228;++i)top_image[i]=0x162d4fff;
        snprintf(report_line,sizeof(report_line),"BUILD %s / TRACE %s",CTR_APP_VERSION,test_report.failed?"FAILED":"SAVING");
        image_text(top_image,TOP_W,21,report_line);
#ifdef CTR_COMPILED_HOMEBREW
        for(unsigned i=TOP_W*184;i<TOP_W*206;++i)top_image[i]=0x162d4fff;
        snprintf(report_line,sizeof(report_line),"GUEST INSTRUCTIONS %llu",(unsigned long long)gsp_guest.cpu.instructions);
        image_text(top_image,TOP_W,18,report_line);
        snprintf(report_line,sizeof(report_line),"HOST FRAME %u / GUEST PC %08X",frame,gsp_guest.cpu.r[15]);
        image_text(top_image,TOP_W,19,report_line);
#endif
        /* Draw prompts before scaling so they reach the actual outputs. */
        freeze_checkpoint("gx2_wait_begin");
        OSTime wait_start=OSGetTime();
        GX2DrawDone();
        phase_add(PH_WAIT,OSTicksToMicroseconds(OSGetTime()-wait_start));
        freeze_checkpoint("scale_begin");
        OSTime scale_start=OSGetTime();
        if(!update_image(&tv_source,&tv_cache,top_image,TOP_W*GUEST_H,0) ||
           !update_image(&drc_source,&drc_cache,bottom_image,BOTTOM_W*GUEST_H,1)) {
            WHBLogPrintf("FAIL cached image scale bounds");result=1;goto cleanup;
        }
        phase_add(PH_SCALE,OSTicksToMicroseconds(OSGetTime()-scale_start));
        OSTime publish_start=OSGetTime();
        publish_image(&tv_source,&tv_cache,0);publish_image(&drc_source,&drc_cache,1);
        phase_add(PH_PUBLISH,OSTicksToMicroseconds(OSGetTime()-publish_start));
        freeze_checkpoint("render_begin");
        OSTime present_start=OSGetTime();
        WHBGfxBeginRender();
        WHBGfxBeginRenderTV();
        GX2CopySurface(&tv_source,0,0,&WHBGfxGetTVColourBuffer()->surface,0,0);
        WHBGfxFinishRenderTV();
        WHBGfxBeginRenderDRC();
        GX2CopySurface(&drc_source,0,0,&WHBGfxGetDRCColourBuffer()->surface,0,0);
        WHBGfxFinishRenderDRC();
        WHBGfxFinishRender();
        phase_add(PH_PRESENT,OSTicksToMicroseconds(OSGetTime()-present_start));
        scene_submitted=true;
        if(draw_candidate && !drawn_frame_submitted) {
            drawn_frame_submitted=true;draw_ready_serial=gsp_presented;
            drawn_latency_ms=OSTicksToMicroseconds(OSGetTime()-gsp_origin)/1000;
            WHBLogPrintf("DRAWN FRAME SUBMITTED session=%u serial=%llu guest_elapsed_ms=%llu session_elapsed_ms=%llu draws=%llu triangles=%llu fragments=%llu shader_steps=%llu; user_picture_confirmation_required=1",
                guest_sessions,(unsigned long long)draw_ready_serial,(unsigned long long)drawn_latency_ms,
                (unsigned long long)(OSTicksToMicroseconds(OSGetTime()-session_started)/1000),
                (unsigned long long)gsp_guest.gsp.pica_draws,(unsigned long long)gsp_guest.gsp.pica_triangles,
                (unsigned long long)gsp_guest.gsp.pica_fragments,(unsigned long long)gsp_guest.gsp.pica_shader_instructions);
            phase_log();result_snapshot("drawn_frame_submitted");report_commit("drawn_frame_submitted");
        }
        freeze_checkpoint("render_done");
        if(freeze_trace_frames)--freeze_trace_frames;
        frame_us=(uint32_t)OSTicksToMicroseconds(OSGetTime()-frame_start);
        ++completed_frames;frame_total_us+=frame_us;
        if(frame_us<frame_min_us)frame_min_us=frame_us;
        if(frame_us>frame_max_us)frame_max_us=frame_us;
        if(frame_guest.state==CTR_GUEST_FAULTED || hid_guest.state==CTR_GUEST_FAULTED || gsp_guest.state==CTR_GUEST_FAULTED)guest_fault_seen=true;
        if ((uint64_t)(OSGetTime()-log_time)>=OSSecondsToTicks(1)) {
            phase_log();
#ifdef CTR_STREAM_AUDIO
            stream_log_native("periodic");
#endif
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
            if(!test_report.failed)report_commit("periodic");
            result_snapshot("periodic");
            log_time=OSGetTime();
        }
        ++frame;
    }
    result=0;
cleanup:
    {
        native_exit_context exit={result,gfx_ready,udp_ready,foreground,&tv_source,&drc_source};
        if(!ctr_test_complete_exit(&lifecycle,exit_observed,&exit_ops,&exit))result=1;
        else result=exit.result;
    }
    ctr_report_storage_close(&report_storage);
    WHBRemoveLogHandler(system_log);
    return result;
}
