#include "report_storage.h"
#include "ctr_sha256.h"
#include <coreinit/filesystem_fsa.h>
#include <errno.h>
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

bool ctr_report_storage_verify(void *context,const char *path,uint64_t bytes,
                                const uint8_t expected[32]) {
    ctr_report_storage *storage=context;
    static const char prefix[]="fs:/vol/external01/";
    if(!storage || storage->client<=0 || !path || !expected || !bytes || bytes>UINT32_MAX ||
       strncmp(path,prefix,sizeof(prefix)-1) || !path[sizeof(prefix)-1] || strstr(path,"/../")) {
        errno=EINVAL;return false;
    }
    FSAStat stat;FSAFileHandle file;
    storage->last_status=FSAGetStat(storage->client,path+3,&stat);
    if(storage->last_status<0) { errno=EIO;return false; }
    if((stat.flags&(FS_STAT_DIRECTORY|FS_STAT_LINK)) || stat.size!=bytes) { errno=EIO;return false; }
    storage->last_status=FSAOpenFileEx(storage->client,path+3,"r",0,0,0,&file);
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

void ctr_report_storage_close(ctr_report_storage *storage) {
    if (!storage || storage->client <= 0) return;
    storage->last_status = FSADelClient(storage->client);
    storage->client = -1;
}
