#define _POSIX_C_SOURCE 200809L
#include "log.h"
#include <errno.h>
#include <string.h>
#include <unistd.h>

static bool fail(capture_log *log,int error,const char *stage) {
    if(!log->failed) { log->failed=true;log->error=error?error:EIO;log->stage=stage; }
    return false;
}

static bool read_back(capture_log *log,const uint8_t expected[32]) {
    FILE *file=fopen(log->path,"rb");
    if(!file)return fail(log,errno,"read_open");
    ctr_sha256 hash;ctr_sha256_init(&hash);
    uint8_t data[4096],digest[32];uint64_t bytes=0;bool ok=true;
    for(;;) {
        size_t n=fread(data,1,sizeof(data),file);bytes+=n;
        if(bytes>log->bytes || !ctr_sha256_update(&hash,data,n)) { ok=false;break; }
        if(n<sizeof(data)) { if(ferror(file))ok=false;break; }
    }
    if(fclose(file)!=0)ok=false;
    if(!ok || bytes!=log->bytes || !ctr_sha256_final(&hash,digest) || memcmp(digest,expected,32))
        return fail(log,EIO,"read_verify");
    if(log->verify && !log->verify(log->context,log->path,log->bytes,expected))
        return fail(log,errno,"sd_read_verify");
    ++log->verified;return true;
}

bool capture_log_close(capture_log *log) {
    if(!log)return false;
    if(log->file) {
        FILE *file=log->file;log->file=NULL;
        if(!log->failed && fflush(file)!=0)fail(log,errno,"flush");
        if(!log->failed) {
            int status;do { status=fsync(fileno(file)); } while(status!=0 && errno==EINTR);
            if(status!=0)fail(log,errno,"file_sync");
        }
        if(fclose(file)!=0)fail(log,errno,"close");
    }
    if(log->failed)return false;
    if(!log->path[0] || !log->bytes)return fail(log,EINVAL,"empty");
    if(log->sync && !log->sync(log->context))return fail(log,errno,"volume_sync");
    ctr_sha256 hash=log->expected;uint8_t digest[32];
    if(!ctr_sha256_final(&hash,digest))return fail(log,EIO,"digest");
    return read_back(log,digest);
}

static bool reopen(capture_log *log) {
    log->file=fopen(log->path,"r+b");
    if(!log->file)return fail(log,errno,"reopen");
    if(fseek(log->file,0,SEEK_END)!=0) {
        fail(log,errno,"seek");capture_log_close(log);return false;
    }
    return true;
}

bool capture_log_line(capture_log *log,const char *text) {
    if(!log || !text)return false;
    if(log->failed)return false;
    if(!log->file)return fail(log,EBADF,"write");
    size_t n=strlen(text);
    if(fwrite(text,1,n,log->file)!=n || fwrite("\n",1,1,log->file)!=1)
        return fail(log,errno,"write");
    if(!ctr_sha256_update(&log->expected,text,n) || !ctr_sha256_update(&log->expected,"\n",1))
        return fail(log,EOVERFLOW,"digest");
    log->bytes+=n+1;
    return capture_log_close(log) && reopen(log);
}

bool capture_log_open(capture_log *log,const char *directory,uint64_t session,
                      capture_log_sync sync,capture_log_verify verify,void *context) {
    if(!log || !directory || !*directory || !sync || !verify)return false;
    if(log->file || log->path[0])return fail(log,EBUSY,"open");
    memset(log,0,sizeof(*log));log->sync=sync;log->verify=verify;log->context=context;
    ctr_sha256_init(&log->expected);
    for(unsigned attempt=0;attempt<32;++attempt) {
        int n=snprintf(log->filename,sizeof(log->filename),"capture-%016llx-%02u.txt",
                       (unsigned long long)session,attempt);
        if(n<0 || (size_t)n>=sizeof(log->filename))return fail(log,ENAMETOOLONG,"filename");
        n=snprintf(log->path,sizeof(log->path),"%s/%s",directory,log->filename);
        if(n<0 || (size_t)n>=sizeof(log->path))return fail(log,ENAMETOOLONG,"path");
        log->file=fopen(log->path,"wbx");
        if(log->file)break;
        if(errno!=EEXIST)return fail(log,errno,"open");
    }
    if(!log->file)return fail(log,EEXIST,"open");
    return capture_log_line(log,"CTR2WUP Install Capture / mcp-capture-v2");
}

bool capture_log_checkpoint(capture_log *log,const char *text) {
    if(!log || !text || log->failed)return false;
    if(log->file)return fail(log,EBUSY,"checkpoint");
    if(!capture_log_close(log) || !reopen(log))return false;
    bool written=capture_log_line(log,text);
    bool closed=capture_log_close(log);
    return written && closed;
}
