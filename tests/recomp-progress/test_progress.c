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
    if(mode==2 && calls%64==0) {
        if(ctr_recomp_instruction(ctx,ctx->r[15],0xe3a00000))ctx->r[15]+=4;
    } else {
        if(mode==3)ctx->r[15]=ctx->r[15]==0x100000?0x100004:0x100000;
        if(mode)ctx->exit=EXIT_UNWIND;
    }
}
const uint32_t recomp_abi=4,recomp_generation=3,recomp_entry_count=4,recomp_module_count=0;
const Entry recomp_entries[]={{0x100000,entry},{0x100004,entry},{0x100008,entry},{0x10000c,entry}};
static const Piece pieces[]={{0x100000,0x100010,UINT64_C(0xdb78db717c9cf005)}};
const Origins recomp_origins[]={{.piece_count=1,.pieces=pieces}};
static const uint8_t code[4096]={0,0,0xa0,0xe3,0,0,0xa0,0xe3,0,0,0xa0,0xe3,0,0,0xa0,0xe3};
static const ctr_guest_image image={code,sizeof(code),{0x100000,1,16},{0,0,0},{0,0,0},0,4096,"original-progress-fixture"};
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
    watchdog_stop();
    printf("{\"ok\":true,\"assertions\":%u,\"idle_dispatch_deadlock_rejected\":true,\"console_verified\":false}\n",checks);
    return 0;
}
