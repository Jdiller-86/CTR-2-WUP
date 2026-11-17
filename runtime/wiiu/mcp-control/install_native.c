#include "control.h"
#include "report_storage.h"
#include <coreinit/debug.h>
#include <coreinit/ios.h>
#include <coreinit/mcp.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <nn/acp.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const CtrControlRequest *request;
    int32_t mcp,error;
    char path[0x280] __attribute__((aligned(64)));
    MCPInstallInfo info __attribute__((aligned(64)));
    MCPInstallProgress progress __attribute__((aligned(64)));
    IOSVec vector __attribute__((aligned(64)));
} Install;

static CtrInstallCaptureInventoryState title_inventory(Install *i) {
    const uint32_t capacity=CTR_CAPTURE_TITLE_LIMIT;
    const uint32_t bytes=capacity*sizeof(MCPTitleListType);
    int32_t before=MCP_TitleCount(i->mcp);
    if (before < 0 || (uint32_t)before >= capacity) return CTR_CAPTURE_INVENTORY_INCONCLUSIVE;
    MCPTitleListType *titles=MEMAllocFromDefaultHeapEx((bytes+63u)&~63u,64);
    if (!titles) return CTR_CAPTURE_INVENTORY_INCONCLUSIVE;
    memset(titles,0,bytes);
    uint32_t found=capacity;
    int32_t result=MCP_TitleList(i->mcp,&found,titles,bytes);
    MCPTitleListType exact __attribute__((aligned(64)))={0};
    int32_t lookup=MCP_GetTitleInfo(i->mcp,i->request->title,&exact);
    CtrInstallCaptureInventory inventory=ctr_install_capture_inventory(titles,bytes,sizeof(*titles),
        before,result,found,MCP_TitleCount(i->mcp),i->request->title);
    inventory=ctr_install_capture_confirm_inventory(inventory,&exact,sizeof(exact),lookup,i->request->title);
    MEMFreeToDefaultHeap(titles);
    return inventory.state;
}

static int32_t info(void *context) {
    Install *i=context;
    if(!ctr_control_ready())return -ECANCELED;
    int32_t result=MCP_InstallGetProgress(i->mcp,&i->progress);
    if(result || i->progress.inProgress)return result?result:-EBUSY;
    result=MCP_InstallGetInfo(i->mcp,i->path,&i->info);
    if(result)return result;
    uint64_t title=0;
    const uint8_t *bytes=(const uint8_t *)&i->info;
    for(unsigned n=0;n<8;++n)title=(title<<8)|bytes[n];
    return title==i->request->title?0:-EINVAL;
}

static int32_t target(void *context,unsigned device) {
    Install *i=context;
    int32_t result=MCP_InstallSetTargetDevice(i->mcp,(MCPInstallTarget)device);
    if(!result && device==MCP_INSTALL_TARGET_USB)result=MCP_InstallSetTargetUsb(i->mcp,1);
    return result;
}

static void complete(IOSError result,void *context) {
    ctr_install_capture_complete(context,result);
}

static int32_t start(void *context,CtrInstallCapture *capture) {
    Install *i=context;
    if(!ctr_control_ready())return -ECANCELED;
    i->vector=(IOSVec){.vaddr=i->path,.len=0x27F};
    return IOS_IoctlvAsync(i->mcp,0x81,1,0,&i->vector,complete,capture);
}

static int32_t progress(void *context,CtrInstallCaptureProgress *out) {
    Install *i=context;
    int32_t result=MCP_InstallGetProgress(i->mcp,&i->progress);
    if(!result)*out=(CtrInstallCaptureProgress){i->progress.tid,i->progress.sizeProgress,i->progress.sizeTotal,
        i->progress.contentsProgress,i->progress.contentsTotal,i->progress.inProgress};
    return result;
}

static int32_t abort_install(void *context) {
    return MCP_InstallTitleAbort(((Install *)context)->mcp);
}

static bool pump(void *context) {
    (void)context;
    OSSleepTicks(OSMillisecondsToTicks(100));
    return ctr_control_ready();
}

static bool record(void *context,const char *event,int32_t result,const CtrInstallCaptureProgress *out) {
    Install *i=context;
    if(!out)OSReport("CTR2WUP control install: %s result=%d\n",event,result);
    if(result && (!strcmp(event,"get_info") || !strcmp(event,"set_target") ||
                  !strcmp(event,"submit_raw") || !strcmp(event,"callback_raw")))i->error=result;
    if(!strcmp(event,"before_submit"))return ctr_control_receipt(i->request,"install_pending",0);
    return true;
}

static bool installed(const CtrControlRequest *r) {
    ACPMetaXml meta __attribute__((aligned(64)))={0};
    ACPResult result = ACPGetTitleMetaXml(r->title, &meta);
    OSReport("CTR2WUP control install: ACPGetTitleMetaXml result=%d\n", result);
    if(result != ACP_RESULT_SUCCESS || meta.title_id!=r->title ||
       meta.title_version!=r->version || meta.app_launch_type || meta.invisible_flag ||
       strncmp(meta.content_platform,"WUP",sizeof(meta.content_platform)))return false;
    ctr_report_storage storage={.client=-1};
    bool ok=ctr_report_storage_sync(&storage);
    for(unsigned n=0;ok && n<r->installed_count;++n) {
        const CtrInstallCaptureFile *file=&r->installed[n];
        ok=ctr_report_storage_verify_installed_file(&storage,r->target,r->title,file->name,file->size,file->sha256);
    }
    ctr_report_storage_close(&storage);
    return ok;
}

void ctr_control_install(const CtrControlRequest *r) {
    Install i={.request=r,.mcp=-1,.error=0};
    snprintf(i.path,sizeof(i.path),"/vol/external01/install/ctr2wup-%s",r->job);
    char path[sizeof(i.path)+3];snprintf(path,sizeof(path),"fs:%s",i.path);
    if(!ctr_install_capture_verify(path,r->files,r->file_count,NULL)) {
        ctr_control_receipt(r,"package_mismatch",-errno);return;
    }
    i.mcp=MCP_Open();
    if(i.mcp<0) { ctr_control_receipt(r,"install_failed",i.mcp);return; }
    const char *state;
    CtrInstallCaptureInventoryState inventory = title_inventory(&i);
    if (inventory == CTR_CAPTURE_INVENTORY_PRESENT) {
        state = "title_exists";
        i.error = -EEXIST;
    } else if (inventory != CTR_CAPTURE_INVENTORY_ABSENT) {
        state = "install_refused";
        i.error = -EIO;
    } else {
        CtrInstallCapture capture={0};
        const CtrInstallCaptureOps ops={record,info,target,start,progress,abort_install,pump};
        if(!ctr_install_capture_run(&capture,&ops,&i,r->target)) {
            state="install_failed";
            if(!i.error)i.error=-EIO;
        } else if(!installed(r)) {
            state="installed_unverified";i.error=-EIO;
        } else {
            state="installed_verified";
        }
    }
    MCP_Close(i.mcp);
    ctr_control_receipt(r,state,i.error);
}
