#include "ctr_fs_snapshot.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ctr_fs_snapshot_result load_one(ctr_fs_volume *v,const char *path) {
    if(!v || !path || !*path)return CTR_FS_SNAPSHOT_ERROR;
    FILE *file=fopen(path,"rb");if(!file)return errno==ENOENT?CTR_FS_SNAPSHOT_MISSING:CTR_FS_SNAPSHOT_ERROR;
    size_t limit=CTR_FS_MAX_VOLUME_BYTES+24+CTR_FS_MAX_FILES*(CTR_FS_PATH_BYTES+8);
    uint8_t *p=malloc(limit+1);if(!p) { fclose(file);return CTR_FS_SNAPSHOT_ERROR; }
    size_t n=fread(p,1,limit+1,file);bool ok=!ferror(file) && n<=limit;
    if(fclose(file))ok=false;
    if(ok)ok=ctr_fs_import(v,p,n);
    free(p);return ok?CTR_FS_SNAPSHOT_OK:CTR_FS_SNAPSHOT_ERROR;
}
ctr_fs_snapshot_result ctr_fs_snapshot_load(ctr_fs_volume *v,const char *path) {
    if(!v || !path || !*path || strlen(path)>4000)return CTR_FS_SNAPSHOT_ERROR;
    ctr_fs_snapshot_result result=load_one(v,path);
    if(result!=CTR_FS_SNAPSHOT_MISSING)return result;
    size_t length=strlen(path);char *previous=malloc(length+10);
    if(!previous)return CTR_FS_SNAPSHOT_ERROR;
    memcpy(previous,path,length);memcpy(previous+length,".previous",10);
    result=load_one(v,previous);free(previous);
    return result==CTR_FS_SNAPSHOT_OK?CTR_FS_SNAPSHOT_RECOVERED:result;
}
static int system_rename(void *context,const char *from,const char *to) {
    (void)context;return rename(from,to);
}
static bool exists(const char *path,bool *present) {
    FILE *file=fopen(path,"rb");
    if(!file) { *present=false;return errno==ENOENT; }
    *present=true;return fclose(file)==0;
}
bool ctr_fs_snapshot_save_with_rename(const char *path,const ctr_fs_volume *v,
                                     ctr_fs_snapshot_rename rename_file,void *context) {
    if(!v || !path || !*path || strlen(path)>4000) { errno=EINVAL;return false; }
    if(!rename_file)rename_file=system_rename;
    uint8_t *p=NULL;size_t n=0;if(!ctr_fs_export(v,&p,&n))return false;
    size_t length=strlen(path);char *temp=malloc(length+9),*previous=malloc(length+10);
    if(!temp || !previous) { free(temp);free(previous);free(p);return false; }
    memcpy(temp,path,length);memcpy(temp+length,".pending",9);
    memcpy(previous,path,length);memcpy(previous+length,".previous",10);
    bool present=false,backup=false,ok=exists(path,&present) && exists(previous,&backup);
    ctr_fs_volume old={0};
    if(ok && (present || backup)) {
        ok=ctr_fs_volume_init(&old,v->archive_id,true);
        if(ok)ok=load_one(&old,present?path:previous)==CTR_FS_SNAPSHOT_OK;
        ctr_fs_volume_close(&old);
        if(!ok)errno=EINVAL;
    }
    /* Recover an interrupted move of the old committed save. Never accept
       corruption as an empty save or overwrite an ambiguous existing backup. */
    if(ok && !present && backup) {
        ok=rename_file(context,previous,path)==0;
        if(ok) { present=true;backup=false; }
    }
    if(ok && backup) { errno=EEXIST;ok=false; }
    if(!ok) { int error=errno;free(previous);free(temp);free(p);errno=error;return false; }
    FILE *file=fopen(temp,"wbx");
    if(!file) { int error=errno;free(previous);free(temp);free(p);errno=error;return false; }
    ok=fwrite(p,1,n,file)==n;
    if(fflush(file))ok=false;
    if(fclose(file))ok=false;
    bool moved=false,published=false;
    if(ok && present) { ok=rename_file(context,path,previous)==0;moved=ok; }
    if(ok) { ok=rename_file(context,temp,path)==0;published=ok; }
    int error=errno;
    if(!ok && moved) {
        /* Leave both siblings if rollback fails; the loader can recover the
           previous committed data. Stale .pending remains visibly blocked. */
        if(rename_file(context,previous,path)==0)remove(temp);
    } else if(!ok && !published)remove(temp); /* Only staging created by this call. */
    if(ok && moved && remove(previous)!=0) { ok=false;error=errno; }
    free(previous);free(temp);free(p);if(!ok)errno=error?error:EIO;return ok;
}
bool ctr_fs_snapshot_save(void *context,const ctr_fs_volume *v) {
    return ctr_fs_snapshot_save_with_rename(context,v,NULL,NULL);
}
