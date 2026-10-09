/* Original application linked to external, pinned libctru/newlib. */
#include <3ds.h>
u32 __ctru_heap_size=2*1024*1024;
u32 __ctru_linear_heap_size=2*1024*1024;
static LightLock lock;
static volatile u32 counter,done,child_initial,child_final;
static __thread u32 marker=0x13579bdf;
static void worker(void *argument) {
    child_initial=marker;marker=(u32)argument;
    LightLock_Lock(&lock);
    for(unsigned i=0;i<1024;++i)++counter;
    LightLock_Unlock(&lock);
    child_final=marker;done=1;
}
int main(void) {
    marker=0xa1;LightLock_Init(&lock);LightLock_Lock(&lock);
    Thread child=threadCreate(worker,(void*)0xb2,8192,0x30,-2,false);
    if(!child) { svcOutputDebugString("CTR2WUP thread create FAIL",25);return 1; }
    svcSleepThread(0);LightLock_Unlock(&lock);
    Result result=threadJoin(child,U64_MAX);threadFree(child);
    bool passed=!result && done && counter==1024 && marker==0xa1 && child_initial==0x13579bdf && child_final==0xb2;
    static const char success[]="CTR2WUP compiled thread/TLS/LightLock PASS";
    static const char failure[]="CTR2WUP compiled thread/TLS/LightLock FAIL";
    svcOutputDebugString(passed?success:failure,sizeof(success)-1);
    return passed?0:1;
}
