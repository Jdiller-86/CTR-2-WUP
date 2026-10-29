#define _POSIX_C_SOURCE 200809L
#include "../test_platform.h"
#include "retained_logs.h"
#include <sys/stat.h>
#include <fcntl.h>
#define CHECK(x) do { if(!(x)){fprintf(stderr,"retained logs line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
int main(void) {
    char root[]="/tmp/ctr-retained-XXXXXX",src[600],dest[600],file[700],copy[700];CHECK(mkdtemp(root)!=NULL);
    snprintf(src,sizeof(src),"%s/source",root);snprintf(dest,sizeof(dest),"%s/copy",root);
    ctr_retained_logs r=ctr_retained_logs_copy(src,dest);CHECK(!r.present && !r.complete && !r.error);
    CHECK(mkdir(src,0777)==0);snprintf(file,sizeof(file),"%s/log.txt",src);int fd=open(file,O_WRONLY|O_CREAT|O_EXCL,0600);CHECK(fd>=0);FILE *f=fdopen(fd,"wb");CHECK(f);
    CHECK(fwrite("system retained data",1,20,f)==20 && fclose(f)==0);
    r=ctr_retained_logs_copy(src,dest);CHECK(r.present && r.complete && r.files==1 && r.bytes==20);
    r=ctr_retained_logs_copy(src,dest);CHECK(r.present && !r.complete && r.error);
    snprintf(copy,sizeof(copy),"%s/log.txt",dest);struct stat permissions;CHECK(stat(copy,&permissions)==0 && (permissions.st_mode&0777)==0600);
    CHECK(stat(dest,&permissions)==0 && (permissions.st_mode&0777)==0700);CHECK(ctr_test_unlink(copy)==0 && ctr_test_remove_directory(dest)==0);
    CHECK(ctr_test_unlink(file)==0);CHECK(symlink("/etc/passwd",file)==0);
    r=ctr_retained_logs_copy(src,dest);CHECK(r.present && !r.complete && r.files==0 && r.error);
    CHECK(ctr_test_unlink(file)==0 && ctr_test_remove_directory(dest)==0 && ctr_test_remove_directory(src)==0 && ctr_test_remove_directory(root)==0);
    puts("{\"ok\":true,\"retained_sd_logs\":true,\"no_slc_access\":true}");return 0;
}
