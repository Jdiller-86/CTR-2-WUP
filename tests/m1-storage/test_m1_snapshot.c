#define _POSIX_C_SOURCE 200809L
#include "m1_snapshot_probe.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static unsigned checks;
static const char source[]="0123456789abcdef0123456789abcdef01234567";
#define CHECK(x) do { ++checks;if(!(x)) { fprintf(stderr,"FAIL %d: %s errno=%d\n",__LINE__,#x,errno);exit(1); } } while(0)
static void temp(char directory[64]) {
    strcpy(directory,"/tmp/ctr2wup-m1-snapshot-XXXXXX");CHECK(mkdtemp(directory)!=NULL);
}
static void path(char output[1024],const char *directory,const char *name) {
    int n=snprintf(output,1024,"%s/%s",directory,name);CHECK(n>0 && n<1024);
}
static void root(char output[1024],const char *directory) {
    int n=snprintf(output,1024,"%s/m1-snapshot-v2-%s",directory,source);CHECK(n>0 && n<1024);
}
static void owned(char output[1024],const char *directory,const char *name) {
    char base[1024];root(base,directory);path(output,base,name);
}
static void cleanup(const char *directory) {
    char base[1024],name[1024];root(base,directory);
    const char *files[]={"producer.bin","complete.bin","original.snapshot","original.snapshot.previous",
        "original.snapshot.pending","not-a-directory","unknown.bin"};
    for(unsigned i=0;i<sizeof(files)/sizeof(*files);++i) { path(name,base,files[i]);CHECK(unlink(name)==0 || errno==ENOENT); }
    path(name,base,".active");CHECK(rmdir(name)==0 || errno==ENOENT);
    CHECK(rmdir(base)==0);CHECK(rmdir(directory)==0);
}
static void byte(const char *file,unsigned at,uint8_t value) {
    FILE *f=fopen(file,"r+b");CHECK(f!=NULL);CHECK(fseek(f,(long)at,SEEK_SET)==0);
    CHECK(fputc(value,f)!=EOF && fclose(f)==0);
}
static uint8_t first_byte(const char *file) {
    FILE *f=fopen(file,"rb");CHECK(f!=NULL);int value=fgetc(f);CHECK(value!=EOF && fclose(f)==0);return (uint8_t)value;
}
static void write_file(const char *file,const char *text) {
    FILE *f=fopen(file,"wbx");CHECK(f!=NULL);CHECK(fputs(text,f)>=0 && fclose(f)==0);
}
static void prepared(const char *directory,CtrM1SnapshotEvidence *e) {
    CHECK(ctr_m1_snapshot_probe(directory,source,100,e));
    CHECK(!strcmp(e->phase,"PREPARED") && e->producer_session==100 && !e->recovery_session && e->current_session==100);
    CHECK(e->production_adapter && e->interruption_prepared && e->storage.result==CTR_M1_STORAGE_PASS);
    CHECK(e->storage.sentinel_written && e->storage.sentinel_readback && e->storage.invalid_path_rejected);
    CHECK(!e->storage.fresh_reload && !e->storage.previous_recovered && !e->storage.pending_rejected && !e->storage.final_readback);
    CHECK(e->storage.writes==2 && e->storage.readbacks>=5 && !e->storage.recoveries);
    char file[1024];owned(file,directory,"original.snapshot");CHECK(access(file,F_OK)!=0 && errno==ENOENT);
    owned(file,directory,"original.snapshot.previous");CHECK(access(file,F_OK)==0);
    owned(file,directory,"original.snapshot.pending");CHECK(access(file,F_OK)==0);
}
static void recovered(const char *directory,CtrM1SnapshotEvidence *e) {
    CHECK(ctr_m1_snapshot_probe(directory,source,200,e));
    CHECK(!strcmp(e->phase,"RECOVERED") && e->producer_session==100 && e->recovery_session==200 && e->current_session==200);
    CHECK(e->production_adapter && e->interruption_prepared && e->storage.result==CTR_M1_STORAGE_PASS);
    CHECK(e->storage.sentinel_written && e->storage.sentinel_readback && e->storage.fresh_reload && e->storage.previous_recovered);
    CHECK(e->storage.pending_rejected && e->storage.invalid_path_rejected && e->storage.final_readback);
    CHECK(e->storage.writes==1 && e->storage.recoveries==1 && e->storage.readbacks>=10);
    char file[1024];owned(file,directory,"original.snapshot");CHECK(access(file,F_OK)==0);
    owned(file,directory,"original.snapshot.previous");CHECK(access(file,F_OK)!=0 && errno==ENOENT);
    owned(file,directory,"original.snapshot.pending");CHECK(access(file,F_OK)!=0 && errno==ENOENT);
}
static void complete(const char *directory,CtrM1SnapshotEvidence *e,uint64_t session) {
    CHECK(ctr_m1_snapshot_probe(directory,source,session,e));
    CHECK(!strcmp(e->phase,"COMPLETE") && e->producer_session==100 && e->recovery_session==200 && e->current_session==session);
    CHECK(e->storage.sentinel_written && e->storage.sentinel_readback && e->storage.fresh_reload && e->storage.previous_recovered);
    CHECK(e->storage.pending_rejected && e->storage.invalid_path_rejected && e->storage.final_readback);
    CHECK(e->storage.writes==0 && e->storage.recoveries==0 && e->storage.readbacks>=4);
}
static void success(void) {
    char directory[64];temp(directory);CtrM1SnapshotEvidence e;prepared(directory,&e);recovered(directory,&e);
    for(unsigned i=0;i<4;++i)complete(directory,&e,300+i);
    CHECK(!ctr_m1_snapshot_probe(directory,source,100,&e) && !strcmp(e.phase,"NONE"));
    CHECK(!ctr_m1_snapshot_probe(directory,source,200,&e) && !strcmp(e.phase,"NONE"));
    cleanup(directory);
}
static void two_processes(void) {
    char directory[64];temp(directory);
    for(unsigned i=0;i<2;++i) {
        pid_t child=fork();CHECK(child>=0);
        if(child==0) { CtrM1SnapshotEvidence e;if(!i)prepared(directory,&e);else recovered(directory,&e);exit(0); }
        int status;CHECK(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
    }
    CtrM1SnapshotEvidence e;complete(directory,&e,300);cleanup(directory);
}
static void arguments(void) {
    CtrM1SnapshotEvidence e;
    CHECK(!ctr_m1_snapshot_probe(NULL,source,1,&e) && e.storage.result==CTR_M1_STORAGE_PATH);
    CHECK(!ctr_m1_snapshot_probe("/tmp",NULL,1,&e) && e.storage.result==CTR_M1_STORAGE_ARGUMENT);
    CHECK(!ctr_m1_snapshot_probe("/tmp","bad",1,&e));
    CHECK(!ctr_m1_snapshot_probe("/tmp",source,0,&e));CHECK(!ctr_m1_snapshot_probe("/tmp",source,1,NULL));
    const char *bad[]={"relative","/tmp/../tmp","/tmp/./x","/tmp//x","/tmp/","/tmp/not-present-ctr2wup","/tmp/x\\y","/tmp/x:y"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i)CHECK(!ctr_m1_snapshot_probe(bad[i],source,1,&e));
}
static void corrupt_prepared(void) {
    const char *files[]={"producer.bin","original.snapshot.previous","original.snapshot.pending"};
    for(unsigned i=0;i<3;++i) {
        char directory[64],file[1024];temp(directory);CtrM1SnapshotEvidence e;prepared(directory,&e);owned(file,directory,files[i]);
        byte(file,0,'X');CHECK(!ctr_m1_snapshot_probe(directory,source,200,&e));
        CHECK(e.storage.result==CTR_M1_STORAGE_CONTENTS && !strcmp(e.phase,"NONE") && !e.storage.writes);
        CHECK(first_byte(file)=='X');cleanup(directory);
    }
    for(unsigned i=0;i<3;++i) {
        char directory[64],file[1024];temp(directory);CtrM1SnapshotEvidence e;prepared(directory,&e);owned(file,directory,files[i]);
        CHECK(unlink(file)==0);CHECK(!ctr_m1_snapshot_probe(directory,source,200,&e) && !e.storage.writes);
        CHECK(access(file,F_OK)!=0 && errno==ENOENT);cleanup(directory);
    }
}
static void corrupt_complete(void) {
    const char *files[]={"producer.bin","complete.bin","original.snapshot"};
    for(unsigned i=0;i<3;++i) {
        char directory[64],file[1024];temp(directory);CtrM1SnapshotEvidence e;prepared(directory,&e);recovered(directory,&e);
        owned(file,directory,files[i]);byte(file,0,'X');CHECK(!ctr_m1_snapshot_probe(directory,source,300,&e));
        CHECK(!strcmp(e.phase,"NONE") && !e.storage.writes && first_byte(file)=='X');cleanup(directory);
    }
}
static void links_and_unknown(void) {
    char directory[64],other[64],file[1024],target[1024];CtrM1SnapshotEvidence e;
    temp(directory);temp(other);char base[1024];root(base,directory);CHECK(symlink(other,base)==0);
    CHECK(!ctr_m1_snapshot_probe(directory,source,100,&e) && e.storage.result==CTR_M1_STORAGE_PATH);
    CHECK(unlink(base)==0 && rmdir(directory)==0 && rmdir(other)==0);
    const char *files[]={"producer.bin","original.snapshot.previous","original.snapshot.pending"};
    for(unsigned hard=0;hard<2;++hard)for(unsigned i=0;i<3;++i) {
        temp(directory);prepared(directory,&e);owned(file,directory,files[i]);
        path(target,directory,"outside.bin");CHECK(rename(file,target)==0);
        CHECK((hard?link(target,file):symlink(target,file))==0);
        CHECK(!ctr_m1_snapshot_probe(directory,source,200,&e) && !e.storage.writes);
        CHECK(first_byte(target)=='C');CHECK(unlink(file)==0 && unlink(target)==0);cleanup(directory);
    }
    for(unsigned completed=0;completed<2;++completed) {
        temp(directory);prepared(directory,&e);if(completed)recovered(directory,&e);
        owned(file,directory,"unknown.bin");write_file(file,"preserve");
        CHECK(!ctr_m1_snapshot_probe(directory,source,300,&e) && !e.storage.writes && first_byte(file)=='p');cleanup(directory);
    }
}
static void busy_and_partial(void) {
    char directory[64],file[1024];CtrM1SnapshotEvidence e;
    temp(directory);prepared(directory,&e);owned(file,directory,".active");CHECK(mkdir(file,0700)==0);
    CHECK(!ctr_m1_snapshot_probe(directory,source,200,&e) && e.storage.result==CTR_M1_STORAGE_BUSY && !e.storage.writes);
    CHECK(rmdir(file)==0);recovered(directory,&e);cleanup(directory);
    const char *extras[]={"complete.bin","original.snapshot","not-a-directory"};
    for(unsigned i=0;i<3;++i) {
        temp(directory);prepared(directory,&e);owned(file,directory,extras[i]);write_file(file,"preserve");
        CHECK(!ctr_m1_snapshot_probe(directory,source,200,&e) && !e.storage.writes && first_byte(file)=='p');cleanup(directory);
    }
    temp(directory);prepared(directory,&e);
    CHECK(!ctr_m1_snapshot_probe(directory,source,100,&e) && e.storage.result==CTR_M1_STORAGE_CONTENTS);
    recovered(directory,&e);cleanup(directory);
}
int main(void) {
    arguments();success();two_processes();corrupt_prepared();corrupt_complete();links_and_unknown();busy_and_partial();
    printf("M1 production snapshot: %u checks passed\n",checks);return 0;
}
