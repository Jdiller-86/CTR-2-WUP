#define _POSIX_C_SOURCE 200809L
#include "install_capture.h"
#include "ctr_sha256.h"
#include "log.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static unsigned assertions;
#define CHECK(x) do { ++assertions;if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1); } } while(0)

static void write_bytes(const char *path,const void *data,size_t length) {
    FILE *f=fopen(path,"wb");CHECK(f);CHECK(fwrite(data,1,length,f)==length);CHECK(fclose(f)==0);
}

static void payload(void) {
    char directory[]="/tmp/ctr-capture-XXXXXX";CHECK(mkdtemp(directory));
    char path[256],other[256];snprintf(path,sizeof(path),"%s/00000000.app",directory);
    static unsigned char data[40001];for(unsigned i=0;i<sizeof(data);++i)data[i]=(unsigned char)(i*17);
    CtrInstallCaptureFile file={.name="00000000.app",.size=sizeof(data)};
    ctr_sha256 hash;ctr_sha256_init(&hash);CHECK(ctr_sha256_update(&hash,data,sizeof(data)));
    CHECK(ctr_sha256_final(&hash,file.sha256));
    const char *failed=NULL;
    CHECK(!ctr_install_capture_verify(directory,&file,1,&failed));
    write_bytes(path,data,sizeof(data));
    CHECK(ctr_install_capture_verify(directory,&file,1,&failed));CHECK(!failed);
    data[20000]^=1;write_bytes(path,data,sizeof(data));
    CHECK(!ctr_install_capture_verify(directory,&file,1,&failed));CHECK(!strcmp(failed,file.name));
    data[20000]^=1;write_bytes(path,data,sizeof(data)-1);
    CHECK(!ctr_install_capture_verify(directory,&file,1,&failed));
    write_bytes(path,data,sizeof(data));
    snprintf(other,sizeof(other),"%s/unexpected",directory);write_bytes(other,"x",1);
    CHECK(!ctr_install_capture_verify(directory,&file,1,&failed));CHECK(unlink(other)==0);
    CHECK(unlink(path)==0);CHECK(symlink("/dev/zero",path)==0);
    CHECK(!ctr_install_capture_verify(directory,&file,1,&failed));CHECK(unlink(path)==0);
    CHECK(mkdir(path,0700)==0);CHECK(!ctr_install_capture_verify(directory,&file,1,&failed));CHECK(rmdir(path)==0);
    CtrInstallCaptureFile duplicate[2]={file,file};
    CHECK(!ctr_install_capture_verify(directory,duplicate,2,&failed));
    file.name="../outside";CHECK(!ctr_install_capture_verify(directory,&file,1,&failed));
    file.name="a\\b";CHECK(!ctr_install_capture_verify(directory,&file,1,&failed));
    CHECK(!ctr_install_capture_verify(directory,&file,0,&failed));
    CHECK(!ctr_install_capture_verify(NULL,&file,1,&failed));CHECK(rmdir(directory)==0);
}

typedef struct {
    CtrInstallCapture *capture;
    int32_t info,target,submit,callback,poll,abort;
    unsigned starts,pumps,polls,aborts,finish_after,cancel_after,records;
    const char *fail_record;
    char events[1024];size_t used;
    int32_t raw;
    bool callback_seen,ended,immediate;
} Mock;

static bool record(void *context,const char *event,int32_t result,const CtrInstallCaptureProgress *p) {
    (void)p;Mock *m=context;++m->records;
    int n=snprintf(m->events+m->used,sizeof(m->events)-m->used,"%s ",event);CHECK(n>0);
    m->used+=(size_t)n;CHECK(m->used<sizeof(m->events));
    if(!strcmp(event,"callback_raw")) { m->callback_seen=true;m->raw=result; }
    if(!strcmp(event,"end"))m->ended=true;
    return !m->fail_record || strcmp(event,m->fail_record);
}
static int32_t info(void *p) { return ((Mock *)p)->info; }
static int32_t target(void *p,unsigned t) { CHECK(t<2);return ((Mock *)p)->target; }
static int32_t start(void *p,CtrInstallCapture *capture) {
    Mock *m=p;m->capture=capture;++m->starts;
    if(m->immediate && !m->submit)ctr_install_capture_complete(capture,m->callback);
    return m->submit;
}
static int32_t progress(void *p,CtrInstallCaptureProgress *out) {
    Mock *m=p;++m->polls;out->total=100;out->bytes=m->polls;out->active=1;return m->poll;
}
static int32_t abort_install(void *p) { Mock *m=p;++m->aborts;return m->abort; }
static bool pump(void *p) {
    Mock *m=p;++m->pumps;
    if(m->pumps==m->finish_after)ctr_install_capture_complete(m->capture,m->callback);
    return !m->cancel_after || m->pumps<m->cancel_after;
}
static const CtrInstallCaptureOps ops={record,info,target,start,progress,abort_install,pump};

static void asynchronous(void) {
    CtrInstallCapture c={0};Mock m={.finish_after=3};
    CHECK(ctr_install_capture_run(&c,&ops,&m,0));CHECK(m.starts==1 && m.pumps==3 && m.aborts==0);
    CHECK(m.callback_seen && m.raw==0 && m.ended);CHECK(c.submitted);
    CHECK(!strcmp(m.events,"intent get_info set_target before_submit submit_raw progress progress progress callback_raw end "));
    CHECK(!ctr_install_capture_run(&c,&ops,&m,0));CHECK(m.starts==1);

    memset(&c,0,sizeof(c));m=(Mock){.immediate=true,.callback=(int32_t)UINT32_C(0xFFFBF442)};
    CHECK(!ctr_install_capture_run(&c,&ops,&m,1));CHECK(m.pumps==0 && m.polls==0);
    CHECK(m.callback_seen && (uint32_t)m.raw==UINT32_C(0xFFFBF442) && m.ended);

    memset(&c,0,sizeof(c));m=(Mock){.finish_after=5,.cancel_after=1,.callback=-20,.abort=-9};
    CHECK(!ctr_install_capture_run(&c,&ops,&m,0));
    CHECK(m.aborts==1 && m.pumps==5 && m.callback_seen && m.raw==-20 && m.ended);
    CHECK(c.abort_requested && atomic_load(&c.complete));

    memset(&c,0,sizeof(c));m=(Mock){.finish_after=4,.callback=0,.fail_record="submit_raw"};
    CHECK(!ctr_install_capture_run(&c,&ops,&m,0));
    CHECK(c.log_failed && m.aborts==1 && m.pumps==4 && m.callback_seen && m.ended);

    const char *failures[]={"intent","get_info","set_target","before_submit"};
    for(unsigned i=0;i<4;++i) {
        memset(&c,0,sizeof(c));m=(Mock){.fail_record=failures[i]};
        CHECK(!ctr_install_capture_run(&c,&ops,&m,0));CHECK(!m.starts && !m.pumps && !c.submitted);
    }
    memset(&c,0,sizeof(c));m=(Mock){.info=-19};
    CHECK(!ctr_install_capture_run(&c,&ops,&m,0));CHECK(!m.starts);
    memset(&c,0,sizeof(c));m=(Mock){.target=-4};
    CHECK(!ctr_install_capture_run(&c,&ops,&m,1));CHECK(!m.starts);
    memset(&c,0,sizeof(c));m=(Mock){.submit=-1};
    CHECK(!ctr_install_capture_run(&c,&ops,&m,0));CHECK(m.starts==1 && !m.pumps && !m.callback_seen && !c.submitted);
    memset(&c,0,sizeof(c));m=(Mock){.finish_after=3,.poll=-3};
    CHECK(ctr_install_capture_run(&c,&ops,&m,0));CHECK(m.callback_seen && m.pumps==3);
    memset(&c,0,sizeof(c));m=(Mock){0};
    CHECK(!ctr_install_capture_run(&c,&ops,&m,2));CHECK(!m.records);
}

static void selection(void) {
    bool armed=false;
    CHECK(!ctr_install_capture_select(NULL,true,false,true));
    CHECK(!ctr_install_capture_select(&armed,true,true,true));CHECK(!armed);
    CHECK(!ctr_install_capture_select(&armed,false,false,false));CHECK(!armed);
    CHECK(!ctr_install_capture_select(&armed,true,false,true));CHECK(!armed);
    CHECK(!ctr_install_capture_select(&armed,true,false,false));CHECK(armed);
    CHECK(!ctr_install_capture_select(&armed,false,true,true));CHECK(armed);
    CHECK(ctr_install_capture_select(&armed,true,true,true));CHECK(!armed);
    CHECK(!ctr_install_capture_select(&armed,true,true,true));CHECK(!armed);
}

typedef struct {
    capture_log *log;
    unsigned syncs,verifications;
    bool missing,corrupt,wrong_sd;
    int sync_error;
} LogProbe;

static bool sync_log(void *context) {
    LogProbe *p=context;++p->syncs;CHECK(!p->log->file);
    if(p->sync_error) { errno=p->sync_error;return false; }
    if(p->missing)CHECK(unlink(p->log->path)==0);
    if(p->corrupt) {
        FILE *f=fopen(p->log->path,"r+b");CHECK(f);
        CHECK(fwrite("?",1,1,f)==1 && fclose(f)==0);
    }
    return true;
}
static bool verify_log(void *context,const char *path,uint64_t bytes,const uint8_t digest[32]) {
    LogProbe *p=context;++p->verifications;
    CHECK(!strcmp(path,p->log->path) && bytes==p->log->bytes && bytes>0 && digest);
    if(p->wrong_sd) { errno=ENOENT;return false; }
    return true;
}
static void logs(void) {
    char directory[]="/tmp/ctr-capture-log-XXXXXX";CHECK(mkdtemp(directory));
    capture_log log={0};LogProbe p={.log=&log};
    CHECK(capture_log_open(&log,directory,1,sync_log,verify_log,&p));
    CHECK(log.verified==1 && log.file && p.syncs==1 && p.verifications==1);
    CHECK(capture_log_line(&log,"CAPTURE event=callback_raw code=0xfffbf442"));
    char path[512];strcpy(path,log.path);
    CHECK(capture_log_close(&log));CHECK(!log.file);
    FILE *f=fopen(path,"rb");CHECK(f);char text[4096]={0};
    CHECK(fread(text,1,sizeof(text)-1,f)==log.bytes && fclose(f)==0);
    CHECK(strstr(text,"mcp-capture-v2\nCAPTURE event=callback_raw code=0xfffbf442\n"));
    ctr_sha256 expected=log.expected;uint8_t a[32],b[32];
    CHECK(ctr_sha256_final(&expected,a));ctr_sha256_init(&expected);
    CHECK(ctr_sha256_update(&expected,text,strlen(text)) && ctr_sha256_final(&expected,b));
    CHECK(!memcmp(a,b,32)); /* Written intent, not a digest derived from read-back. */
    CHECK(capture_log_checkpoint(&log,"EXIT stage=main_return"));CHECK(!log.file);
    log.sync=NULL;log.verify=NULL;unsigned syncs=p.syncs,verified=p.verifications;
    CHECK(capture_log_checkpoint(&log,"EXIT after_system_transition"));
    CHECK(p.syncs==syncs && p.verifications==verified); /* No new FSA client after transition. */
    log.sync=sync_log;log.verify=verify_log;
    CHECK(capture_log_checkpoint(&log,"Foreground reacquired; verify all preceding bytes"));
    CHECK(p.syncs>syncs && p.verifications>verified);
    capture_log second={0};p=(LogProbe){.log=&second};
    CHECK(capture_log_open(&second,directory,1,sync_log,verify_log,&p));
    CHECK(strcmp(path,second.path)); /* Existing per-session evidence is preserved. */
    CHECK(capture_log_close(&second));CHECK(unlink(second.path)==0 && unlink(path)==0);

    for(unsigned failure=0;failure<5;++failure) {
        log=(capture_log){0};p=(LogProbe){.log=&log};
        CHECK(capture_log_open(&log,directory,2+failure,sync_log,verify_log,&p));
        if(failure==0)p.missing=true;
        if(failure==1)p.corrupt=true;
        if(failure==2)p.sync_error=ENOSPC;
        if(failure==3)p.wrong_sd=true;
        if(failure==4) { CHECK(capture_log_close(&log));CHECK(unlink(log.path)==0); }
        CHECK(!capture_log_close(&log));CHECK(log.failed && !log.file);
        const char *stage=log.stage;int error=log.error;
        CHECK(!capture_log_line(&log,"No false saved message"));
        CHECK(!capture_log_close(&log) && log.stage==stage && log.error==error);
        CHECK(!capture_log_checkpoint(&log,"Never recreate missing output"));
        if(failure==0 || failure==4)CHECK(access(log.path,F_OK)!=0);
        else CHECK(unlink(log.path)==0);
    }
    log=(capture_log){0};p=(LogProbe){.log=&log,.wrong_sd=true};
    CHECK(!capture_log_open(&log,directory,99,sync_log,verify_log,&p));
    CHECK(log.failed && !strcmp(log.stage,"sd_read_verify") && !log.file);
    CHECK(unlink(log.path)==0);CHECK(rmdir(directory)==0);
}

int main(void) { payload();asynchronous();selection();logs();printf("Install capture: %u checks passed\n",assertions);return 0; }
