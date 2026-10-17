#define _POSIX_C_SOURCE 200809L
/* Original, fixed-payload MCP error capture. No installer/CFW source is copied.
 * IOS install command 0x81 and path-vector framing were checked against GX2
 * 0dce0a96818cae3c4a4aea0f9b722e2cfda2b5ac, InstallWindow.cpp. */
#include "install_capture.h"
#include "install_capture_package.h"
#include "log.h"
#include "display.h"
#include "diagnostic_audio.h"
#include "test_report.h"
#include "test_lifecycle.h"
#include "report_storage.h"
#include <coreinit/ios.h>
#include <coreinit/mcp.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/foreground.h>
#include <coreinit/debug.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <coreinit/title.h>
#include <proc_ui/procui.h>
#include <sysapp/launch.h>
#include <vpad/input.h>
#include <whb/proc.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define PAYLOAD "fs:/vol/external01/install/CTR2WUP-M1"
#define MCP_PATH "/vol/app_sd/install/CTR2WUP-M1"
#define LOG_ROOT "fs:/vol/external01/ctr2wup"
#define LOG_DIRECTORY LOG_ROOT "/install-check/install-capture"
static capture_log report;
static ctr_report_storage storage={.client=-1};
static ctr_diagnostic_audio audio;
static CtrInstallCapture capture;
static int32_t mcp=-1;
static atomic_bool exit_requested;
static bool exit_observed,foreground=true,display_ready,input_valid,platform_failed;
static ctr_test_lifecycle lifecycle;
static uint32_t buttons,held_buttons;
static MCPInstallInfo info __attribute__((aligned(64)));
static MCPInstallProgress progress_info __attribute__((aligned(64)));
static char install_path[0x280] __attribute__((aligned(64)))=MCP_PATH;
static IOSVec install_vector __attribute__((aligned(64)));

static void notice(const char *format,...) {
    char text[512];va_list args;va_start(args,format);
    vsnprintf(text,sizeof(text),format,args);va_end(args);
    OSReport("%s\n",text);capture_display_line(text);
}

static bool line(const char *text) {
    OSReport("%s\n",text);
    return capture_log_line(&report,text);
}

static bool record(void *unused,const char *event,int32_t result,
                   const CtrInstallCaptureProgress *progress) {
    (void)unused;char text[384];
    int n=snprintf(text,sizeof(text),"CAPTURE event=%s code=0x%08x signed=%ld target_title=%s",
        event,(unsigned)(uint32_t)result,(long)result,CTR_CAPTURE_TITLE);
    if(progress && n>0 && (size_t)n<sizeof(text))
        n+=snprintf(text+n,sizeof(text)-(size_t)n," active=%u title=%016llx bytes=%llu/%llu contents=%u/%u",
            progress->active,(unsigned long long)progress->title,(unsigned long long)progress->bytes,
            (unsigned long long)progress->total,progress->contents,progress->total_contents);
    if(progress)notice("MCP %s: 0x%08x / %llu of %llu bytes",event,(unsigned)(uint32_t)result,
        (unsigned long long)progress->bytes,(unsigned long long)progress->total);
    else notice("MCP %s: 0x%08x",event,(unsigned)(uint32_t)result);
    return n>0 && (size_t)n<sizeof(text) && line(text);
}

static bool audio_begin(void) {
    bool ok=ctr_diagnostic_audio_begin(&audio,foreground);
    char text[160];snprintf(text,sizeof(text),"AUDIO result=%s owned=%u inherited=%u resets=%u",
        ctr_diagnostic_audio_result_name(audio.result),audio.owned,audio.last_inherited,audio.inherited_resets);
    if(report.file)line(text);
    return ok;
}

static bool pump(void *unused) {
    (void)unused;buttons=0;held_buttons=0;input_valid=false;
    ProcUIStatus status=ProcUIProcessMessages(TRUE);
    bool was_foreground=foreground;
    if(status==PROCUI_STATUS_EXITING) {
        exit_requested=true;exit_observed=true;
    } else if(status==PROCUI_STATUS_RELEASE_FOREGROUND) {
        if(!ctr_diagnostic_audio_end(&audio,NULL,NULL))platform_failed=true;
        report.sync=NULL;report.verify=NULL;ctr_report_storage_close(&storage);
        ProcUIDrawDoneRelease();
    }
    foreground=status==PROCUI_STATUS_IN_FOREGROUND;
    if(!foreground) {
        /* IOS still owns an active install request. Preserve closed stdio
         * checkpoints while draining it, without reopening an FSA client. */
        report.sync=NULL;report.verify=NULL;ctr_report_storage_close(&storage);
    }
    if(foreground) {
        if(!was_foreground) {
            report.sync=ctr_report_storage_sync;report.verify=ctr_report_storage_verify;
            capture_display_invalidate();
            if(!audio_begin())exit_requested=true;
        }
        VPADStatus pad;VPADReadError error;
        if(VPADRead(VPAD_CHAN_0,&pad,1,&error)>0 && error==VPAD_READ_SUCCESS) {
            buttons=pad.trigger;held_buttons=pad.hold;input_valid=true;
        }
        if(buttons&(VPAD_BUTTON_MINUS|VPAD_BUTTON_B))exit_requested=true;
        if(display_ready && !capture_display_draw()) {
            line("DISPLAY draw=failed");platform_failed=true;exit_requested=true;
        }
    }
    OSSleepTicks(OSMillisecondsToTicks(20));
    return !exit_requested;
}

static bool payload_ready(void) {
    const char *failed=NULL;
    bool ok=ctr_install_capture_verify(PAYLOAD,ctr_capture_files,7,&failed);
    notice(ok?"Original M1 package verified.":"Package check failed: %s",failed?failed:"inventory");
    char text[192];snprintf(text,sizeof(text),"PAYLOAD exact_seven_files=%u failed=%s errno=%d",
        ok,failed?failed:"none",ok?0:errno);
    return line(text) && ok;
}

static bool title_absent(void) {
    int32_t count=MCP_TitleCount(mcp);
    if(!record(NULL,"title_count",count,NULL) || count<0 || count>4096)return false;
    if(!count)return true;
    uint32_t bytes=(uint32_t)count*sizeof(MCPTitleListType),found=(uint32_t)count;
    MCPTitleListType *titles=MEMAllocFromDefaultHeapEx((bytes+63u)&~63u,64);
    if(!titles) { record(NULL,"title_list_allocation",-1,NULL);return false; }
    int32_t result=MCP_TitleList(mcp,&found,titles,bytes);
    bool absent=result==0 && found==(uint32_t)count;
    if(absent)for(uint32_t i=0;i<found;++i)if(titles[i].titleId==CTR_CAPTURE_TITLE_ID)absent=false;
    MEMFreeToDefaultHeap(titles);
    if(!record(NULL,"title_list",result,NULL))return false;
    return line(absent?"COLLISION title_absent=1":"COLLISION title_absent=0 installation_blocked=1") && absent;
}

static int32_t get_info(void *unused) {
    (void)unused;memset(&info,0,sizeof(info));
    int32_t result=MCP_InstallGetInfo(mcp,install_path,&info);
    if(!record(NULL,"get_info_api",result,NULL))return -10002;
    if(result)return result;
    /* MCP's get-info response begins with the big-endian title ID. */
    const unsigned char *bytes=(const unsigned char *)&info;uint64_t title=0;
    for(unsigned i=0;i<8;++i)title=(title<<8)|bytes[i];
    return title==CTR_CAPTURE_TITLE_ID?0:-10001;
}

static int32_t set_target(void *unused,unsigned target) {
    (void)unused;int32_t result=MCP_InstallSetTargetDevice(mcp,(MCPInstallTarget)target);
    if(!record(NULL,"target_device",result,NULL))return -10002;
    if(!result && target==MCP_INSTALL_TARGET_USB) {
        result=MCP_InstallSetTargetUsb(mcp,1);
        if(!record(NULL,"target_usb",result,NULL))return -10002;
    }
    return result;
}

static void completed(IOSError result,void *context) {
    ctr_install_capture_complete(context,(int32_t)result);
}

static int32_t start(void *unused,CtrInstallCapture *state) {
    (void)unused;
    install_vector=(IOSVec){.vaddr=install_path,.len=0x27F};
    return (int32_t)IOS_IoctlvAsync(mcp,0x81,1,0,&install_vector,completed,state);
}

static int32_t get_progress(void *unused,CtrInstallCaptureProgress *out) {
    (void)unused;memset(&progress_info,0,sizeof(progress_info));
    int32_t result=MCP_InstallGetProgress(mcp,&progress_info);
    if(!result)*out=(CtrInstallCaptureProgress){.title=progress_info.tid,
        .bytes=progress_info.sizeProgress,.total=progress_info.sizeTotal,
        .contents=progress_info.contentsProgress,.total_contents=progress_info.contentsTotal,
        .active=progress_info.inProgress};
    return result;
}

static int32_t abort_install(void *unused) { (void)unused;return MCP_InstallTitleAbort(mcp); }

static uint32_t home_exit(void *unused) { (void)unused;exit_requested=true;return 0; }
static uint32_t save_ready(void *unused) { (void)unused;OSSavesDone_ReadyToRelease();return 0; }
static void cleanup(void *unused) {
    (void)unused;
    if(mcp>=0) { MCP_Close(mcp);mcp=-1; }
    bool audio_ok=ctr_diagnostic_audio_end(&audio,NULL,NULL);
    if(!audio_ok)platform_failed=true;
    if(report.path[0] && !report.failed) {
        const char *text=audio_ok?"AUDIO release=complete":"AUDIO release=failed";
        if(report.file)line(text);else capture_log_checkpoint(&report,text);
    }
    capture_display_close(foreground);display_ready=false;
    capture_log_close(&report);
    ctr_report_storage_close(&storage);
}
static void clear_callbacks(void *unused) {
    (void)unused;ProcUIClearCallbacks();
    report.sync=NULL;report.verify=NULL;
    ctr_report_storage_close(&storage);
    ProcUISetSaveCallback(save_ready,NULL);
}
static void request_menu(void *unused) { (void)unused;SYSLaunchMenu(); }
static ctr_proc_status process_messages(void *unused) {
    (void)unused;
    switch(ProcUIProcessMessages(TRUE)) {
        case PROCUI_STATUS_IN_FOREGROUND:return CTR_PROC_FOREGROUND;
        case PROCUI_STATUS_IN_BACKGROUND:return CTR_PROC_BACKGROUND;
        case PROCUI_STATUS_RELEASE_FOREGROUND:return CTR_PROC_RELEASE;
        case PROCUI_STATUS_EXITING:return CTR_PROC_EXITING;
        default:return (ctr_proc_status)-1;
    }
}
static void release_foreground(void *unused) { (void)unused;ProcUIDrawDoneRelease(); }
static void wait_messages(void *unused) { (void)unused;OSSleepTicks(OSMillisecondsToTicks(5)); }
static void shutdown_procui(void *unused) { (void)unused;ProcUIShutdown(); }
static void shutdown_whb(void *unused) { (void)unused;WHBProcShutdown(); }
static void checkpoint(void *unused,const char *message) {
    (void)unused;
    OSReport("%s\n",message);
    if(report.path[0] && !report.failed)capture_log_checkpoint(&report,message);
    ctr_report_storage_close(&storage);
}
static const ctr_exit_ops exit_ops={cleanup,clear_callbacks,request_menu,process_messages,
    release_foreground,wait_messages,shutdown_procui,shutdown_whb,checkpoint};

int main(void) {
    bool saved=false;
    WHBProcInit();
    lifecycle.title_id=OSGetTitleID();
    if(ctr_test_from_hbl(lifecycle.title_id))
        ProcUIRegisterCallback(PROCUI_CALLBACK_HOME_BUTTON_DENIED,home_exit,NULL,99);
    /* Establish checked startup evidence before graphics. Reuse WUT's mount;
     * never create a competing WHB mount or unmount the process-wide SD. */
    char parent[512];struct stat directory_info;
    bool ready=ctr_test_report_directory(LOG_ROOT,true,parent,sizeof(parent)) &&
        (mkdir(LOG_DIRECTORY,0777)==0 || errno==EEXIST) &&
        lstat(LOG_DIRECTORY,&directory_info)==0 && S_ISDIR(directory_info.st_mode) &&
        capture_log_open(&report,LOG_DIRECTORY,(uint64_t)OSGetSystemTime(),
            ctr_report_storage_sync,ctr_report_storage_verify,&storage);
    if(!ready && !report.failed) {
        report.failed=true;report.error=errno?errno:EIO;report.stage="directory";
    }
    if(ready) {
        char text[256];snprintf(text,sizeof(text),
            "IDENTITY capture_source=%s source_dirty=%u package_source=%s title=%s protocol=mcp-capture-v2",
            CTR_SOURCE_COMMIT,CTR_SOURCE_DIRTY,CTR_CAPTURE_PACKAGE_SOURCE,CTR_CAPTURE_TITLE);
        ready=line(text) && line("PACKAGE artifact_sha256=" CTR_CAPTURE_PACKAGE_SHA256);
    }
    bool audio_ready=audio_begin();
    display_ready=capture_display_init();
    platform_failed=!audio_ready || !display_ready;
    notice("Checking the existing M1 package.");
    if(report.file)line(display_ready?"DISPLAY native_GX2=ready":"DISPLAY native_GX2=failed");
    if(ready) {
        ready=audio_ready && display_ready && !report.failed && payload_ready();
        if(ready) {
            mcp=MCP_Open();
            ready=record(NULL,"mcp_open",mcp,NULL) && mcp>=0 && title_absent();
        }
    }
    if(ready) {
        notice("SD startup report: exact read-back verified.");
        notice("Use the SAME destination as the failed GX2 attempt.");
        notice("A: internal memory. X: USB. Minus: exit.");
        bool selection_armed=false;
        while(pump(NULL)) {
            if(ctr_install_capture_select(&selection_armed,input_valid,
                (held_buttons&(VPAD_BUTTON_A|VPAD_BUTTON_X))!=0,
                (buttons&(VPAD_BUTTON_A|VPAD_BUTTON_X))!=0)) {
                unsigned target=(buttons&VPAD_BUTTON_X)?1:0;
                /* Recheck after waiting for the explicit selection. */
                if(payload_ready() && title_absent()) {
                    static const CtrInstallCaptureOps ops={record,get_info,set_target,start,get_progress,abort_install,pump};
                    bool ok=ctr_install_capture_run(&capture,&ops,NULL,target);
                    notice(ok?"Installation succeeded.":"Installation stopped.");
                    if(capture.submitted)notice("Raw MCP callback: 0x%08x",(unsigned)(uint32_t)capture.result);
                } else notice("Preflight changed; installation blocked.");
                break;
            }
        }
    } else notice("Preflight blocked installation.");
    /* Finish and verify the SD view in the foreground. A system exit may
     * instead leave a partial stdio-only checkpoint; never call that saved. */
    while(!foreground && !exit_requested && pump(NULL)) {}
    if(report.file) {
        line("CAPTURE scope=installation_diagnosis full_milestone_acceptance=0");
        line("END OF REPORT");
    }
    saved=capture_log_close(&report) && report.verify!=NULL;
    if(saved) {
        notice("SD REPORT: CLOSED READ-BACK VERIFIED");
        notice("sd:/ctr2wup/install-check/install-capture/");
        notice("File: %s",report.filename);
        notice("FTP: refresh the directory listing before copying.");
    } else {
        notice("SD REPORT FAILED. Photograph this screen.");
        notice("Stage: %s / errno: %d / FSA: 0x%08x",report.stage?report.stage:"unknown",report.error,
            (unsigned)(uint32_t)storage.last_status);
    }
    notice("Minus: exit. Return only the install-capture folder.");
    if(foreground) {
        if(display_ready && !capture_display_draw())platform_failed=true;
        if(!saved && !exit_observed)exit_requested=false;
    }
    if(!display_ready)exit_requested=true;
    while(!exit_requested && pump(NULL)) {}
    if(!ctr_test_complete_exit(&lifecycle,exit_observed,&exit_ops,NULL))saved=false;
    ctr_report_storage_close(&storage);
    return saved && !report.failed && !platform_failed?0:1;
}
