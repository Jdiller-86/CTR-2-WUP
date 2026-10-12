#include "ctr_thread_fixture.h"
#include "ctr_kernel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned assertions;
#define CHECK(x) do { ++assertions;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static uint32_t create(ctr_guest *g,uint32_t entry,uint32_t stack,uint32_t priority,uint32_t processor) {
    g->cpu.r[0]=priority;g->cpu.r[1]=entry;g->cpu.r[2]=0x12345678;g->cpu.r[3]=stack;g->cpu.r[4]=processor;
    ctr_guest_svc(g,8);return g->cpu.r[1];
}
static void contexts(void) {
    ctr_guest g;CHECK(ctr_thread_fixture_open(&g));
    g.cpu.cpsr=0xa8000010;g.cpu.r[12]=0x11223344;g.cpu.vfp[31]=0x3f800000;g.cpu.fpscr=0x80000000;
    uint32_t handle=create(&g,0x100300,0x0fffe000,0x30,0xfffffffe);CHECK(handle && !g.cpu.r[0]);
    unsigned child=ctr_guest_handle_for(&g,handle)->object;
    CHECK(g.threads[child].references==2 && g.threads[child].id==2);
    g.cpu.exclusive_valid=true;g.cpu.exclusive_generation=g.memory.generation;
    CHECK(ctr_thread_schedule(&g,true) && g.current_thread==child && !g.cpu.exclusive_valid);
    CHECK(g.cpu.r[0]==0x12345678 && g.cpu.r[13]==0x0fffe000 && g.cpu.r[15]==0x100300);
    CHECK(g.cpu.cpsr==0x10 && !g.cpu.vfp[31] && !g.cpu.fpscr && g.cpu.tls_address==0x1ff82200);
    g.cpu.r[12]=0x55667788;g.cpu.vfp[31]=0x40000000;g.cpu.fpscr=0x40000000;
    CHECK(ctr_thread_schedule(&g,true) && g.current_thread==0 && g.cpu.r[12]==0x11223344);
    CHECK(g.cpu.cpsr==0xa8000010 && g.cpu.vfp[31]==0x3f800000 && g.cpu.fpscr==0x80000000);
    CHECK(ctr_thread_schedule(&g,true) && g.cpu.r[12]==0x55667788 && g.cpu.vfp[31]==0x40000000 && g.cpu.fpscr==0x40000000);
    g.cpu.r[1]=CTR_GUEST_CURRENT_THREAD;ctr_guest_svc(&g,0x27);uint32_t duplicate=g.cpu.r[1];
    CHECK(duplicate && !g.cpu.r[0] && g.threads[child].references==3);
    g.cpu.r[1]=duplicate;ctr_guest_svc(&g,0x37);CHECK(!g.cpu.r[0] && g.cpu.r[1]==2);
    g.cpu.r[0]=duplicate;g.cpu.r[1]=0x18;ctr_guest_svc(&g,0x0c);CHECK(!g.cpu.r[0] && g.threads[child].priority==0x18);
    g.cpu.r[1]=duplicate;ctr_guest_svc(&g,0x0b);CHECK(!g.cpu.r[0] && g.cpu.r[1]==0x18);
    g.cpu.r[0]=duplicate;g.cpu.r[1]=0x17;ctr_guest_svc(&g,0x0c);CHECK(g.cpu.r[0]==0xe0e01bee && g.threads[child].priority==0x18);
    /* A strict-priority runnable worker wins even on a rotation. */
    CHECK(ctr_thread_schedule(&g,true) && g.current_thread==child);
    CHECK(ctr_guest_handle_close(&g,duplicate) && ctr_guest_handle_close(&g,handle));
    CHECK(g.threads[child].active && g.threads[child].references==1);
    ctr_guest_svc(&g,9);CHECK(!g.threads[child].active);
    CHECK(ctr_thread_schedule(&g,true) && g.current_thread==0);
    CHECK(!ctr_guest_handle_close(&g,handle));ctr_guest_close(&g);
}
static void creation_failures(void) {
    ctr_guest g;CHECK(ctr_thread_fixture_open(&g));
    const uint32_t cases[][4]={{0x100302,0x0fffe000,0x30,0},{0x8000000,0x0fffe000,0x30,0},
      {0x100300,0x0fffe004,0x30,0},{0x100300,0xdead0000,0x30,0},{0x100300,0x0fffe000,0x17,0},
      {0x100300,0x0fffe000,0x40,0},{0x100300,0x0fffe000,0x30,1}};
    for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);++i) {
        uint64_t generation=g.memory.generation;uint32_t id=g.next_thread_id;
        CHECK(!create(&g,cases[i][0],cases[i][1],cases[i][2],cases[i][3]) && g.cpu.r[0]);
        CHECK(!g.threads_enabled && g.next_thread_id==id && g.memory.generation==generation);
        for(unsigned j=1;j<CTR_GUEST_MAX_THREADS;++j)CHECK(!g.threads[j].active);
    }
    for(unsigned i=1;i<CTR_GUEST_MAX_THREADS;++i)CHECK(create(&g,0x100300,0x0fffe000,0x30,0));
    CHECK(!create(&g,0x100300,0x0fffe000,0x30,0) && g.cpu.r[0]==0xd86007f3);
    ctr_guest_close(&g);CHECK(ctr_thread_fixture_open(&g));
    uint32_t event=ctr_guest_event_create(&g,0);CHECK(event);
    for(unsigned i=1;i<64;++i)CHECK(ctr_guest_handle_new(&g,CTR_HANDLE_EVENT,0));
    CHECK(!create(&g,0x100300,0x0fffe000,0x30,0) && g.cpu.r[0]==0xd86007f3);
    CHECK(!g.threads_enabled && g.next_thread_id==2 && !g.threads[1].active);ctr_guest_close(&g);
}
static void waits_and_pins(void) {
    ctr_guest g;CHECK(ctr_thread_fixture_open(&g));
    g.cpu.r[0]=3;g.cpu.r[1]=g.cpu.r[2]=0;g.cpu.r[3]=4096;g.cpu.r[4]=3;ctr_guest_svc(&g,1);CHECK(!g.cpu.r[0]);
    uint32_t base=g.cpu.r[1],handle=create(&g,0x100300,base+4096,0x30,0);CHECK(handle && g.allocations[0].pins==1);
    g.cpu.r[0]=1;g.cpu.r[1]=base;g.cpu.r[2]=0;g.cpu.r[3]=4096;g.cpu.r[4]=0;ctr_guest_svc(&g,1);
    CHECK(g.cpu.r[0]==0xe0a01bf5 && g.allocations[0].pins==1);
    g.cpu.r[0]=handle;g.cpu.r[2]=10;g.cpu.r[3]=0;ctr_guest_svc(&g,0x24);CHECK(g.state==CTR_GUEST_WAITING);
    CHECK(ctr_thread_schedule(&g,false) && g.current_thread==1);
    /* Closing the handle does not release the waiter's join reference. */
    CHECK(ctr_guest_handle_close(&g,handle) && g.threads[1].references==2);
    ctr_guest_svc(&g,9);CHECK(g.threads[1].finished && !g.allocations[0].pins);
    CHECK(ctr_thread_schedule(&g,true) && g.current_thread==0 && !g.cpu.r[0]);
    CHECK(!g.wait_count && !g.threads[1].active);
    g.cpu.r[0]=1;g.cpu.r[1]=base;g.cpu.r[2]=0;g.cpu.r[3]=4096;g.cpu.r[4]=0;ctr_guest_svc(&g,1);CHECK(!g.cpu.r[0]);
    /* Reuse gets a fresh ID, TLS bytes and zero FP state. */
    CHECK(ctr_memory_write(&g.memory,0x1ff82200,4,0xdeadbeef)==CTR_MEM_OK);
    handle=create(&g,0x100300,0x0fffe000,0x30,0);CHECK(handle && g.threads[1].id==3);
    uint32_t value;CHECK(ctr_memory_read(&g.memory,0x1ff82200,4,1,&value)==CTR_MEM_OK && !value);
    g.cpu.r[0]=handle;g.cpu.r[2]=5;g.cpu.r[3]=0;ctr_guest_svc(&g,0x24);
    g.now_ns=5;ctr_threads_poll(&g);CHECK(g.state==CTR_GUEST_READY && g.cpu.r[0]==0x09401bfe);
    ctr_guest_close(&g);
}
static void mutex_owner_exit(void) {
    ctr_guest g;CHECK(ctr_thread_fixture_open(&g));
    g.cpu.r[1]=1;ctr_guest_svc(&g,0x13);uint32_t mutex=g.cpu.r[1];CHECK(mutex && !g.cpu.r[0]);
    uint32_t handle=create(&g,0x100300,0x0fffe000,0x30,0);CHECK(handle);
    CHECK(ctr_thread_schedule(&g,true));g.cpu.r[0]=mutex;ctr_guest_svc(&g,0x14);CHECK(g.cpu.r[0]==0xe0a01bf5);
    g.cpu.r[0]=mutex;g.cpu.r[2]=g.cpu.r[3]=UINT32_MAX;ctr_guest_svc(&g,0x24);CHECK(g.state==CTR_GUEST_WAITING);
    CHECK(ctr_thread_schedule(&g,true) && g.current_thread==0);ctr_guest_svc(&g,9);
    CHECK(ctr_thread_schedule(&g,true) && g.current_thread==1 && !g.cpu.r[0]);
    unsigned object=ctr_guest_handle_for(&g,mutex)->object;CHECK(g.mutexes[object].owner==2 && g.mutexes[object].depth==1);
    CHECK(ctr_guest_handle_close(&g,mutex) && g.mutexes[object].active);
    ctr_guest_svc(&g,9);CHECK(!g.mutexes[object].active);
    CHECK(!ctr_thread_schedule(&g,true) && g.process_exited && g.state==CTR_GUEST_EXITED);ctr_guest_close(&g);
}
static void arbiter(void) {
    ctr_guest g;CHECK(ctr_thread_fixture_open(&g));ctr_guest_svc(&g,0x21);
    uint32_t handle=g.cpu.r[1],address=0x08000200;CHECK(handle && !g.cpu.r[0]);
    unsigned object=ctr_guest_handle_for(&g,handle)->object;
    CHECK(ctr_memory_write(&g.memory,address,4,UINT32_MAX)==CTR_MEM_OK);
    g.cpu.r[0]=handle;g.cpu.r[1]=address;g.cpu.r[2]=4;g.cpu.r[3]=0;g.cpu.r[4]=0;g.cpu.r[5]=0;
    ctr_guest_svc(&g,0x22);uint32_t value;
    CHECK(g.state==CTR_GUEST_READY && g.cpu.r[0]==0x09401bfe && !g.wait_count);
    CHECK(ctr_memory_read(&g.memory,address,4,1,&value)==CTR_MEM_OK && value==0xfffffffe);
    /* Invalid timeout/access/type cannot decrement memory or retain an object. */
    const uint32_t invalid[][3]={{address+1,2,0},{0x100000,2,0},{address,5,0}};
    for(unsigned i=0;i<3;++i) {
        uint64_t gen=g.memory.generation;g.cpu.r[0]=handle;g.cpu.r[1]=invalid[i][0];g.cpu.r[2]=invalid[i][1];g.cpu.r[3]=invalid[i][2];
        ctr_guest_svc(&g,0x22);CHECK(g.cpu.r[0] && g.memory.generation==gen && g.arbiters[object].references==1);
    }
    g.now_ns=UINT64_MAX-1;g.cpu.r[0]=handle;g.cpu.r[1]=address;g.cpu.r[2]=4;g.cpu.r[3]=0;g.cpu.r[4]=2;g.cpu.r[5]=0;
    ctr_guest_svc(&g,0x22);CHECK(g.cpu.r[0]==0xe0e01bee && !g.wait_count);g.now_ns=0;
    CHECK(create(&g,0x100300,0x0fffe000,0x30,0));CHECK(create(&g,0x100300,0x0fffd000,0x20,0));
    /* Higher-priority thread 2 waits first, then thread 1. */
    CHECK(ctr_thread_schedule(&g,true) && g.current_thread==2);
    g.cpu.r[0]=handle;g.cpu.r[1]=address;g.cpu.r[2]=1;g.cpu.r[3]=0;ctr_guest_svc(&g,0x22);CHECK(g.state==CTR_GUEST_WAITING);
    CHECK(ctr_thread_schedule(&g,true) && g.current_thread==0);CHECK(ctr_thread_schedule(&g,true) && g.current_thread==1);
    g.cpu.r[0]=handle;g.cpu.r[1]=address;g.cpu.r[2]=2;g.cpu.r[3]=0;ctr_guest_svc(&g,0x22);CHECK(g.state==CTR_GUEST_WAITING);
    CHECK(ctr_thread_schedule(&g,true) && g.current_thread==0);
    CHECK(g.arbiters[object].references==3);
    g.cpu.r[0]=handle;g.cpu.r[1]=address+4;g.cpu.r[2]=0;g.cpu.r[3]=1;ctr_guest_svc(&g,0x22);
    CHECK(!g.cpu.r[0] && g.arbiters[object].references==3);
    g.cpu.r[0]=handle;g.cpu.r[1]=address;g.cpu.r[2]=0;g.cpu.r[3]=1;ctr_guest_svc(&g,0x22);
    CHECK(g.arbiters[object].references==2 && g.threads[2].state==CTR_GUEST_READY && !g.threads[2].r[0]);
    CHECK(g.threads[1].state==CTR_GUEST_WAITING);
    g.cpu.r[0]=handle;g.cpu.r[1]=address;g.cpu.r[2]=0;g.cpu.r[3]=UINT32_MAX;ctr_guest_svc(&g,0x22);
    CHECK(g.arbiters[object].references==1 && g.threads[1].state==CTR_GUEST_READY && !g.threads[1].r[0]);
    /* A wait retains a closed arbiter and releases it at timeout. */
    g.cpu.r[0]=handle;g.cpu.r[1]=address;g.cpu.r[2]=3;g.cpu.r[3]=0;g.cpu.r[4]=10;g.cpu.r[5]=0;ctr_guest_svc(&g,0x22);
    CHECK(g.state==CTR_GUEST_WAITING && ctr_guest_handle_close(&g,handle));CHECK(g.arbiters[object].active);
    g.now_ns=10;ctr_threads_poll(&g);CHECK(g.state==CTR_GUEST_READY && g.cpu.r[0]==0x09401bfe && !g.arbiters[object].active);
    ctr_guest_close(&g);
}
static void budget_invariance(void) {
    ctr_guest paths[3];const unsigned budgets[]={1,17,4096};
    for(unsigned i=0;i<3;++i) {
        CHECK(ctr_thread_fixture_open(&paths[i]));
        for(unsigned n=0;n<1000 && paths[i].state==CTR_GUEST_READY;++n)ctr_guest_run(&paths[i],budgets[i],0,i==1);
        CHECK(paths[i].state==CTR_GUEST_WAITING);
        for(unsigned n=0;n<1000 && paths[i].state!=CTR_GUEST_EXITED;++n)ctr_guest_run(&paths[i],budgets[i],1000000,i==1);
        CHECK(!ctr_thread_fixture_check(&paths[i]));
        if(i) { CHECK(paths[i].cpu.instructions==paths[0].cpu.instructions);CHECK(!memcmp(paths[i].storage[2],paths[0].storage[2],paths[0].storage_size[2])); }
    }
    for(unsigned i=0;i<3;++i)ctr_guest_close(&paths[i]);
}
static void resource_limits(void) {
    ctr_guest g;CHECK(ctr_thread_fixture_open(&g));g.cpu.r[1]=0xffff8001;ctr_guest_svc(&g,0x38);
    uint32_t handle=g.cpu.r[1];CHECK(handle && !g.cpu.r[0] && g.resource_references==1);
    for(unsigned i=0;i<9;++i)CHECK(ctr_memory_write(&g.memory,0x08000200+i*4,4,i)==CTR_MEM_OK);
    g.cpu.r[0]=0x08000300;g.cpu.r[1]=handle;g.cpu.r[2]=0x08000200;g.cpu.r[3]=9;ctr_guest_svc(&g,0x39);CHECK(!g.cpu.r[0]);
    const uint32_t limits[]={0x18,64*1024*1024,8,32,16,16,0,8,8};uint32_t value;
    for(unsigned i=0;i<9;++i) { CHECK(ctr_memory_read(&g.memory,0x08000300+i*8,4,1,&value)==CTR_MEM_OK && value==limits[i]);CHECK(ctr_memory_read(&g.memory,0x08000304+i*8,4,1,&value)==CTR_MEM_OK && !value); }
    g.cpu.r[0]=0x08000300;g.cpu.r[1]=handle;g.cpu.r[2]=0x08000200;g.cpu.r[3]=9;ctr_guest_svc(&g,0x3a);CHECK(!g.cpu.r[0]);
    /* The diagnostic shared-time page adds one 4 KiB mapping. */
    CHECK(ctr_memory_read(&g.memory,0x08000308,4,1,&value)==CTR_MEM_OK && value==36864);
    CHECK(ctr_memory_read(&g.memory,0x08000310,4,1,&value)==CTR_MEM_OK && value==1);
    /* Preflight the complete type list before any output, including aliases. */
    CHECK(ctr_memory_write(&g.memory,0x08000220,4,9)==CTR_MEM_OK);uint64_t generation=g.memory.generation;
    g.cpu.r[0]=0x08000300;g.cpu.r[1]=handle;g.cpu.r[2]=0x08000200;g.cpu.r[3]=9;ctr_guest_svc(&g,0x39);
    CHECK(g.cpu.r[0]==0xe0e01bee && g.memory.generation==generation);
    g.cpu.r[0]=0x08000fc0;g.cpu.r[1]=handle;g.cpu.r[2]=0x08000200;g.cpu.r[3]=9;ctr_guest_svc(&g,0x39);CHECK(g.cpu.r[0]==0xe0e01bf5 && g.memory.generation==generation);
    CHECK(ctr_guest_handle_close(&g,handle) && !g.resource_references);
    g.cpu.r[1]=handle;ctr_guest_svc(&g,0x39);CHECK(g.cpu.r[0]==0xd8e007f7);ctr_guest_close(&g);
}
static void fp_word_faults(void) {
    ctr_guest g;CHECK(ctr_thread_fixture_open(&g));uint32_t address=0x08000ffc;
    CHECK(ctr_memory_write(&g.memory,address,4,0x12345678)==CTR_MEM_OK);g.cpu.r[15]=0x100000;
    g.cpu.vfp[1]=0x87654321;g.cpu.fpscr=0x03000000;
    CHECK(ctr_cpu_fp_transfer(&g.cpu,0,2,address,true,0xed9c0b00)==CTR_CPU_MEMORY_FAULT);
    CHECK(g.cpu.vfp[0]==0x12345678 && g.cpu.vfp[1]==0x87654321 && g.cpu.fpscr==0x03000000 && g.cpu.r[15]==0x100000);
    g.cpu.vfp[0]=0xaabbccdd;CHECK(ctr_cpu_fp_transfer(&g.cpu,0,2,address,false,0xed8c0b00)==CTR_CPU_MEMORY_FAULT);
    uint32_t value;CHECK(ctr_memory_read(&g.memory,address,4,1,&value)==CTR_MEM_OK && value==0xaabbccdd);
    CHECK(ctr_cpu_fp_transfer(&g.cpu,32,2,address,true,0xed9c0b00)==CTR_CPU_UNSUPPORTED);
    ctr_guest_close(&g);
}
static void signed_halves(void) {
    static const uint32_t rows[][5]={{0x8000,0x8000,0x7fffffff,0xbfffffff,1},
        {0x8000,0x7fff,0x80000000,0x40008000,1},{0xffff,2,5,3,0},{0x7fff,0x7fff,0,0x3fff0001,0}};
    for(unsigned i=0;i<4;++i) {
        ctr_guest g;CHECK(ctr_thread_fixture_open(&g));uint32_t opcode=0xe1042381;
        for(unsigned byte=0;byte<4;++byte)g.storage[0][byte]=(uint8_t)(opcode>>(8*byte));
        g.cpu.r[1]=rows[i][0];g.cpu.r[3]=rows[i][1];g.cpu.r[2]=rows[i][2];g.cpu.cpsr=0xa0050010;
        CHECK(ctr_cpu_step(&g.cpu)==CTR_CPU_OK && g.cpu.r[4]==rows[i][3]);
        CHECK(g.cpu.cpsr==(0xa0050010u|(rows[i][4]?0x08000000u:0)));
        g.cpu.r[15]=0x100000;g.cpu.r[1]=1;g.cpu.r[3]=2;g.cpu.r[2]=3;g.cpu.cpsr|=0x08000000;
        CHECK(ctr_cpu_step(&g.cpu)==CTR_CPU_OK && g.cpu.r[4]==5 && (g.cpu.cpsr&0x08000000));
        ctr_guest_close(&g);
    }
    for(unsigned operand=0;operand<4;++operand)for(unsigned skipped=0;skipped<2;++skipped) {
        static const unsigned shifts[]={16,12,8,0};ctr_guest g;CHECK(ctr_thread_fixture_open(&g));
        uint32_t opcode=(0xe1042381u&~(15u<<shifts[operand]))|(15u<<shifts[operand]);
        if(skipped)opcode&=0x0fffffff; /* EQ while Z is clear. */
        for(unsigned byte=0;byte<4;++byte)g.storage[0][byte]=(uint8_t)(opcode>>(8*byte));
        uint32_t before[16];memcpy(before,g.cpu.r,sizeof(before));uint32_t flags=g.cpu.cpsr;
        CHECK(ctr_cpu_step(&g.cpu)==(skipped?CTR_CPU_OK:CTR_CPU_UNSUPPORTED));
        CHECK(g.cpu.cpsr==flags && !memcmp(before,g.cpu.r,15*sizeof(*before)) && g.cpu.r[15]==0x100000+(skipped?4:0));
        ctr_guest_close(&g);
    }
}
int main(void) {
    contexts();creation_failures();waits_and_pins();mutex_owner_exit();arbiter();budget_invariance();resource_limits();fp_word_faults();signed_halves();
    int result=ctr_thread_selftest();if(result)fprintf(stderr,"thread_selftest=%d\n",result);CHECK(result==0);
    printf("{\"ok\":true,\"assertions\":%u,\"original_arm_thread_scheduler\":true,\"console_verified\":false}\n",assertions);return 0;
}
