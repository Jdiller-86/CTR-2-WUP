/* Original standard libctru CPU-framebuffer program; no PICA commands. */
#include <3ds.h>
u32 __ctru_heap_size=2*1024*1024;
u32 __ctru_linear_heap_size=2*1024*1024;
int main(void) {
    gfxInit(GSP_RGB565_OES,GSP_RGB565_OES,false);
    for(unsigned frame=0;frame<3;++frame) {
        u16 w,h;u16 *top=(u16*)gfxGetFramebuffer(GFX_TOP,GFX_LEFT,&w,&h);
        for(unsigned i=0;i<(unsigned)w*h;++i)top[i]=(frame&1)?0xf800:0x07e0;
        u16 *bottom=(u16*)gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,&w,&h);
        for(unsigned i=0;i<(unsigned)w*h;++i)bottom[i]=0x001f;
        gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();
    }
    gfxExit();static const char text[]="CTR2WUP compiled graphics PASS";svcOutputDebugString(text,sizeof(text)-1);
    return 0;
}
