#define _POSIX_C_SOURCE 200809L
#include "../test_platform.h"
#include "../report/native_platform.h"
#include <stdarg.h>
#define VPAD_BUTTON_R 0x20u
#define VPAD_BUTTON_L 0x40u
#define VPAD_BUTTON_PLUS 0x80u
static char root[512];static const char *mode;static unsigned checks,polls;static uint64_t ticks;
static bool display_alive,proc_alive,cleared;
#define CHECK(c) do { ++checks;if(!(c)){fprintf(stderr,"audio frontend %s line %d: %s\n",mode?mode:"parent",__LINE__,#c);exit(1);} } while(0)
#define DATA_ROOT root
#define CTR_SOURCE_COMMIT "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define CTR_SOURCE_DIRTY 0
#define CTR_APP_VERSION "0.27.0"
#define main app_main
#include "../../runtime/wiiu/guest_audio.c"
#undef main
void OSReport(const char *fmt,...) { (void)fmt; }
uint64_t OSGetTime(void) { ticks+=20;return ticks; }
uint64_t OSGetTitleID(void) { return 0x0005000013374842ull; }
uint64_t OSTicksToMilliseconds(uint64_t t) { return t; }
uint64_t OSMillisecondsToTicks(uint64_t t) { return t; }
void OSSleepTicks(uint64_t t) { ticks+=t; }
void OSSavesDone_ReadyToRelease(void) {}
void WHBProcInit(void) { CHECK(!proc_alive);proc_alive=true; }
void WHBProcShutdown(void) { CHECK(!proc_alive); }
void SYSLaunchMenu(void) { CHECK(false); }
ProcUIStatus ProcUIProcessMessages(int block) {
    CHECK(block && proc_alive);++polls;
    if(!strcmp(mode,"interrupted") && polls==2)return PROCUI_STATUS_RELEASE_FOREGROUND;
    if(!strcmp(mode,"interrupted") && polls==3)return PROCUI_STATUS_IN_BACKGROUND;
    return PROCUI_STATUS_IN_FOREGROUND;
}
void ProcUIDrawDoneRelease(void) { CHECK(!audio.owned); }
void ProcUIShutdown(void) { CHECK(closed_io && !report.file && !audio.owned && !display_alive && cleared);proc_alive=false; }
void ProcUIClearCallbacks(void) { CHECK(!display_alive);cleared=true; }
void ProcUISetSaveCallback(uint32_t (*cb)(void*),void *ctx) { CHECK(cb(ctx)==0); }
void ProcUIRegisterCallback(int event,uint32_t (*cb)(void*),void *ctx,int priority) { (void)event;(void)cb;(void)ctx;(void)priority; }
int VPADRead(int channel,VPADStatus *pad,int count,VPADReadError *error) {
    CHECK(channel==0 && count==1 && polls<2000);*error=VPAD_READ_SUCCESS;pad->trigger=0;
    if(failed)pad->trigger=VPAD_BUTTON_MINUS;
    else if(complete)pad->trigger=!strcmp(mode,"hearing-fail")?VPAD_BUTTON_L:VPAD_BUTTON_R;
    return 1;
}
bool ctr_diagnostic_audio_begin(ctr_diagnostic_audio *a,bool fg) { CHECK(fg);a->owned=true;return true; }
bool ctr_diagnostic_audio_end(ctr_diagnostic_audio *a,void (*release)(void*),void *ctx) { release(ctx);a->owned=false;++a->releases;return true; }
void ctr_diagnostic_display_labels(const char *a,const char *b) { CHECK(a && b); }
bool ctr_diagnostic_display_init(void) { display_alive=true;return true; }
bool ctr_diagnostic_display_row(unsigned row,const char *text) { CHECK(row<14 && text);return true; }
bool ctr_diagnostic_display_draw(void) { CHECK(display_alive && foreground);return true; }
void ctr_diagnostic_display_invalidate(void) {}
void ctr_diagnostic_display_close(bool fg) { (void)fg;CHECK(!audio.owned);display_alive=false; }
bool ctr_report_storage_sync(void *ctx) { CHECK(failed || native.report.status!=CTR_NATIVE_PCM_PLAYING);if(!strcmp(mode,"slow-storage"))ticks+=2000;((ctr_report_storage*)ctx)->client=7;if(!strcmp(mode,"sd-fail")){errno=EIO;return false;}return true; }
void ctr_report_storage_close(ctr_report_storage *s) { s->client=-1; }
bool ctr_report_storage_verify(void *ctx,const char *path,uint64_t size,const uint8_t digest[32]) {
    (void)ctx;uint8_t bytes[8192],actual[32];FILE *f=fopen(path,"rb");CHECK(f && size==8192);
    CHECK(fread(bytes,1,8192,f)==8192 && fclose(f)==0);ctr_sha256 sha;ctr_sha256_init(&sha);
    CHECK(ctr_sha256_update(&sha,bytes,8192) && ctr_sha256_final(&sha,actual));CHECK(!memcmp(actual,digest,32));return true;
}
bool ctr_native_pcm_prepare(ctr_native_pcm *s,const int16_t *p,size_t n) {
    CHECK(p && n==70398 && audio.owned);if(!strcmp(mode,"native-fail")){++s->report.failures;return false;}
    s->report.prepared=true;s->report.frames=(uint32_t)n;return true;
}
bool ctr_native_pcm_play(ctr_native_pcm *s) { s->report.status=CTR_NATIVE_PCM_PLAYING;return true; }
bool ctr_native_pcm_pause(ctr_native_pcm *s) { s->report.status=CTR_NATIVE_PCM_PAUSED;return true; }
bool ctr_native_pcm_resume(ctr_native_pcm *s) { CHECK(s->report.status==CTR_NATIVE_PCM_PAUSED);s->report.status=CTR_NATIVE_PCM_PLAYING;return true; }
bool ctr_native_pcm_snapshot(ctr_native_pcm *s,ctr_native_pcm_report *out) {
    if(s->report.status==CTR_NATIVE_PCM_PLAYING) {
        uint32_t p=s->report.cursor[0]+3000;if(p>=s->report.frames){p=s->report.frames;s->report.status=CTR_NATIVE_PCM_COMPLETE;}
        s->report.cursor[0]=s->report.cursor[1]=p;
    }
    if(s->report.status==CTR_NATIVE_PCM_PAUSED && !strcmp(mode,"pause-drift"))++s->report.cursor[0];
    *out=s->report;return true;
}
void ctr_native_pcm_release(void *ctx) { ctr_native_pcm *s=ctx;if(s->report.prepared){++s->report.releases;s->report.prepared=false;} }
ctr_retained_logs ctr_retained_logs_copy(const char *a,const char *b) { CHECK(a && b);return (ctr_retained_logs){0}; }
static int child(const char *name,const char *directory) {
    (void)directory;mode=name;
#ifdef _WIN32
    CHECK(ctr_test_temp_directory(root,sizeof(root),"ctr-audio"));
#else
    CHECK(snprintf(root,sizeof(root),"/tmp/ctr-audio-XXXXXX")>0 && mkdtemp(root)!=NULL);
#endif
    int status=app_main();bool expected=!strcmp(mode,"happy") || !strcmp(mode,"hearing-fail") || !strcmp(mode,"slow-storage");
    CHECK((status==0)==expected && proc_alive==false);
    if(expected)CHECK(core_ok && paused && resumed && held_ok && complete && native.report.releases==1);
    if(!strcmp(mode,"hearing-fail"))CHECK(!strcmp(hearing,"fail"));
    if(!strcmp(mode,"happy"))CHECK(!strcmp(hearing,"pass"));
    CHECK(ctr_test_unlink(report.path)==0 && ctr_test_unlink(result.path)==0);
    char directory_results[600];CHECK(snprintf(directory_results,sizeof(directory_results),"%s/results",root)>0);
    CHECK(ctr_test_remove_directory(directory_results)==0 && ctr_test_remove_directory(root)==0);
    return 0;
}
int main(int argc,char **argv) {
    if(argc==3)return child(argv[1],argv[2]);
    const char *modes[]={"happy","hearing-fail","native-fail","pause-drift","interrupted","sd-fail","slow-storage"};
    for(unsigned i=0;i<7;++i) {
        ctr_test_child process;unsigned status;
        CHECK(ctr_test_child_start(&process,modes[i],".",child) && ctr_test_child_wait(&process,&status) && status==0);
    }
    puts("{\"ok\":true,\"guest_audio_frontend_cases\":7,\"console_verified\":false}");return 0;
}
