/* Original libctru application: standard CRT/heap/TLS, no SDK hook overrides. */
#include <3ds.h>
u32 __ctru_heap_size=2*1024*1024;
u32 __ctru_linear_heap_size=2*1024*1024;
int main(void) {
    if(!aptIsActive() || aptShouldClose() || !aptMainLoop()) {
        static const char fail[]="CTR2WUP compiled libctru boot FAIL";
        svcOutputDebugString(fail,sizeof(fail)-1);return 1;
    }
    static const char text[]="CTR2WUP compiled libctru boot PASS";
    svcOutputDebugString(text,sizeof(text)-1);
    return 0;
}
