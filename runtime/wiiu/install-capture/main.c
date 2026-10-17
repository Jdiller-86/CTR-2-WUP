#define _POSIX_C_SOURCE 200809L
/* Original, fixed-payload MCP error capture. No installer/CFW source is copied.
 * IOS install command 0x81 and path-vector framing were checked against GX2
 * 0dce0a96818cae3c4a4aea0f9b722e2cfda2b5ac, InstallWindow.cpp. */
#include "install_capture.h"
#include "install_capture_package.h"
#include "test_report.h"
#include "test_lifecycle.h"
#include "report_storage.h"
#include <coreinit/ios.h>
#include <coreinit/mcp.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/foreground.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <coreinit/title.h>
#include <proc_ui/procui.h>
#include <sysapp/launch.h>
#include <vpad/input.h>
#include <whb/log.h>
#include <whb/log_console.h>
#include <whb/proc.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define PAYLOAD "fs:/vol/external01/install/CTR2WUP-M1"
#define MCP_PATH "/vol/app_sd/install/CTR2WUP-M1"
#define LOG_ROOT "fs:/vol/external01/ctr2wup"
#define LOG_DIRECTORY LOG_ROOT "/install-check/install-capture"
static ctr_test_report report;
static ctr_report_storage storage={.client=-1};
static CtrInstallCapture capture;
static int32_t mcp=-1;
static bool exit_requested,exit_observed,foreground=true,console_ready,input_valid;
static ctr_test_lifecycle lifecycle;
static uint32_t buttons,held_buttons;
static MCPInstallInfo info __attribute__((aligned(64)));
static MCPInstallProgress progress_info __attribute__((aligned(64)));
static char install_path[0x280] __attribute__((aligned(64)))=MCP_PATH;
static IOSVec install_vector __attribute__((aligned(64)));

static bool line(const char *text) {
    WHBLogPrintf("%s",text);
    return ctr_test_report_line(&report,text) && ctr_test_report_commit(&report);
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
    return n>0 && (size_t)n<sizeof(text) && line(text);
}

static bool pump(void *unused) {
    (void)unused;buttons=0;held_buttons=0;input_valid=false;
    ProcUIStatus status=ProcUIProcessMessages(TRUE);
    foreground=status==PROCUI_STATUS_IN_FOREGROUND;
    if(status==PROCUI_STATUS_EXITING) { exit_requested=true;exit_observed=true; }
    else if(status==PROCUI_STATUS_RELEASE_FOREGROUND)ProcUIDrawDoneRelease();
    if(foreground) {
        VPADStatus pad;VPADReadError error;
        if(VPADRead(VPAD_CHAN_0,&pad,1,&error)>0 && error==VPAD_READ_SUCCESS) {
            buttons=pad.trigger;held_buttons=pad.hold;input_valid=true;
        }
        if(buttons&(VPAD_BUTTON_MINUS|VPAD_BUTTON_B))exit_requested=true;
        WHBLogConsoleDraw();
    }
    OSSleepTicks(OSMillisecondsToTicks(20));
    return !exit_requested;
}

static bool payload_ready(void) {
    const char *failed=NULL;
    bool ok=ctr_install_capture_verify(PAYLOAD,ctr_capture_files,7,&failed);
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
    if(console_ready) { WHBLogConsoleFree();console_ready=false; }
    ctr_test_report_close(&report);
}
static void clear_callbacks(void *unused) {
    (void)unused;ProcUIClearCallbacks();
    ctr_test_report_set_volume_sync(&report,NULL,NULL);
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
    if(report.path[0] && !report.failed)ctr_test_report_checkpoint(&report,message);
}
static const ctr_exit_ops exit_ops={cleanup,clear_callbacks,request_menu,process_messages,
    release_foreground,wait_messages,shutdown_procui,shutdown_whb,checkpoint};

int main(void) {
    bool saved=false;
    WHBProcInit();
    lifecycle.title_id=OSGetTitleID();
    if(ctr_test_from_hbl(lifecycle.title_id))
        ProcUIRegisterCallback(PROCUI_CALLBACK_HOME_BUTTON_DENIED,home_exit,NULL,99);
    /* The pinned libwhb console owns OSScreen/MEM1 and returns FALSE even
     * after initialization. It must not be combined with WHBGfx ownership. */
    (void)WHBLogConsoleInit();console_ready=true;
    WHBLogPrintf("CTR2WUP Install Capture");
    WHBLogPrintf("Checking the existing M1 package. No files will be replaced.");
    WHBLogConsoleDraw();
    char parent[512];struct stat directory_info;
    bool ready=ctr_test_report_directory(LOG_ROOT,true,parent,sizeof(parent)) &&
        (mkdir(LOG_DIRECTORY,0777)==0 || errno==EEXIST) &&
        lstat(LOG_DIRECTORY,&directory_info)==0 && S_ISDIR(directory_info.st_mode) &&
        ctr_test_report_open(&report,LOG_DIRECTORY,"install-capture","0.27.0",(uint64_t)OSGetSystemTime());
    if(ready) {
        ctr_test_report_set_volume_sync(&report,ctr_report_storage_sync,&storage);
        char text[256];snprintf(text,sizeof(text),
            "IDENTITY capture_source=%s source_dirty=%u package_source=%s title=%s protocol=mcp-capture-v1",
            CTR_SOURCE_COMMIT,CTR_SOURCE_DIRTY,CTR_CAPTURE_PACKAGE_SOURCE,CTR_CAPTURE_TITLE);
        ready=line(text) && line("PACKAGE artifact_sha256=" CTR_CAPTURE_PACKAGE_SHA256) && payload_ready();
        if(ready) {
            mcp=MCP_Open();
            ready=record(NULL,"mcp_open",mcp,NULL) && mcp>=0 && title_absent();
        }
    }
    if(ready) {
        WHBLogPrintf("Use the SAME destination as the failed GX2 attempt.");
        WHBLogPrintf("A: install to internal memory. X: install to USB.");
        WHBLogPrintf("Minus: exit. One attempt; the exact error is saved automatically.");
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
                    WHBLogPrintf(ok?"Installation succeeded.":"Installation stopped. Exact results are in the SD report.");
                    if(capture.submitted)WHBLogPrintf("Raw MCP callback: 0x%08x",(unsigned)(uint32_t)capture.result);
                } else WHBLogPrintf("Preflight changed; installation blocked.");
                break;
            }
        }
    } else WHBLogPrintf("Preflight blocked installation. Check the report or take a photo.");
    if(report.file) {
        line("CAPTURE scope=installation_diagnosis full_milestone_acceptance=0");
        ctr_test_report_line(&report,"END OF REPORT");
    }
    saved=ctr_test_report_close(&report) && report.path[0];
    if(saved)WHBLogPrintf("Report saved: sd:/ctr2wup/install-check/install-capture/");
    else WHBLogPrintf("REPORT WRITE FAILED. Please photograph this screen.");
    WHBLogPrintf("Minus: exit. Return the install-capture folder by FTP.");
    if(foreground) {
        WHBLogConsoleDraw();
        if(!saved && !exit_observed)exit_requested=false;
    }
    while(!exit_requested && pump(NULL)) {}
    if(!ctr_test_complete_exit(&lifecycle,exit_observed,&exit_ops,NULL))saved=false;
    ctr_report_storage_close(&storage);
    return saved?0:1;
}
