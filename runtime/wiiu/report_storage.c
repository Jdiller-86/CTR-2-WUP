#include "report_storage.h"
#include "ctr_sha256.h"
#include <coreinit/filesystem_fsa.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

bool ctr_report_storage_sync(void *context) {
    ctr_report_storage *storage = context;
    if (!storage) { errno = EINVAL; return false; }
    if (storage->client <= 0) {
        storage->client = FSAAddClient(NULL);
        if (storage->client <= 0) {
            storage->last_status = storage->client;
            errno = EIO;
            return false;
        }
    }
    storage->last_status = FSAFlushVolume(storage->client, "/vol/external01");
    if (storage->last_status < 0) { errno = EIO; return false; }
    return true;
}

static bool verify_file(void *context,const char *path,uint64_t bytes,
                        const uint8_t expected[32]) {
    ctr_report_storage *storage=context;
    if(!storage || storage->client<=0 || !path || !expected || !bytes || bytes>UINT32_MAX) {
        errno=EINVAL;return false;
    }
    FSAStat stat;FSAFileHandle file;
    storage->last_status=FSAGetStat(storage->client,path,&stat);
    if(storage->last_status<0) { errno=EIO;return false; }
    if((stat.flags&(FS_STAT_DIRECTORY|FS_STAT_LINK)) || stat.size!=bytes) { errno=EIO;return false; }
    storage->last_status=FSAOpenFileEx(storage->client,path,"r",0,0,0,&file);
    if(storage->last_status<0) { errno=EIO;return false; }
    ctr_sha256 hash;ctr_sha256_init(&hash);
    uint8_t buffer[4096] __attribute__((aligned(64))),digest[32];
    uint64_t offset=0;bool ok=true;
    while(offset<bytes) {
        uint32_t count=bytes-offset<sizeof(buffer)?(uint32_t)(bytes-offset):sizeof(buffer);
        storage->last_status=FSAReadFile(storage->client,buffer,1,count,file,0);
        if(storage->last_status!=(int32_t)count || !ctr_sha256_update(&hash,buffer,count)) { ok=false;break; }
        offset+=count;
    }
    if(ok) {
        storage->last_status=FSAGetStatFile(storage->client,file,&stat);
        ok=storage->last_status>=0 && !(stat.flags&(FS_STAT_DIRECTORY|FS_STAT_LINK)) && stat.size==bytes;
    }
    int32_t close_status=FSACloseFile(storage->client,file);
    if(ok && close_status<0) { storage->last_status=close_status;ok=false; }
    if(!ok || !ctr_sha256_final(&hash,digest) || memcmp(digest,expected,32)) { errno=EIO;return false; }
    storage->last_status=0;return true;
}

bool ctr_report_storage_verify(void *context,const char *path,uint64_t bytes,
                                const uint8_t expected[32]) {
    static const char prefix[]="fs:/vol/external01/";
    if(!path || strncmp(path,prefix,sizeof(prefix)-1) || !path[sizeof(prefix)-1] || strstr(path,"/../")) {
        errno=EINVAL;return false;
    }
    return verify_file(context,path+3,bytes,expected);
}

bool ctr_report_storage_verify_title(void *context,unsigned target,uint64_t title,
                                    const char *relative,uint64_t bytes,const uint8_t expected[32]) {
    /* Read-only recognition of original application files. Never accept a
     * device/path from SD metadata or traverse title/save/system namespaces. */
    static const char *const allowed[]={"code/app.xml","code/cos.xml","code/ctr2wup.rpx",
        "meta/meta.xml","meta/iconTex.tga","meta/bootLogoTex.tga","meta/bootDrcTex.tga","meta/bootTvTex.tga",
        "meta/bootMovie.h264"};
    bool valid=false;
    if(relative)for(size_t i=0;i<sizeof(allowed)/sizeof(*allowed);++i)
        if(!strcmp(relative,allowed[i]))valid=true;
    if(!valid || target>1 || (title>>32)!=UINT64_C(0x00050000)) { errno=EINVAL;return false; }
    char path[192];
    int n=snprintf(path,sizeof(path),"/vol/storage_%s01/usr/title/00050000/%08x/%s",
                   target?"usb":"mlc",(unsigned)(uint32_t)title,relative);
    if(n<0 || (size_t)n>=sizeof(path)) { errno=ENAMETOOLONG;return false; }
    return verify_file(context,path,bytes,expected);
}

void ctr_report_storage_close(ctr_report_storage *storage) {
    if (!storage || storage->client <= 0) return;
    storage->last_status = FSADelClient(storage->client);
    storage->client = -1;
}
