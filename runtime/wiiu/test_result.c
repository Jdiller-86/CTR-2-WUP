#include "test_result.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

static bool fail(ctr_test_result *r,int error,const char *stage) {
    if(!r->failed) { r->failed=true;r->error_code=error?error:EIO;r->error_stage=stage; }
    return false;
}
static bool read_back(ctr_test_result *r,const char *expected) {
    char actual[CTR_TEST_RESULT_BYTES+1];FILE *file=fopen(r->path,"rb");
    if(!file)return fail(r,errno,"read_open");
    size_t n=fread(actual,1,sizeof(actual),file);bool ok=!ferror(file);
    int error=errno;if(fclose(file)!=0) { ok=false;error=errno; }
    if(!ok)return fail(r,error,"read_close");
    if(n!=CTR_TEST_RESULT_BYTES || (expected?memcmp(actual,expected,n):
        memcmp(actual,r->identity,strlen(r->identity))))return fail(r,EIO,"read_verify");
    return true;
}
static bool write_result(ctr_test_result *r,FILE *file,const char *body) {
    char text[CTR_TEST_RESULT_BYTES];memset(text,' ',sizeof(text));
    int n=snprintf(text,sizeof(text),"%s%s\nStorage: closed binary writes and exact read-back. Power-loss recovery unverified.\n",r->identity,body);
    if(n<0 || (size_t)n>=sizeof(text)) { fclose(file);return fail(r,EMSGSIZE,"format"); }
    text[n]=' ';text[sizeof(text)-1]='\n';
    bool ok=fwrite(text,1,sizeof(text),file)==sizeof(text);
    int error=errno;
    if(fflush(file)!=0) { ok=false;error=errno; }
    if(fclose(file)!=0) { ok=false;error=errno; }
    if(!ok)return fail(r,error,"write_close");
    if(!read_back(r,text))return false;
    ++r->verified_writes;return true;
}
bool ctr_test_result_open(ctr_test_result *r,const char *directory,
                         const char *application,const char *version,uint64_t id) {
    if(!r)return false;
    if(r->ready)return fail(r,EBUSY,"open");
    memset(r,0,sizeof(*r));
    if(!directory || !*directory || !application || !*application || !version || !*version ||
       strlen(application)>48 || strlen(version)>32 ||
       strspn(application,"abcdefghijklmnopqrstuvwxyz0123456789-")!=strlen(application) ||
       strspn(version,"0123456789.")!=strlen(version))return fail(r,EINVAL,"arguments");
    int n;
    FILE *file=NULL;
    for(unsigned attempt=0;attempt<32;++attempt) {
        n=snprintf(r->filename,sizeof(r->filename),"result-%s-%s-%016llx-%02u.txt",application,version,(unsigned long long)id,attempt);
        if(n<0 || (size_t)n>=sizeof(r->filename))return fail(r,ENAMETOOLONG,"filename");
        n=snprintf(r->path,sizeof(r->path),"%s/%s",directory,r->filename);
        if(n<0 || (size_t)n>=sizeof(r->path))return fail(r,ENAMETOOLONG,"path");
        n=snprintf(r->identity,sizeof(r->identity),"CTR2WUP result report\nApplication: %s | Build: %s\nSession ticks: %016llx\nFile: %s\n",application,version,(unsigned long long)id,r->filename);
        if(n<0 || (size_t)n>=sizeof(r->identity))return fail(r,ENAMETOOLONG,"identity");
        file=fopen(r->path,"wbx");if(file)break;
        if(errno!=EEXIST)return fail(r,errno,"open");
    }
    if(!file)return fail(r,EEXIST,"open");
    if(!write_result(r,file,"Session: STARTED\nGuide: not completed\n"))return false;
    r->ready=true;return true;
}
bool ctr_test_result_update(ctr_test_result *r,const char *body) {
    if(!r || !body)return false;
    if(r->failed)return false;
    if(!r->ready)return fail(r,EBADF,"update");
    if(strlen(body)+strlen(r->identity)+160>=CTR_TEST_RESULT_BYTES)return fail(r,EMSGSIZE,"format");
    /* Validate the reserved identity before modifying an existing result. */
    if(!read_back(r,NULL))return false;
    FILE *file=fopen(r->path,"r+b");
    if(!file)return fail(r,errno,"update_open");
    return write_result(r,file,body);
}
