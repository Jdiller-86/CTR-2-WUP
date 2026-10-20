/* Original normal-CRT/libctru texture/TEV program. No external assets.
 * Geometry and texture occupy separate owned linear allocations. Every tiled
 * render-target and RGB565 display pixel is checked by the guest before PASS. */
#include <3ds.h>
#include <stdlib.h>
#include <string.h>
u32 __ctru_heap_size=2*1024*1024;
u32 __ctru_linear_heap_size=4*1024*1024;
static void fail(void) {
    static const char message[]="CTR2WUP compiled textured PICA FAIL";
    svcOutputDebugString(message,sizeof(message)-1);svcBreak(USERBREAK_PANIC);abort();
}
static void request(Result result) { if(R_FAILED(result))fail(); }
static unsigned tile(unsigned x,unsigned y,unsigned width) {
    unsigned small=0;
    for(unsigned bit=0;bit<3;++bit)small|=((x>>bit)&1)<<(bit*2)|((y>>bit)&1)<<(bit*2+1);
    return ((y/8)*(width/8)+x/8)*64+small;
}
static u32 texel(unsigned x,unsigned y) {
    unsigned red=x&1,green=((x>>2)^(y>>1))&1,blue=((x>>3)^y)&1;
    return (red?0xff000000:0)|(green?0x00ff0000:0)|(blue?0x0000ff00:0)|255;
}
static u32 expected(unsigned x,unsigned y,unsigned width,unsigned height) {
    if(x<width/8 || x>=width*7/8 || y<height/8 || y>=height*7/8)return 0x102030ff;
    unsigned u=((2*x+1-width/4)*16)/(width*3/2);
    unsigned v=7-((2*y+1-height/4)*8)/(height*3/2);
    u32 result=texel(u,v);
    /* Triangle 1 averages to (128,128,128,255). Its six byte-domain stages
     * produce (89,36,116,194), (126,187,135,161), (126,146,129,252),
     * (255,170,255,174), (128,108,128,174), then (184,212,157,191). */
    if((2*x+1)*height+(2*y+1)*width>=2*width*height)return 0xc8f4cdbf;
    /* Alpha selects Texture0 red; a zero red byte fails GEQUAL 128. */
    if(!(u&1u))result=0x102030ff;
    return result;
}
static u16 rgb565(u32 rgba) { return (u16)(((rgba>>27)<<11)|(((rgba>>18)&63)<<5)|((rgba>>11)&31)); }
static void pipeline(void) {
    const u16 disabled[]={0x08f,0x100,0x104,0x105,0x106,0x112,0x114,0x115,0x117,0x11b};
    for(unsigned i=0;i<sizeof(disabled)/sizeof(*disabled);++i)GPUCMD_AddWrite(disabled[i],0);
    GPUCMD_AddWrite(GPUREG_LOGIC_OP,3);GPUCMD_AddWrite(GPUREG_DEPTH_COLOR_MASK,0xf00);
    GPUCMD_AddWrite(GPUREG_COLORBUFFER_WRITE,15);
    GPUCMD_AddWrite(GPUREG_TEXUNIT_CONFIG,1);
    GPUCMD_AddWrite(GPUREG_TEXUNIT0_DIM,(16u<<16)|8u);
    GPUCMD_AddWrite(GPUREG_TEXUNIT0_PARAM,0);GPUCMD_AddWrite(GPUREG_TEXUNIT0_LOD,0);
    GPUCMD_AddWrite(GPUREG_TEXUNIT0_TYPE,0);
    GPUCMD_AddWrite(GPUREG_FACECULLING_CONFIG,0);GPUCMD_AddWrite(GPUREG_SCISSORTEST_MODE,0);
    GPUCMD_AddWrite(GPUREG_SCISSORTEST_POS,0);GPUCMD_AddWrite(GPUREG_SCISSORTEST_DIM,0);GPUCMD_AddWrite(GPUREG_VIEWPORT_XY,0);
    GPUCMD_AddWrite(0x064,1);GPUCMD_AddWrite(0x06f,0x103);
    GPUCMD_AddWrite(GPUREG_SH_OUTMAP_TOTAL,3);GPUCMD_AddWrite(GPUREG_SH_OUTMAP_O0,0x03020100);
    GPUCMD_AddWrite(GPUREG_SH_OUTMAP_O1,0x0b0a0908);GPUCMD_AddWrite(GPUREG_SH_OUTMAP_O2,0x1f1f0d0c);
    GPUCMD_AddWrite(GPUREG_ATTRIBBUFFERS_FORMAT_LOW,0x7ff);GPUCMD_AddWrite(GPUREG_ATTRIBBUFFERS_FORMAT_HIGH,0x20020000);
    GPUCMD_AddWrite(GPUREG_ATTRIBBUFFER0_OFFSET,0);GPUCMD_AddWrite(GPUREG_ATTRIBBUFFER0_CONFIG1,0x20);
    GPUCMD_AddWrite(GPUREG_ATTRIBBUFFER0_CONFIG2,0x20180000);
    GPUCMD_AddWrite(GPUREG_NUMVERTICES,3);GPUCMD_AddWrite(GPUREG_GEOSTAGE_CONFIG,0);
    GPUCMD_AddWrite(GPUREG_VERTEX_OFFSET,123);GPUCMD_AddWrite(GPUREG_PRIMITIVE_CONFIG,0);
    GPUCMD_AddWrite(GPUREG_VSH_INPUTBUFFER_CONFIG,0xa0000002);GPUCMD_AddWrite(GPUREG_VSH_ENTRYPOINT,0);
    GPUCMD_AddWrite(GPUREG_VSH_ATTRIBUTES_PERMUTATION_LOW,0x76543210);
    GPUCMD_AddWrite(GPUREG_VSH_ATTRIBUTES_PERMUTATION_HIGH,0xfedcba98);GPUCMD_AddWrite(GPUREG_VSH_OUTMAP_MASK,7);
    const u32 shader[]={0x4c000000,0x4c201000,0x4c402000,0x88000000};
    GPUCMD_AddWrite(GPUREG_VSH_CODETRANSFER_CONFIG,0);GPUCMD_AddWrites(GPUREG_VSH_CODETRANSFER_DATA,shader,4);
    GPUCMD_AddWrite(GPUREG_VSH_CODETRANSFER_END,1);GPUCMD_AddWrite(GPUREG_VSH_OPDESCS_CONFIG,0);
    GPUCMD_AddWrite(GPUREG_VSH_OPDESCS_DATA,0x36f);
}
static void combiner(bool chain) {
    static const u16 bases[]={0xc0,0xc8,0xd0,0xd8,0xf0,0xf8};
    static const u32 stages[6][5]={
        {0x0e3e00e3,0,0x00040001,0x65e949b3,0},
        {0x00ef00ef,0,0x00050002,0x21139725,0},
        {0x0e3f0e3f,0x00120210,0x00080004,0x532fd35b,0},
        {0x00ef00ef,0,0x00010003,0xb1f14395,1},
        {0x03ef03ef,0,0x00040009,0x77b52f53,0},
        {0x03ef03ef,0,0x00020008,0x113bc771,0}
    };
    for(unsigned stage=0;stage<6;++stage)for(unsigned field=0;field<5;++field)
        GPUCMD_AddWrite(bases[stage]+field,chain?stages[stage][field]:field?0:stage?0xf000f:0x30003);
    if(!chain)GPUCMD_AddWrite(bases[0]+1,2u<<12); /* Alpha = Texture0 red. */
}
static void color(bool green) {
    const u32 packed[]={green?0x3f000000:0x3f00003f,0x3f00,green?0:0x003f0000};
    GPUCMD_AddWrite(GPUREG_FIXEDATTRIB_INDEX,1);GPUCMD_AddIncrementalWrites(GPUREG_FIXEDATTRIB_DATA0,packed,3);
}
int main(void) {
    gfxInit(GSP_RGB565_OES,GSP_RGB565_OES,false);
    u32 *texture=linearMemAlign(16*8*4,128);
    u32 *commands=linearAlloc(4096);u8 *geometry=linearMemAlign(128,16);
    u32 *targets[2]={linearAlloc(240*400*4),linearAlloc(240*320*4)};
    if(!commands || !geometry || !texture || !targets[0] || !targets[1])fail();
    static const float vertices[4][6]={{-.75f,-.75f,-.5f,1.f,0.f,0.f},{.75f,-.75f,-.5f,1.f,1.f,0.f},
        {-.75f,.75f,-.5f,1.f,0.f,1.f},{.75f,.75f,-.5f,1.f,1.f,1.f}};
    const u8 indices[]={0,1,2,1,3,2};memset(geometry,0,128);memcpy(geometry,vertices,sizeof(vertices));
    memcpy(geometry+96,indices,sizeof(indices));
    for(unsigned i=0;i<6;++i)((u16*)(geometry+112))[i]=indices[i];
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<16;++x)texture[tile(x,y,16)]=texel(x,y);
    for(unsigned screen=0;screen<2;++screen) {
        u16 width,height;u16 *output=(u16*)gfxGetFramebuffer(screen?GFX_BOTTOM:GFX_TOP,GFX_LEFT,&width,&height);
        if(width!=240 || height!=(screen?320:400))fail();
        unsigned pixels=(unsigned)width*height;
        request(GX_MemoryFill(targets[screen],0x102030ff,targets[screen]+pixels,GX_FILL_32BIT_DEPTH|GX_FILL_TRIGGER,NULL,0,NULL,0));gspWaitForPSC0();
        GPUCMD_SetBuffer(commands,1024,0);
        if(!screen) { pipeline();GPUCMD_AddWrite(GPUREG_TEXUNIT0_ADDR1,osConvertVirtToPhys(texture)/8); }
        /* The second list reuses the original texture/shader state, not an upload. */
        GPUCMD_AddWrite(GPUREG_ATTRIBBUFFERS_LOC,(osConvertVirtToPhys(geometry)/16)*2);
        GPUCMD_AddWrite(GPUREG_COLORBUFFER_LOC,osConvertVirtToPhys(targets[screen])/8);
        GPUCMD_AddWrite(GPUREG_FRAMEBUFFER_DIM,width|((height-1)<<12)|(1<<24));
        GPUCMD_AddWrite(GPUREG_VIEWPORT_WIDTH,0x45e000);
        GPUCMD_AddWrite(GPUREG_VIEWPORT_HEIGHT,screen?0x464000:0x469000);
        for(unsigned triangle=0;triangle<2;++triangle) {
            color(triangle!=0);combiner(triangle!=0);
            /* Alpha testing precedes the merger. RGB adds ONE/ONE to the
             * seeded target; alpha uses ONE/ZERO. Reset blend mode on the
             * first triangle of each retained list. */
            GPUCMD_AddWrite(GPUREG_FRAGOP_ALPHA_TEST,1u|((triangle?GPU_EQUAL:GPU_GEQUAL)<<4)|((triangle?191u:128u)<<8));
            GPUCMD_AddWrite(GPUREG_COLOR_OPERATION,triangle?0x100u:0u);
            GPUCMD_AddWrite(GPUREG_BLEND_FUNC,0x01110000u);
            GPUCMD_AddWrite(GPUREG_LOGIC_OP,GPU_LOGICOP_COPY);
            GPUCMD_AddWrite(GPUREG_DEPTH_COLOR_MASK,15u<<8);
            GPUCMD_AddWrite(GPUREG_COLORBUFFER_READ,triangle?15u:0u);
            GPUCMD_AddWrite(GPUREG_TEXUNIT0_PARAM,triangle?
                GPU_TEXTURE_MAG_FILTER(GPU_LINEAR)|GPU_TEXTURE_MIN_FILTER(GPU_LINEAR):0);
            if(triangle) {
                GPUCMD_AddWrite(0x2c0,0x80000000); /* c0 in float32 W/Z/Y/X order. */
                GPUCMD_AddWrite(0x2c1,0);GPUCMD_AddWrite(0x2c1,0);
                GPUCMD_AddWrite(0x2c1,0x3f000000);GPUCMD_AddWrite(0x2c1,0x3f000000);
            }
            GPUCMD_AddWrite(0x2cb,2);
            GPUCMD_AddWrite(0x2cc,triangle?0x4c420000:0x4c402000); /* o2 = c0 / v2 */
            GPUCMD_AddWrite(GPUREG_INDEXBUFFER_CONFIG,(screen?0x80000000:0)|(screen?112+triangle*6:96+triangle*3));
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
    linearFree(commands);linearFree(geometry);linearFree(texture);linearFree(targets[0]);linearFree(targets[1]);gfxExit();
    static const char message[]="CTR2WUP compiled textured PICA PASS";svcOutputDebugString(message,sizeof(message)-1);return 0;
}
