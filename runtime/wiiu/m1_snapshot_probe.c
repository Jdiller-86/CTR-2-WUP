#define _POSIX_C_SOURCE 200809L
#include "m1_snapshot_probe.h"
#include "ctr_fs_snapshot.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PATH_BYTES 512u
#define RECORD_BYTES 128u
typedef struct {
    char root[PATH_BYTES],lock[PATH_BYTES],producer[PATH_BYTES],complete[PATH_BYTES];
    char primary[PATH_BYTES],previous[PATH_BYTES],pending[PATH_BYTES],invalid[PATH_BYTES];
    const char *source;
    CtrM1SnapshotEvidence *out;
    uint8_t *original,*replacement;
    size_t original_bytes,replacement_bytes;
    ctr_fs_volume first,next;
} Probe;

static bool fail(Probe *p,CtrM1StorageResult result,const char *stage,int error) {
    if(p->out->storage.result==CTR_M1_STORAGE_PASS) {
        p->out->storage.result=result;p->out->storage.error_stage=stage;
        p->out->storage.error_code=error?error:EIO;
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
static void put64(uint8_t *d,uint64_t n) { for(unsigned i=0;i<8;++i)d[i]=(uint8_t)(n>>(8*i)); }
static uint64_t get64(const uint8_t *d) {
    uint64_t n=0;for(unsigned i=0;i<8;++i)n|=(uint64_t)d[i]<<(8*i);return n;
}
static void record(uint8_t d[RECORD_BYTES],const char *source,uint64_t producer,uint64_t recovery,unsigned kind) {
    for(unsigned i=0;i<RECORD_BYTES;++i)d[i]=(uint8_t)(i*37u+19u);
    memcpy(d,"CTR2WUP M1 snapshot-v2\n",22);memcpy(d+24,source,40);
    put64(d+64,producer);put64(d+72,recovery);d[80]=(uint8_t)kind;
    uint32_t crc=crc32(d,124);for(unsigned i=0;i<4;++i)d[124+i]=(uint8_t)(crc>>(8*i));
}
static bool make_path(char output[PATH_BYTES],const char *directory,const char *name) {
    int n=snprintf(output,PATH_BYTES,"%s/%s",directory,name);return n>=0 && (size_t)n<PATH_BYTES;
}
static bool absent(Probe *p,const char *path,const char *stage) {
    struct stat st;
    if(lstat(path,&st)==0)return fail(p,CTR_M1_STORAGE_CONTENTS,stage,EEXIST);
    return errno==ENOENT?true:fail(p,CTR_M1_STORAGE_IO,stage,errno);
}
static bool read_file(Probe *p,const char *path,uint8_t *data,size_t bytes,const char *stage) {
    struct stat before,opened,after;
    if(lstat(path,&before)!=0)return fail(p,CTR_M1_STORAGE_IO,stage,errno);
    if(!S_ISREG(before.st_mode) || S_ISLNK(before.st_mode) || before.st_size!=(off_t)bytes || before.st_nlink>1)
        return fail(p,CTR_M1_STORAGE_CONTENTS,stage,EINVAL);
    FILE *file=fopen(path,"rb");if(!file)return fail(p,CTR_M1_STORAGE_IO,stage,errno);
    bool ok=fstat(fileno(file),&opened)==0 && S_ISREG(opened.st_mode) &&
        opened.st_dev==before.st_dev && opened.st_ino==before.st_ino && opened.st_size==before.st_size;
    size_t n=ok?fread(data,1,bytes,file):0;int end=ok?fgetc(file):EOF;int error=errno;
    if(ferror(file)) { ok=false;error=errno; }
    if(fclose(file)!=0) { ok=false;error=errno; }
    if(!ok)return fail(p,CTR_M1_STORAGE_IO,stage,error);
    if(lstat(path,&after)!=0 || !S_ISREG(after.st_mode) || S_ISLNK(after.st_mode) ||
       after.st_dev!=before.st_dev || after.st_ino!=before.st_ino || after.st_size!=before.st_size ||
       n!=bytes || end!=EOF)return fail(p,CTR_M1_STORAGE_CONTENTS,stage,ESTALE);
    ++p->out->storage.readbacks;return true;
}
static bool exact(Probe *p,const char *path,const uint8_t *expected,size_t bytes,const char *stage) {
    uint8_t *actual=malloc(bytes?bytes:1);
    if(!actual)return fail(p,CTR_M1_STORAGE_IO,stage,ENOMEM);
    bool ok=read_file(p,path,actual,bytes,stage);
    if(ok && memcmp(actual,expected,bytes))ok=fail(p,CTR_M1_STORAGE_CONTENTS,stage,EIO);
    free(actual);return ok;
}
static bool create(Probe *p,const char *path,const uint8_t *data,size_t bytes,const char *stage) {
    FILE *file=fopen(path,"wbx");if(!file)return fail(p,CTR_M1_STORAGE_IO,stage,errno);
    bool ok=fwrite(data,1,bytes,file)==bytes;int error=errno;
    if(fflush(file)!=0) { ok=false;error=errno; }
    if(fclose(file)!=0) { ok=false;error=errno; }
    return ok?exact(p,path,data,bytes,stage):fail(p,CTR_M1_STORAGE_IO,stage,error);
}
static bool remove_exact(Probe *p,const char *path,const uint8_t *data,size_t bytes,const char *stage) {
    return exact(p,path,data,bytes,stage) && (remove(path)==0 || fail(p,CTR_M1_STORAGE_IO,stage,errno));
}
static bool inventory(Probe *p) {
    const char *allowed[]={".active","producer.bin","complete.bin","original.snapshot",
        "original.snapshot.previous","original.snapshot.pending","not-a-directory"};
    DIR *d=opendir(p->root);if(!d)return fail(p,CTR_M1_STORAGE_IO,"inventory_open",errno);
    bool ok=true;struct dirent *e;errno=0;
    while((e=readdir(d))!=NULL) {
        if(!strcmp(e->d_name,".") || !strcmp(e->d_name,".."))continue;
        unsigned i;for(i=0;i<sizeof(allowed)/sizeof(*allowed) && strcmp(e->d_name,allowed[i]);++i){}
        if(i==sizeof(allowed)/sizeof(*allowed)) { ok=fail(p,CTR_M1_STORAGE_CONTENTS,"unexpected_file",EEXIST);break; }
    }
    int error=errno;
    if(ok && error)ok=fail(p,CTR_M1_STORAGE_IO,"inventory_read",error);
    if(closedir(d)!=0 && ok)ok=fail(p,CTR_M1_STORAGE_IO,"inventory_close",errno);
    return ok;
}
static bool volumes(Probe *p) {
    uint8_t data[RECORD_BYTES];
    if(!ctr_fs_volume_init(&p->first,4,true) || !ctr_fs_volume_init(&p->next,4,true))
        return fail(p,CTR_M1_STORAGE_IO,"volume_init",ENOMEM);
    record(data,p->source,p->out->producer_session,0,3);
    if(ctr_fs_seed(&p->first,"/m1-original.bin",data,sizeof(data))!=0)
        return fail(p,CTR_M1_STORAGE_IO,"original_seed",ENOMEM);
    record(data,p->source,p->out->producer_session,0,4);
    if(ctr_fs_seed(&p->next,"/m1-original.bin",data,sizeof(data))!=0 ||
       !ctr_fs_export(&p->first,&p->original,&p->original_bytes) ||
       !ctr_fs_export(&p->next,&p->replacement,&p->replacement_bytes))
        return fail(p,CTR_M1_STORAGE_IO,"snapshot_export",ENOMEM);
    return true;
}
static bool load_exact(Probe *p,ctr_fs_snapshot_result expected,const uint8_t *bytes,size_t count,const char *stage) {
    ctr_fs_volume loaded={0};uint8_t *actual=NULL;size_t size=0;
    bool ok=ctr_fs_volume_init(&loaded,4,true) && ctr_fs_snapshot_load(&loaded,p->primary)==expected &&
        ctr_fs_export(&loaded,&actual,&size) && size==count && !memcmp(actual,bytes,count);
    free(actual);ctr_fs_volume_close(&loaded);
    if(!ok)return fail(p,CTR_M1_STORAGE_RECOVERY,stage,EIO);
    ++p->out->storage.readbacks;return true;
}
static bool invalid_path(Probe *p) {
    uint8_t data[RECORD_BYTES];record(data,p->source,p->out->producer_session,0,5);
    if(!create(p,p->invalid,data,sizeof(data),"invalid_create"))return false;
    char child[PATH_BYTES];
    if(!make_path(child,p->invalid,"must-fail.snapshot"))return fail(p,CTR_M1_STORAGE_PATH,"invalid_path",ENAMETOOLONG);
    errno=0;bool saved=ctr_fs_snapshot_save_with_rename(child,&p->next,NULL,NULL);int error=errno;
    if(saved || error!=ENOTDIR)return fail(p,CTR_M1_STORAGE_RECOVERY,"invalid_path_errno",error);
    if(!remove_exact(p,p->invalid,data,sizeof(data),"invalid_cleanup"))return false;
    p->out->storage.invalid_path_rejected=true;return true;
}
typedef struct { unsigned calls; } Fault;
static int interrupted_rename(void *context,const char *from,const char *to) {
    Fault *fault=context;++fault->calls;
    if(fault->calls>=2) { errno=EACCES;return -1; }
    struct stat st;
    if(lstat(to,&st)==0) { errno=EEXIST;return -1; }
    if(errno!=ENOENT)return -1;
    return rename(from,to);
}
static bool prepare(Probe *p) {
    if(!absent(p,p->primary,"initial_primary") || !absent(p,p->previous,"initial_previous") ||
       !absent(p,p->pending,"initial_pending") || !absent(p,p->complete,"initial_complete") ||
       !absent(p,p->invalid,"initial_invalid") || !volumes(p) || !invalid_path(p))return false;
    uint8_t producer[RECORD_BYTES];record(producer,p->source,p->out->producer_session,0,1);
    if(!create(p,p->producer,producer,sizeof(producer),"producer_create"))return false;
    if(!ctr_fs_snapshot_save_with_rename(p->primary,&p->first,NULL,NULL))
        return fail(p,CTR_M1_STORAGE_IO,"original_publish",errno);
    ++p->out->storage.writes;
    if(!exact(p,p->primary,p->original,p->original_bytes,"original_readback") ||
       !load_exact(p,CTR_FS_SNAPSHOT_OK,p->original,p->original_bytes,"original_load"))return false;
    p->out->storage.sentinel_written=true;p->out->storage.sentinel_readback=true;
    Fault fault={0};errno=0;
    bool saved=ctr_fs_snapshot_save_with_rename(p->primary,&p->next,interrupted_rename,&fault);int error=errno;
    if(saved || error!=EACCES || fault.calls!=3)return fail(p,CTR_M1_STORAGE_RECOVERY,"interruption_fault",error);
    if(!absent(p,p->primary,"prepared_primary") ||
       !exact(p,p->previous,p->original,p->original_bytes,"prepared_previous") ||
       !exact(p,p->pending,p->replacement,p->replacement_bytes,"prepared_pending"))return false;
    ++p->out->storage.writes;p->out->interruption_prepared=true;p->out->phase="PREPARED";return true;
}
static bool recover(Probe *p) {
    if(!absent(p,p->primary,"recovery_primary") || !absent(p,p->complete,"recovery_complete") ||
       !absent(p,p->invalid,"recovery_invalid") ||
       !exact(p,p->previous,p->original,p->original_bytes,"recovery_previous") ||
       !exact(p,p->pending,p->replacement,p->replacement_bytes,"recovery_pending") ||
       !load_exact(p,CTR_FS_SNAPSHOT_RECOVERED,p->original,p->original_bytes,"production_recovery"))return false;
    p->out->storage.sentinel_written=true;p->out->storage.sentinel_readback=true;
    p->out->storage.fresh_reload=true;p->out->storage.previous_recovered=true;
    ++p->out->storage.recoveries;p->out->interruption_prepared=true;
    errno=0;bool saved=ctr_fs_snapshot_save_with_rename(p->primary,&p->next,NULL,NULL);int error=errno;
    if(saved || error!=EEXIST)return fail(p,CTR_M1_STORAGE_RECOVERY,"pending_rejection",error);
    if(!absent(p,p->previous,"recovered_previous") ||
       !exact(p,p->primary,p->original,p->original_bytes,"recovered_primary") ||
       !exact(p,p->pending,p->replacement,p->replacement_bytes,"rejected_pending") ||
       !load_exact(p,CTR_FS_SNAPSHOT_OK,p->original,p->original_bytes,"preserved_original"))return false;
    p->out->storage.pending_rejected=true;
    if(!remove_exact(p,p->pending,p->replacement,p->replacement_bytes,"pending_cleanup") || !invalid_path(p))return false;
    if(!ctr_fs_snapshot_save_with_rename(p->primary,&p->next,NULL,NULL))
        return fail(p,CTR_M1_STORAGE_IO,"replacement_publish",errno);
    ++p->out->storage.writes;
    if(!exact(p,p->primary,p->replacement,p->replacement_bytes,"replacement_readback") ||
       !load_exact(p,CTR_FS_SNAPSHOT_OK,p->replacement,p->replacement_bytes,"replacement_load") ||
       !absent(p,p->previous,"final_previous") || !absent(p,p->pending,"final_pending"))return false;
    p->out->storage.final_readback=true;p->out->recovery_session=p->out->current_session;
    uint8_t complete[RECORD_BYTES];record(complete,p->source,p->out->producer_session,p->out->recovery_session,2);
    if(!create(p,p->complete,complete,sizeof(complete),"complete_create"))return false;
    p->out->phase="RECOVERED";return true;
}
static bool completed(Probe *p) {
    uint8_t actual[RECORD_BYTES],expected[RECORD_BYTES];
    if(!read_file(p,p->complete,actual,sizeof(actual),"complete_read"))return false;
    p->out->recovery_session=get64(actual+72);
    record(expected,p->source,p->out->producer_session,p->out->recovery_session,2);
    if(!p->out->recovery_session || p->out->recovery_session==p->out->producer_session ||
       p->out->current_session==p->out->recovery_session || memcmp(actual,expected,sizeof(actual)))
        return fail(p,CTR_M1_STORAGE_CONTENTS,"complete_identity",EINVAL);
    if(!absent(p,p->previous,"complete_previous") || !absent(p,p->pending,"complete_pending") ||
       !absent(p,p->invalid,"complete_invalid") ||
       !exact(p,p->primary,p->replacement,p->replacement_bytes,"complete_primary") ||
       !load_exact(p,CTR_FS_SNAPSHOT_OK,p->replacement,p->replacement_bytes,"complete_load"))return false;
    p->out->storage.sentinel_written=true;p->out->storage.sentinel_readback=true;
    p->out->storage.fresh_reload=true;p->out->storage.previous_recovered=true;
    p->out->storage.pending_rejected=true;p->out->storage.invalid_path_rejected=true;
    p->out->storage.final_readback=true;p->out->interruption_prepared=true;p->out->phase="COMPLETE";return true;
}
bool ctr_m1_snapshot_probe(const char *directory,const char *source,uint64_t session,CtrM1SnapshotEvidence *out) {
    if(!out)return false;
    memset(out,0,sizeof(*out));out->phase="NONE";out->storage.error_stage="none";out->current_session=session;
    Probe p={0};p.out=out;p.source=source;
    if(!source || strlen(source)!=40 || strspn(source,"0123456789abcdef")!=40 || !session)
        return fail(&p,CTR_M1_STORAGE_ARGUMENT,"identity",EINVAL);
    if(!ctr_m1_directory_safe(directory,lstat))return fail(&p,CTR_M1_STORAGE_PATH,"directory",errno?errno:EINVAL);
    char name[80];snprintf(name,sizeof(name),"m1-snapshot-v2-%s",source);
    if(!make_path(p.root,directory,name) || !make_path(p.lock,p.root,".active") ||
       !make_path(p.producer,p.root,"producer.bin") || !make_path(p.complete,p.root,"complete.bin") ||
       !make_path(p.primary,p.root,"original.snapshot") || !make_path(p.previous,p.root,"original.snapshot.previous") ||
       !make_path(p.pending,p.root,"original.snapshot.pending") || !make_path(p.invalid,p.root,"not-a-directory"))
        return fail(&p,CTR_M1_STORAGE_PATH,"paths",ENAMETOOLONG);
    if(mkdir(p.root,0700)!=0 && errno!=EEXIST)return fail(&p,CTR_M1_STORAGE_IO,"namespace_create",errno);
    struct stat st;
    if(lstat(p.root,&st)!=0 || !S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode))
        return fail(&p,CTR_M1_STORAGE_PATH,"namespace_type",EINVAL);
    if(mkdir(p.lock,0700)!=0)return fail(&p,CTR_M1_STORAGE_BUSY,"namespace_lock",errno);
    bool ok=inventory(&p);bool existing=false;
    if(ok && lstat(p.producer,&st)==0)existing=true;
    else if(ok && errno!=ENOENT)ok=fail(&p,CTR_M1_STORAGE_IO,"producer_stat",errno);
    if(ok && existing) {
        uint8_t actual[RECORD_BYTES],expected[RECORD_BYTES];
        ok=read_file(&p,p.producer,actual,sizeof(actual),"producer_read");
        if(ok) {
            out->producer_session=get64(actual+64);record(expected,source,out->producer_session,0,1);
            if(!out->producer_session || out->producer_session==session || memcmp(actual,expected,sizeof(actual)))
                ok=fail(&p,CTR_M1_STORAGE_CONTENTS,"producer_identity",EINVAL);
        }
        if(ok)ok=volumes(&p);
        if(ok) {
            bool done=lstat(p.complete,&st)==0;
            if(!done && errno!=ENOENT)ok=fail(&p,CTR_M1_STORAGE_IO,"complete_stat",errno);
            else { out->production_adapter=true;ok=done?completed(&p):recover(&p); }
        }
    } else if(ok) {
        out->producer_session=session;out->production_adapter=true;ok=prepare(&p);
    }
    free(p.original);free(p.replacement);ctr_fs_volume_close(&p.first);ctr_fs_volume_close(&p.next);
    if(rmdir(p.lock)!=0)ok=fail(&p,CTR_M1_STORAGE_IO,"namespace_unlock",errno);
    if(!ok)out->phase="NONE";
    return ok;
}
