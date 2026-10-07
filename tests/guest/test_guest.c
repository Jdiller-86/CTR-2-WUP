#include "ctr_guest_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks,debugs;
#define CHECK(expr) do { ++checks; if (!(expr)) { fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,#expr); exit(1); } } while (0)
static void debug(void *context,const char *message) { (void)context; CHECK(!strcmp(message,"CTR2WUP ARM guest framebuffer fixture")); ++debugs; }
static void loader_tests(void) {
    ctr_guest guest; uint32_t value;
    CHECK(ctr_frame_open(&guest));
    CHECK(guest.cpu.r[15]==0x100000 && guest.cpu.r[13]==0x10000000 && guest.cpu.cpsr==0x10);
    CHECK(guest.tls_address==0x1ff82000);
    CHECK(ctr_memory_read(&guest.memory,CTR_FRAME_TOP,4,CTR_MEM_READ,&value)==CTR_MEM_OK && value==0);
    CHECK(ctr_memory_write(&guest.memory,0x100000,4,1)==CTR_MEM_PERMISSION);
    CHECK(ctr_memory_read(&guest.memory,CTR_FRAME_DATA,4,CTR_MEM_EXEC,&value)==CTR_MEM_PERMISSION);
    CHECK(!ctr_frame_input(&guest,0,true,320,0));
    ctr_guest_close(&guest);
    CHECK(guest.state==CTR_GUEST_EXITED && guest.memory.count==0);
    ctr_guest_image bad=ctr_frame_image;
    bad.text.address=0x08000000; CHECK(!ctr_guest_open(&guest,&bad,NULL,0));
    CHECK(guest.state==CTR_GUEST_FAULTED && guest.fault==CTR_CPU_MEMORY_FAULT && guest.memory.count==0);
    ctr_guest_close(&guest);
    bad=ctr_frame_image; bad.text.size=0; CHECK(!ctr_guest_open(&guest,&bad,NULL,0)); ctr_guest_close(&guest);
    bad=ctr_frame_image; bad.text.size=3; CHECK(!ctr_guest_open(&guest,&bad,NULL,0)); ctr_guest_close(&guest);
    bad=ctr_frame_image; bad.stack_size=3; CHECK(!ctr_guest_open(&guest,&bad,NULL,0)); ctr_guest_close(&guest);
    bad=ctr_frame_image; bad.code_size=100; CHECK(!ctr_guest_open(&guest,&bad,NULL,0)); ctr_guest_close(&guest);
    bad=ctr_frame_image; bad.data.size=4095; CHECK(!ctr_guest_open(&guest,&bad,NULL,0)); ctr_guest_close(&guest);
    bad=ctr_frame_image; bad.bss_size=0xffffffffu; CHECK(!ctr_guest_open(&guest,&bad,NULL,0)); ctr_guest_close(&guest);
    bad=ctr_frame_image; bad.data.address=0xfffff000u; CHECK(!ctr_guest_open(&guest,&bad,NULL,0)); ctr_guest_close(&guest);
    bad=ctr_frame_image; bad.stack_size=128u*1024u*1024u; CHECK(!ctr_guest_open(&guest,&bad,NULL,0)); ctr_guest_close(&guest);
}
static uint32_t event(ctr_guest *g,unsigned reset) {
    g->cpu.r[1]=reset; CHECK(ctr_guest_svc(g,0x17)==CTR_GUEST_READY && g->cpu.r[0]==0); return g->cpu.r[1];
}
static void wait(ctr_guest *g,uint32_t handle,uint64_t nanos) {
    g->cpu.r[0]=handle; g->cpu.r[2]=(uint32_t)nanos; g->cpu.r[3]=(uint32_t)(nanos>>32); ctr_guest_svc(g,0x24);
}
static void kernel_tests(void) {
    ctr_guest g; CHECK(ctr_frame_open(&g));
    uint32_t first=event(&g,0);
    wait(&g,first,0); CHECK(g.cpu.r[0]==0x09401bfe && g.state==CTR_GUEST_READY);
    CHECK(ctr_guest_signal(&g,first)); wait(&g,first,0); CHECK(g.cpu.r[0]==0);
    wait(&g,first,0); CHECK(g.cpu.r[0]==0x09401bfe); /* one-shot consumed */
    g.cpu.r[1]=first; ctr_guest_svc(&g,0x27); CHECK(g.cpu.r[0]==0); uint32_t duplicate=g.cpu.r[1];
    g.cpu.r[0]=first; ctr_guest_svc(&g,0x23); CHECK(g.cpu.r[0]==0 && !ctr_guest_signal(&g,first));
    wait(&g,duplicate,100); CHECK(g.state==CTR_GUEST_WAITING);
    CHECK(ctr_guest_run(&g,0,99,false)==CTR_GUEST_WAITING);
    CHECK(ctr_guest_signal(&g,duplicate) && g.state==CTR_GUEST_READY && g.cpu.r[0]==0);
    wait(&g,duplicate,100); CHECK(g.state==CTR_GUEST_WAITING);
    CHECK(ctr_guest_run(&g,0,199,false)==CTR_GUEST_READY && g.cpu.r[0]==0x09401bfe);
    uint32_t sticky=event(&g,1); CHECK(ctr_guest_signal(&g,sticky));
    wait(&g,sticky,0); CHECK(g.cpu.r[0]==0); wait(&g,sticky,0); CHECK(g.cpu.r[0]==0);
    g.cpu.r[0]=sticky; ctr_guest_svc(&g,0x19); wait(&g,sticky,0); CHECK(g.cpu.r[0]==0x09401bfe);
    wait(&g,sticky,UINT64_MAX); CHECK(g.state==CTR_GUEST_WAITING);
    CHECK(ctr_guest_run(&g,0,1000000000,false)==CTR_GUEST_WAITING);
    CHECK(ctr_guest_signal(&g,sticky) && g.state==CTR_GUEST_READY);
    g.cpu.r[0]=123; ctr_guest_svc(&g,0x23); CHECK(g.cpu.r[0]==0xd8e007f7);
    g.cpu.r[0]=500; g.cpu.r[1]=0; ctr_guest_svc(&g,0x0a); CHECK(g.state==CTR_GUEST_SLEEPING);
    CHECK(ctr_guest_run(&g,0,1000000499,false)==CTR_GUEST_SLEEPING);
    CHECK(ctr_guest_run(&g,0,1000000500,false)==CTR_GUEST_READY);
    g.cpu.r[0]=CTR_FRAME_DATA+32; g.cpu.r[1]=36;
    CHECK(ctr_guest_svc(&g,0x3d)==CTR_GUEST_READY && g.cpu.r[0]==0);
    g.cpu.r[0]=g.cpu.r[1]=0; ctr_guest_svc(&g,0x0a); CHECK(g.state==CTR_GUEST_YIELDED);
    CHECK(ctr_guest_run(&g,0,2000000000,false)==CTR_GUEST_READY);
    ctr_guest_svc(&g,0x28); CHECK(g.cpu.r[0]==536223712u && g.cpu.r[1]==0);
    CHECK(ctr_guest_svc(&g,0x32)==CTR_GUEST_FAULTED && g.fault_detail==0x32);
    ctr_guest_close(&g);
    CHECK(ctr_frame_open(&g)); g.cpu.r[1]=2;
    CHECK(ctr_guest_svc(&g,0x17)==CTR_GUEST_FAULTED); ctr_guest_close(&g);
    CHECK(ctr_frame_open(&g));
    for (unsigned i=0;i<32;++i) (void)event(&g,0);
    g.cpu.r[1]=0; ctr_guest_svc(&g,0x17); CHECK(g.cpu.r[0]==0xd86007f3);
    ctr_guest_close(&g);
    CHECK(ctr_frame_open(&g)); g.cpu.r[0]=0xffffffffu; g.cpu.r[1]=2;
    CHECK(ctr_guest_svc(&g,0x3d)==CTR_GUEST_FAULTED); ctr_guest_close(&g);
    CHECK(ctr_frame_open(&g)); CHECK(ctr_guest_run(&g,0,100,false)==CTR_GUEST_READY);
    CHECK(ctr_guest_run(&g,0,99,false)==CTR_GUEST_FAULTED); ctr_guest_close(&g);
}
static void next_frame(ctr_guest *g,uint32_t target,uint64_t *clock,bool translated) {
    uint32_t frames=0;
    for (unsigned i=0;i<100 && frames<target;++i) {
        CHECK(ctr_guest_run(g,50000,*clock,translated)!=CTR_GUEST_FAULTED);
        *clock+=1000000;
        CHECK(ctr_frame_count(g,&frames));
    }
    CHECK(frames==target);
}
static void execution_tests(void) {
    ctr_guest interpreted,translated; uint64_t clock_a=0,clock_b=0; uint32_t value;
    CHECK(ctr_frame_open(&interpreted) && ctr_frame_open(&translated));
    interpreted.debug=translated.debug=debug;
    CHECK(ctr_frame_input(&interpreted,0,false,0,0) && ctr_frame_input(&translated,0,false,0,0));
    next_frame(&interpreted,1,&clock_a,false); next_frame(&translated,1,&clock_b,true);
    CHECK(interpreted.cpu.instructions==translated.cpu.instructions && translated.cpu.fallback_instructions==0);
    CHECK(!memcmp(interpreted.storage[2],translated.storage[2],interpreted.storage_size[2]));
    CHECK(ctr_memory_read(&translated.memory,CTR_FRAME_TOP,4,CTR_MEM_READ,&value)==CTR_MEM_OK && value==0xffc03010);
    CHECK(ctr_frame_input(&interpreted,1,true,100,80) && ctr_frame_input(&translated,1,true,100,80));
    next_frame(&interpreted,2,&clock_a,false); next_frame(&translated,2,&clock_b,true);
    CHECK(!memcmp(interpreted.cpu.r,translated.cpu.r,sizeof(interpreted.cpu.r)) && interpreted.cpu.cpsr==translated.cpu.cpsr);
    CHECK(!memcmp(interpreted.storage[2],translated.storage[2],interpreted.storage_size[2]));
    CHECK(ctr_memory_read(&translated.memory,CTR_FRAME_TOP,4,CTR_MEM_READ,&value)==CTR_MEM_OK && value==0xff22bb10);
    CHECK(ctr_memory_read(&translated.memory,CTR_FRAME_BOTTOM+((80*320+100)*4),4,CTR_MEM_READ,&value)==CTR_MEM_OK && value==0xffffffffu);
    CHECK(ctr_memory_read(&translated.memory,CTR_FRAME_BOTTOM,4,CTR_MEM_READ,&value)==CTR_MEM_OK && value==0xffbb2277u);
    CHECK(ctr_frame_exit(&translated));
    CHECK(ctr_guest_run(&translated,1000,clock_b+1000000,true)==CTR_GUEST_EXITED);
    CHECK(debugs==2);
    ctr_guest_close(&interpreted); ctr_guest_close(&translated);
}
int main(void) {
    loader_tests(); kernel_tests(); execution_tests();
    printf("{\"ok\":true,\"assertions\":%u,\"fixture_frames_per_path\":2,\"aot_interpreter_memory_match\":true,\"console_verified\":false}\n",checks);
    return 0;
}
