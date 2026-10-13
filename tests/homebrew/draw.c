/* Original normal-CRT/libctru PICA triangle program. No game/assets/patches.
 * The guest supplies real command/resource/target addresses and reads back
 * every rendered and transferred pixel before reporting its own PASS. */
#include <3ds.h>
#include <stdlib.h>
#include <string.h>
u32 __ctru_heap_size=2*1024*1024;
u32 __ctru_linear_heap_size=4*1024*1024;
static void fail(void) {
    static const char message[]="CTR2WUP compiled PICA draws FAIL";
    svcOutputDebugString(message,sizeof(message)-1);svcBreak(USERBREAK_PANIC);abort();
}
static void request(Result result) { if(R_FAILED(result))fail(); }
static unsigned tile(unsigned x,unsigned y,unsigned width) {
    unsigned small=0;
    for(unsigned bit=0;bit<3;++bit)small|=((x>>bit)&1)<<(bit*2)|((y>>bit)&1)<<(bit*2+1);
    return ((y/8)*(width/8)+x/8)*64+small;
}
static u32 expected(unsigned x,unsigned y,unsigned width,unsigned height) {
    if(x<width/8 || x>=width*7/8 || y<height/8 || y>=height*7/8)return 0x102030ff;
    return (2*x+1)*height+(2*y+1)*width<2*width*height?0xff0000ff:0x00ff00ff;
}
static u16 rgb565(u32 rgba) { return (u16)(((rgba>>27)<<11)|(((rgba>>18)&63)<<5)|((rgba>>11)&31)); }
static void pipeline(void) {
    const u16 disabled[]={0x080,0x08f,0x100,0x104,0x105,0x106,0x112,0x114,0x115,0x117,0x11b};
    for(unsigned i=0;i<sizeof(disabled)/sizeof(*disabled);++i)GPUCMD_AddWrite(disabled[i],0);
    GPUCMD_AddWrite(GPUREG_LOGIC_OP,3);GPUCMD_AddWrite(GPUREG_DEPTH_COLOR_MASK,0xf00);
    GPUCMD_AddWrite(GPUREG_COLORBUFFER_WRITE,15);
    const u16 tev[]={0xc0,0xc8,0xd0,0xd8,0xf0,0xf8};
    for(unsigned i=0;i<6;++i) {
        GPUCMD_AddWrite(tev[i],i?0xf000f:0);GPUCMD_AddWrite(tev[i]+1,0);
        GPUCMD_AddWrite(tev[i]+2,0);GPUCMD_AddWrite(tev[i]+4,0);
    }
    GPUCMD_AddWrite(GPUREG_FACECULLING_CONFIG,0);GPUCMD_AddWrite(GPUREG_SCISSORTEST_MODE,0);
    GPUCMD_AddWrite(GPUREG_SCISSORTEST_POS,0);GPUCMD_AddWrite(GPUREG_SCISSORTEST_DIM,0);GPUCMD_AddWrite(GPUREG_VIEWPORT_XY,0);
    GPUCMD_AddWrite(GPUREG_SH_OUTMAP_TOTAL,2);GPUCMD_AddWrite(GPUREG_SH_OUTMAP_O0,0x03020100);
    GPUCMD_AddWrite(GPUREG_SH_OUTMAP_O1,0x0b0a0908);
    GPUCMD_AddWrite(GPUREG_ATTRIBBUFFERS_FORMAT_LOW,255);GPUCMD_AddWrite(GPUREG_ATTRIBBUFFERS_FORMAT_HIGH,0x10020000);
    GPUCMD_AddWrite(GPUREG_ATTRIBBUFFER0_OFFSET,0);GPUCMD_AddWrite(GPUREG_ATTRIBBUFFER0_CONFIG1,0);
    GPUCMD_AddWrite(GPUREG_ATTRIBBUFFER0_CONFIG2,0x10100000);
    GPUCMD_AddWrite(GPUREG_NUMVERTICES,3);GPUCMD_AddWrite(GPUREG_GEOSTAGE_CONFIG,0);
    GPUCMD_AddWrite(GPUREG_VERTEX_OFFSET,123);GPUCMD_AddWrite(GPUREG_PRIMITIVE_CONFIG,0);
    GPUCMD_AddWrite(GPUREG_VSH_INPUTBUFFER_CONFIG,0xa0000001);GPUCMD_AddWrite(GPUREG_VSH_ENTRYPOINT,0);
    GPUCMD_AddWrite(GPUREG_VSH_ATTRIBUTES_PERMUTATION_LOW,0x76543210);
    GPUCMD_AddWrite(GPUREG_VSH_ATTRIBUTES_PERMUTATION_HIGH,0xfedcba98);GPUCMD_AddWrite(GPUREG_VSH_OUTMAP_MASK,3);
    const u32 shader[]={0x4c000000,0x4c201000,0x88000000};
    GPUCMD_AddWrite(GPUREG_VSH_CODETRANSFER_CONFIG,0);GPUCMD_AddWrites(GPUREG_VSH_CODETRANSFER_DATA,shader,3);
    GPUCMD_AddWrite(GPUREG_VSH_CODETRANSFER_END,1);GPUCMD_AddWrite(GPUREG_VSH_OPDESCS_CONFIG,0);
    GPUCMD_AddWrite(GPUREG_VSH_OPDESCS_DATA,0x36f);
}
static void color(bool green) {
    const u32 packed[]={0x3f000000,green?0x00003f00:0,green?0:0x003f0000};
    GPUCMD_AddWrite(GPUREG_FIXEDATTRIB_INDEX,1);GPUCMD_AddIncrementalWrites(GPUREG_FIXEDATTRIB_DATA0,packed,3);
}
int main(void) {
    gfxInit(GSP_RGB565_OES,GSP_RGB565_OES,false);
    u32 *commands=linearAlloc(4096);u8 *resources=linearMemAlign(128,16);
    u32 *targets[2]={linearAlloc(240*400*4),linearAlloc(240*320*4)};
    if(!commands || !resources || !targets[0] || !targets[1])fail();
    static const float positions[4][4]={{-.75f,-.75f,-.5f,1.f},{.75f,-.75f,-.5f,1.f},
        {-.75f,.75f,-.5f,1.f},{.75f,.75f,-.5f,1.f}};
    const u8 indices[]={0,1,2,1,3,2};memset(resources,0,128);memcpy(resources,positions,sizeof(positions));
    memcpy(resources+64,indices,sizeof(indices));
    for(unsigned i=0;i<6;++i)((u16*)(resources+80))[i]=indices[i];
    for(unsigned screen=0;screen<2;++screen) {
        u16 width,height;u16 *output=(u16*)gfxGetFramebuffer(screen?GFX_BOTTOM:GFX_TOP,GFX_LEFT,&width,&height);
        if(width!=240 || height!=(screen?320:400))fail();
        unsigned pixels=(unsigned)width*height;
        request(GX_MemoryFill(targets[screen],0x102030ff,targets[screen]+pixels,GX_FILL_32BIT_DEPTH|GX_FILL_TRIGGER,NULL,0,NULL,0));gspWaitForPSC0();
        GPUCMD_SetBuffer(commands,1024,0);
        if(!screen)pipeline(); /* Real retained shader/TEV state in the second list. */
        GPUCMD_AddWrite(GPUREG_ATTRIBBUFFERS_LOC,(osConvertVirtToPhys(resources)/16)*2);
        GPUCMD_AddWrite(GPUREG_COLORBUFFER_LOC,osConvertVirtToPhys(targets[screen])/8);
        GPUCMD_AddWrite(GPUREG_FRAMEBUFFER_DIM,width|((height-1)<<12)|(1<<24));
        /* Exact wire values for 120, 200 and 160. This renderer fixture does
         * not require guest floating arithmetic under CRT FPSCR controls. */
        GPUCMD_AddWrite(GPUREG_VIEWPORT_WIDTH,0x45e000);
        GPUCMD_AddWrite(GPUREG_VIEWPORT_HEIGHT,screen?0x464000:0x469000);
        for(unsigned triangle=0;triangle<2;++triangle) {
            color(triangle!=0);
            GPUCMD_AddWrite(GPUREG_INDEXBUFFER_CONFIG,(screen?0x80000000:0)|(screen?80+triangle*6:64+triangle*3));
            GPUCMD_AddWrite(GPUREG_DRAWELEMENTS,1);
        }
        GPUCMD_AddWrite(GPUREG_FRAMEBUFFER_FLUSH,1);
        u32 *submitted,words;GPUCMD_Split(&submitted,&words);
        if(submitted!=commands || words>1024 || (words&3))fail();
        request(GX_ProcessCommandList(submitted,words*4,2));gspWaitForP3D();
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)
            if(targets[screen][tile(x,y,width)]!=expected(x,y,width,height))fail();
        request(GX_DisplayTransfer(targets[screen],GX_BUFFER_DIM(width,height),(u32*)output,GX_BUFFER_DIM(width,height),GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565)));gspWaitForPPF();
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)
            if(output[y*width+x]!=rgb565(expected(x,y,width,height)))fail();
    }
    gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();
    static const char ready[]="CTR2WUP DRAW READY";svcOutputDebugString(ready,sizeof(ready)-1);svcSleepThread(250000000);
    linearFree(commands);linearFree(resources);linearFree(targets[0]);linearFree(targets[1]);gfxExit();
    static const char message[]="CTR2WUP compiled PICA draws PASS";svcOutputDebugString(message,sizeof(message)-1);return 0;
}
