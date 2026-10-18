#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "m1_storage_probe.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static unsigned checks;
static const char source[]="0123456789abcdef0123456789abcdef01234567";
#define CHECK(x) do { ++checks;if(!(x)) { fprintf(stderr,"FAIL %d: %s errno=%d\n",__LINE__,#x,errno);exit(1); } } while(0)
static void path(char output[1024],const char *directory,const char *name) {
    int n=snprintf(output,1024,"%s/%s",directory,name);CHECK(n>0 && n<1024);
}
static void root(char output[1024],const char *directory) {
    int n=snprintf(output,1024,"%s/m1-scratch-v1-%s",directory,source);CHECK(n>0 && n<1024);
}
static void temp(char directory[64]) {
    strcpy(directory,"/tmp/ctr2wup-m1-storage-XXXXXX");CHECK(mkdtemp(directory)!=NULL);
}
static void write_bytes(const char *filename,const uint8_t *bytes,size_t n) {
    FILE *f=fopen(filename,"wbx");CHECK(f!=NULL);CHECK(fwrite(bytes,1,n,f)==n);CHECK(fclose(f)==0);
}
static void read_bytes(const char *filename,uint8_t *bytes,size_t n) {
    FILE *f=fopen(filename,"rb");CHECK(f!=NULL);CHECK(fread(bytes,1,n,f)==n);CHECK(fgetc(f)==EOF);CHECK(fclose(f)==0);
}
static void cleanup(const char *directory) {
    char base[1024],name[1024];root(base,directory);
    const char *files[]={"sentinel.bin","recovery.good","recovery.backup","recovery.pending","not-a-directory"};
    for(unsigned i=0;i<sizeof(files)/sizeof(*files);++i) { path(name,base,files[i]);CHECK(unlink(name)==0 || errno==ENOENT); }
    path(name,base,".active");CHECK(rmdir(name)==0 || errno==ENOENT);
    CHECK(rmdir(base)==0);CHECK(rmdir(directory)==0);
}
static void success(void) {
    char directory[64];temp(directory);CtrM1StorageEvidence e;
    CHECK(ctr_m1_storage_probe(directory,source,&e));CHECK(e.result==CTR_M1_STORAGE_PASS && !e.fresh_reload);
    CHECK(e.sentinel_written && e.sentinel_readback && e.previous_recovered && e.pending_rejected && e.invalid_path_rejected && e.final_readback);
    CHECK(e.writes==5 && e.readbacks>=15 && e.recoveries==1);
    char base[1024],name[1024];root(base,directory);path(name,base,"sentinel.bin");uint8_t first[128],again[128];read_bytes(name,first,128);
    for(unsigned i=0;i<4;++i) {
        CHECK(ctr_m1_storage_probe(directory,source,&e));CHECK(e.fresh_reload && e.sentinel_written && e.sentinel_readback && e.previous_recovered);
        CHECK(e.writes==4 && e.recoveries==1);read_bytes(name,again,128);CHECK(!memcmp(first,again,128));
    }
    CHECK(!memcmp(first+24,source,40));cleanup(directory);
}
static void arguments(void) {
    CtrM1StorageEvidence e;CHECK(!ctr_m1_storage_probe(NULL,source,&e));CHECK(e.result==CTR_M1_STORAGE_PATH);
    CHECK(!ctr_m1_storage_probe("/tmp",NULL,&e));CHECK(e.result==CTR_M1_STORAGE_ARGUMENT);
    CHECK(!ctr_m1_storage_probe("/tmp","bad",&e));CHECK(e.result==CTR_M1_STORAGE_ARGUMENT);
    CHECK(!ctr_m1_storage_probe("/tmp","0123456789ABCDEF0123456789abcdef01234567",&e));CHECK(e.result==CTR_M1_STORAGE_ARGUMENT);
    CHECK(!ctr_m1_storage_probe("/tmp",source,NULL));
    const char *bad[]={"relative","/tmp/../tmp","/tmp/./x","/tmp//x","/tmp/","/tmp/does-not-exist-ctr2wup","/tmp/x\\y","/tmp/x:y"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i) { CHECK(!ctr_m1_storage_probe(bad[i],source,&e));CHECK(e.result==CTR_M1_STORAGE_PATH); }
    for(int i=0;i<=CTR_M1_STORAGE_RECOVERY;++i)CHECK(strcmp(ctr_m1_storage_result_name((CtrM1StorageResult)i),"INVALID"));
    CHECK(!strcmp(ctr_m1_storage_result_name((CtrM1StorageResult)100),"INVALID"));
}
static unsigned inspected;
static const char *bad_node;
static int bad_error;
static int inspect_sd(const char *name,struct stat *st) {
    /* Virtual devoptab ancestors are not mounted directories. */
    CHECK(strncmp(name,"fs:/vol/external01",18)==0);
    ++inspected;memset(st,0,sizeof(*st));
    if(bad_node && !strcmp(name,bad_node)) {
        if(bad_error) { errno=bad_error;return -1; }
        st->st_mode=S_IFLNK;return 0;
    }
    st->st_mode=S_IFDIR;return 0;
}
static void native_directories(void) {
    const char *mount="fs:/vol/external01";
    inspected=0;CHECK(ctr_m1_directory_safe(mount,inspect_sd));CHECK(inspected==1);
    inspected=0;CHECK(ctr_m1_directory_safe("fs:/vol/external01/ctr2wup/install-check/source",inspect_sd));
    CHECK(inspected==4);
    const char *bad[]={"fs:/vol","fs:/vol/external010/test","fs:/vol/storage_mlc01/test",
        "fs:/vol/external01/","fs:/vol/external01//x","fs:/vol/external01/../x",
        "fs:/vol/external01/./x","fs:/vol/external01/x:y","fs:/vol/external01/x\\y"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i) {
        errno=EIO;CHECK(!ctr_m1_directory_safe(bad[i],inspect_sd));CHECK(errno==EINVAL);
    }
    bad_node=mount;CHECK(!ctr_m1_directory_safe(mount,inspect_sd));
    bad_node="fs:/vol/external01/linked";
    CHECK(!ctr_m1_directory_safe("fs:/vol/external01/linked/child",inspect_sd));
    bad_error=EACCES;CHECK(!ctr_m1_directory_safe(bad_node,inspect_sd));CHECK(errno==EACCES);
    bad_node=NULL;bad_error=0;
    CHECK(!ctr_m1_directory_safe(mount,NULL));CHECK(errno==EINVAL);
}
static void corrupt_sentinel(void) {
    char directory[64];temp(directory);CtrM1StorageEvidence e;CHECK(ctr_m1_storage_probe(directory,source,&e));
    char base[1024],name[1024];root(base,directory);path(name,base,"sentinel.bin");uint8_t before[128],after[128];read_bytes(name,before,128);
    before[73]^=1;CHECK(unlink(name)==0);write_bytes(name,before,128);
    CHECK(!ctr_m1_storage_probe(directory,source,&e));CHECK(e.result==CTR_M1_STORAGE_CONTENTS && !e.fresh_reload);
    read_bytes(name,after,128);CHECK(!memcmp(before,after,128));cleanup(directory);
    temp(directory);CHECK(ctr_m1_storage_probe(directory,source,&e));root(base,directory);path(name,base,"sentinel.bin");read_bytes(name,before,128);
    CHECK(unlink(name)==0);write_bytes(name,before,127);CHECK(!ctr_m1_storage_probe(directory,source,&e));CHECK(e.result==CTR_M1_STORAGE_CONTENTS);
    FILE *f=fopen(name,"rb");CHECK(f!=NULL);CHECK(fseek(f,0,SEEK_END)==0 && ftell(f)==127);CHECK(fclose(f)==0);cleanup(directory);
}
static void symlinks(void) {
    char directory[64],other[64];temp(directory);temp(other);CtrM1StorageEvidence e;
    char base[1024],name[1024],target[1024];root(base,directory);CHECK(symlink(other,base)==0);
    CHECK(!ctr_m1_storage_probe(directory,source,&e));CHECK(e.result==CTR_M1_STORAGE_PATH);
    CHECK(unlink(base)==0);CHECK(rmdir(directory)==0);CHECK(rmdir(other)==0);
    temp(directory);temp(other);path(name,directory,"linked");CHECK(symlink(other,name)==0);
    CHECK(!ctr_m1_storage_probe(name,source,&e));CHECK(e.result==CTR_M1_STORAGE_PATH);CHECK(unlink(name)==0);CHECK(rmdir(directory)==0);CHECK(rmdir(other)==0);
    temp(directory);CHECK(ctr_m1_storage_probe(directory,source,&e));root(base,directory);path(name,base,"sentinel.bin");
    uint8_t original[128],after[128];read_bytes(name,original,128);CHECK(unlink(name)==0);path(target,directory,"outside.bin");write_bytes(target,original,128);CHECK(symlink(target,name)==0);
    CHECK(!ctr_m1_storage_probe(directory,source,&e));CHECK(e.result==CTR_M1_STORAGE_CONTENTS);read_bytes(target,after,128);CHECK(!memcmp(original,after,128));
    CHECK(unlink(name)==0);CHECK(unlink(target)==0);cleanup(directory);
    temp(directory);CHECK(ctr_m1_storage_probe(directory,source,&e));root(base,directory);path(name,base,"recovery.backup");path(target,base,"sentinel.bin");CHECK(symlink(target,name)==0);
    CHECK(!ctr_m1_storage_probe(directory,source,&e));CHECK(e.result==CTR_M1_STORAGE_CONTENTS);CHECK(unlink(name)==0);cleanup(directory);
}
static void locked_and_interrupted(void) {
    char directory[64];temp(directory);CtrM1StorageEvidence e;CHECK(ctr_m1_storage_probe(directory,source,&e));
    char base[1024],name[1024],sentinel[1024];root(base,directory);path(name,base,".active");CHECK(mkdir(name,0700)==0);
    CHECK(!ctr_m1_storage_probe(directory,source,&e));CHECK(e.result==CTR_M1_STORAGE_BUSY && e.writes==0);CHECK(rmdir(name)==0);
    path(sentinel,base,"sentinel.bin");uint8_t original[128],again[128];read_bytes(sentinel,original,128);
    const char *stages[]={"recovery.good","recovery.backup","recovery.pending","not-a-directory"};
    for(unsigned i=0;i<sizeof(stages)/sizeof(*stages);++i) {
        path(name,base,stages[i]);write_bytes(name,original,128);
        CHECK(!ctr_m1_storage_probe(directory,source,&e));CHECK(e.result==CTR_M1_STORAGE_CONTENTS && e.writes==0);
        read_bytes(name,again,128);CHECK(!memcmp(original,again,128));CHECK(unlink(name)==0);
        CHECK(ctr_m1_storage_probe(directory,source,&e));CHECK(e.fresh_reload && e.previous_recovered && e.pending_rejected);
    }
    cleanup(directory);
}
int main(void) {
    success();arguments();native_directories();corrupt_sentinel();symlinks();locked_and_interrupted();
    printf("M1 storage: %u checks passed\n",checks);return 0;
}
