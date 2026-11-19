#define _POSIX_C_SOURCE 200809L
#include "control.h"
#include "report_storage.h"
#include <coreinit/debug.h>
#include <coreinit/launch.h>
#include <coreinit/mcp.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <coreinit/title.h>
#include <rpxloader/rpxloader.h>
#include <sysapp/launch.h>
#include <sysapp/title.h>
#include <sys/stat.h>
#include <wups.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

WUPS_PLUGIN_NAME("CTR2WUP Control");
WUPS_PLUGIN_DESCRIPTION("Paired local Wii U testing commands");
WUPS_PLUGIN_VERSION("0.1.0");
WUPS_PLUGIN_AUTHOR("CTR2WUP contributors");
WUPS_PLUGIN_LICENSE("MIT");

static OSThread worker __attribute__((aligned(64)));
static uint8_t stack[0x20000] __attribute__((aligned(64)));
static atomic_bool stopping,foreground;
static bool started;

static bool menu(uint64_t title) {
    return title && title == _SYSGetSystemApplicationTitleId(SYSTEM_APP_ID_WII_U_MENU);
}

bool ctr_control_ready(void) {
    return atomic_load(&foreground) && !atomic_load(&stopping);
}

static bool directory(const char *path) {
    if(mkdir(path,0700)==0)return true;
    struct stat st;
    return errno==EEXIST && lstat(path,&st)==0 && S_ISDIR(st.st_mode);
}

static bool write_text(const char *path,const char *text) {
    FILE *file=fopen(path,"wb");
    if(!file)return false;
    bool ok=fputs(text,file)>=0 && fflush(file)==0;
    return fclose(file)==0 && ok;
}

bool ctr_control_receipt(const CtrControlRequest *r,const char *state,int32_t result) {
    char path[128],text[176];
    snprintf(path,sizeof(path),CTR_CONTROL_ROOT "/result-%s.txt",r->job);
    snprintf(text,sizeof(text),"CTR1 %016llx %08x %s %s %d\n",(unsigned long long)r->epoch,r->sequence,r->job,state,result);
    if(!write_text(path,text))return false;
    ctr_report_storage storage={.client=-1};
    bool ok=ctr_report_storage_sync(&storage);
    ctr_report_storage_close(&storage);
    return ok;
}

static bool read_packet(const char *path, uint8_t *bytes, size_t capacity, size_t *size) {
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    *size = fread(bytes, 1, capacity, file);
    bool ok = !ferror(file);
    return fclose(file) == 0 && ok;
}

static void running_job(char job[33],bool loader) {
    strcpy(job,"-");
    char path[256]={0};
    if(!loader || RPXLoader_GetPathOfRunningExecutable(path,sizeof(path)-1)!=RPX_LOADER_RESULT_SUCCESS)return;
    static const char prefix[]=CTR_CONTROL_APPS;
    if(strncmp(path,prefix,sizeof(prefix)-1))return;
    const char *name=path+sizeof(prefix)-1;
    if(strspn(name,"0123456789abcdef")!=32 || (strcmp(name+32,".rpx") && strcmp(name+32,".wuhb")))return;
    memcpy(job,name,32);job[32]=0;
}

static void publish_state(uint64_t epoch,uint32_t sequence,uint64_t title,const char *public,const char *job) {
    char text[192];
    snprintf(text,sizeof(text),"CTR1 %016llx %08x %016llx %s %u %s\n",(unsigned long long)epoch,sequence,
             (unsigned long long)title,public,menu(title) && ctr_control_ready()?1u:0u,job);
    write_text(CTR_CONTROL_ROOT "/state.txt",text);
}

static void launch_homebrew(const CtrControlRequest *r,bool loader) {
    if(!loader) { ctr_control_receipt(r,"loader_unavailable",-ENOSYS);return; }
    const CtrInstallCaptureFile *file=r->files;
    char path[128];snprintf(path,sizeof(path),CTR_SD_ROOT CTR_CONTROL_APPS "%s",file->name);
    ctr_report_storage storage={.client=-1};
    bool verified=ctr_report_storage_sync(&storage) && ctr_report_storage_verify(&storage,path,file->size,file->sha256);
    ctr_report_storage_close(&storage);
    if(!verified || !ctr_control_ready()) { ctr_control_receipt(r,"homebrew_refused",-EIO);return; }
    if (!ctr_control_receipt(r, "launch_requested", 0) || !ctr_control_ready()) return;
    remove(CTR_CONTROL_ROOT "/state.txt");
    int32_t result = RPXLoader_LaunchHomebrew(path + sizeof(CTR_SD_ROOT) - 1);
    if (result && ctr_control_ready()) ctr_control_receipt(r, "launch_failed", result);
    if (!result) atomic_store(&stopping, true);
}

static void launch_title(const CtrControlRequest *r) {
    int32_t mcp=MCP_Open();
    if(mcp<0) { ctr_control_receipt(r,"launch_failed",mcp);return; }
    MCPTitleListType info __attribute__((aligned(64)))={0};
    int32_t result=MCP_GetTitleInfo(mcp,r->title,&info);
    MCP_Close(mcp);
    if(result || info.titleId!=r->title || info.appType!=MCP_APP_TYPE_GAME || !ctr_control_ready()) {
        ctr_control_receipt(r,"title_refused",result?result:-EINVAL);return;
    }
    if(!ctr_control_receipt(r,"launch_requested",0) || !ctr_control_ready())return;
    remove(CTR_CONTROL_ROOT "/state.txt");
    SYSLaunchTitle(r->title);
    atomic_store(&stopping,true);
}

static void dispatch(const CtrControlRequest *r,uint64_t current,bool loader) {
    if(!ctr_control_ready()) { ctr_control_receipt(r,"foreground_required",-EBUSY);return; }
    switch(r->action) {
        case CTR_CONTROL_REBOOT:
            if(!ctr_control_receipt(r,"reboot_requested",0) || !ctr_control_ready())return;
            remove(CTR_CONTROL_ROOT "/state.txt");
            OSForceFullRelaunch();
            OSLaunchTitlel(OS_TITLE_ID_REBOOT,0);
            atomic_store(&stopping,true);
            break;
        case CTR_CONTROL_HOMEBREW:launch_homebrew(r,loader);break;
        case CTR_CONTROL_TITLE:launch_title(r);break;
        case CTR_CONTROL_INSTALL:
            if(menu(current))ctr_control_install(r);
            else ctr_control_receipt(r,"menu_required",-EBUSY);
            break;
    }
}

static int run(int argc,const char **argv) {
    (void)argc;(void)argv;
    uint8_t key[33];
    size_t size;
    if (!read_packet(CTR_SD_ROOT "wiiu/ctr2wup-control.pub", key, sizeof(key), &size) || size != 32)
        return 0;
    if (!directory(CTR_SD_ROOT "ctr2wup") || !directory(CTR_CONTROL_ROOT)) return 0;
    const uint64_t epoch=(uint64_t)OSGetTime(),title=OSGetTitleID();
    const bool loader=RPXLoader_InitLibrary()==RPX_LOADER_RESULT_SUCCESS;
    char public[65],job[33];
    for(unsigned n=0;n<32;++n)snprintf(public+n*2,3,"%02x",key[n]);
    running_job(job, loader && title == _SYSGetSystemApplicationTitleId(SYSTEM_APP_ID_HEALTH_AND_SAFETY));
    uint32_t sequence=1;
    publish_state(epoch,sequence,title,public,job);
    bool previous=ctr_control_ready();
    while(!atomic_load(&stopping) && sequence!=UINT32_MAX) {
        bool active=ctr_control_ready();
        if(active!=previous) {
            publish_state(epoch,sequence,title,public,job);previous=active;
        }
        if(active) {
            char path[128];
            snprintf(path,sizeof(path),CTR_CONTROL_ROOT "/req-%016llx-%08x.bin",(unsigned long long)epoch,sequence);
            uint8_t packet[CTR_CONTROL_PACKET_BYTES+1];
            if (read_packet(path, packet, sizeof(packet), &size)) {
                CtrControlRequest request;
                if(ctr_control_decode(packet,size,key,epoch,sequence,&request))dispatch(&request,title,loader);
                else OSReport("CTR2WUP control: rejected command signature or format\n");
                if (!atomic_load(&stopping)) {
                    remove(path);
                    publish_state(epoch, ++sequence, title, public, job);
                }
            }
        }
        OSSleepTicks(OSMillisecondsToTicks(250));
    }
    if (loader) RPXLoader_DeInitLibrary();
    return 0;
}

ON_APPLICATION_START() {
    atomic_store(&stopping,false);
    atomic_store(&foreground,true);
    started=OSCreateThread(&worker,run,0,NULL,stack+sizeof(stack),sizeof(stack),18,OS_THREAD_ATTRIB_AFFINITY_CPU1);
    if(started)OSResumeThread(&worker);
}

ON_RELEASE_FOREGROUND() { atomic_store(&foreground,false); }
ON_ACQUIRED_FOREGROUND() { atomic_store(&foreground,true); }
ON_APPLICATION_REQUESTS_EXIT() { atomic_store(&stopping,true); }
ON_APPLICATION_ENDS() {
    atomic_store(&stopping,true);
    if(started)OSJoinThread(&worker,NULL);
    started=false;
}
