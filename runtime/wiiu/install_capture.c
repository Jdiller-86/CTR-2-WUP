#define _POSIX_C_SOURCE 200809L
#include "install_capture.h"
#include "ctr_sha256.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

bool ctr_install_capture_verify(const char *directory,
                                const CtrInstallCaptureFile *files,size_t count,
                                const char **failed_file) {
    if(failed_file)*failed_file="inventory";
    if(!directory || !files || !count || count>64) { errno=EINVAL;return false; }
    struct stat st;
    if(lstat(directory,&st)!=0 || !S_ISDIR(st.st_mode)) { errno=ENOTDIR;return false; }
    for(size_t i=0;i<count;++i) {
        if(!files[i].name || !*files[i].name || strchr(files[i].name,'/') ||
           strchr(files[i].name,'\\') || !strcmp(files[i].name,".") || !strcmp(files[i].name,"..")) {
            errno=EINVAL;return false;
        }
        for(size_t j=0;j<i;++j)if(!strcmp(files[i].name,files[j].name)) { errno=EINVAL;return false; }
    }
    DIR *dir=opendir(directory);
    if(!dir)return false;
    size_t seen=0;bool valid=true;
    for(;;) {
        errno=0;struct dirent *entry=readdir(dir);
        if(!entry) { if(errno)valid=false;break; }
        if(!strcmp(entry->d_name,".") || !strcmp(entry->d_name,".."))continue;
        size_t i=0;
        while(i<count && strcmp(entry->d_name,files[i].name))++i;
        if(i==count) { errno=EINVAL;valid=false;break; }
        ++seen;
    }
    int error=errno;
    if(closedir(dir)!=0) { valid=false;error=errno; }
    if(!valid || seen!=count) { errno=error?error:EINVAL;return false; }
    uint8_t buffer[16384],digest[32];
    for(size_t i=0;i<count;++i) {
        if(failed_file)*failed_file=files[i].name;
        char path[512];int length=snprintf(path,sizeof(path),"%s/%s",directory,files[i].name);
        if(length<0 || (size_t)length>=sizeof(path)) { errno=ENAMETOOLONG;return false; }
        struct stat before,opened,after;
        if(lstat(path,&before)!=0)return false;
        if(!S_ISREG(before.st_mode) || before.st_size<0 || (uint64_t)before.st_size!=files[i].size) { errno=EINVAL;return false; }
        FILE *file=fopen(path,"rb");
        if(!file)return false;
        valid=fstat(fileno(file),&opened)==0 && S_ISREG(opened.st_mode) &&
            opened.st_dev==before.st_dev && opened.st_ino==before.st_ino && opened.st_size==before.st_size;
        ctr_sha256 hash;ctr_sha256_init(&hash);
        uint64_t bytes=0;
        while(valid) {
            size_t n=fread(buffer,1,sizeof(buffer),file);
            bytes+=n;
            if(bytes>files[i].size || !ctr_sha256_update(&hash,buffer,n)) { valid=false;break; }
            if(n<sizeof(buffer)) { if(ferror(file))valid=false;break; }
        }
        if(fclose(file)!=0)valid=false;
        if(lstat(path,&after)!=0 || !S_ISREG(after.st_mode) || after.st_dev!=before.st_dev ||
           after.st_ino!=before.st_ino || after.st_size!=before.st_size || after.st_mtime!=before.st_mtime)valid=false;
        if(!valid || bytes!=files[i].size || !ctr_sha256_final(&hash,digest) || memcmp(digest,files[i].sha256,32)) {
            errno=EIO;return false;
        }
    }
    if(failed_file)*failed_file=NULL;
    return true;
}

static uint64_t title_id(const unsigned char *data) {
    uint64_t id=0;
    for(unsigned i=0;i<8;++i)id=(id<<8)|data[i];
    return id;
}

CtrInstallCaptureInventory ctr_install_capture_inventory(
    const void *entries,size_t bytes,size_t stride,int32_t before,
    int32_t result,uint32_t returned,int32_t after,uint64_t title) {
    CtrInstallCaptureInventory out={CTR_CAPTURE_INVENTORY_INCONCLUSIVE,"invalid_buffer",UINT32_MAX};
    if(result) { out.reason="list_error";return out; }
    if(!entries || stride<8 || bytes%stride || bytes/stride>CTR_CAPTURE_TITLE_LIMIT ||
       returned>bytes/stride)return out;
    const unsigned char *data=entries;
    for(uint32_t i=0;i<returned;++i) {
        if(title_id(data+(size_t)i*stride)==title) {
            out.state=CTR_CAPTURE_INVENTORY_PRESENT;out.reason="target_match";
            out.match_index=i;return out;
        }
    }
    if(before<0 || after<0 || (uint32_t)before>CTR_CAPTURE_TITLE_LIMIT ||
       (uint32_t)after>CTR_CAPTURE_TITLE_LIMIT)out.reason="count_error";
    else if(before!=after)out.reason="count_changed";
    else if((uint32_t)before>bytes/stride || returned==bytes/stride)out.reason="capacity_limit";
    else {
        /* TitleCount sizes an allocation; TitleList's output is the number
         * actually enumerated. A successful underfilled buffer is complete,
         * even when its returned count differs from the global counter. */
        out.state=CTR_CAPTURE_INVENTORY_ABSENT;
        out.reason=returned==(uint32_t)before?"complete_absence":"complete_list_count_difference";
    }
    return out;
}

CtrInstallCaptureInventory ctr_install_capture_confirm_inventory(
    CtrInstallCaptureInventory inventory,const void *entry,size_t bytes,
    int32_t result,uint64_t title) {
    /* A nonzero GetTitleInfo result is not a reliable not-found code. It
     * cannot establish absence or override an inconclusive list. */
    if(inventory.state==CTR_CAPTURE_INVENTORY_PRESENT || result)return inventory;
    if(!entry || bytes<8 || title_id(entry)!=title) {
        return (CtrInstallCaptureInventory){CTR_CAPTURE_INVENTORY_INCONCLUSIVE,
            "lookup_invalid",UINT32_MAX};
    }
    return (CtrInstallCaptureInventory){CTR_CAPTURE_INVENTORY_PRESENT,
        "target_lookup_match",UINT32_MAX};
}

void ctr_install_capture_complete(CtrInstallCapture *capture,int32_t result) {
    capture->result=result;
    atomic_store_explicit(&capture->complete,true,memory_order_release);
}

bool ctr_install_capture_select(bool *armed,bool valid,bool held,bool pressed) {
    if(!armed || !valid)return false;
    if(!*armed) {
        if(!held && !pressed)*armed=true;
        return false;
    }
    if(!pressed)return false;
    *armed=false;return true;
}

static bool record(CtrInstallCapture *capture,const CtrInstallCaptureOps *ops,void *context,
                   const char *event,int32_t result,const CtrInstallCaptureProgress *progress) {
    if(!ops->record(context,event,result,progress))capture->log_failed=true;
    return !capture->log_failed;
}

bool ctr_install_capture_run(CtrInstallCapture *capture,const CtrInstallCaptureOps *ops,
                             void *context,unsigned target) {
    if(!capture || !ops || !ops->record || !ops->info || !ops->target || !ops->start ||
       !ops->progress || !ops->abort || !ops->pump || target>1 || capture->attempted)return false;
    capture->attempted=true;
    atomic_init(&capture->complete,false);
    if(!record(capture,ops,context,"intent",(int32_t)target,NULL))return false;
    int32_t result=ops->info(context);
    if(!record(capture,ops,context,"get_info",result,NULL) || result)return false;
    result=ops->target(context,target);
    if(!record(capture,ops,context,"set_target",result,NULL) || result)return false;
    /* Persist the last safe checkpoint before handing ownership to IOS. */
    if(!record(capture,ops,context,"before_submit",0,NULL))return false;
    result=ops->start(context,capture);
    capture->submitted=result==0;
    record(capture,ops,context,"submit_raw",result,NULL);
    if(result)return false;
    bool cancel=capture->log_failed;
    CtrInstallCaptureProgress previous={0};int32_t previous_result=0;
    bool have_progress=false;
    while(!atomic_load_explicit(&capture->complete,memory_order_acquire)) {
        if(cancel && !capture->abort_requested) {
            capture->abort_requested=true;
            result=ops->abort(context);
            record(capture,ops,context,"abort",result,NULL);
        }
        CtrInstallCaptureProgress progress={0};
        result=ops->progress(context,&progress);
        if(!have_progress || result!=previous_result || progress.title!=previous.title ||
           progress.bytes!=previous.bytes || progress.total!=previous.total ||
           progress.contents!=previous.contents || progress.total_contents!=previous.total_contents ||
           progress.active!=previous.active) {
            record(capture,ops,context,"progress",result,&progress);
            previous=progress;previous_result=result;have_progress=true;
        }
        if(!ops->pump(context) || capture->log_failed)cancel=true;
    }
    record(capture,ops,context,"callback_raw",capture->result,NULL);
    record(capture,ops,context,"end",capture->result,NULL);
    return capture->result==0 && !capture->log_failed && !capture->abort_requested;
}
