#include "ctr_apt.h"
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
static void output(ctr_guest *g,uint32_t size,uint32_t at) {
    CHECK(ctr_memory_write(&g->memory,g->cpu.tls_address+0x180,4,(size<<14)|2)==CTR_MEM_OK);
    CHECK(ctr_memory_write(&g->memory,g->cpu.tls_address+0x184,4,at)==CTR_MEM_OK);
}
static uint32_t init(ctr_guest *g,uint32_t *lock,uint32_t events[2]) {
    CHECK(ctr_service_fixture_open(g));CHECK(ctr_apt_configure(g));CHECK(!ctr_apt_configure(g));
    uint32_t h=ctr_guest_session_open(g,CTR_SERVICE_APT);CHECK(h);
    const uint32_t getlock[]={0x00010040,0};send(g,h,getlock,2);
    CHECK(g->state==CTR_GUEST_READY && word(g,0)==0x000100c2 && word(g,1)==0);*lock=word(g,5);CHECK(ctr_guest_handle_for(g,*lock)->type==CTR_HANDLE_MUTEX);
    const uint32_t initialize[]={0x00020080,0x300,0};send(g,h,initialize,3);CHECK(word(g,0)==0x00020043 && word(g,1)==0 && word(g,2)==0x04000000);
    events[0]=word(g,3);events[1]=word(g,4);CHECK(events[0]!=events[1] && ctr_guest_handle_for(g,events[1])->type==CTR_HANDLE_EVENT);return h;
}
static void lifecycle(void) {
    ctr_guest g;uint32_t lock,events[2],h=init(&g,&lock,events);
    const uint32_t enable[]={0x00030040,0},notify[]={0x00430040,0x300},glance[]={0x000e0080,0x300,8},receive[]={0x000d0080,0x300,8};
    send(&g,h,enable,2);CHECK(word(&g,1)==0 && g.apt.pending && g.events[g.apt.events[1]].signaled);
    output(&g,8,DATA+128);send(&g,h,receive,3);CHECK(word(&g,1)!=0 && g.apt.pending); /* Need NotifyToWait. */
    send(&g,h,notify,2);CHECK(word(&g,1)==0);
    CHECK(ctr_memory_write(&g.memory,DATA+128,4,0x44332211)==CTR_MEM_OK);
    for(unsigned i=0;i<2;++i) { send(&g,h,glance,3);CHECK(word(&g,1)==0 && word(&g,2)==0 && word(&g,3)==1 && word(&g,4)==0 && word(&g,6)==0 && g.apt.pending); }
    send(&g,h,receive,3);CHECK(word(&g,1)==0 && !g.apt.pending && !g.events[g.apt.events[1]].signaled);
    uint32_t value;CHECK(ctr_memory_read(&g.memory,DATA+128,4,CTR_MEM_READ,&value)==CTR_MEM_OK && value==0x44332211); /* Empty wakeup does not write output. */
    send(&g,h,receive,3);CHECK(word(&g,1)!=0);
    send(&g,h,enable,2);CHECK(word(&g,1)!=0 && !g.apt.pending);
    const uint32_t model[]={0x01020000};send(&g,h,model,1);CHECK(word(&g,1)==0 && word(&g,2)==0);
    output(&g,1,DATA+132);CHECK(ctr_memory_write(&g.memory,DATA+140,4,0x10)==CTR_MEM_OK);
    const uint32_t unlock[]={0x004b00c2,7,4,1,0x10402,DATA+140};send(&g,h,unlock,6);CHECK(word(&g,1)==0 && word(&g,2)==0 && !g.apt.transitions);
    const uint32_t final[]={0x00040040,0x300};send(&g,h,final,2);CHECK(word(&g,1)==0 && !g.apt.initialized);
    CHECK(g.events[ctr_guest_handle_for(&g,events[0])->object].active); /* Copied events survive unregister. */
    for(unsigned i=0;i<2;++i)CHECK(ctr_guest_handle_close(&g,events[i]));
    CHECK(ctr_guest_handle_close(&g,lock));CHECK(g.mutexes[g.apt.mutex].active); /* Manager retains lock across ephemeral sessions. */
    CHECK(ctr_guest_handle_close(&g,h));h=ctr_guest_session_open(&g,CTR_SERVICE_APT);
    const uint32_t getlock[]={0x00010040,0};send(&g,h,getlock,2);lock=word(&g,5);CHECK(word(&g,1)==0);
    const uint32_t initialize[]={0x00020080,0x300,0};send(&g,h,initialize,3);CHECK(word(&g,1)==0);
    events[0]=word(&g,3);events[1]=word(&g,4);
    send(&g,h,enable,2);send(&g,h,notify,2);output(&g,8,DATA+128);send(&g,h,receive,3);
    const uint32_t prepare[]={0x00220040,1},close[]={0x00270044,0,0,0,2,0};send(&g,h,prepare,2);CHECK(word(&g,1)==0 && g.apt.closing);
    send(&g,h,close,6);CHECK(g.state==CTR_GUEST_EXITED && g.process_exited);ctr_guest_close(&g);
}
static void rejection(void) {
    for(unsigned kind=0;kind<14;++kind) {
        ctr_guest g;uint32_t lock,events[2],h=init(&g,&lock,events);
        output(&g,1,DATA+132);CHECK(ctr_memory_write(&g.memory,DATA+140,4,0x10)==CTR_MEM_OK);
        uint32_t w[]={0x004b00c2,7,4,1,0x10402,DATA+140};
        if(kind==0)w[1]=99;
        if(kind==1)w[2]=3;
        if(kind==2)w[3]=2;
        if(kind==3)w[4]=0x10002;
        if(kind==4)w[5]=0xdead0000;
        if(kind==5)w[5]=g.tls_address+0x80;
        if(kind==6)output(&g,1,g.tls_address+0x84);
        if(kind==7)output(&g,1,0x100000);
        if(kind==8)output(&g,1,0xdead0000);
        if(kind==9) { CHECK(ctr_memory_map(&g.memory,0x09000000,256,3,g.storage[4]+0x80,256)==CTR_MEM_OK);output(&g,1,0x09000000); }
        if(kind==10)w[0]=0x004b00c1;
        if(kind==11)w[0]=0x004e0000; /* Hardware reset is unavailable. */
        if(kind==12)w[0]=0x00310000; /* Cross-application jumps are unavailable. */
        if(kind==13)CHECK(ctr_memory_write(&g.memory,DATA+140,4,0x20)==CTR_MEM_OK);
        for(unsigned i=0;i<6;++i)CHECK(ctr_memory_write(&g.memory,g.tls_address+0x80+i*4,4,w[i])==CTR_MEM_OK);
        uint8_t before[256];memcpy(before,g.storage[4]+0x80,256);uint64_t generation=g.memory.generation;
        g.cpu.r[0]=h;ctr_guest_svc(&g,0x32);CHECK(g.state==CTR_GUEST_FAULTED && g.apt.initialized && !g.apt.enabled);
        CHECK(!memcmp(before,g.storage[4]+0x80,256) && generation==g.memory.generation);ctr_guest_close(&g);
    }
    /* Capacity failure rolls back retained objects; no forged event handles. */
    ctr_guest g;CHECK(ctr_service_fixture_open(&g));CHECK(ctr_apt_configure(&g));uint32_t h=ctr_guest_session_open(&g,CTR_SERVICE_APT);
    for(unsigned i=0;i<63;++i)CHECK(ctr_guest_handle_new(&g,CTR_HANDLE_SESSION,0));
    const uint32_t getlock[]={0x00010040,0};send(&g,h,getlock,2);CHECK(word(&g,1)!=0 && !g.apt.lock);
    for(unsigned i=0;i<16;++i)CHECK(!g.mutexes[i].active);
    ctr_guest_close(&g);
}
int main(void) { lifecycle();rejection();printf("{\"ok\":true,\"assertions\":%u,\"standalone_apt_lifecycle\":true,\"console_verified\":false}\n",assertions);return 0; }
