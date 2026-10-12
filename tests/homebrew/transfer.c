/* Original stock-libctru GX queue program. No PICA draw commands or game data. */
#include <3ds.h>
#include <stdlib.h>
u32 __ctru_heap_size=2*1024*1024;
u32 __ctru_linear_heap_size=4*1024*1024;
static void fail(void) {
    static const char message[]="CTR2WUP compiled GX transfers FAIL";
    svcOutputDebugString(message,sizeof(message)-1);svcBreak(USERBREAK_PANIC);abort();
}
static void request(Result result) { if(R_FAILED(result))fail(); }
int main(void) {
    gfxInit(GSP_RGB565_OES,GSP_RGB565_OES,false);
    const unsigned pixels=400*240,bytes=pixels*4;
    u32 *source=linearAlloc(bytes),*copy=linearAlloc(bytes);
    u8 *stripes=linearAlloc(256),*output=linearAlloc(512);
    if(!source || !copy || !stripes || !output)fail();
    request(GX_MemoryFill(source,0x11223344,source+pixels,GX_FILL_32BIT_DEPTH|GX_FILL_TRIGGER,
                         copy,0x55667788,copy+8,GX_FILL_32BIT_DEPTH|GX_FILL_TRIGGER));gspWaitForPSC0();
    if(source[0]!=0x11223344 || source[pixels-1]!=0x11223344 || copy[0]!=0x55667788 || copy[7]!=0x55667788)fail();
    request(GX_MemoryFill(NULL,0,NULL,0,copy,0xaabbccdd,copy+8,GX_FILL_32BIT_DEPTH|GX_FILL_TRIGGER));gspWaitForPSC1();
    if(copy[0]!=0xaabbccdd || copy[7]!=0xaabbccdd)fail();
    for(unsigned i=0;i<256;++i)stripes[i]=(u8)i;
    for(unsigned i=0;i<512;++i)output[i]=0xa5;
    request(GX_TextureCopy((u32*)stripes,GX_BUFFER_DIM(2,1),(u32*)output,GX_BUFFER_DIM(1,1),128,8));gspWaitForPPF();
    for(unsigned k=0;k<128;++k)if(output[k+(k/16)*16]!=(u8)(k+(k/32)*16))fail();
    for(unsigned row=0;row<7;++row)for(unsigned k=16;k<32;++k)if(output[row*32+k]!=0xa5)fail();
    request(GX_FlushCacheRegions(source,bytes,copy,bytes,NULL,0));
    for(unsigned frame=0;frame<3;++frame) {
        u32 color=(frame&1)?0xff0000ff:0x00ff00ff;
        request(GX_MemoryFill(source,color,source+pixels,GX_FILL_32BIT_DEPTH|GX_FILL_TRIGGER,NULL,0,NULL,0));gspWaitForPSC0();
        request(GX_RequestDma(source,copy,bytes));gspWaitForDMA();
        for(unsigned i=0;i<pixels;++i)if(copy[i]!=color)fail();
        u16 w,h;u16 *top=(u16*)gfxGetFramebuffer(GFX_TOP,GFX_LEFT,&w,&h);if((unsigned)w*h!=pixels)fail();
        request(GX_DisplayTransfer(copy,GX_BUFFER_DIM(w,h),(u32*)top,GX_BUFFER_DIM(w,h),GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565)));gspWaitForPPF();
        u16 *bottom=(u16*)gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,&w,&h);
        request(GX_MemoryFill(NULL,0,NULL,0,(u32*)bottom,0x001f,(u32*)(bottom+(unsigned)w*h),GX_FILL_TRIGGER));gspWaitForPSC1();
        if(top[0]!=(frame&1?0xf800:0x07e0) || top[pixels-1]!=(frame&1?0xf800:0x07e0) || bottom[0]!=0x001f)fail();
        gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();svcSleepThread(250000000);
    }
    linearFree(source);linearFree(copy);linearFree(stripes);linearFree(output);gfxExit();
    static const char message[]="CTR2WUP compiled GX transfers PASS";svcOutputDebugString(message,sizeof(message)-1);return 0;
}
