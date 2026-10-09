/* Original normal-libctru input consumer; producer uses the standard HID ring. */
#include <3ds.h>
u32 __ctru_heap_size=2*1024*1024;
u32 __ctru_linear_heap_size=2*1024*1024;
int main(void) {
    static const char ready[][22]={"CTR2WUP INPUT READY 0","CTR2WUP INPUT READY 1","CTR2WUP INPUT READY 2"};
    static const u32 buttons[]={KEY_A,KEY_B,0};
    static const s16 cx[]={-30,100,0},cy[]={40,-100,0};
    static const u16 tx[]={17,319,0},ty[]={23,239,0};
    for(unsigned stage=0;stage<3;++stage) {
        svcOutputDebugString(ready[stage],21);
        do { hidScanInput(); } while((hidKeysHeld()&0xfff)!=buttons[stage]);
        circlePosition circle;touchPosition touch;hidCircleRead(&circle);hidTouchRead(&touch);
        if(circle.dx!=cx[stage] || circle.dy!=cy[stage] || touch.px!=tx[stage] || touch.py!=ty[stage] ||
           !!(hidKeysHeld()&KEY_TOUCH)!=(stage<2) ||
           (stage<2 && !(hidKeysDown()&buttons[stage])) ||
           (stage==2 && !(hidKeysUp()&KEY_B))) {
            static const char fail[]="CTR2WUP compiled input FAIL";svcOutputDebugString(fail,sizeof(fail)-1);return 1;
        }
    }
    static const char pass[]="CTR2WUP compiled input PASS";svcOutputDebugString(pass,sizeof(pass)-1);return 0;
}
