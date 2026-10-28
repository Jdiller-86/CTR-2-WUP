#define _POSIX_C_SOURCE 200809L
#include "retained_logs.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static bool copied(const char *source,const char *destination,ctr_retained_logs *r,unsigned depth) {
    if(depth>4) { errno=EOVERFLOW;return false; }
    DIR *dir=opendir(source);if(!dir)return false;
    bool ok=true;struct dirent *entry;
    while(ok) {
        errno=0;entry=readdir(dir);if(!entry) { if(errno)ok=false;break; }
        const char *name=entry->d_name;if(!strcmp(name,".") || !strcmp(name,".."))continue;
        if(++r->entries>512) { errno=EOVERFLOW;ok=false;break; }
        if(strpbrk(name,"/\\:") || strlen(name)>100) { errno=EINVAL;ok=false;break; }
        char src[768],dst[768];
        int a=snprintf(src,sizeof(src),"%s/%s",source,name),b=snprintf(dst,sizeof(dst),"%s/%s",destination,name);
        if(a<0 || b<0 || (size_t)a>=sizeof(src) || (size_t)b>=sizeof(dst)) { errno=ENAMETOOLONG;ok=false;break; }
        struct stat info;if(lstat(src,&info)!=0) { ok=false;break; }
        if(S_ISDIR(info.st_mode)) {
            if(mkdir(dst,0777)!=0 || !copied(src,dst,r,depth+1))ok=false;
            continue;
        }
        if(!S_ISREG(info.st_mode) || info.st_size<0 || info.st_size>8*1024*1024 ||
           r->files>=256 || r->bytes+(uint64_t)info.st_size>32*1024*1024) { errno=EOVERFLOW;ok=false;break; }
        FILE *in=fopen(src,"rb"),*out=NULL;if(in)out=fopen(dst,"wbx");
        if(!in || !out) { if(in)fclose(in);ok=false;break; }
        uint8_t bytes[4096];size_t n;uint64_t size=0;
        while((n=fread(bytes,1,sizeof(bytes),in))!=0) {
            if(size+n>(uint64_t)info.st_size || fwrite(bytes,1,n,out)!=n) { ok=false;break; }size+=n;
        }
        if(ferror(in) || size!=(uint64_t)info.st_size)ok=false;
        if(fclose(in)!=0)ok=false;
        if(fflush(out)!=0)ok=false;
        if(fclose(out)!=0)ok=false;
        if(!ok)break;
        in=fopen(src,"rb");out=fopen(dst,"rb");
        if(!in || !out) { if(in)fclose(in);if(out)fclose(out);ok=false;break; }
        uint8_t actual[4096];uint64_t compared=0;
        while((n=fread(bytes,1,sizeof(bytes),in))!=0) {
            if(compared+n>size || fread(actual,1,n,out)!=n || memcmp(bytes,actual,n)) { ok=false;break; }compared+=n;
        }
        if(ferror(in) || ferror(out) || compared!=size || fgetc(out)!=EOF)ok=false;
        if(fclose(in)!=0)ok=false;
        if(fclose(out)!=0)ok=false;
        if(ok) { ++r->files;r->bytes+=size; }
    }
    int error=errno;if(closedir(dir)!=0)ok=false;
    if(!ok)errno=error?error:EIO;
    return ok;
}
ctr_retained_logs ctr_retained_logs_copy(const char *source,const char *destination) {
    ctr_retained_logs r={0};struct stat info;
    if(!source || !destination) { r.error=EINVAL;return r; }
    if(lstat(source,&info)!=0) { if(errno!=ENOENT)r.error=errno;return r; }
    r.present=true;
    if(!S_ISDIR(info.st_mode) || mkdir(destination,0777)!=0) { r.error=errno?errno:EINVAL;return r; }
    r.complete=copied(source,destination,&r,0);if(!r.complete)r.error=errno?errno:EIO;return r;
}
