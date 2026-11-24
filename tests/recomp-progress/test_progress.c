#define _POSIX_C_SOURCE 200809L
#include "ctr_recomp.h"
#include "recomp.h"
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <signal.h>
#include <unistd.h>
#endif

static unsigned checks,calls,mode;
static unsigned span_reads,span_writes;
#define CHECK(x) do { ++checks;if(!(x)) { fprintf(stderr,"FAIL line %u: %s\n",__LINE__,#x);exit(1); } } while(0)
#ifdef _WIN32
static HANDLE deadline_event,deadline_worker;
static DWORD WINAPI deadline(void *event) {
    DWORD result=WaitForSingleObject((HANDLE)event,2000);
    if(result==WAIT_OBJECT_0)return 0;
    static const char text[]="FAIL: generated execution did not return (Windows watchdog)\n";
    DWORD written=0;
    BOOL logged=WriteFile(GetStdHandle(STD_ERROR_HANDLE),text,(DWORD)(sizeof(text)-1),&written,NULL);
    (void)logged;
    /* End the entire test, including a stuck main thread, without CRT locks. */
    if(!TerminateProcess(GetCurrentProcess(),1))_Exit(1);
    return 1;
}
static void watchdog_start(void) {
    deadline_event=CreateEventA(NULL,TRUE,FALSE,NULL);CHECK(deadline_event!=NULL);
    deadline_worker=CreateThread(NULL,0,deadline,deadline_event,0,NULL);CHECK(deadline_worker!=NULL);
}
static void watchdog_stop(void) {
    CHECK(SetEvent(deadline_event));
    CHECK(WaitForSingleObject(deadline_worker,1000)==WAIT_OBJECT_0);
    CHECK(CloseHandle(deadline_worker));CHECK(CloseHandle(deadline_event));
}
#else
static void deadline(int signal_number) {
    (void)signal_number;
    static const char text[]="FAIL: generated execution did not return\n";
    ssize_t written=write(STDERR_FILENO,text,sizeof(text)-1);
    (void)written;_Exit(1);
}
static void watchdog_start(void) { CHECK(signal(SIGALRM,deadline)!=SIG_ERR);alarm(2); }
static void watchdog_stop(void) { alarm(0); }
#endif
static void entry(Context *ctx) {
    ++calls;
    if(mode==9) {
        CHECK(ctx->host->lookup(ctx,0x100000u)==entry);
        CHECK(!ctx->host->lookup(ctx,0x100001u));
        CHECK(!ctx->host->lookup(ctx,0x100100u));
        CHECK(!ctx->host->lookup(ctx,0x100200u));
        CHECK(ctx->host->lookup(ctx,0x100201u)==entry);
        CHECK(!ctx->host->lookup(ctx,0x0ffffcu));
        CHECK(!ctx->host->lookup(ctx,0x100300u));
        CTR_INSTRUCTION(0x100000u,0xe3a00000u);
        ctx->r[15]=0x100004u;
        return;
    }
    if(mode==8) {
        CTR_INSTRUCTION(0x10001cu,0xe5810000u);
        mem_write32(ctx,ctx->r[1],ctx->r[0]);
        ctx->r[2]=mem_read32(ctx,ctx->r[1]);
        ctx->r[15]=0x100020u;
        return;
    }
    if(mode==7) {
        CTR_INSTRUCTION(0x10001cu,0xe5810000u);
        mem_write32(ctx,ctx->r[1],ctx->r[0]);
        CTR_INSTRUCTION(0x100020u,0xe3a00000u);
        ctx->r[0]=0;
        CTR_INSTRUCTION(0x100010u,0xed2d8b02u);
        mem_write32(ctx,ctx->r[1]+1u,ctx->vfp[16]);
        return;
    }
    if(mode==6) {
        CTR_INSTRUCTION(0x100000u,0xe3a00000u);
        if(!ctx->depth) { ctx->depth=1;++ctx->r[11]; }
        if(++ctx->r[12]==65u) {
            ctx->r[1]=CTR_GUEST_CURRENT_THREAD;ctx->svc=0x37;ctx->exit=EXIT_SVC;
        }
        return;
    }
    if(mode==5) {
        CTR_INSTRUCTION(0x10001cu,0xe5810000u);
        INTERPRET(0x10001cu,0xe5810000u);
        CTR_INSTRUCTION(0x100020u,0xe3a00000u);
        ctx->r[0]=0;ctx->r[15]=0x100024u;return;
    }
    if(mode>=4) {
        uint32_t pc=ctx->r[15],op=pc==0x100010?0xed2d8b02u:pc==0x100014?0xecbd8b02u:0xee300a00u;
        if(!ctr_recomp_instruction(ctx,pc,op))return;
        if(pc==0x100010) {
            uint32_t at=ctx->r[13]-8u;
            uint8_t *span=mem_write_span(ctx,at,8u);
            if(span) {
                ++span_writes;
                store32(span,ctx->vfp[16]);store32(span+4,ctx->vfp[17]);
            } else {
                mem_write32(ctx,at,ctx->vfp[16]);mem_write32(ctx,at+4u,ctx->vfp[17]);
            }
            ctx->r[13]=at;
        } else if(pc==0x100014) {
            uint32_t at=ctx->r[13];
            const uint8_t *span=mem_read_span(ctx,at,8u);
            if(span) {
                ++span_reads;
                ctx->vfp[16]=load32(span);ctx->vfp[17]=load32(span+4);
            } else {
                ctx->vfp[16]=mem_read32(ctx,at);ctx->vfp[17]=mem_read32(ctx,at+4u);
            }
            ctx->r[13]=at+8u;
        } else ctx->vfp[0]=0; /* Unsupported controls must abort before this. */
        ctx->r[15]=pc+4u;return;
    }
    if(mode==2 && calls%64==0) {
        if(ctr_recomp_instruction(ctx,ctx->r[15],0xe3a00000))ctx->r[15]+=4;
    } else {
        if(mode==3)ctx->r[15]=ctx->r[15]==0x100000?0x100004:0x100000;
        if(mode)ctx->exit=EXIT_UNWIND;
    }
}
const uint32_t recomp_abi=4,recomp_generation=3,recomp_entry_count=10,recomp_module_count=0;
const Entry recomp_entries[]={{0x100000,entry},{0x100004,entry},{0x100008,entry},{0x10000c,entry},{0x100010,entry},
    {0x100014,entry},{0x100018,entry},{0x10001c,entry},{0x100020,entry},{0x100201,entry}};
static const Piece pieces[]={{0x100000,0x100024,UINT64_C(0xde97987107677125)}};
const Origins recomp_origins[]={{.piece_count=1,.pieces=pieces}};
static const uint8_t code[4096]={0,0,0xa0,0xe3,0,0,0xa0,0xe3,0,0,0xa0,0xe3,0,0,0xa0,0xe3,
    2,0x8b,0x2d,0xed,2,0x8b,0xbd,0xec,0,0x0a,0x30,0xee,0,0,0x81,0xe5,0,0,0xa0,0xe3};
static const ctr_guest_image image={code,sizeof(code),{0x100000,1,36},{0,0,0},{0,0,0},0,4096,"original-progress-fixture"};
int main(void) {
    watchdog_start();
    for(mode=0;mode<4;++mode) {
        ctr_guest g;calls=0;
        CHECK(ctr_guest_open(&g,&image,NULL,0));CHECK(ctr_recomp_attach(&g,false));
        ctr_guest_state state=ctr_guest_run(&g,mode==2?4:1,0,true);
        if(mode==2) {
            CHECK(state==CTR_GUEST_READY && g.cpu.instructions==4 && calls==256);
            CHECK(g.cpu.r[15]==0x100010 && g.fault==CTR_CPU_OK);
        } else {
            CHECK(state==CTR_GUEST_FAULTED && g.fault==CTR_CPU_MISSING_BLOCK);
            CHECK(g.cpu.instructions==0 && calls==64 && g.cpu.fallback_instructions==0);
            CHECK(g.fault_detail==0x100000);
        }
        ctr_guest_close(&g);
    }
    mode=4;ctr_guest g;CHECK(ctr_guest_open(&g,&image,NULL,0));CHECK(ctr_recomp_attach(&g,false));
    g.cpu.r[15]=0x100010;g.cpu.fpscr=0x03000000;g.cpu.vfp[16]=0x7fc01234;g.cpu.vfp[17]=1;
    uint32_t stack=g.cpu.r[13],value;
    CHECK(ctr_guest_run(&g,1,0,true)==CTR_GUEST_READY && g.cpu.r[13]==stack-8u);
    CHECK(ctr_memory_read(&g.memory,stack-8u,4,CTR_MEM_READ,&value)==CTR_MEM_OK && value==0x7fc01234u);
    CHECK(ctr_memory_read(&g.memory,stack-4u,4,CTR_MEM_READ,&value)==CTR_MEM_OK && value==1u);
    g.cpu.vfp[16]=g.cpu.vfp[17]=0;
    CHECK(ctr_guest_run(&g,1,0,true)==CTR_GUEST_READY && g.cpu.r[13]==stack);
    CHECK(g.cpu.vfp[16]==0x7fc01234u && g.cpu.vfp[17]==1 && g.cpu.fpscr==0x03000000 && g.cpu.instructions==2);
    g.cpu.vfp[0]=0x3f800000;
    CHECK(ctr_guest_run(&g,1,0,true)==CTR_GUEST_FAULTED && g.fault==CTR_CPU_UNSUPPORTED);
    CHECK(g.cpu.r[15]==0x100018 && g.cpu.vfp[0]==0x3f800000 && g.cpu.fpscr==0x03000000);
    ctr_guest_close(&g);
    static _Alignas(4096) uint8_t store_page[8192];
    CHECK(ctr_guest_open(&g,&image,NULL,0));CHECK(ctr_recomp_attach(&g,false));
    CHECK(ctr_memory_map(&g.memory,0x09000000,sizeof(store_page),CTR_MEM_READ|CTR_MEM_WRITE,
                         store_page,sizeof(store_page))==CTR_MEM_OK);
    for(unsigned run=0;run<2;++run) {
        ctr_memory_resource_key key;
        CHECK(ctr_memory_resource_key_capture(&g.memory,0x09000000,8,CTR_MEM_READ,&key));
        uint64_t generation=g.memory.generation,version=g.memory.resource_clock;
        g.cpu.r[15]=0x100010;g.cpu.r[13]=0x09000008;g.cpu.vfp[16]=run+1u;g.cpu.vfp[17]=run+2u;
        CHECK(g.executor(&g.cpu,g.executor_context,1)==CTR_CPU_LIMIT);
        CHECK(!ctr_memory_resource_key_current(&g.memory,&key));
        CHECK(g.memory.generation==generation+2u);
        CHECK(store_page[0]==run+1u && store_page[4]==run+2u);
        if(run)CHECK(g.memory.resource_clock==version+1u);
    }
    CHECK(span_writes==1u);
    ctr_memory_resource_key first,second;
    CHECK(ctr_memory_resource_key_capture(&g.memory,0x09000ffcu,4,CTR_MEM_READ,&first));
    CHECK(ctr_memory_resource_key_capture(&g.memory,0x09001000u,4,CTR_MEM_READ,&second));
    uint64_t generation=g.memory.generation;
    g.cpu.exclusive_valid=true;g.cpu.exclusive_size=4;g.cpu.exclusive_generation=generation;
    g.cpu.r[15]=0x100010;g.cpu.r[13]=0x09001004;g.cpu.vfp[16]=0x12345678;g.cpu.vfp[17]=0xfedcba98;
    CHECK(g.executor(&g.cpu,g.executor_context,1)==CTR_CPU_LIMIT);
    CHECK(span_writes==2u && g.memory.generation==generation+2u && !g.cpu.exclusive_valid);
    CHECK(!ctr_memory_resource_key_current(&g.memory,&first) && !ctr_memory_resource_key_current(&g.memory,&second));
    for(unsigned run=0;run<2;++run) {
        g.cpu.r[15]=0x100014;g.cpu.r[13]=0x09000ffc;g.cpu.vfp[16]=g.cpu.vfp[17]=0;
        CHECK(g.executor(&g.cpu,g.executor_context,1)==CTR_CPU_LIMIT);
        CHECK(g.cpu.vfp[16]==0x12345678 && g.cpu.vfp[17]==0xfedcba98);
    }
    CHECK(span_reads==1u);
    g.cpu.r[15]=0x100010;g.cpu.r[13]=0x09002004;
    generation=g.memory.generation;
    CHECK(g.executor(&g.cpu,g.executor_context,1)==CTR_CPU_MEMORY_FAULT);
    CHECK(g.cpu.r[15]==0x100010 && g.cpu.r[13]==0x09002004 && span_writes==2u);
    CHECK(g.memory.generation==generation+1u && load32(store_page+sizeof(store_page)-4u)==0x12345678);
    ctr_guest_close(&g);
    CHECK(ctr_guest_open(&g,&image,NULL,0));CHECK(ctr_recomp_attach(&g,false));
    CHECK(ctr_memory_map(&g.memory,0x09000000,32,CTR_MEM_READ|CTR_MEM_WRITE,store_page+1,32)==CTR_MEM_OK);
    for(unsigned run=0;run<2;++run) {
        g.cpu.r[15]=0x100010;g.cpu.r[13]=0x09000008;g.cpu.vfp[16]=0x10203040u;g.cpu.vfp[17]=0x50607080u;
        CHECK(g.executor(&g.cpu,g.executor_context,1)==CTR_CPU_LIMIT);
        CHECK(load32(store_page+1)==0x10203040u && load32(store_page+5)==0x50607080u);
        g.cpu.r[15]=0x100014;g.cpu.vfp[16]=g.cpu.vfp[17]=0;
        CHECK(g.executor(&g.cpu,g.executor_context,1)==CTR_CPU_LIMIT);
        CHECK(g.cpu.vfp[16]==0x10203040u && g.cpu.vfp[17]==0x50607080u);
    }
    CHECK(span_reads==1u && span_writes==2u);
    ctr_guest_close(&g);
    CHECK(ctr_guest_open(&g,&image,NULL,0));CHECK(ctr_recomp_attach(&g,true));
    g.cpu.r[15]=0x100018;g.cpu.fpscr=0x03000000;g.cpu.vfp[0]=0x3fa00000;
    CHECK(ctr_guest_run(&g,1,0,true)==CTR_GUEST_READY);
    CHECK(g.cpu.r[15]==0x10001c && g.cpu.vfp[0]==0x40200000 && g.cpu.fpscr==0x03000000);
    CHECK(g.cpu.instructions==1 && g.cpu.fallback_instructions==1);
    ctr_guest_close(&g);
    mode=5;
    CHECK(ctr_guest_open(&g,&image,NULL,0));
    CHECK(ctr_memory_map(&g.memory,0x09000000,4096,CTR_MEM_READ|CTR_MEM_WRITE,g.storage[0],4096)==CTR_MEM_OK);
    CHECK(ctr_recomp_attach(&g,true));
    g.cpu.r[15]=0x10001c;g.cpu.r[0]=0x12345678;g.cpu.r[1]=0x09000020;
    CHECK(ctr_guest_run(&g,2,0,true)==CTR_GUEST_FAULTED && g.fault==CTR_CPU_CODE_INVALIDATED);
    CHECK(g.cpu.r[15]==0x100020 && g.cpu.r[0]==0x12345678);
    CHECK(g.cpu.instructions==1 && g.cpu.fallback_instructions==1);
    ctr_guest_close(&g);
    mode=6;
    for(unsigned batch=0;batch<2u;++batch) {
        CHECK(ctr_guest_open(&g,&image,NULL,0));CHECK(ctr_recomp_attach(&g,false));
        if(!batch)g.executor_uncontended=NULL;
        g.threads_enabled=true;g.quantum_left=17u;
        CHECK(ctr_guest_run(&g,100,0,true)==CTR_GUEST_READY);
        CHECK(g.cpu.instructions==100u && g.quantum_left==13u && !g.context_switches);
        CHECK(g.cpu.r[12]==100u && g.cpu.r[11]==5u && g.cpu.r[1]==1u);
        ctr_guest_close(&g);
    }
    mode=7;
    CHECK(ctr_guest_open(&g,&image,NULL,0));CHECK(ctr_recomp_attach(&g,false));
    CHECK(ctr_memory_map(&g.memory,0x09000000,32,CTR_MEM_READ|CTR_MEM_WRITE,store_page,32)==CTR_MEM_OK);
    g.cpu.r[15]=0x10001c;g.cpu.r[1]=0x09000000;g.cpu.r[0]=0x12345678;
    CHECK(ctr_guest_run(&g,3,0,true)==CTR_GUEST_FAULTED);
    CHECK(g.cpu.memory_fault==CTR_MEM_ALIGNMENT && g.cpu.r[15]==0x100010);
    CHECK(g.cpu.instructions==3u && g.cpu.r[0]==0u && load32(store_page)==0x12345678);
    ctr_guest_close(&g);
    mode=8;
    CHECK(ctr_guest_open(&g,&image,NULL,0));CHECK(ctr_recomp_attach(&g,false));
    for(unsigned bank=0;bank<2;++bank) {
        CHECK(ctr_memory_map(&g.memory,0x09000000u+bank*8192u,4096,CTR_MEM_READ|CTR_MEM_WRITE,
                             store_page+bank*4096u,4096)==CTR_MEM_OK);
    }
    for(unsigned i=0;i<3;++i) {
        g.cpu.r[15]=0x10001cu;g.cpu.r[1]=0x09000000u+(i&1u)*8192u;
        g.cpu.r[0]=0x12345670u+i;
        CHECK(g.executor(&g.cpu,g.executor_context,1)==CTR_CPU_LIMIT);
        CHECK(g.cpu.r[2]==g.cpu.r[0] && load32(store_page+(i&1u)*4096u)==g.cpu.r[0]);
    }
    CHECK(ctr_memory_change_range(&g.memory,0x09000000u,4096,CTR_MEM_READ,false)==CTR_MEM_OK);
    g.cpu.r[15]=0x10001cu;g.cpu.r[0]=0;
    CHECK(g.executor(&g.cpu,g.executor_context,1)==CTR_CPU_MEMORY_FAULT);
    CHECK(g.cpu.memory_fault==CTR_MEM_PERMISSION && load32(store_page)==0x12345672u);
    ctr_guest_close(&g);
    mode=9;
    CHECK(ctr_guest_open(&g,&image,NULL,0));CHECK(ctr_recomp_attach(&g,false));
    CHECK(g.executor(&g.cpu,g.executor_context,1)==CTR_CPU_LIMIT && g.cpu.r[15]==0x100004u);
    ctr_guest_close(&g);
    watchdog_stop();
    printf("{\"ok\":true,\"assertions\":%u,\"idle_dispatch_deadlock_rejected\":true,\"console_verified\":false}\n",checks);
    return 0;
}
