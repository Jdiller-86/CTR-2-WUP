#include "ctr_kernel_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned assertions;
#define CHECK(x) do { ++assertions;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static uint32_t create(ctr_guest *g,unsigned svc,uint32_t first,uint32_t second) {
    g->cpu.r[1]=first;g->cpu.r[2]=second;ctr_guest_svc(g,svc);CHECK(!g->cpu.r[0]);return g->cpu.r[1];
}
static void close_handle(ctr_guest *g,uint32_t handle) {
    g->cpu.r[0]=handle;ctr_guest_svc(g,0x23);CHECK(!g->cpu.r[0]);
}
static void wait_one(ctr_guest *g,uint32_t handle,uint64_t nanos) {
    g->cpu.r[0]=handle;g->cpu.r[2]=(uint32_t)nanos;g->cpu.r[3]=(uint32_t)(nanos>>32);ctr_guest_svc(g,0x24);
}
static void many(ctr_guest *g,const uint32_t *handles,unsigned count,bool all,uint64_t nanos) {
    for(unsigned i=0;i<count && i<16;++i)CHECK(ctr_memory_write(&g->memory,0x08000200+i*4,4,handles[i])==CTR_MEM_OK);
    g->cpu.r[0]=(uint32_t)nanos;g->cpu.r[1]=0x08000200;g->cpu.r[2]=count;
    g->cpu.r[3]=all;g->cpu.r[4]=(uint32_t)(nanos>>32);ctr_guest_svc(g,0x25);
}
static void semaphore_release(ctr_guest *g,uint32_t handle,unsigned count) {
    g->cpu.r[1]=handle;g->cpu.r[2]=count;ctr_guest_svc(g,0x16);
}
static void synchronization(void) {
    ctr_guest g;CHECK(ctr_kernel_fixture_open(&g));
    uint32_t mutex=create(&g,0x13,1,0);ctr_guest_handle *m=ctr_guest_handle_for(&g,mutex);
    CHECK(m && g.mutexes[m->object].depth==1);
    wait_one(&g,mutex,0);CHECK(!g.cpu.r[0] && g.mutexes[m->object].depth==2);
    g.cpu.r[0]=mutex;ctr_guest_svc(&g,0x14);CHECK(!g.cpu.r[0] && g.mutexes[m->object].depth==1);
    g.cpu.r[0]=mutex;ctr_guest_svc(&g,0x14);CHECK(!g.cpu.r[0] && !g.mutexes[m->object].depth);
    g.cpu.r[0]=mutex;ctr_guest_svc(&g,0x14);CHECK(g.cpu.r[0]==0xe0a01bf5);
    uint32_t sem=create(&g,0x15,1,2);unsigned object=ctr_guest_handle_for(&g,sem)->object;
    wait_one(&g,sem,0);CHECK(!g.cpu.r[0] && !g.semaphores[object].count);
    wait_one(&g,sem,0);CHECK(g.cpu.r[0]==0x09401bfe && !g.wait_count);
    semaphore_release(&g,sem,3);CHECK(g.cpu.r[0]==0xe0e01bee && !g.semaphores[object].count);
    semaphore_release(&g,sem,2);CHECK(!g.cpu.r[0] && !g.cpu.r[1] && g.semaphores[object].count==2);
    uint32_t event=create(&g,0x17,0,0),handles[]={event,sem};
    many(&g,handles,2,false,0);CHECK(!g.cpu.r[0] && g.cpu.r[1]==1 && g.semaphores[object].count==1);
    many(&g,handles,2,true,100);CHECK(g.state==CTR_GUEST_WAITING && g.semaphores[object].count==1);
    CHECK(ctr_guest_signal(&g,event) && g.state==CTR_GUEST_READY && !g.cpu.r[0] && g.cpu.r[1]==UINT32_MAX);
    CHECK(!g.semaphores[object].count && !g.wait_count);
    many(&g,handles,2,true,50);CHECK(g.state==CTR_GUEST_WAITING);
    CHECK(ctr_guest_run(&g,0,49,false)==CTR_GUEST_WAITING);
    CHECK(ctr_guest_run(&g,0,50,false)==CTR_GUEST_READY && g.cpu.r[0]==0x09401bfe);
    uint32_t duplicate[]={event,event};many(&g,duplicate,2,true,0);CHECK(g.cpu.r[0]==0xe0e01bee);
    CHECK(ctr_guest_signal(&g,event));many(&g,duplicate,2,false,0);CHECK(!g.cpu.r[0] && !g.cpu.r[1]);
    many(&g,handles,2,false,UINT64_MAX);CHECK(g.state==CTR_GUEST_WAITING);
    close_handle(&g,event);close_handle(&g,sem);CHECK(g.events[0].active && g.semaphores[object].active);
    CHECK(ctr_guest_run(&g,0,100000,false)==CTR_GUEST_WAITING);
    /* Retained wait objects survive handle closure. Device signals refer to objects. */
    g.events[0].signaled=true;ctr_sync_poll(&g);CHECK(g.state==CTR_GUEST_READY && !g.events[0].active && !g.semaphores[object].active);
    wait_one(&g,sem,0);CHECK(g.cpu.r[0]==0xd8e007f7);
    handles[0]=mutex;handles[1]=0xdeadbeef;many(&g,handles,2,false,0);CHECK(g.cpu.r[0]==0xd8e007f7 && !g.wait_count);
    g.cpu.r[0]=0;g.cpu.r[1]=0xfffffff0;g.cpu.r[2]=16;g.cpu.r[3]=0;g.cpu.r[4]=0;
    ctr_guest_svc(&g,0x25);CHECK(g.cpu.r[0]==0xe0e01bf5);
    many(&g,handles,0,false,0);CHECK(g.cpu.r[0]==0xe0e01bee);
    g.cpu.r[1]=3;ctr_guest_svc(&g,0x13);CHECK(g.cpu.r[0]==0xe0e01bee && !g.cpu.r[1]);
    g.cpu.r[1]=UINT32_MAX;g.cpu.r[2]=3;ctr_guest_svc(&g,0x15);CHECK(g.cpu.r[0]==0xe0e01bee);
    ctr_guest_close(&g);
}
static void shared_memory(void) {
    ctr_guest g;CHECK(ctr_kernel_fixture_open(&g));uint32_t value;
    g.cpu.r[0]=3;g.cpu.r[1]=g.cpu.r[2]=0;g.cpu.r[3]=4096;g.cpu.r[4]=3;ctr_guest_svc(&g,1);
    CHECK(!g.cpu.r[0]);uint32_t source=g.cpu.r[1];
    CHECK(ctr_memory_write(&g.memory,source,4,0x78563412)==CTR_MEM_OK);
    g.cpu.r[0]=3;g.cpu.r[1]=source;g.cpu.r[2]=4096;g.cpu.r[3]=3;ctr_guest_svc(&g,0x1e);
    CHECK(!g.cpu.r[0]);uint32_t shared=g.cpu.r[1];unsigned object=ctr_guest_handle_for(&g,shared)->object;
    CHECK(g.allocations[0].pins==1);
    g.cpu.r[0]=shared;g.cpu.r[1]=0x10001000;g.cpu.r[2]=1;g.cpu.r[3]=0x10000000;ctr_guest_svc(&g,0x1f);
    CHECK(!g.cpu.r[0] && g.shared[object].references==2);
    CHECK(ctr_memory_read(&g.memory,0x10001000,4,1,&value)==CTR_MEM_OK && value==0x78563412);
    CHECK(ctr_memory_write(&g.memory,0x10001000,4,0)==CTR_MEM_PERMISSION);
    g.cpu.r[2]=0x10001000;ctr_guest_svc(&g,2);CHECK(g.cpu.r[4]==6 && g.cpu.r[3]==1);
    g.cpu.r[0]=1;g.cpu.r[1]=source;g.cpu.r[2]=0;g.cpu.r[3]=4096;g.cpu.r[4]=0;ctr_guest_svc(&g,1);
    CHECK(g.cpu.r[0]==0xe0a01bf5 && g.allocations[0].pins==1);
    g.cpu.r[1]=shared;ctr_guest_svc(&g,0x27);CHECK(!g.cpu.r[0]);uint32_t duplicate=g.cpu.r[1];
    close_handle(&g,shared);CHECK(g.shared[object].references==2);
    g.cpu.r[0]=duplicate;g.cpu.r[1]=0x10002000;g.cpu.r[2]=3;g.cpu.r[3]=3;ctr_guest_svc(&g,0x1f);CHECK(!g.cpu.r[0]);
    CHECK(ctr_memory_write(&g.memory,0x10002000,4,0x11223344)==CTR_MEM_OK);
    CHECK(ctr_memory_read(&g.memory,source,4,1,&value)==CTR_MEM_OK && value==0x11223344);
    g.cpu.r[0]=duplicate;g.cpu.r[1]=0x10001000;ctr_guest_svc(&g,0x20);CHECK(!g.cpu.r[0]);
    g.cpu.r[0]=duplicate;g.cpu.r[1]=0x10002000;ctr_guest_svc(&g,0x20);CHECK(!g.cpu.r[0]);
    close_handle(&g,duplicate);CHECK(!g.shared[object].active && !g.allocations[0].pins);
    g.cpu.r[0]=1;g.cpu.r[1]=source;g.cpu.r[2]=0;g.cpu.r[3]=4096;g.cpu.r[4]=0;ctr_guest_svc(&g,1);CHECK(!g.cpu.r[0]);
    uint32_t owned=ctr_shared_create(&g,4096,1);CHECK(owned);object=ctr_guest_handle_for(&g,owned)->object;
    ctr_memory before=g.memory;
    g.cpu.r[0]=owned;g.cpu.r[1]=0x10003000;g.cpu.r[2]=3;g.cpu.r[3]=0x10000000;ctr_guest_svc(&g,0x1f);
    CHECK(g.cpu.r[0]==0xe0e01bee && !memcmp(&before,&g.memory,sizeof(before)));
    g.cpu.r[0]=owned;g.cpu.r[1]=0x10003000;g.cpu.r[2]=1;g.cpu.r[3]=0x10000000;ctr_guest_svc(&g,0x1f);CHECK(!g.cpu.r[0]);
    close_handle(&g,owned);CHECK(g.shared[object].active && g.shared[object].references==1);
    CHECK(ctr_memory_read(&g.memory,0x10003000,4,1,&value)==CTR_MEM_OK && !value);
    wait_one(&g,owned,0);CHECK(g.cpu.r[0]==0xd8e007f7);
    ctr_guest_close(&g); /* Live mapping without a handle retains ownership until close. */
}
static void exhaustion(void) {
    ctr_guest g;CHECK(ctr_kernel_fixture_open(&g));
    for(unsigned i=0;i<8;++i)CHECK(ctr_shared_create(&g,4096,3));
    CHECK(!ctr_shared_create(&g,4096,3));
    for(unsigned i=0;i<16;++i)(void)create(&g,0x13,0,0);
    g.cpu.r[1]=0;ctr_guest_svc(&g,0x13);CHECK(g.cpu.r[0]==0xd86007f3);
    for(unsigned i=0;i<16;++i)(void)create(&g,0x15,0,1);
    g.cpu.r[1]=0;g.cpu.r[2]=1;ctr_guest_svc(&g,0x15);CHECK(g.cpu.r[0]==0xd86007f3);
    ctr_guest_close(&g);CHECK(ctr_kernel_fixture_open(&g));
    uint32_t event=create(&g,0x17,0,0);
    for(unsigned i=1;i<64;++i){g.cpu.r[1]=event;ctr_guest_svc(&g,0x27);CHECK(!g.cpu.r[0]);}
    CHECK(!ctr_shared_create(&g,4096,1));
    for(unsigned i=0;i<8;++i)CHECK(!g.shared[i].active);
    g.cpu.r[1]=1;ctr_guest_svc(&g,0x13);CHECK(g.cpu.r[0]==0xd86007f3 && !g.mutexes[0].active);
    ctr_guest_close(&g);
}
int main(void) {
    synchronization();shared_memory();exhaustion();CHECK(ctr_kernel_selftest()==0);
    printf("{\"ok\":true,\"assertions\":%u,\"shared_memory_and_synchronization\":true,\"original_arm_kernel_probe\":true,\"console_verified\":false}\n",assertions);return 0;
}
