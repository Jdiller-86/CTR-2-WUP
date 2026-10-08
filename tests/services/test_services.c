/* Independent wire-format assertions and malformed-input ownership tests. */
#include "ctr_service_fixture.h"
#include "ctr_ipc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned assertions;
#define CHECK(x) do { ++assertions;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
#define DATA UINT32_C(0x08000000)
#define INVALID UINT32_C(0xd8e007f7)
static uint32_t read_word(ctr_guest *g,unsigned word) {
    uint32_t value;
    CHECK(ctr_memory_read(&g->memory,g->cpu.tls_address+0x80+word*4,4,CTR_MEM_READ,&value)==CTR_MEM_OK);
    return value;
}
static void write_words(ctr_guest *g,const uint32_t *words,unsigned count) {
    for (unsigned i=0;i<count;++i)
        CHECK(ctr_memory_write(&g->memory,g->cpu.tls_address+0x80+i*4,4,words[i])==CTR_MEM_OK);
}
static void send(ctr_guest *g,uint32_t handle,const uint32_t *words,unsigned count) {
    write_words(g,words,count);g->cpu.r[0]=handle;ctr_guest_svc(g,0x32);
}
static uint32_t connect(ctr_guest *g) {
    g->cpu.r[1]=DATA;
    CHECK(ctr_guest_svc(g,0x2d)==CTR_GUEST_READY && g->cpu.r[0]==0);
    CHECK(ctr_guest_session_for(g,g->cpu.r[1])!=NULL);
    return g->cpu.r[1];
}
static uint32_t directory(ctr_guest *g) {
    uint32_t handle=connect(g),request[]={0x00010002,0x20,0xdeadbeef};
    send(g,handle,request,3);
    CHECK(g->state==CTR_GUEST_READY && g->cpu.r[0]==0 && read_word(g,0)==0x00010040 && read_word(g,1)==0);
    return handle;
}
static uint32_t cfg(ctr_guest *g,uint32_t srv) {
    const uint32_t request[]={0x00050100,0x3a676663,0x75,5,1};
    send(g,srv,request,5);
    CHECK(g->state==CTR_GUEST_READY && g->cpu.r[0]==0);
    CHECK(read_word(g,0)==0x00050042 && read_word(g,1)==0 && read_word(g,2)==0x10);
    uint32_t handle=read_word(g,3);
    CHECK(ctr_guest_session_for(g,handle)->service==CTR_SERVICE_CFG);return handle;
}
static unsigned sessions(const ctr_guest *g) {
    unsigned count=0;for(unsigned i=0;i<16;++i)count+=g->sessions[i].active;return count;
}
static void ownership_and_configuration(void) {
    ctr_guest g;CHECK(ctr_service_fixture_open(&g));
    CHECK(g.cpu.tls_address==g.tls_address);
    CHECK(!ctr_guest_configure(&g,(ctr_guest_configuration){7,1,0}));
    CHECK(!ctr_guest_configure(&g,(ctr_guest_configuration){1,12,0}));
    CHECK(!ctr_guest_configure(&g,(ctr_guest_configuration){1,1,2}));
    CHECK(!ctr_guest_configure(&g,(ctr_guest_configuration){1,1,4}));
    CHECK(!ctr_guest_configure(&g,(ctr_guest_configuration){1,1,5}));
    CHECK(ctr_guest_configure(&g,(ctr_guest_configuration){2,7,3}));
    uint32_t srv=directory(&g),service=cfg(&g,srv);
    CHECK(!ctr_guest_configure(&g,(ctr_guest_configuration){1,1,0}));
    for (unsigned i=0;i<3;++i) {
        uint32_t request[]={i==0?0x00020000:i==1?0x00050000:0x00060000};
        send(&g,service,request,1);
        CHECK(g.state==CTR_GUEST_READY && g.cpu.r[0]==0);
        CHECK(read_word(&g,0)==(request[0]|0x80) && read_word(&g,1)==0);
        CHECK(read_word(&g,2)==(i==2?0:i==1?3:2));
    }
    const uint32_t language[]={0x00010082,1,0x000a0002,0x1e,DATA+32};
    CHECK(ctr_memory_write(&g.memory,DATA+32,4,0xaabbccdd)==CTR_MEM_OK);
    send(&g,service,language,5);
    uint32_t value;
    CHECK(ctr_memory_read(&g.memory,DATA+32,4,CTR_MEM_READ,&value)==CTR_MEM_OK && value==0xaabbcc07);
    CHECK(read_word(&g,0)==0x00010042 && read_word(&g,2)==0x1e && read_word(&g,3)==DATA+32);
    g.cpu.r[1]=service;ctr_guest_svc(&g,0x27);uint32_t duplicate=g.cpu.r[1];
    CHECK(g.cpu.r[0]==0 && duplicate!=service);
    CHECK(ctr_guest_session_for(&g,service)==ctr_guest_session_for(&g,duplicate));
    g.cpu.r[0]=service;ctr_guest_svc(&g,0x23);CHECK(g.cpu.r[0]==0);
    CHECK(!ctr_guest_session_for(&g,service) && ctr_guest_session_for(&g,duplicate));
    const uint32_t model[]={0x00050000};send(&g,duplicate,model,1);CHECK(read_word(&g,2)==3);
    for(unsigned svc=0x18;svc<=0x19;++svc) {
        g.cpu.r[0]=duplicate;ctr_guest_svc(&g,svc);CHECK(g.cpu.r[0]==INVALID);
    }
    g.cpu.r[0]=duplicate;g.cpu.r[2]=g.cpu.r[3]=0;ctr_guest_svc(&g,0x24);CHECK(g.cpu.r[0]==INVALID);
    g.cpu.r[0]=duplicate;ctr_guest_svc(&g,0x23);CHECK(g.cpu.r[0]==0 && sessions(&g)==1);
    send(&g,duplicate,model,1);CHECK(g.state==CTR_GUEST_READY && g.cpu.r[0]==INVALID);
    CHECK(g.ipc_header==0 && g.ipc_result==INVALID);
    uint32_t replacement=cfg(&g,srv);CHECK(replacement!=duplicate && replacement!=service);
    g.cpu.r[0]=srv;ctr_guest_svc(&g,0x23);CHECK(g.cpu.r[0]==0);
    send(&g,replacement,model,1);CHECK(read_word(&g,2)==3); /* Service survives directory closure. */
    g.cpu.r[0]=replacement;ctr_guest_svc(&g,0x23);CHECK(sessions(&g)==0);
    CHECK(g.state==CTR_GUEST_READY);ctr_guest_close(&g);
}
static void rejection_tests(void) {
    for(unsigned kind=0;kind<14;++kind) {
        ctr_guest g;CHECK(ctr_service_fixture_open(&g));uint32_t srv=directory(&g),service=cfg(&g,srv);
        uint32_t request[]={0x00010082,1,0x000a0002,0x1c,DATA+32};
        if(kind==0)request[3]=0x1a; /* Read-only descriptor. */
        if(kind==1)request[3]=0x2c; /* Declared size differs. */
        if(kind==2)request[1]=2;
        if(kind==3)request[2]=0x000a0000; /* Other configuration blocks remain unsupported. */
        if(kind==4)request[4]=0x00100000; /* RX code. */
        if(kind==5)request[4]=0xdead0000;
        if(kind==6)request[4]=g.tls_address+0x84; /* Output may not overwrite IPC. */
        if(kind==7)request[0]=0x00010081; /* Truncated translated descriptor. */
        if(kind==8)request[0]=0x00020fff; /* 127 words exceed the TLS buffer. */
        if(kind==9)request[0]=0x00021000; /* Reserved header bits. */
        if(kind==10)request[0]=0x00030000; /* Unique-console hash not implemented. */
        if(kind==11)request[0]=0x00050001; /* Unexpected translated data. */
        if(kind==12) { request[3]=0x1e;g.memory.maps[1].permissions=CTR_MEM_WRITE; } /* RW needs both rights. */
        if(kind==13) {
            request[4]=0x09000000;
            CHECK(ctr_memory_map(&g.memory,request[4],1,CTR_MEM_READ|CTR_MEM_WRITE,g.storage[4]+0x80,1)==CTR_MEM_OK);
        }
        write_words(&g,request,5);uint8_t before[4096];memcpy(before,g.storage[4],sizeof(before));
        uint64_t generation=g.memory.generation;unsigned objects=sessions(&g);
        g.cpu.r[0]=service;ctr_guest_svc(&g,0x32);
        CHECK(g.state==CTR_GUEST_FAULTED && g.ipc_failures==1);
        CHECK(g.fault==(kind==4 || kind==5 || kind==12?CTR_CPU_MEMORY_FAULT:CTR_CPU_UNSUPPORTED));
        CHECK(!memcmp(before,g.storage[4],sizeof(before)) && g.memory.generation==generation && sessions(&g)==objects);
        CHECK(g.ipc_header==request[0] && !strcmp(g.ipc_service,"cfg:u"));
        if(kind==4 || kind==5 || kind==12)CHECK(g.cpu.memory_fault==(kind==5?CTR_MEM_UNMAPPED:CTR_MEM_PERMISSION));
        ctr_guest_close(&g);
    }
    for(unsigned kind=0;kind<6;++kind) {
        ctr_guest g;CHECK(ctr_service_fixture_open(&g));uint32_t srv=directory(&g);
        uint32_t request[]={0x00050100,0x3a676663,0x75,5,1};
        if(kind==0)request[3]=9;
        if(kind==1)request[3]=0;
        if(kind==2)request[2]=0x4175; /* Nonzero padding. */
        if(kind==3)request[1]=0x3a006663; /* Embedded NUL. */
        if(kind==4)request[4]=2;
        if(kind==5)request[0]=0x00050101;
        write_words(&g,request,5);uint64_t generation=g.memory.generation;
        g.cpu.r[0]=srv;ctr_guest_svc(&g,0x32);
        CHECK(g.state==CTR_GUEST_FAULTED && g.fault==CTR_CPU_UNSUPPORTED);
        CHECK(sessions(&g)==1 && g.memory.generation==generation);ctr_guest_close(&g);
    }
    ctr_guest g;CHECK(ctr_service_fixture_open(&g));uint32_t srv=directory(&g);
    const uint32_t unavailable[]={0x00050100,0x3a676663,0x69,5,1}; /* cfg:i is not advertised. */
    send(&g,srv,unavailable,5);
    CHECK(g.state==CTR_GUEST_READY && g.cpu.r[0]==0 && sessions(&g)==1);
    CHECK(read_word(&g,0)==0x00050040 && read_word(&g,1)==0xd88067fa);
    CHECK(g.ipc_failures==1 && g.ipc_result==0xd88067fa);
    CHECK(!strcmp(g.ipc_target,"cfg:i"));
    CHECK(cfg(&g,srv)!=0);ctr_guest_close(&g);
    CHECK(ctr_service_fixture_open(&g));srv=connect(&g);
    const uint32_t lookup[]={0x00050100,0x3a676663,0x75,5,1};send(&g,srv,lookup,5);
    CHECK(g.state==CTR_GUEST_FAULTED && sessions(&g)==1);ctr_guest_close(&g);
    CHECK(ctr_service_fixture_open(&g));srv=connect(&g);
    const uint32_t registration[]={0x00010002,0x10,0};send(&g,srv,registration,3);
    CHECK(g.state==CTR_GUEST_FAULTED && !ctr_guest_session_for(&g,srv)->registered);ctr_guest_close(&g);
}
static void memory_and_resource_tests(void) {
    ctr_guest g;
    for(unsigned kind=0;kind<5;++kind) {
        CHECK(ctr_service_fixture_open(&g));uint32_t srv=directory(&g);
        const uint32_t model[]={0x00050000};write_words(&g,model,1);
        if(kind==0)g.cpu.tls_address=UINT32_MAX-32;
        if(kind==1)g.cpu.tls_address+=1;
        if(kind==2)g.cpu.tls_address=0xdead0000;
        if(kind==3)g.cpu.tls_address=0x00100000;
        if(kind==4)g.cpu.tls_address+=4096-0x90;
        g.cpu.r[0]=srv;uint64_t generation=g.memory.generation;ctr_guest_svc(&g,0x32);
        CHECK(g.state==CTR_GUEST_FAULTED && g.fault==CTR_CPU_MEMORY_FAULT);
        CHECK(g.memory.generation==generation && sessions(&g)==1);ctr_guest_close(&g);
    }
    CHECK(ctr_service_fixture_open(&g));uint32_t handles[16];
    for(unsigned i=0;i<16;++i)handles[i]=connect(&g);
    CHECK(sessions(&g)==16);g.cpu.r[1]=DATA;ctr_guest_svc(&g,0x2d);
    CHECK(g.cpu.r[0]==0xd86007f3 && g.cpu.r[1]==0 && sessions(&g)==16);
    g.cpu.r[0]=handles[3];ctr_guest_svc(&g,0x23);uint32_t replacement=connect(&g);
    CHECK(replacement!=handles[3] && !ctr_guest_session_for(&g,handles[3]));ctr_guest_close(&g);
    CHECK(ctr_service_fixture_open(&g));uint32_t srv=directory(&g);
    for(unsigned i=0;i<63;++i) { g.cpu.r[1]=srv;ctr_guest_svc(&g,0x27);CHECK(g.cpu.r[0]==0); }
    const uint32_t lookup[]={0x00050100,0x3a676663,0x75,5,1};send(&g,srv,lookup,5);
    CHECK(g.state==CTR_GUEST_READY && read_word(&g,1)==0xd86007f3 && sessions(&g)==1);
    ctr_guest_close(&g);
    CHECK(ctr_service_fixture_open(&g));g.cpu.r[1]=0xdead0000;ctr_guest_svc(&g,0x2d);
    CHECK(g.state==CTR_GUEST_FAULTED && g.fault==CTR_CPU_MEMORY_FAULT && !sessions(&g));ctr_guest_close(&g);
    CHECK(ctr_service_fixture_open(&g));
    CHECK(ctr_memory_write(&g.memory,DATA,4,0x3a707367)==CTR_MEM_OK); /* gsp: is not a named port. */
    g.cpu.r[1]=DATA;ctr_guest_svc(&g,0x2d);
    CHECK(g.state==CTR_GUEST_FAULTED && g.fault==CTR_CPU_UNSUPPORTED && !sessions(&g));ctr_guest_close(&g);
    CHECK(ctr_service_fixture_open(&g));g.cpu.r[1]=0;ctr_guest_svc(&g,0x17);uint32_t event=g.cpu.r[1];
    g.cpu.r[0]=event;ctr_guest_svc(&g,0x32);CHECK(g.state==CTR_GUEST_READY && g.cpu.r[0]==INVALID);
    CHECK(ctr_guest_signal(&g,event));ctr_guest_close(&g);
}
static void execution(void) {
    CHECK(ctr_service_selftest()==0);
    ctr_guest a,b;CHECK(ctr_service_fixture_open(&a) && ctr_service_fixture_open(&b));
    CHECK(ctr_guest_configure(&a,(ctr_guest_configuration){2,7,3}));
    CHECK(ctr_guest_configure(&b,(ctr_guest_configuration){2,7,3}));
    for(unsigned slice=0;slice<1000 && a.state!=CTR_GUEST_EXITED;++slice) {
        unsigned budget=slice%3==0?1:7;
        CHECK(ctr_guest_run(&a,budget,slice,false)==ctr_guest_run(&b,budget,slice,true));
        CHECK(a.state!=CTR_GUEST_FAULTED && a.cpu.instructions==b.cpu.instructions);
        CHECK(!memcmp(a.cpu.r,b.cpu.r,sizeof(a.cpu.r)) && a.cpu.cpsr==b.cpu.cpsr);
        CHECK(a.svc_count==b.svc_count && a.ipc_requests==b.ipc_requests);
        for(unsigned i=0;i<5;++i)if(a.storage_size[i])CHECK(!memcmp(a.storage[i],b.storage[i],a.storage_size[i]));
    }
    CHECK(a.state==CTR_GUEST_EXITED && a.ipc_requests==5 && !a.ipc_failures && a.svc_count==9);
    uint32_t value;
    CHECK(ctr_memory_read(&a.memory,DATA+0x64,4,CTR_MEM_READ,&value)==CTR_MEM_OK && value==2);
    CHECK(ctr_memory_read(&a.memory,DATA+0x70,4,CTR_MEM_READ,&value)==CTR_MEM_OK && value==3);
    CHECK(ctr_memory_read(&a.memory,DATA+0x20,1,CTR_MEM_READ,&value)==CTR_MEM_OK && value==7);
    CHECK(!sessions(&a) && !sessions(&b));ctr_guest_close(&a);ctr_guest_close(&b);
}
int main(void) {
    ownership_and_configuration();rejection_tests();memory_and_resource_tests();execution();
    printf("{\"ok\":true,\"assertions\":%u,\"arm_tls_srv_cfg_startup\":true,\"aot_interpreter_memory_match\":true,\"console_verified\":false}\n",assertions);
    return 0;
}
