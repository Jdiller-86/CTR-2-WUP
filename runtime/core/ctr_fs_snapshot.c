#include "ctr_fs_snapshot.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ctr_fs_snapshot_result ctr_fs_snapshot_load(ctr_fs_volume *v,const char *path) {
    if(!v || !path || !*path)return CTR_FS_SNAPSHOT_ERROR;
    FILE *file=fopen(path,"rb");if(!file)return errno==ENOENT?CTR_FS_SNAPSHOT_MISSING:CTR_FS_SNAPSHOT_ERROR;
    size_t limit=CTR_FS_MAX_VOLUME_BYTES+24+CTR_FS_MAX_FILES*(CTR_FS_PATH_BYTES+8);
    uint8_t *p=malloc(limit+1);if(!p) { fclose(file);return CTR_FS_SNAPSHOT_ERROR; }
    size_t n=fread(p,1,limit+1,file);bool ok=!ferror(file) && n<=limit;
    if(fclose(file))ok=false;
    if(ok)ok=ctr_fs_import(v,p,n);
    free(p);return ok?CTR_FS_SNAPSHOT_OK:CTR_FS_SNAPSHOT_ERROR;
}
bool ctr_fs_snapshot_save(void *context,const ctr_fs_volume *v) {
    const char *path=context;if(!path || !*path || strlen(path)>4000)return false;
    uint8_t *p=NULL;size_t n=0;if(!ctr_fs_export(v,&p,&n))return false;
    size_t length=strlen(path);char *temp=malloc(length+9);if(!temp) { free(p);return false; }
    memcpy(temp,path,length);memcpy(temp+length,".pending",9);
    FILE *file=fopen(temp,"wbx");if(!file) { free(temp);free(p);return false; }
    bool ok=fwrite(p,1,n,file)==n;
    if(fflush(file))ok=false;
    if(fclose(file))ok=false;
    if(ok && rename(temp,path))ok=false;
    if(!ok)remove(temp); /* Only the sibling successfully created by this call. */
    free(temp);free(p);return ok;
}
