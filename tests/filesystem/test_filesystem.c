/* Wire/ownership, hostile guest ranges and persistent snapshot corruption. */
#include "ctr_fs.h"
#include "ctr_service_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define DATA UINT32_C(0x08000000)
static unsigned assertions;
#define CHECK(x) do { ++assertions;if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1); } } while(0)
static uint32_t word(ctr_guest *g,unsigned i) {
    uint32_t n;CHECK(ctr_memory_read(&g->memory,g->cpu.tls_address+0x80+i*4,4,CTR_MEM_READ,&n)==CTR_MEM_OK);return n;
}
static void send(ctr_guest *g,uint32_t h,const uint32_t *w,unsigned n) {
    for(unsigned i=0;i<n;++i)CHECK(ctr_memory_write(&g->memory,g->cpu.tls_address+0x80+i*4,4,w[i])==CTR_MEM_OK);
    g->cpu.r[0]=h;ctr_guest_svc(g,0x32);
}
static void path(ctr_guest *g,const char *p) {
    for(unsigned i=0;i<=strlen(p);++i)CHECK(ctr_memory_write(&g->memory,DATA+128+i,1,(unsigned char)p[i])==CTR_MEM_OK);
}
static uint32_t fs(ctr_guest *g) {
    uint32_t h=ctr_guest_session_open(g,CTR_SERVICE_FS);CHECK(h);
    const uint32_t w[]={0x08010002,0x20,0};send(g,h,w,3);CHECK(word(g,1)==0);return h;
}
static uint32_t mount(ctr_guest *g,uint32_t h,uint32_t id) {
    CHECK(ctr_memory_write(&g->memory,DATA+100,1,0)==CTR_MEM_OK);
    const uint32_t w[]={0x080c00c2,id,1,1,0x4002,DATA+100};send(g,h,w,6);
    CHECK(g->state==CTR_GUEST_READY && word(g,1)==0 && word(g,0)==0x080c00c0 && word(g,3)==0);return word(g,2);
}
static uint32_t open_file(ctr_guest *g,uint32_t h,uint32_t ar,unsigned flags) {
    const uint32_t w[]={0x080201c2,0,ar,0,3,10,flags,0,(10u<<14)|2,DATA+128};send(g,h,w,10);
    CHECK(g->state==CTR_GUEST_READY && word(g,1)==0 && word(g,2)==0x10);return word(g,3);
}
static bool persist(void *context,const ctr_fs_volume *v) { CHECK(v->dirty);++*(unsigned *)context;return true; }
static void lifecycle(void) {
    ctr_guest g;ctr_fs_volume v;CHECK(ctr_service_fixture_open(&g));CHECK(ctr_fs_volume_init(&v,4,true));CHECK(ctr_fs_mount(&g,&v));CHECK(!ctr_fs_mount(&g,&v));
    unsigned commits=0;v.persist=persist;v.persist_context=&commits;
    uint32_t h=fs(&g),ar=mount(&g,h,4);path(&g,"/save.dat");uint32_t file=open_file(&g,h,ar,7);CHECK(v.files[0].references==1 && !ctr_fs_volume_close(&v));
    const uint32_t size0[]={0x08040000};send(&g,file,size0,1);CHECK(word(&g,0)==0x080400c0 && word(&g,2)==0);
    CHECK(ctr_memory_write(&g.memory,DATA+256,4,0x44332211)==CTR_MEM_OK);
    uint32_t write[]={0x08030102,3,0,4,0,0x4a,DATA+256};send(&g,file,write,7);CHECK(word(&g,1)==0 && word(&g,2)==4);
    const uint8_t expected[]={0,0,0,0x11,0x22,0x33,0x44};CHECK(v.files[0].size==7 && !memcmp(v.files[0].bytes,expected,7));
    uint32_t read[]={0x080200c2,0,0,12,0xcc,DATA+300};send(&g,file,read,6);CHECK(word(&g,1)==0 && word(&g,2)==7 && word(&g,3)==0xcc && word(&g,4)==DATA+300);
    for(unsigned i=0;i<7;++i) { uint32_t n;CHECK(ctr_memory_read(&g.memory,DATA+300+i,1,CTR_MEM_READ,&n)==CTR_MEM_OK && n==expected[i]); }
    read[1]=UINT32_MAX;read[2]=UINT32_MAX;send(&g,file,read,6);CHECK(word(&g,1)==0 && word(&g,2)==0); /* 64-bit EOF, no truncation. */
    const uint32_t deletion[]={0x08040142,0,ar,0,3,10,0x28002,DATA+128};send(&g,h,deletion,8);CHECK(word(&g,1)==CTR_FS_BUSY);
    const uint32_t close_ar[]={0x080e0080,ar,0};send(&g,h,close_ar,3);CHECK(word(&g,1)==0);
    send(&g,file,size0,1);CHECK(word(&g,2)==7); /* Open files survive archive closure. */
    uint32_t next=mount(&g,h,4);CHECK(next!=ar);send(&g,h,close_ar,3);CHECK(word(&g,1)==CTR_FS_INVALID);
    const uint32_t shrink[]={0x08050080,2,0};send(&g,file,shrink,3);CHECK(word(&g,1)==0 && v.files[0].size==2);
    const uint32_t grow[]={0x08050080,6,0};send(&g,file,grow,3);CHECK(word(&g,1)==0 && v.files[0].size==6);
    for(unsigned i=0;i<6;++i)CHECK(!v.files[0].bytes[i]);
    const uint32_t huge[]={0x08050080,0,1};send(&g,file,huge,3);CHECK(word(&g,1)==CTR_FS_FULL && v.files[0].size==6);
    const uint32_t commit[]={0x080d0144,next,0,0,0,0,0xa,0,0xc,0};send(&g,h,commit,10);CHECK(word(&g,1)==0 && commits==1 && v.commits==1 && !v.dirty);
    g.cpu.r[1]=file;ctr_guest_svc(&g,0x27);uint32_t duplicate=g.cpu.r[1];CHECK(g.cpu.r[0]==0 && duplicate!=file && v.files[0].references==1);
    const uint32_t close_file[]={0x08080000};send(&g,file,close_file,1);CHECK(word(&g,1)==0 && !v.files[0].references);
    send(&g,duplicate,size0,1);CHECK(word(&g,1)==CTR_FS_INVALID);CHECK(ctr_guest_handle_close(&g,file));CHECK(ctr_guest_handle_close(&g,duplicate));
    uint32_t delete2[]={0x08040142,0,next,0,3,10,0x28002,DATA+128};send(&g,h,delete2,8);CHECK(word(&g,1)==0 && !v.files[0].active);
    uint32_t second=open_file(&g,h,next,7);CHECK(second!=file && v.files[0].references==1);
    CHECK(ctr_guest_handle_close(&g,h));send(&g,second,size0,1);CHECK(word(&g,1)==0);ctr_guest_close(&g);CHECK(!v.files[0].references && ctr_fs_volume_close(&v));
}
static void rejection(void) {
    for(unsigned kind=0;kind<14;++kind) {
        ctr_guest g;ctr_fs_volume v;CHECK(ctr_service_fixture_open(&g));CHECK(ctr_fs_volume_init(&v,4,true));CHECK(ctr_fs_mount(&g,&v));
        uint32_t h=fs(&g),ar=mount(&g,h,4);path(&g,"/save.dat");uint32_t file=open_file(&g,h,ar,7);
        uint32_t w[]={0x08030102,0,0,4,0,0x4a,DATA+256};
        if(kind==0)w[5]=0x4c;
        if(kind==1)w[5]=0x5a;
        if(kind==2)w[4]=0x100; /* Timestamp updates are not implemented. */
        if(kind==3)w[3]=CTR_FS_MAX_FILE_BYTES+1;
        if(kind==4)w[6]=0xdead0000;
        if(kind==5)w[6]=0xfffffffdu;
        if(kind==6) { w[0]=0x080200c2;w[4]=0x4c;w[5]=0x100000; } /* Output RX. */
        if(kind==7)w[6]=g.tls_address+0x84;
        if(kind==8) {
            CHECK(ctr_memory_map(&g.memory,0x09000000,256,CTR_MEM_READ|CTR_MEM_WRITE,g.storage[4]+0x80,256)==CTR_MEM_OK);w[6]=0x09000000;
        }
        if(kind==9)w[0]=0x08030103;
        if(kind==10)w[0]=0x08070040; /* File attributes absent. */
        if(kind==11) { w[6]=DATA+4094; } /* Two mapped bytes then a hole. */
        if(kind==12) { w[0]=0x080200c2;w[4]=0x4c;w[5]=DATA+4094; }
        if(kind==13) { w[0]=0x080200c2;w[4]=0x4c;w[5]=g.tls_address+0x84; }
        /* Preserve input TLS bytes, file contents and the memory generation. */
        for(unsigned i=0;i<7;++i)CHECK(ctr_memory_write(&g.memory,g.tls_address+0x80+i*4,4,w[i])==CTR_MEM_OK);
        uint8_t before[256];memcpy(before,g.storage[4]+0x80,256);uint64_t generation=g.memory.generation;
        g.cpu.r[0]=file;ctr_guest_svc(&g,0x32);
        CHECK(g.state==CTR_GUEST_FAULTED && !v.files[0].size && v.files[0].references==1);
        CHECK(!memcmp(before,g.storage[4]+0x80,256) && generation==g.memory.generation);
        ctr_guest_close(&g);CHECK(ctr_fs_volume_close(&v));
    }
    const char *bad[]={"/../x","/..","/.","relative","/a/b","/a\\b","/a:b","/a\x01","/\xc0\x80","/\xed\xa0\x80","/\xf4\x90\x80\x80"};
    ctr_fs_volume v;CHECK(ctr_fs_volume_init(&v,4,true));for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i)CHECK(ctr_fs_seed(&v,bad[i],NULL,0)==CTR_FS_INVALID);CHECK(ctr_fs_volume_close(&v));
}
static void snapshots(void) {
    ctr_fs_volume v,next;CHECK(ctr_fs_volume_init(&v,4,true));CHECK(ctr_fs_seed(&v,"/記録.dat","original",8)==0);CHECK(ctr_fs_seed(&v,"/empty",NULL,0)==0);
    uint8_t *p;size_t n;CHECK(ctr_fs_export(&v,&p,&n));CHECK(!memcmp(p,"CTRFS001\x04\0\0\0\x02\0\0\0",16));
    CHECK(ctr_fs_volume_init(&next,4,true));CHECK(ctr_fs_import(&next,p,n));CHECK(!next.dirty && !strcmp(next.files[0].path,"/記録.dat") && next.files[0].size==8 && !memcmp(next.files[0].bytes,"original",8));CHECK(!ctr_fs_import(&next,p,n));CHECK(ctr_fs_volume_close(&next));
    for(size_t i=0;i<n;++i) {
        CHECK(ctr_fs_volume_init(&next,4,true));p[i]^=1;CHECK(!ctr_fs_import(&next,p,n) && !next.files[0].active);p[i]^=1;CHECK(ctr_fs_volume_close(&next));
    }
    for(size_t i=0;i<n;++i) { CHECK(ctr_fs_volume_init(&next,4,true));CHECK(!ctr_fs_import(&next,p,i));CHECK(ctr_fs_volume_close(&next)); }
    CHECK(ctr_fs_volume_init(&next,9,true));CHECK(!ctr_fs_import(&next,p,n));CHECK(ctr_fs_volume_close(&next));
    free(p);CHECK(ctr_fs_volume_close(&v));
}
int main(void) {
    lifecycle();rejection();snapshots();
    printf("{\"ok\":true,\"assertions\":%u,\"bounded_private_filesystem\":true,\"console_verified\":false}\n",assertions);return 0;
}
