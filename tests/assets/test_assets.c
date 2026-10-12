#define _POSIX_C_SOURCE 200809L
#include "ctr_romfs_file.h"
#include "ctr_sha256.h"
#include "ctr_guest.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "../test_platform.h"

static unsigned checks;
#define CHECK(x) do { ++checks;if(!(x)) { fprintf(stderr,"line %u: %s\n",__LINE__,#x);exit(1); } } while(0)
static void hex(uint8_t *output,const char *text) {
    for(unsigned i=0;i<32;++i) { unsigned byte;CHECK(sscanf(text+i*2,"%2x",&byte)==1);output[i]=(uint8_t)byte; }
}
static void vector(const char *input,const char *expected) {
    uint8_t golden[32],digest[32];hex(golden,expected);
    for(unsigned chunk=1;chunk<=67;++chunk) {
        ctr_sha256 c;ctr_sha256_init(&c);size_t n=strlen(input);
        for(size_t at=0;at<n;at+=chunk)CHECK(ctr_sha256_update(&c,input+at,n-at<chunk?n-at:chunk));
        CHECK(ctr_sha256_update(&c,NULL,0));CHECK(ctr_sha256_final(&c,digest));CHECK(!memcmp(golden,digest,32));
        CHECK(!ctr_sha256_final(&c,digest));CHECK(!ctr_sha256_update(&c,input,1));
    }
}
static void sha_tests(void) {
    vector("","e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    vector("abc","ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    vector("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq","248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    vector("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu","cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
    ctr_sha256 c;uint8_t digest[32],expected[32],bytes[1000];memset(bytes,'a',sizeof(bytes));ctr_sha256_init(&c);
    for(unsigned i=0;i<1000;++i)CHECK(ctr_sha256_update(&c,bytes,sizeof(bytes)));
    CHECK(ctr_sha256_final(&c,digest));hex(expected,"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");CHECK(!memcmp(expected,digest,32));
    ctr_sha256_init(&c);c.bytes=UINT64_MAX/8;CHECK(!ctr_sha256_update(&c,bytes,1));CHECK(c.bytes==UINT64_MAX/8);
    CHECK(!ctr_sha256_update(NULL,bytes,1));CHECK(!ctr_sha256_update(&c,NULL,1));CHECK(!ctr_sha256_final(&c,NULL));
}
static void file_tests(void) {
    char directory[384],path[512];CHECK(ctr_test_temp_directory(directory,sizeof(directory),"ctr2wup-assets"));
    CHECK(snprintf(path,sizeof(path),"%s/asset.bin",directory)>0);
    uint8_t bytes[8193],expected[32],got[64];for(unsigned i=0;i<sizeof(bytes);++i)bytes[i]=(uint8_t)(37*i+19);
    FILE *output=fopen(path,"wbx");CHECK(output && fwrite(bytes,1,sizeof(bytes),output)==sizeof(bytes));CHECK(fclose(output)==0);
    ctr_sha256 c;ctr_sha256_init(&c);CHECK(ctr_sha256_update(&c,bytes,sizeof(bytes)));CHECK(ctr_sha256_final(&c,expected));
    ctr_romfs_file file={0};CHECK(ctr_romfs_file_open(&file,path,expected));CHECK(file.size==sizeof(bytes));
    CHECK(file.view.read(file.view.context,4093,got,sizeof(got)));CHECK(!memcmp(got,bytes+4093,sizeof(got)));
    CHECK(file.view.read(file.view.context,sizeof(bytes),NULL,0));CHECK(!file.view.read(file.view.context,sizeof(bytes),got,1));
    CHECK(!file.view.read(file.view.context,UINT64_MAX,got,1));CHECK(!file.view.read(file.view.context,0,NULL,1));
    CHECK(!ctr_romfs_file_open(&file,path,expected));
    ctr_guest guest={0};CHECK(ctr_fs_mount_romfs(&guest,&file.view));CHECK(file.view.mounts==1);
    CHECK(!ctr_romfs_file_close(&file) && file.file);ctr_guest_close(&guest);CHECK(!file.view.mounts);
    file.view.references=1;CHECK(!ctr_romfs_file_close(&file));file.view.references=0;
    CHECK(ctr_romfs_file_close(&file));CHECK(ctr_romfs_file_close(&file));
    expected[0]^=1;CHECK(!ctr_romfs_file_open(&file,path,expected));CHECK(!file.file && !file.view.read);
    expected[0]^=1;CHECK(ctr_test_unlink(path)==0);CHECK(!ctr_romfs_file_open(&file,path,expected));
    CHECK(!ctr_romfs_file_open(NULL,path,expected));CHECK(!ctr_romfs_file_open(&file,path,NULL));
    CHECK(ctr_test_remove_directory(directory)==0);
}
int main(void) {
    sha_tests();file_tests();
    printf("{\"ok\":true,\"assertions\":%u,\"sha256_vectors\":true,\"file_backed_romfs\":true,\"console_verified\":false}\n",checks);
    return 0;
}
