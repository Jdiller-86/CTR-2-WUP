/* Original save/read/reload application, normal CRT/libctru/newlib startup. */
#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
u32 __ctru_heap_size=2*1024*1024;
u32 __ctru_linear_heap_size=2*1024*1024;
static int fail(const char *stage) { svcOutputDebugString(stage,strlen(stage));return 1; }
int main(void) {
    FS_Path empty=fsMakePath(PATH_EMPTY,"");
    if(R_FAILED(archiveMount(ARCHIVE_SAVEDATA,empty,"save")))return fail("CTR2WUP filesystem FAIL: archive mount");
    unsigned char record[12]={'C','T','R','2','S','A','V','E',0,0,0,0};
    u32 visits=0;
    FILE *file=fopen("save:/progress.dat","rb");
    if(file) {
        unsigned char old[12];
        if(fread(old,1,12,file)!=12 || fgetc(file)!=EOF || fclose(file) || memcmp(old,record,8))return fail("CTR2WUP filesystem FAIL: prior record");
        visits=(u32)old[8]|(u32)old[9]<<8|(u32)old[10]<<16|(u32)old[11]<<24;
        if(!visits || visits==0xffffffffu)return fail("CTR2WUP filesystem FAIL: prior counter");
    }
    ++visits;for(unsigned i=0;i<4;++i)record[8+i]=(unsigned char)(visits>>(8*i));
    file=fopen("save:/progress.dat","wb+");if(!file)return fail("CTR2WUP filesystem FAIL: progress open");
    if(fwrite(record,1,12,file)!=12 || fflush(file) || fseek(file,0,SEEK_SET))return fail("CTR2WUP filesystem FAIL: progress write/seek");
    unsigned char check[16];memset(check,0xa5,sizeof(check));
    if(fread(check,1,16,file)!=12 || memcmp(check,record,12) || check[12]!=0xa5 || fseek(file,0,SEEK_END) || ftell(file)!=12 || fclose(file))return fail("CTR2WUP filesystem FAIL: progress verify");
    /* UTF-16 paths emitted by devoptab include an original Japanese filename. */
    file=fopen("save:/記録.dat","wb");if(!file)return fail("CTR2WUP filesystem FAIL: UTF-16 open");
    if(fwrite(record,1,12,file)!=12 || fclose(file))return fail("CTR2WUP filesystem FAIL: UTF-16 write");
    file=fopen("save:/temporary.dat","wb");if(!file || fputs("remove",file)<0 || fclose(file) || unlink("save:/temporary.dat"))return fail("CTR2WUP filesystem FAIL: temporary file");
    FS_Archive archive;
    if(R_FAILED(FSUSER_OpenArchive(&archive,ARCHIVE_SAVEDATA,empty)) ||
       R_FAILED(FSUSER_ControlArchive(archive,ARCHIVE_ACTION_COMMIT_SAVE_DATA,NULL,0,NULL,0)) ||
       R_FAILED(FSUSER_CloseArchive(archive)))return fail("CTR2WUP filesystem FAIL: commit");
    if(archiveUnmount("save")<0)return fail("CTR2WUP filesystem FAIL: unmount");
    static const char first[]="CTR2WUP compiled filesystem SAVE 1 PASS";
    static const char second[]="CTR2WUP compiled filesystem RELOAD PASS";
    svcOutputDebugString(visits==1?first:second,visits==1?sizeof(first)-1:sizeof(second)-1);
    return 0;
}
