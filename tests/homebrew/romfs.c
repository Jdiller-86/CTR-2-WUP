/* Original asset-reader application through normal CRT/libctru/newlib RomFS. */
#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
u32 __ctru_heap_size=2*1024*1024;
u32 __ctru_linear_heap_size=2*1024*1024;
static int fail(const char *message) { svcOutputDebugString(message,strlen(message));return 1; }
int main(void) {
    if(R_FAILED(romfsInit()))return fail("CTR2WUP RomFS FAIL: mount");
    static const char root[]="CTR2WUP assets\n",japanese[]="read-only fixture\n";
    unsigned char bytes[64];FILE *file=fopen("romfs:/readme.txt","rb");
    if(!file || fread(bytes,1,sizeof(bytes),file)!=sizeof(root)-1 || memcmp(bytes,root,sizeof(root)-1) || fclose(file))return fail("CTR2WUP RomFS FAIL: root read");
    file=fopen("romfs:/levels/config.bin","rb");
    if(!file || fseek(file,7,SEEK_SET) || fread(bytes,1,4,file)!=4 || memcmp(bytes,"\7\10\11\12",4) ||
       fseek(file,-2,SEEK_END) || fgetc(file)!=14 || fgetc(file)!=15 || fgetc(file)!=EOF || fclose(file))return fail("CTR2WUP RomFS FAIL: nested seek/EOF");
    file=fopen("romfs:/levels/設定.txt","rb");
    if(!file || fread(bytes,1,sizeof(bytes),file)!=sizeof(japanese)-1 || memcmp(bytes,japanese,sizeof(japanese)-1) || fclose(file))return fail("CTR2WUP RomFS FAIL: UTF-16 path");
    DIR *directory=opendir("romfs:/levels");if(!directory)return fail("CTR2WUP RomFS FAIL: directory open");
    unsigned found=0;struct dirent *entry;
    while((entry=readdir(directory))) {
        if(!strcmp(entry->d_name,"config.bin"))found|=1;
        if(!strcmp(entry->d_name,"設定.txt"))found|=2;
    }
    if(closedir(directory) || found!=3)return fail("CTR2WUP RomFS FAIL: enumeration");
    file=fopen("romfs:/readme.txt","wb");if(file) { fclose(file);return fail("CTR2WUP RomFS FAIL: writable asset"); }
    if(R_FAILED(romfsExit()))return fail("CTR2WUP RomFS FAIL: unmount");
    static const char pass[]="CTR2WUP compiled RomFS PASS";svcOutputDebugString(pass,sizeof(pass)-1);return 0;
}
