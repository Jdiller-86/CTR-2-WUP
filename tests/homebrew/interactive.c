/* Original standard-libctru application for live console acceptance. */
#include <3ds.h>
u32 __ctru_heap_size=2*1024*1024;
u32 __ctru_linear_heap_size=2*1024*1024;
int main(void) {
    gfxInit(GSP_RGB565_OES,GSP_RGB565_OES,false);
    static const char ready[]="CTR2WUP INTERACTIVE READY";svcOutputDebugString(ready,sizeof(ready)-1);
    svcSleepThread(1000000);
    for(;;) {
        hidScanInput();u32 buttons=hidKeysHeld();if(buttons&KEY_B)break;
        u16 w,h;u16 *top=(u16*)gfxGetFramebuffer(GFX_TOP,GFX_LEFT,&w,&h);
        for(unsigned i=0;i<(unsigned)w*h;++i)top[i]=(buttons&KEY_A)?0x07e0:0xf800;
        u16 *bottom=(u16*)gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,&w,&h);
        for(unsigned i=0;i<(unsigned)w*h;++i)bottom[i]=0x001f;
        if(buttons&KEY_TOUCH) {
            touchPosition touch;hidTouchRead(&touch);
            if(touch.px<320 && touch.py<240)bottom[touch.px*240+239-touch.py]=0xffff;
        }
        gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();
        svcSleepThread(1000000);
    }
    gfxExit();static const char done[]="CTR2WUP compiled interactive PASS";svcOutputDebugString(done,sizeof(done)-1);return 0;
}
