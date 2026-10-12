#include "ctr_relay_fixture.h"
#include "ctr_kernel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned assertions;
#define CHECK(x) do { ++assertions;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static uint32_t read_word(ctr_guest *g,uint32_t address) {
    uint32_t value;CHECK(ctr_memory_read(&g->memory,address,4,1,&value)==CTR_MEM_OK);return value;
}
static void write_word(ctr_guest *g,uint32_t address,uint32_t value) {
    CHECK(ctr_memory_write(&g->memory,address,4,value)==CTR_MEM_OK);
}
static uint32_t send(ctr_guest *g,uint32_t session,const uint32_t *words,unsigned count) {
    for(unsigned i=0;i<count;++i)write_word(g,g->cpu.tls_address+0x80+i*4,words[i]);
    g->cpu.r[0]=session;ctr_guest_svc(g,0x32);return read_word(g,g->cpu.tls_address+0x84);
}
static uint32_t start(ctr_guest *g,uint32_t *event) {
    CHECK(ctr_relay_fixture_open(g));uint32_t session=ctr_guest_session_open(g,CTR_SERVICE_GSP);CHECK(session);
    const uint32_t acquire[]={0x160042,0,0,0xffff8001};CHECK(!send(g,session,acquire,4));
    *event=ctr_guest_event_create(g,0);CHECK(*event);return session;
}
static uint32_t registration(ctr_guest *g,uint32_t session,uint32_t event) {
    uint32_t request[]={0x130042,1,0,event};CHECK(!send(g,session,request,4));
    CHECK(g->gsp.registered && read_word(g,g->cpu.tls_address+0x80)==0x130082);
    CHECK(!read_word(g,g->cpu.tls_address+0x88) && !read_word(g,g->cpu.tls_address+0x8c));
    uint32_t shared=read_word(g,g->cpu.tls_address+0x90);CHECK(ctr_guest_handle_for(g,shared)->type==CTR_HANDLE_SHARED);
    g->cpu.r[0]=shared;g->cpu.r[1]=0x10002000;g->cpu.r[2]=3;g->cpu.r[3]=3;ctr_guest_svc(g,0x1f);
    CHECK(!g->cpu.r[0]);return shared;
}
static void submit(ctr_guest *g,unsigned screen,unsigned slot,unsigned front) {
    uint32_t address=0x10002200+screen*0x40,base=0x08001000+(screen?400*240*2:0);
    uint32_t info[]={front,base,base,480,screen?0x102:0x142,front,0};
    for(unsigned i=0;i<7;++i)write_word(g,address+4+slot*28+i*4,info[i]);
    write_word(g,address,0x100|slot);
}
static void lifetime(void) {
    ctr_guest g;uint32_t event,session=start(&g,&event),shared=registration(&g,session,event);
    unsigned e=g.gsp.relay_event,s=g.gsp.relay_shared;
    CHECK(g.events[e].references==2 && g.shared[s].references==3);
    uint32_t duplicate=ctr_guest_handle_new(&g,CTR_HANDLE_SESSION,ctr_guest_handle_for(&g,session)->object);CHECK(duplicate);
    CHECK(ctr_guest_handle_close(&g,event) && ctr_guest_handle_close(&g,shared));
    CHECK(g.events[e].references==1 && g.shared[s].references==2);
    CHECK(ctr_guest_handle_close(&g,session) && g.gsp.registered);
    CHECK(ctr_gsp_poll(&g,g.gsp.next_vblank_ns) && g.events[e].signaled && g.shared[s].storage[1]==2);
    CHECK(ctr_guest_handle_close(&g,duplicate) && !g.gsp.registered && !g.gsp.right);
    CHECK(!g.events[e].active && g.shared[s].active && g.shared[s].references==1);
    /* The surviving mapping owns the page; unregister never leaves a dangling alias. */
    CHECK(read_word(&g,0x10002000)==0x200);
    ctr_guest_close(&g);
}
static void timing_and_frames(void) {
    ctr_guest g;uint32_t event,session=start(&g,&event);registration(&g,session,event);
    submit(&g,0,1,0);submit(&g,1,0,1);uint64_t due=g.gsp.next_vblank_ns,generation=g.memory.generation;
    g.cpu.r[0]=event;g.cpu.r[2]=g.cpu.r[3]=0xffffffff;ctr_guest_svc(&g,0x24);CHECK(g.state==CTR_GUEST_WAITING);
    CHECK(ctr_guest_run(&g,0,due-1,false)==CTR_GUEST_WAITING && !g.gsp.swaps[0]);
    g.cpu.exclusive_valid=true;
    CHECK(ctr_guest_run(&g,0,due,false)==CTR_GUEST_READY && !g.cpu.r[0]);
    CHECK(g.gsp.swaps[0]==1 && g.gsp.swaps[1]==1 && g.gsp.info[0][0]==0 && g.gsp.info[1][0]==1);
    CHECK(read_word(&g,0x10002200)==1 && !read_word(&g,0x10002240));
    CHECK(g.memory.generation==generation+1 && !g.cpu.exclusive_valid);
    CHECK(read_word(&g,0x10002000)==0x200 && g.shared[g.gsp.relay_shared].storage[0xc]==2 && g.shared[g.gsp.relay_shared].storage[0xd]==3);
    CHECK(ctr_gsp_poll(&g,due) && g.gsp.vblanks==1); /* Same instant cannot double signal. */
    write_word(&g,0x10002000,0);submit(&g,0,0,1);
    CHECK(ctr_gsp_poll(&g,due+5*CTR_GSP_FRAME_NS) && g.gsp.vblanks==2 && g.gsp.coalesced_vblanks==4);
    CHECK(g.gsp.swaps[0]==2 && g.gsp.swaps[1]==1 && g.gsp.info[0][0]==1);
    CHECK(g.gsp.next_vblank_ns==due+6*CTR_GSP_FRAME_NS && read_word(&g,0x10002000)==0x200);
    ctr_guest_close(&g);
}
static void rings(void) {
    for(unsigned count=0;count<=52;count+=count==0?51:1) {
        ctr_guest g;uint32_t event,session=start(&g,&event);registration(&g,session,event);
        uint8_t *page=g.shared[g.gsp.relay_shared].storage;memset(page+0xc,0xaa,52);
        write_word(&g,0x10002000,51|(count<<8));uint64_t due=g.gsp.next_vblank_ns;
        CHECK(ctr_gsp_poll(&g,due) && page[0]==51 && page[1]==(count?52:2));
        if(!count)CHECK(page[0xc+51]==2 && page[0xc]==3 && !page[2] && !g.gsp.dropped_interrupts);
        else if(count==51)CHECK(page[0xc+50]==2 && page[0xc+51]==0xaa && page[2]==1 && g.gsp.dropped_interrupts==1);
        else CHECK(page[0xc+50]==0xaa && page[2]==1 && g.gsp.dropped_interrupts==2);
        CHECK(g.events[g.gsp.relay_event].signaled==(count<52));ctr_guest_close(&g);
    }
}
static void rejection(void) {
    for(unsigned kind=0;kind<11;++kind) {
        ctr_guest g;uint32_t event,session=start(&g,&event);registration(&g,session,event);
        submit(&g,0,1,0);submit(&g,1,1,0);
        if(kind==0)write_word(&g,0x10002000,52);
        if(kind==1)write_word(&g,0x10002000,53<<8);
        if(kind==2)write_word(&g,0x10002000,2<<16);
        if(kind==3)write_word(&g,0x10002000,1<<24);
        if(kind==4)write_word(&g,0x10002800,0x100); /* Pending malformed GX command. */
        if(kind==5)write_word(&g,0x10002240,0x102);
        if(kind==6)write_word(&g,0x10002240,0x10101);
        if(kind==7)write_word(&g,0x10002240+32+16,0x105);
        if(kind==8) { write_word(&g,0x10002240+32+4,0xdead0000);write_word(&g,0x10002240+32+8,0xdead0000); }
        if(kind==9)CHECK(ctr_memory_change_range(&g.memory,0x08001000+400*240*2,4096,0,false)==CTR_MEM_OK);
        if(kind==10)g.gsp.next_vblank_ns=UINT64_MAX-1;
        uint8_t before[4096];memcpy(before,g.shared[g.gsp.relay_shared].storage,sizeof(before));
        uint64_t generation=g.memory.generation;
        CHECK(!ctr_gsp_poll(&g,g.gsp.next_vblank_ns));
        CHECK(g.state==CTR_GUEST_FAULTED && g.gsp.failures==1 && !g.gsp.swaps[0] && !g.gsp.swaps[1]);
        CHECK(!memcmp(before,g.shared[g.gsp.relay_shared].storage,sizeof(before)) && generation==g.memory.generation);
        CHECK(!g.events[g.gsp.relay_event].signaled && !g.gsp.vblanks);
        CHECK(g.fault==(kind==8 || kind==9?CTR_CPU_MEMORY_FAULT:CTR_CPU_UNSUPPORTED));ctr_guest_close(&g);
    }
}
static void service_errors(void) {
    ctr_guest g;uint32_t event,session=start(&g,&event),request[]={0x130042,1,0,0};
    request[3]=session;CHECK(send(&g,session,request,4)==0xd8e007f7 && !g.gsp.registered && g.state==CTR_GUEST_READY);
    uint32_t stale=ctr_guest_event_create(&g,0);CHECK(ctr_guest_handle_close(&g,stale));request[3]=stale;
    CHECK(send(&g,session,request,4)==0xd8e007f7);
    registration(&g,session,event);request[3]=event;
    CHECK(send(&g,session,request,4)==0xe0a01bf5);
    uint32_t other=ctr_guest_session_open(&g,CTR_SERVICE_GSP),unregister[]={0x140000};
    CHECK(send(&g,other,unregister,1)==0xe0a01bf5 && g.gsp.registered);
    CHECK(!send(&g,session,unregister,1) && !g.gsp.registered && g.gsp.right);
    CHECK(send(&g,session,unregister,1)==0xe0a01bf5);
    /* Fill all shared slots: a failed registration cannot retain the relay event. */
    while(ctr_shared_create(&g,4096,3)) {}
    unsigned refs=g.events[ctr_guest_handle_for(&g,event)->object].references;
    CHECK(send(&g,session,request,4)==0xd86007f3 && !g.gsp.registered);
    CHECK(g.events[ctr_guest_handle_for(&g,event)->object].references==refs && g.state==CTR_GUEST_READY);
    ctr_guest_close(&g);
}
static void handle_capacity(void) {
    ctr_guest g;uint32_t event,session=start(&g,&event),request[]={0x130042,1,0,event};
    unsigned e=ctr_guest_handle_for(&g,event)->object,shared_before=0;
    for(unsigned i=0;i<8;++i)shared_before+=g.shared[i].active;
    while(ctr_guest_handle_new(&g,CTR_HANDLE_EVENT,e)) {}
    unsigned refs=g.events[e].references;
    CHECK(send(&g,session,request,4)==0xd86007f3 && !g.gsp.registered && g.events[e].references==refs);
    unsigned shared_after=0;for(unsigned i=0;i<8;++i)shared_after+=g.shared[i].active;
    CHECK(shared_before==shared_after);ctr_guest_close(&g);
    session=start(&g,&event);request[3]=event;g.now_ns=UINT64_MAX;
    CHECK(send(&g,session,request,4)==0xe0a01bf5 && !g.gsp.registered);ctr_guest_close(&g);
}
int main(void) {
    lifetime();timing_and_frames();rings();rejection();service_errors();handle_capacity();
    int result=ctr_relay_selftest();if(result)fprintf(stderr,"relay selftest code=%d\n",result);CHECK(!result);
    printf("{\"ok\":true,\"assertions\":%u,\"original_arm_shared_gsp_vblank\":true,\"console_verified\":false}\n",assertions);return 0;
}
