#include "ctr_romfs_file.h"
#include "ctr_sha256.h"
#include <errno.h>
#include <limits.h>
#include <string.h>

static bool read_blob(void *context,uint64_t offset,void *output,uint32_t size) {
    ctr_romfs_file *file=context;
    if(!file || !file->file || (!output && size) || offset>file->size || size>file->size-offset || offset>LONG_MAX)return false;
    if(!size)return true;
    if(fseek(file->file,(long)offset,SEEK_SET)!=0)return false;
    return fread(output,1,size,file->file)==size && !ferror(file->file);
}
bool ctr_romfs_file_open(ctr_romfs_file *file,const char *path,const uint8_t expected[32]) {
    if(!file || !path || !*path || !expected) { errno=EINVAL;return false; }
    if(file->file || file->view.mounts || file->view.references || file->view.read) { errno=EBUSY;return false; }
    FILE *input=fopen(path,"rb");if(!input)return false;
    bool ok=fseek(input,0,SEEK_END)==0;long size=ok?ftell(input):-1;
    if(size<40 || fseek(input,0,SEEK_SET)!=0) { fclose(input);errno=EINVAL;return false; }
    ctr_sha256 hash;ctr_sha256_init(&hash);uint8_t bytes[4096],digest[32];uint64_t left=(uint64_t)size;
    while(left && ok) {
        size_t count=left<sizeof(bytes)?(size_t)left:sizeof(bytes);
        ok=fread(bytes,1,count,input)==count && !ferror(input) && ctr_sha256_update(&hash,bytes,count);left-=count;
    }
    ok=ok && ctr_sha256_final(&hash,digest) && memcmp(digest,expected,32)==0;
    if(!ok) { fclose(input);errno=EIO;return false; }
    file->file=input;file->size=(uint64_t)size;
    if(!ctr_fs_romfs_init(&file->view,file->size,read_blob,file)) {
        fclose(input);memset(file,0,sizeof(*file));errno=EBUSY;return false;
    }
    return true;
}
bool ctr_romfs_file_close(ctr_romfs_file *file) {
    if(!file)return false;
    if(file->view.mounts || file->view.references) { errno=EBUSY;return false; }
    if(!file->file)return true;
    if(!ctr_fs_romfs_close(&file->view))return false;
    bool ok=fclose(file->file)==0;memset(file,0,sizeof(*file));return ok;
}
