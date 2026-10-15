#define _POSIX_C_SOURCE 200809L
#include "m1_storage_probe.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PATH_BYTES 512u
#define SENTINEL_BYTES 128u
typedef struct {
    char root[PATH_BYTES],lock[PATH_BYTES],sentinel[PATH_BYTES];
    char good[PATH_BYTES],backup[PATH_BYTES],pending[PATH_BYTES],invalid[PATH_BYTES];
    uint8_t expected[SENTINEL_BYTES],damaged[SENTINEL_BYTES];
    CtrM1StorageEvidence *out;
} Probe;
static bool fail(Probe *p,CtrM1StorageResult result,const char *stage,int error) {
    if(p->out->result==CTR_M1_STORAGE_PASS) {
        p->out->result=result;p->out->error_stage=stage;p->out->error_code=error?error:EIO;
    }
    return false;
}
static uint32_t crc32(const uint8_t *d,size_t bytes) {
    uint32_t crc=UINT32_MAX;
    for(size_t i=0;i<bytes;++i) {
        crc^=d[i];for(unsigned j=0;j<8;++j)crc=(crc>>1)^((0u-(crc&1u))&UINT32_C(0xedb88320));
    }
    return ~crc;
}
static void original(uint8_t d[SENTINEL_BYTES],const char *source) {
    for(unsigned i=0;i<SENTINEL_BYTES;++i)d[i]=(uint8_t)(i*37u+19u);
    memcpy(d,"CTR2WUP M1 scratch-v1\n",21);memcpy(d+24,source,40);
    memcpy(d+64,"Original diagnostic data; not a save.\n",37);
    uint32_t crc=crc32(d,124);for(unsigned i=0;i<4;++i)d[124+i]=(uint8_t)(crc>>(i*8));
}
static bool make_path(char output[PATH_BYTES],const char *directory,const char *name) {
    int n=snprintf(output,PATH_BYTES,"%s/%s",directory,name);return n>=0 && (size_t)n<PATH_BYTES;
}
static bool directory_safe(const char *path) {
    if(!path || !*path || strlen(path)>384)return false;
    size_t start=path[0]=='/'?1:!strncmp(path,"fs:/",4)?4:0;
    if(!start || !path[start])return false;
    char part[PATH_BYTES];strcpy(part,path);
    for(size_t at=start;;++at)if(path[at]=='/' || !path[at]) {
        if(at==start || (at-start==1 && path[start]=='.') ||
           (at-start==2 && path[start]=='.' && path[start+1]=='.'))return false;
        for(size_t i=start;i<at;++i)
            if(path[i]=='\\' || path[i]==':' || (unsigned char)path[i]<32)return false;
        part[at]='\0';struct stat st;
        if(lstat(part,&st)!=0 || !S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode))return false;
        if(!path[at])return true;
        part[at]='/';start=at+1;
    }
}
static bool absent(Probe *p,const char *path,const char *stage) {
    struct stat st;
    if(lstat(path,&st)==0)return fail(p,CTR_M1_STORAGE_CONTENTS,stage,EEXIST);
    if(errno!=ENOENT)return fail(p,CTR_M1_STORAGE_IO,stage,errno);
    return true;
}
/* An exact-length regular file is checked both by pathname and by its open
 * descriptor. This rejects symlinks before reading and replacement during open. */
static bool read_exact(Probe *p,const char *path,const uint8_t *expected,const char *stage) {
    struct stat before,opened,after;
    if(lstat(path,&before)!=0)return fail(p,CTR_M1_STORAGE_IO,stage,errno);
    if(!S_ISREG(before.st_mode) || S_ISLNK(before.st_mode) || before.st_size!=SENTINEL_BYTES)
        return fail(p,CTR_M1_STORAGE_CONTENTS,stage,EINVAL);
    FILE *f=fopen(path,"rb");if(!f)return fail(p,CTR_M1_STORAGE_IO,stage,errno);
    uint8_t actual[SENTINEL_BYTES+1];bool ok=fstat(fileno(f),&opened)==0 && S_ISREG(opened.st_mode) &&
        opened.st_dev==before.st_dev && opened.st_ino==before.st_ino;
    size_t n=ok?fread(actual,1,sizeof(actual),f):0;
    int error=errno;
    if(ferror(f)) { ok=false;error=errno; }
    if(fclose(f)!=0) { ok=false;error=errno; }
    if(!ok)return fail(p,CTR_M1_STORAGE_IO,stage,error);
    if(lstat(path,&after)!=0 || !S_ISREG(after.st_mode) || S_ISLNK(after.st_mode) ||
       after.st_dev!=before.st_dev || after.st_ino!=before.st_ino ||
       after.st_size!=SENTINEL_BYTES)return fail(p,CTR_M1_STORAGE_CONTENTS,stage,ESTALE);
    if(n!=SENTINEL_BYTES || memcmp(actual,expected,SENTINEL_BYTES))
        return fail(p,CTR_M1_STORAGE_CONTENTS,stage,EIO);
    ++p->out->readbacks;return true;
}
static bool create(Probe *p,const char *path,const uint8_t *data,const char *stage) {
    FILE *f=fopen(path,"wbx");if(!f)return fail(p,CTR_M1_STORAGE_IO,stage,errno);
    bool ok=fwrite(data,1,SENTINEL_BYTES,f)==SENTINEL_BYTES;int error=errno;
    if(fflush(f)!=0) { ok=false;error=errno; }
    if(fclose(f)!=0) { ok=false;error=errno; }
    if(!ok)return fail(p,CTR_M1_STORAGE_IO,stage,error);
    ++p->out->writes;return read_exact(p,path,data,stage);
}
static bool remove_exact(Probe *p,const char *path,const uint8_t *expected,const char *stage) {
    if(!read_exact(p,path,expected,stage))return false;
    if(remove(path)!=0)return fail(p,CTR_M1_STORAGE_IO,stage,errno);
    return true;
}
static bool rename_exact(Probe *p,const char *from,const char *to,const char *stage) {
    if(!read_exact(p,from,p->expected,stage) || !absent(p,to,stage))return false;
    if(rename(from,to)!=0)return fail(p,CTR_M1_STORAGE_RECOVERY,stage,errno);
    return read_exact(p,to,p->expected,stage);
}
static bool recovery(Probe *p) {
    /* A previous interrupted probe is preserved for review rather than silently
     * treating unknown state as ours. Normal probes end with sentinel only. */
    if(!absent(p,p->good,"initial_good") || !absent(p,p->backup,"initial_backup") ||
       !absent(p,p->pending,"initial_pending") || !absent(p,p->invalid,"initial_invalid"))return false;
    if(!create(p,p->good,p->expected,"good_create") ||
       !create(p,p->pending,p->damaged,"pending_create"))return false;
    if(!rename_exact(p,p->good,p->backup,"backup_publish"))return false;
    /* Deliberately damaged pending bytes must not match the valid CRC/source
     * record; read them exactly before rejecting, without following a link. */
    if(!read_exact(p,p->pending,p->damaged,"pending_reject"))return false;
    if(crc32(p->damaged,124)==((uint32_t)p->damaged[124]|(uint32_t)p->damaged[125]<<8|
       (uint32_t)p->damaged[126]<<16|(uint32_t)p->damaged[127]<<24))
        return fail(p,CTR_M1_STORAGE_CONTENTS,"pending_crc",EIO);
    p->out->pending_rejected=true;
    if(!rename_exact(p,p->backup,p->good,"backup_recover"))return false;
    p->out->previous_recovered=true;++p->out->recoveries;
    if(!remove_exact(p,p->pending,p->damaged,"pending_cleanup"))return false;
    /* A child under an owned regular file guarantees ENOTDIR on supported
     * filesystems, avoiding permission tests which behave differently as root. */
    if(!create(p,p->invalid,p->expected,"invalid_create"))return false;
    char child[PATH_BYTES];if(!make_path(child,p->invalid,"must-fail.bin"))
        return fail(p,CTR_M1_STORAGE_PATH,"invalid_path",ENAMETOOLONG);
    errno=0;FILE *f=fopen(child,"wbx");int error=errno;
    if(f) { fclose(f);return fail(p,CTR_M1_STORAGE_CONTENTS,"invalid_path_accepted",EIO); }
    if(error!=ENOTDIR)return fail(p,CTR_M1_STORAGE_IO,"invalid_path_errno",error);
    p->out->invalid_path_rejected=true;
    if(!remove_exact(p,p->invalid,p->expected,"invalid_cleanup"))return false;
    /* An ordinary valid write/readback must still work after the invalid path. */
    if(!create(p,p->pending,p->expected,"final_create") ||
       !read_exact(p,p->pending,p->expected,"final_readback"))return false;
    p->out->final_readback=true;
    return remove_exact(p,p->pending,p->expected,"final_cleanup") &&
        remove_exact(p,p->good,p->expected,"good_cleanup");
}
bool ctr_m1_storage_probe(const char *directory,const char *source,CtrM1StorageEvidence *out) {
    if(!out)return false;
    memset(out,0,sizeof(*out));out->error_stage="none";
    Probe p={0};p.out=out;
    if(!source || strlen(source)!=40 || strspn(source,"0123456789abcdef")!=40)
        return fail(&p,CTR_M1_STORAGE_ARGUMENT,"source",EINVAL);
    if(!directory_safe(directory))return fail(&p,CTR_M1_STORAGE_PATH,"directory",EINVAL);
    char namespace_name[80];snprintf(namespace_name,sizeof(namespace_name),"m1-scratch-v1-%s",source);
    if(!make_path(p.root,directory,namespace_name) || !make_path(p.lock,p.root,".active") ||
       !make_path(p.sentinel,p.root,"sentinel.bin") || !make_path(p.good,p.root,"recovery.good") ||
       !make_path(p.backup,p.root,"recovery.backup") || !make_path(p.pending,p.root,"recovery.pending") ||
       !make_path(p.invalid,p.root,"not-a-directory"))return fail(&p,CTR_M1_STORAGE_PATH,"paths",ENAMETOOLONG);
    if(mkdir(p.root,0700)!=0 && errno!=EEXIST)return fail(&p,CTR_M1_STORAGE_IO,"namespace_create",errno);
    struct stat st;
    if(lstat(p.root,&st)!=0 || !S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode))
        return fail(&p,CTR_M1_STORAGE_PATH,"namespace_type",EINVAL);
    if(mkdir(p.lock,0700)!=0)return fail(&p,CTR_M1_STORAGE_BUSY,"namespace_lock",errno);
    original(p.expected,source);memcpy(p.damaged,p.expected,sizeof(p.damaged));p.damaged[73]^=0x80;
    bool ok=true;
    if(lstat(p.sentinel,&st)==0) {
        ok=read_exact(&p,p.sentinel,p.expected,"sentinel_reload");out->fresh_reload=ok;
    } else if(errno==ENOENT)ok=create(&p,p.sentinel,p.expected,"sentinel_create");
    else ok=fail(&p,CTR_M1_STORAGE_IO,"sentinel_stat",errno);
    if(ok) { out->sentinel_written=true;ok=read_exact(&p,p.sentinel,p.expected,"sentinel_readback");out->sentinel_readback=ok; }
    if(ok)ok=recovery(&p);
    if(rmdir(p.lock)!=0)ok=fail(&p,CTR_M1_STORAGE_IO,"namespace_unlock",errno);
    return ok;
}
const char *ctr_m1_storage_result_name(CtrM1StorageResult result) {
    switch(result) {
    case CTR_M1_STORAGE_PASS:return "PASS";
    case CTR_M1_STORAGE_ARGUMENT:return "ARGUMENT";
    case CTR_M1_STORAGE_PATH:return "PATH";
    case CTR_M1_STORAGE_IO:return "IO";
    case CTR_M1_STORAGE_CONTENTS:return "CONTENTS";
    case CTR_M1_STORAGE_BUSY:return "BUSY";
    case CTR_M1_STORAGE_RECOVERY:return "RECOVERY";
    default:return "INVALID";
    }
}
