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
    if(mode>=4) {
        uint32_t pc=ctx->r[15],op=pc==0x100010?0xed2d8b02u:pc==0x100014?0xecbd8b02u:0xee300a00u;
        if(!ctr_recomp_instruction(ctx,pc,op))return;
        if(pc==0x100010) {
            uint32_t at=ctx->r[13]-8u;mem_write32(ctx,at,ctx->vfp[16]);mem_write32(ctx,at+4u,ctx->vfp[17]);ctx->r[13]=at;
        } else if(pc==0x100014) {
            uint32_t at=ctx->r[13];ctx->vfp[16]=mem_read32(ctx,at);ctx->vfp[17]=mem_read32(ctx,at+4u);ctx->r[13]=at+8u;
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
const uint32_t recomp_abi=4,recomp_generation=3,recomp_entry_count=7,recomp_module_count=0;
const Entry recomp_entries[]={{0x100000,entry},{0x100004,entry},{0x100008,entry},{0x10000c,entry},{0x100010,entry},{0x100014,entry},{0x100018,entry}};
static const Piece pieces[]={{0x100000,0x10001c,UINT64_C(0xad8ea38efce738ca)}};
const Origins recomp_origins[]={{.piece_count=1,.pieces=pieces}};
static const uint8_t code[4096]={0,0,0xa0,0xe3,0,0,0xa0,0xe3,0,0,0xa0,0xe3,0,0,0xa0,0xe3,
    2,0x8b,0x2d,0xed,2,0x8b,0xbd,0xec,0,0x0a,0x30,0xee};
static const ctr_guest_image image={code,sizeof(code),{0x100000,1,28},{0,0,0},{0,0,0},0,4096,"original-progress-fixture"};
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
    watchdog_stop();
    printf("{\"ok\":true,\"assertions\":%u,\"idle_dispatch_deadlock_rejected\":true,\"console_verified\":false}\n",checks);
    return 0;
}
