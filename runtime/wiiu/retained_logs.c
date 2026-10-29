#define _POSIX_C_SOURCE 200809L
#include "retained_logs.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool copy_file(const char *source,const char *destination,ctr_retained_logs *r) {
    /* Validate the opened object, not an earlier pathname stat. Retain both
     * descriptors through exact readback, even if a directory entry changes. */
    errno=0;int fd=open(source,O_RDONLY|O_NOFOLLOW);if(fd<0)return false;
    struct stat info;
    if(fstat(fd,&info)!=0) { int error=errno;close(fd);errno=error;return false; }
    if(!S_ISREG(info.st_mode) || info.st_size<0 || info.st_size>8*1024*1024 ||
       r->files>=256 || r->bytes+(uint64_t)info.st_size>32*1024*1024) { close(fd);errno=EOVERFLOW;return false; }
    FILE *in=fdopen(fd,"rb");if(!in) { int error=errno;close(fd);errno=error;return false; }
    fd=open(destination,O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
    if(fd<0) { int error=errno;fclose(in);errno=error;return false; }
    FILE *out=fdopen(fd,"w+b");if(!out) { int error=errno;close(fd);fclose(in);errno=error;return false; }
    uint8_t bytes[4096],actual[4096];size_t n;uint64_t size=0;bool ok=true;
    while((n=fread(bytes,1,sizeof(bytes),in))!=0) {
        if(size+n>(uint64_t)info.st_size || fwrite(bytes,1,n,out)!=n) { ok=false;break; }size+=n;
    }
    if(ferror(in) || size!=(uint64_t)info.st_size || fflush(out)!=0)ok=false;
    if(ok && (fseek(in,0,SEEK_SET)!=0 || fseek(out,0,SEEK_SET)!=0))ok=false;
    uint64_t compared=0;
    while(ok && (n=fread(bytes,1,sizeof(bytes),in))!=0) {
        if(compared+n>size || fread(actual,1,n,out)!=n || memcmp(bytes,actual,n))ok=false;
        compared+=n;
    }
    if(ferror(in) || ferror(out) || compared!=size || fgetc(out)!=EOF)ok=false;
    int error=ok?0:(errno?errno:EIO);
    if(fclose(in)!=0 && !error)error=errno?errno:EIO;
    if(fclose(out)!=0 && !error)error=errno?errno:EIO;
    ok=error==0;
    if(ok) { ++r->files;r->bytes+=size; }else errno=error;
    return ok;
}
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
        DIR *sub=opendir(src);
        if(sub) {
            struct stat info;bool directory=lstat(src,&info)==0 && S_ISDIR(info.st_mode);
            if(closedir(sub)!=0)directory=false;
            if(!directory || mkdir(dst,0700)!=0 || !copied(src,dst,r,depth+1)) { errno=errno?errno:EINVAL;ok=false; }
        } else if(!copy_file(src,dst,r))ok=false;
    }
    int error=ok?0:(errno?errno:EIO);if(closedir(dir)!=0) { if(!error)error=errno?errno:EIO;ok=false; }
    if(!ok)errno=error?error:EIO;
    return ok;
}
ctr_retained_logs ctr_retained_logs_copy(const char *source,const char *destination) {
    ctr_retained_logs r={0};struct stat info;
    if(!source || !destination) { r.error=EINVAL;return r; }
    if(lstat(source,&info)!=0) { if(errno!=ENOENT)r.error=errno;return r; }
    r.present=true;
    if(!S_ISDIR(info.st_mode)) { r.error=ENOTDIR;return r; }
    if(mkdir(destination,0700)!=0) { r.error=errno?errno:EIO;return r; }
    r.complete=copied(source,destination,&r,0);if(!r.complete)r.error=errno?errno:EIO;return r;
}
