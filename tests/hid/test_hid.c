/* Original wire-layout, ownership and publication checks, independent of emitter. */
#include "ctr_hid_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned assertions;
#define CHECK(x) do { ++assertions;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static uint32_t read_at(ctr_guest *g,uint32_t at) {
    uint32_t value;CHECK(ctr_memory_read(&g->memory,at,4,CTR_MEM_READ,&value)==CTR_MEM_OK);return value;
}
static unsigned active_handles(const ctr_guest *g) {
    unsigned count=0;for(unsigned i=0;i<64;++i)count+=g->handles[i].active;return count;
}
static void map(ctr_guest *g,uint32_t handle,unsigned rights) {
    g->cpu.r[0]=handle;g->cpu.r[1]=0x10001000;g->cpu.r[2]=rights;g->cpu.r[3]=0x10000000;ctr_guest_svc(g,0x1f);
}
static void wait(ctr_guest *g,uint32_t handle,uint64_t time) {
    g->cpu.r[0]=handle;g->cpu.r[2]=(uint32_t)time;g->cpu.r[3]=(uint32_t)(time>>32);ctr_guest_svc(g,0x24);
}
static void accelerometer(void) {
    ctr_guest g;CHECK(ctr_hid_fixture_open(&g));
    uint32_t session=ctr_guest_session_open(&g,CTR_SERVICE_HID),handles[6];
    CHECK(session && ctr_hid_handles(&g,handles));
    map(&g,handles[0],1);CHECK(!g.cpu.r[0]);
    uint32_t tls=g.cpu.tls_address+0x80;
    CHECK(ctr_memory_write(&g.memory,tls,4,0x00110000)==CTR_MEM_OK);
    g.cpu.r[0]=session;ctr_guest_svc(&g,0x32);
    CHECK(read_at(&g,tls)==0x00110040 && !read_at(&g,tls+4));
    ctr_hid_acceleration(&g,32,-512,128);
    CHECK(ctr_hid_update(&g,0,0,0,false,0,0,5000000));
    wait(&g,handles[3],0);CHECK(g.cpu.r[0]==0x09401bfe);
    CHECK(ctr_hid_update(&g,0,0,0,false,0,0,10000000));
    wait(&g,handles[3],0);CHECK(!g.cpu.r[0]);
    CHECK(read_at(&g,0x10001108)==2681118 && read_at(&g,0x10001128)==0xfe000020u);
    CHECK(read_at(&g,0x1000112c)==128u);
    CHECK(ctr_memory_write(&g.memory,tls,4,0x00120000)==CTR_MEM_OK);
    g.cpu.r[0]=session;ctr_guest_svc(&g,0x32);
    CHECK(read_at(&g,tls)==0x00120040 && !read_at(&g,tls+4));
    CHECK(ctr_hid_update(&g,0,0,0,false,0,0,20000000));
    wait(&g,handles[3],0);CHECK(g.cpu.r[0]==0x09401bfe && !read_at(&g,0x10001118));
    ctr_guest_close(&g);
}
static void gyroscope(void) {
    ctr_guest g;CHECK(ctr_hid_fixture_open(&g));
    uint32_t session=ctr_guest_session_open(&g,CTR_SERVICE_HID),handles[6];
    CHECK(session && ctr_hid_handles(&g,handles));
    map(&g,handles[0],1);CHECK(!g.cpu.r[0]);
    uint32_t tls=g.cpu.tls_address+0x80;
    CHECK(ctr_memory_write(&g.memory,tls,4,0x00130000)==CTR_MEM_OK);
    g.cpu.r[0]=session;ctr_guest_svc(&g,0x32);
    CHECK(read_at(&g,tls)==0x00130040 && !read_at(&g,tls+4));
    CHECK(ctr_memory_write(&g.memory,tls,4,0x00150000)==CTR_MEM_OK);
    g.cpu.r[0]=session;ctr_guest_svc(&g,0x32);
    CHECK(read_at(&g,tls)==0x00150080 && read_at(&g,tls+8)==0x41660000u);
    CHECK(ctr_memory_write(&g.memory,tls,4,0x00160000)==CTR_MEM_OK);
    g.cpu.r[0]=session;ctr_guest_svc(&g,0x32);
    CHECK(read_at(&g,tls)==0x00160180 && read_at(&g,tls+8)==0x1a2c0000u);
    ctr_hid_angular_velocity(&g,32,-20,64);
    CHECK(ctr_hid_update(&g,0,0,0,false,0,0,5000000));
    wait(&g,handles[4],0);CHECK(g.cpu.r[0]==0x09401bfe);
    CHECK(ctr_hid_update(&g,0,0,0,false,0,0,10000000));
    wait(&g,handles[4],0);CHECK(!g.cpu.r[0]);
    CHECK(read_at(&g,0x10001158)==2681118 && read_at(&g,0x10001170)==0x00400020u);
    CHECK(read_at(&g,0x10001178)==0xffec0020u && read_at(&g,0x1000117c)==64u);
    CHECK(ctr_memory_write(&g.memory,tls,4,0x00140000)==CTR_MEM_OK);
    g.cpu.r[0]=session;ctr_guest_svc(&g,0x32);
    CHECK(read_at(&g,tls)==0x00140040 && !read_at(&g,tls+4));
    CHECK(ctr_hid_update(&g,0,0,0,false,0,0,20000000));
    wait(&g,handles[4],0);CHECK(g.cpu.r[0]==0x09401bfe && !read_at(&g,0x10001168));
    ctr_guest_close(&g);
}
static void wire_and_ownership(void) {
    ctr_guest g;CHECK(ctr_hid_fixture_open(&g));uint32_t handle=ctr_guest_session_open(&g,CTR_SERVICE_HID),copies[6];CHECK(handle);
    uint32_t tls=g.cpu.tls_address+0x80;CHECK(ctr_memory_write(&g.memory,tls,4,0xa0000)==CTR_MEM_OK);
    g.cpu.r[0]=handle;ctr_guest_svc(&g,0x32);
    CHECK(g.state==CTR_GUEST_READY && !g.cpu.r[0] && g.ipc_requests==1 && !g.ipc_failures);
    CHECK(read_at(&g,tls)==0xa0047 && !read_at(&g,tls+4) && read_at(&g,tls+8)==0x14000000);
    for(unsigned i=0;i<6;++i) {
        copies[i]=read_at(&g,tls+12+i*4);ctr_guest_handle *h=ctr_guest_handle_for(&g,copies[i]);
        CHECK(h && h->type==(i?CTR_HANDLE_EVENT:CTR_HANDLE_SHARED) && copies[i]!=g.hid.handles[i]);
    }
    CHECK(active_handles(&g)==13);
    map(&g,copies[0],3);CHECK(g.cpu.r[0]==0xe0e01bee);map(&g,copies[0],1);CHECK(!g.cpu.r[0]);
    CHECK(ctr_memory_write(&g.memory,0x10001028,4,0)==CTR_MEM_PERMISSION);
    unsigned shared=ctr_guest_handle_for(&g,copies[0])->object;CHECK(g.shared[shared].references==3);
    uint32_t again[6];CHECK(ctr_hid_handles(&g,again));
    for(unsigned i=0;i<6;++i) { CHECK(again[i]!=copies[i]);CHECK(ctr_guest_handle_close(&g,again[i])); }
    for(unsigned i=0;i<6;++i)CHECK(ctr_guest_handle_close(&g,copies[i]));
    CHECK(active_handles(&g)==7 && g.shared[shared].references==2);
    uint64_t code=g.memory.code_generation;g.cpu.exclusive_valid=true;
    CHECK(ctr_hid_update(&g,0x401,12,-34,true,319,239,1000000));
    CHECK(!g.cpu.exclusive_valid && g.memory.code_generation==code && g.hid.samples==1);
    CHECK(read_at(&g,0x10001010)==1 && read_at(&g,0x100010b8)==1 && read_at(&g,0x1000101c)==0x401);
    CHECK(read_at(&g,0x10001038)==0x401 && read_at(&g,0x1000103c)==0x401 && !read_at(&g,0x10001040));
    CHECK(read_at(&g,0x10001044)==0xffde000c && read_at(&g,0x100010d0)==0x00ef013f && read_at(&g,0x100010d4)==1);
    uint32_t held;int x,y;bool touched;uint16_t tx,ty;
    CHECK(ctr_hid_read(&g,0x10001000,&held,&x,&y,&touched,&tx,&ty) && held==0x401 && x==12 && y==-34 && touched && tx==319 && ty==239);
    CHECK(!ctr_hid_read(&g,0x10001000,NULL,&x,&y,&touched,&tx,&ty));
    wait(&g,g.hid.handles[1],0);CHECK(!g.cpu.r[0]);wait(&g,g.hid.handles[1],0);CHECK(g.cpu.r[0]==0x09401bfe);
    wait(&g,g.hid.handles[1],UINT64_MAX);CHECK(g.state==CTR_GUEST_WAITING);
    CHECK(ctr_hid_update(&g,2,156,156,false,0xffff,0xffff,2000000));
    CHECK(g.state==CTR_GUEST_READY && !g.cpu.r[0] && !g.wait_count);
    CHECK(read_at(&g,0x10001048)==0x50000002 && read_at(&g,0x1000104c)==0x50000002 && read_at(&g,0x10001050)==0x401);
    CHECK(ctr_hid_read(&g,0x10001000,&held,&x,&y,&touched,&tx,&ty) && held==0x50000002 && x==156 && y==156 && !touched && !tx && !ty);
    for(unsigned i=3;i<=16;++i)CHECK(ctr_hid_update(&g,0,-156,0,false,0,0,i*1000000));
    CHECK(read_at(&g,0x10001010)==0 && read_at(&g,0x100010b8)==0);
    CHECK(read_at(&g,0x10001000)==4289789 && read_at(&g,0x10001008)==2144894);
    CHECK(read_at(&g,0x100010a8)==4289789 && read_at(&g,0x100010b0)==2144894);
    CHECK(read_at(&g,0x10001028)==0x20000000 && read_at(&g,0x10001034)==0x0000ff64);
    uint8_t before[4096];memcpy(before,g.shared[shared].storage,sizeof(before));uint64_t generation=g.memory.generation;
    CHECK(!ctr_hid_update(&g,1<<20,0,0,false,0,0,17000000));
    CHECK(!ctr_hid_update(&g,0,157,0,false,0,0,17000000));
    CHECK(!ctr_hid_update(&g,0,0,-157,false,0,0,17000000));
    CHECK(!ctr_hid_update(&g,0,0,0,true,320,1,17000000));
    CHECK(!ctr_hid_update(&g,0,0,0,true,1,240,17000000));
    CHECK(!ctr_hid_update(&g,0,0,0,false,0,0,16000000));
    CHECK(!ctr_hid_update(&g,0,0,0,false,0,0,15000000));
    CHECK(!memcmp(before,g.shared[shared].storage,sizeof(before)) && generation==g.memory.generation && g.hid.samples==16);
    g.cpu.r[0]=g.hid.handles[0];g.cpu.r[1]=0x10001000;ctr_guest_svc(&g,0x20);CHECK(!g.cpu.r[0] && g.shared[shared].references==1);
    CHECK(ctr_guest_handle_close(&g,handle));ctr_guest_close(&g);
}
static void failures(void) {
    ctr_guest g;CHECK(ctr_hid_fixture_open(&g));CHECK(!ctr_hid_update(&g,1u<<24,0,0,false,0,0,1));
    CHECK(!g.hid.initialized && !active_handles(&g));
    uint32_t event=ctr_guest_event_create(&g,0);CHECK(event);
    for(unsigned i=1;i<61;++i)CHECK(ctr_guest_handle_new(&g,CTR_HANDLE_EVENT,ctr_guest_handle_for(&g,event)->object));
    uint32_t out[6];CHECK(!ctr_hid_handles(&g,out) && !g.hid.initialized && active_handles(&g)==61);
    for(unsigned i=0;i<8;++i)CHECK(!g.shared[i].active);
    for(unsigned i=1;i<32;++i)CHECK(!g.events[i].active);
    ctr_guest_close(&g);CHECK(ctr_hid_fixture_open(&g));CHECK(ctr_hid_handles(&g,out));
    for(unsigned i=0;i<6;++i)CHECK(ctr_guest_handle_close(&g,out[i]));
    event=g.hid.handles[1];unsigned object=ctr_guest_handle_for(&g,event)->object;
    for(unsigned i=6;i<61;++i)CHECK(ctr_guest_handle_new(&g,CTR_HANDLE_EVENT,object));
    unsigned refs=g.events[object].references;CHECK(!ctr_hid_handles(&g,out) && active_handles(&g)==61 && refs==g.events[object].references);
    for(unsigned i=0;i<6;++i)CHECK(!out[i]);
    ctr_guest_close(&g);CHECK(ctr_hid_fixture_open(&g));
    for(unsigned i=0;i<32;++i)CHECK(ctr_guest_event_create(&g,0));
    CHECK(!ctr_hid_update(&g,0,0,0,false,0,0,1) && !g.hid.initialized && active_handles(&g)==32);
    for(unsigned i=0;i<8;++i)CHECK(!g.shared[i].active);
    ctr_guest_close(&g);
    for(unsigned i=0;i<2;++i) {
        CHECK(ctr_hid_fixture_open(&g));uint32_t session=ctr_guest_session_open(&g,CTR_SERVICE_HID);CHECK(session);
        CHECK(ctr_memory_write(&g.memory,g.cpu.tls_address+0x80,4,i==0?0x50000:0xa0001)==CTR_MEM_OK);
        g.cpu.r[0]=session;ctr_guest_svc(&g,0x32);
        CHECK(g.state==CTR_GUEST_FAULTED && g.fault==CTR_CPU_UNSUPPORTED && !g.hid.initialized && g.ipc_failures==1);
        ctr_guest_close(&g);
    }
}
int main(void) {
    wire_and_ownership();accelerometer();gyroscope();failures();CHECK(!ctr_hid_selftest());
    printf("{\"ok\":true,\"assertions\":%u,\"original_arm_hid_consumer\":true,\"hid_shared_ring_events\":true,\"console_verified\":false}\n",assertions);return 0;
}
