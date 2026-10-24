/* Original normal-CRT/libctru texture/TEV/depth/stencil program. No external assets.
 * Owned top D16 and bottom D24S8 targets retain the same displayed images.
 * Every material/buffer prepass, packed sample and display pixel is checked. */
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
static u16 expected_depth(unsigned x,unsigned y,unsigned width,unsigned height) {
    if(x<width/8 || x>=width*7/8 || y<height/8 || y>=height*7/8)return 65535;
    if((2*x+1)*height+(2*y+1)*width>=2*width*height)return 0;
    unsigned u=((2*x+1-width/4)*16)/(width*3/2);
    return (u&1u)?0:65535; /* Alpha-rejected cutouts retain their far clear. */
}
static u16 rgb565(u32 rgba) { return (u16)(((rgba>>27)<<11)|(((rgba>>18)&63)<<5)|((rgba>>11)&31)); }
static void shader(bool material) {
    GPUCMD_AddWrite(0x064,1);GPUCMD_AddWrite(0x06f,material?0x703:0x103);
    GPUCMD_AddWrite(GPUREG_SH_OUTMAP_TOTAL,material?5:3);
    GPUCMD_AddWrite(GPUREG_SH_OUTMAP_O0,0x03020100);
    GPUCMD_AddWrite(GPUREG_SH_OUTMAP_O1,0x0b0a0908);
    GPUCMD_AddWrite(GPUREG_SH_OUTMAP_O2,0x1f1f0d0c);
    if(material) {
        GPUCMD_AddWrite(GPUREG_SH_OUTMAP_O3,0x1f1f0f0e); /* UV1 semantics14/15. */
        GPUCMD_AddWrite(GPUREG_SH_OUTMAP_O4,0x1f1f1716); /* UV2 semantics22/23. */
    }
    GPUCMD_AddWrite(GPUREG_VSH_OUTMAP_MASK,material?31:7);
    static const u32 basic[]={0x4c000000,0x4c201000,0x4c402000,0x88000000};
    static const u32 layered[]={0x4c000000,0x4c201000,0x4c402000,
        0x4c621000,0x4c822000,0x88000000}; /* o3=c1, o4=c2. */
    GPUCMD_AddWrite(GPUREG_VSH_CODETRANSFER_CONFIG,0);
    GPUCMD_AddWrites(GPUREG_VSH_CODETRANSFER_DATA,material?layered:basic,material?6:4);
    GPUCMD_AddWrite(GPUREG_VSH_CODETRANSFER_END,1);
}
static void pipeline(void) {
    const u16 disabled[]={0x08f,0x100,0x104,0x105,0x106,0x112,0x117,0x11b};
    for(unsigned i=0;i<sizeof(disabled)/sizeof(*disabled);++i)GPUCMD_AddWrite(disabled[i],0);
    GPUCMD_AddWrite(GPUREG_LOGIC_OP,3);GPUCMD_AddWrite(GPUREG_DEPTH_COLOR_MASK,0xf00);
    GPUCMD_AddWrite(GPUREG_COLORBUFFER_WRITE,15);
    GPUCMD_AddWrite(GPUREG_DEPTHBUFFER_READ,2);GPUCMD_AddWrite(GPUREG_DEPTHBUFFER_WRITE,2);
    GPUCMD_AddWrite(GPUREG_DEPTHBUFFER_FORMAT,0);
    GPUCMD_AddWrite(GPUREG_DEPTHMAP_SCALE,0xbf0000); /* Canonical -1 float24. */
    GPUCMD_AddWrite(GPUREG_DEPTHMAP_OFFSET,0);GPUCMD_AddWrite(GPUREG_DEPTHMAP_ENABLE,1);
    GPUCMD_AddWrite(GPUREG_TEXUNIT_CONFIG,1);
    GPUCMD_AddWrite(GPUREG_TEXUNIT0_DIM,(16u<<16)|8u);
    GPUCMD_AddWrite(GPUREG_TEXUNIT0_PARAM,0);GPUCMD_AddWrite(GPUREG_TEXUNIT0_LOD,0);
    GPUCMD_AddWrite(GPUREG_TEXUNIT0_TYPE,0);
    GPUCMD_AddWrite(GPUREG_FACECULLING_CONFIG,0);GPUCMD_AddWrite(GPUREG_SCISSORTEST_MODE,0);
    GPUCMD_AddWrite(GPUREG_SCISSORTEST_POS,0);GPUCMD_AddWrite(GPUREG_SCISSORTEST_DIM,0);GPUCMD_AddWrite(GPUREG_VIEWPORT_XY,0);
    GPUCMD_AddWrite(GPUREG_ATTRIBBUFFERS_FORMAT_LOW,0x7ff);GPUCMD_AddWrite(GPUREG_ATTRIBBUFFERS_FORMAT_HIGH,0x20020000);
    GPUCMD_AddWrite(GPUREG_ATTRIBBUFFER0_OFFSET,0);GPUCMD_AddWrite(GPUREG_ATTRIBBUFFER0_CONFIG1,0x20);
    GPUCMD_AddWrite(GPUREG_ATTRIBBUFFER0_CONFIG2,0x20180000);
    GPUCMD_AddWrite(GPUREG_NUMVERTICES,3);GPUCMD_AddWrite(GPUREG_GEOSTAGE_CONFIG,0);
    GPUCMD_AddWrite(GPUREG_VERTEX_OFFSET,123);GPUCMD_AddWrite(GPUREG_PRIMITIVE_CONFIG,0);
    GPUCMD_AddWrite(GPUREG_VSH_INPUTBUFFER_CONFIG,0xa0000002);GPUCMD_AddWrite(GPUREG_VSH_ENTRYPOINT,0);
    GPUCMD_AddWrite(GPUREG_VSH_ATTRIBUTES_PERMUTATION_LOW,0x76543210);
    GPUCMD_AddWrite(GPUREG_VSH_ATTRIBUTES_PERMUTATION_HIGH,0xfedcba98);
    shader(false);GPUCMD_AddWrite(GPUREG_VSH_OPDESCS_CONFIG,0);
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
    GPUCMD_AddWrite(0x0e0,0);GPUCMD_AddWrite(0x0fd,0);
    for(unsigned stage=0;stage<6;++stage)for(unsigned field=0;field<5;++field)
        GPUCMD_AddWrite(bases[stage]+field,chain?stages[stage][field]:field?0:stage?0xf000f:0x30003);
    if(!chain)GPUCMD_AddWrite(bases[0]+1,2u<<12); /* Alpha = Texture0 red. */
}
static void color(bool green) {
    const u32 packed[]={green?0x3f000000:0x3f00003f,0x3f00,green?0:0x003f0000};
    GPUCMD_AddWrite(GPUREG_FIXEDATTRIB_INDEX,1);GPUCMD_AddIncrementalWrites(GPUREG_FIXEDATTRIB_DATA0,packed,3);
}
static void stencil(GPU_TESTFUNC function,unsigned reference,unsigned input_mask,unsigned write_mask,
                    GPU_STENCILOP fail_op,GPU_STENCILOP depth_fail,GPU_STENCILOP pass) {
    GPUCMD_AddWrite(GPUREG_STENCIL_TEST,1u|((u32)function<<4)|(write_mask<<8)|
        (reference<<16)|(input_mask<<24));
    GPUCMD_AddWrite(GPUREG_STENCIL_OP,(u32)fail_op|((u32)depth_fail<<4)|((u32)pass<<8));
}
static void index_buffer(unsigned screen,unsigned triangle) {
    GPUCMD_AddWrite(GPUREG_INDEXBUFFER_CONFIG,(screen?0x80000000:0)|
        (screen?112+(triangle%2)*6:96+(triangle%2)*3));
}
static void submit(u32 *commands) {
    GPUCMD_AddWrite(GPUREG_FRAMEBUFFER_FLUSH,1);
    u32 *submitted,words;GPUCMD_Split(&submitted,&words);
    if(submitted!=commands || words>1024 || (words&3))fail();
    request(GX_ProcessCommandList(submitted,words*4,2));gspWaitForP3D();
}
static void coordinate_uniform(unsigned index,u32 x,u32 y) {
    GPUCMD_AddWrite(0x2c0,0x80000000u|index); /* Float32 W/Z/Y/X upload order. */
    GPUCMD_AddWrite(0x2c1,0);GPUCMD_AddWrite(0x2c1,0);
    GPUCMD_AddWrite(0x2c1,y);GPUCMD_AddWrite(0x2c1,x);
}
static void probe_material(u32 *commands,u32 *target,u8 *depth,unsigned width,unsigned height,
                           const u16 *texture1,const u8 *texture2) {
    shader(true);
    coordinate_uniform(1,0x3f000000,0x3f000000); /* UV1=(1/2,1/2). */
    coordinate_uniform(2,0x3fa00000,0xbe800000); /* UV2=(5/4,-1/4). */
    GPUCMD_AddWrite(GPUREG_TEXUNIT0_PARAM,0);
    GPUCMD_AddWrite(GPUREG_TEXUNIT1_DIM,(8u<<16)|8u);
    GPUCMD_AddWrite(GPUREG_TEXUNIT1_PARAM,GPU_TEXTURE_MAG_FILTER(GPU_LINEAR)|GPU_TEXTURE_MIN_FILTER(GPU_LINEAR));
    GPUCMD_AddWrite(GPUREG_TEXUNIT1_LOD,0);GPUCMD_AddWrite(GPUREG_TEXUNIT1_TYPE,GPU_RGB565);
    GPUCMD_AddWrite(GPUREG_TEXUNIT1_ADDR,osConvertVirtToPhys(texture1)/8);
    GPUCMD_AddWrite(GPUREG_TEXUNIT2_DIM,(8u<<16)|8u);
    GPUCMD_AddWrite(GPUREG_TEXUNIT2_PARAM,GPU_TEXTURE_WRAP_S(GPU_REPEAT)|GPU_TEXTURE_WRAP_T(GPU_MIRRORED_REPEAT));
    GPUCMD_AddWrite(GPUREG_TEXUNIT2_LOD,0);GPUCMD_AddWrite(GPUREG_TEXUNIT2_TYPE,GPU_L8);
    GPUCMD_AddWrite(GPUREG_TEXUNIT2_ADDR,osConvertVirtToPhys(texture2)/8);
    combiner(false);
    GPUCMD_AddWrite(0x0c1,0); /* Preserve Texture0 alpha rather than its red lane. */
    GPUCMD_AddWrite(0x0c8,0x004f004f);GPUCMD_AddWrite(0x0ca,0x00020002); /* Previous+Texture1. */
    GPUCMD_AddWrite(0x0d0,0x005f005f);GPUCMD_AddWrite(0x0d2,0x00010001); /* Previous*Texture2. */
    color(false);GPUCMD_AddWrite(GPUREG_DEPTH_COLOR_MASK,0xf00);
    GPUCMD_AddWrite(GPUREG_FRAGOP_ALPHA_TEST,0);GPUCMD_AddWrite(GPUREG_COLOR_OPERATION,0);
    GPUCMD_AddWrite(GPUREG_LOGIC_OP,GPU_LOGICOP_COPY);GPUCMD_AddWrite(GPUREG_COLORBUFFER_READ,0);
    for(unsigned route=0;route<2;++route) {
        GPUCMD_AddWrite(GPUREG_TEXUNIT_CONFIG,7u|(route?1u<<13:0));
        index_buffer(0,0);GPUCMD_AddWrite(GPUREG_DRAWELEMENTS,1);submit(commands);
        /* Texture1's four center texels average to(191,64,64). Texture2's
         * own UV maps to storage(2,6), L=75; the UV1 route maps to(4,3), L=112.
         * TEV adds Texture1 with saturation, then truncates the L/255 product. */
        unsigned luminance=route?112u:75u;
        static const unsigned addition[3]={191,64,64};
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
            bool covered=x>=width/8 && x<width*7/8 && y>=height/8 && y<height*7/8 &&
                (2*x+1)*height+(2*y+1)*width<2*width*height;
            u32 expected=0x102030ff;
            if(covered) {
                unsigned u=((2*x+1-width/4)*16)/(width*3/2);
                unsigned v=7-((2*y+1-height/4)*8)/(height*3/2);
                u32 sampled=texel(u,v);expected=255;
                for(unsigned lane=0;lane<3;++lane) {
                    unsigned sum=((sampled>>(24-lane*8))&255u)+addition[lane];
                    if(sum>255)sum=255;
                    expected|=(sum*luminance/255u)<<(24-lane*8);
                }
            }
            unsigned at=tile(x,y,width);
            if(target[at]!=expected || depth[at*2]!=255 || depth[at*2+1]!=255)fail();
        }
        request(GX_MemoryFill(target,0x102030ff,target+width*height,GX_FILL_32BIT_DEPTH|GX_FILL_TRIGGER,
            (u32*)depth,65535,(u32*)(depth+width*height*2),GX_FILL_16BIT_DEPTH|GX_FILL_TRIGGER));
        gspWaitForPSC0();GPUCMD_SetBuffer(commands,1024,0);
    }
    static const char passed[]="CTR2WUP compiled multi-texture PASS";
    svcOutputDebugString(passed,sizeof(passed)-1);
    GPUCMD_AddWrite(GPUREG_TEXUNIT_CONFIG,1);shader(false);
}
static void probe_buffer(u32 *commands,u32 *target,u8 *depth,unsigned width,unsigned height) {
    static const u16 bases[]={0xc0,0xc8,0xd0,0xd8,0xf0,0xf8};
    static const u32 stages[6][5]={
        {0x00ed00ed,0,0x00020002,0x130d0703,0},
        {0x00ed00ed,0,0x00020002,0x17110b05,0},
        {0x000f000f,0,0,0,0},
        {0x00df00fd,0,0x00050002,0,0},
        {0x00ed00fd,0,0x00020002,0x1f000000,0},
        {0x00fd00fd,0,0x00020002,0,0}
    };
    /* Visible starts zero, pending starts (17,29,43,71). Stage 0 reads zero,
     * stage 1 reads that initial color, and stage 2 is a real passthrough.
     * RGB updates at 0/2 and alpha at 1/3 reach readers two stages later. Outputs:
     * (3,7,13,19), (22,40,60,94), (22,40,60,94), (25,47,73,0),
     * (47,87,133,125), (69,127,193,125). */
    GPUCMD_AddWrite(0x0fd,0x472b1d11);GPUCMD_AddWrite(0x0e0,0xa500);
    for(unsigned stage=0;stage<6;++stage)for(unsigned field=0;field<5;++field)
        GPUCMD_AddWrite(bases[stage]+field,stages[stage][field]);
    color(false);GPUCMD_AddWrite(GPUREG_DEPTH_COLOR_MASK,0xf00);
    GPUCMD_AddWrite(GPUREG_FRAGOP_ALPHA_TEST,0);GPUCMD_AddWrite(GPUREG_COLOR_OPERATION,0);
    GPUCMD_AddWrite(GPUREG_LOGIC_OP,GPU_LOGICOP_COPY);GPUCMD_AddWrite(GPUREG_COLORBUFFER_READ,0);
    index_buffer(0,0);GPUCMD_AddWrite(GPUREG_DRAWELEMENTS,1);submit(commands);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
        bool covered=x>=width/8 && x<width*7/8 && y>=height/8 && y<height*7/8 &&
            (2*x+1)*height+(2*y+1)*width<2*width*height;
        unsigned at=tile(x,y,width);
        if(target[at]!=(covered?0x457fc17d:0x102030ff) ||
           depth[at*2]!=255 || depth[at*2+1]!=255)fail();
    }
    static const char passed[]="CTR2WUP compiled TEV buffer PASS";
    svcOutputDebugString(passed,sizeof(passed)-1);
    request(GX_MemoryFill(target,0x102030ff,target+width*height,GX_FILL_32BIT_DEPTH|GX_FILL_TRIGGER,
        (u32*)depth,65535,(u32*)(depth+width*height*2),GX_FILL_16BIT_DEPTH|GX_FILL_TRIGGER));
    gspWaitForPSC0();GPUCMD_SetBuffer(commands,1024,0);
}
static void prepare_stencil(u32 *commands,u32 *target,const u8 *depth,unsigned width,unsigned height) {
    color(false);combiner(false);GPUCMD_AddWrite(GPUREG_TEXUNIT0_PARAM,0);
    GPUCMD_AddWrite(GPUREG_FRAGOP_ALPHA_TEST,0);GPUCMD_AddWrite(GPUREG_COLOR_OPERATION,0);
    GPUCMD_AddWrite(GPUREG_LOGIC_OP,GPU_LOGICOP_COPY);GPUCMD_AddWrite(GPUREG_COLORBUFFER_READ,0);
    GPUCMD_AddWrite(GPUREG_ATTRIBBUFFER0_OFFSET,0);
    GPUCMD_AddWrite(0x2cb,2);GPUCMD_AddWrite(0x2cc,0x4c402000);
    /* Stencil-only preparation preserves color and the far depth clear. */
    GPUCMD_AddWrite(GPUREG_DEPTH_COLOR_MASK,0);
    stencil(GPU_ALWAYS,0x0c,0x0f,0x0f,GPU_STENCIL_KEEP,GPU_STENCIL_KEEP,GPU_STENCIL_REPLACE);
    for(unsigned triangle=0;triangle<2;++triangle) {
        index_buffer(1,triangle);GPUCMD_AddWrite(GPUREG_DRAWELEMENTS,1);
    }
    /* Compare reference 2 against stored C, not the reverse. GREATER fails;
     * only the high stencil nibble changes. A mistaken pass also writes color
     * and near depth, which the independent preparation check would reject. */
    GPUCMD_AddWrite(GPUREG_DEPTH_COLOR_MASK,1u|(GPU_ALWAYS<<4)|(15u<<8)|(1u<<12));
    stencil(GPU_GREATER,2,0x0f,0xf0,GPU_STENCIL_INVERT,GPU_STENCIL_KEEP,GPU_STENCIL_KEEP);
    index_buffer(1,0);GPUCMD_AddWrite(GPUREG_DRAWELEMENTS,1);submit(commands);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
        bool covered=x>=width/8 && x<width*7/8 && y>=height/8 && y<height*7/8;
        bool lower=(2*x+1)*height+(2*y+1)*width<2*width*height;
        unsigned expected=covered?(lower?0x5c:0xac):0xa5;
        unsigned at=tile(x,y,width);const u8 *stored=depth+at*4;
        if(target[at]!=0x102030ff || stored[0]!=255 || stored[1]!=255 ||
           stored[2]!=255 || stored[3]!=expected)fail();
    }
    static const char prepared[]="CTR2WUP compiled stencil preparation PASS";
    svcOutputDebugString(prepared,sizeof(prepared)-1);
    GPUCMD_SetBuffer(commands,1024,0);
}
int main(void) {
    gfxInit(GSP_RGB565_OES,GSP_RGB565_OES,false);
    u32 *texture=linearMemAlign(16*8*4,128);
    u16 *texture1=linearMemAlign(8*8*2,128);u8 *texture2=linearMemAlign(8*8,128);
    u32 *commands=linearAlloc(4096);u8 *geometry=linearMemAlign(256,16);
    u32 *targets[2]={linearAlloc(240*400*4),linearAlloc(240*320*4)};
    u8 *depths[2]={linearAlloc(240*400*2),linearAlloc(240*320*4)};
    if(!commands || !geometry || !texture || !texture1 || !texture2 ||
       !targets[0] || !targets[1] || !depths[0] || !depths[1])fail();
    static const float vertices[4][6]={{-.75f,-.75f,0.f,1.f,0.f,0.f},{.75f,-.75f,0.f,1.f,1.f,0.f},
        {-.75f,.75f,0.f,1.f,0.f,1.f},{.75f,.75f,0.f,1.f,1.f,1.f}};
    const u8 indices[]={0,1,2,1,3,2};memset(geometry,0,256);memcpy(geometry,vertices,sizeof(vertices));
    float far_vertices[4][6];memcpy(far_vertices,vertices,sizeof(far_vertices));
    for(unsigned i=0;i<4;++i)far_vertices[i][2]=-1.f;
    memcpy(geometry+128,far_vertices,sizeof(far_vertices));
    memcpy(geometry+96,indices,sizeof(indices));
    for(unsigned i=0;i<6;++i)((u16*)(geometry+112))[i]=indices[i];
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<16;++x)texture[tile(x,y,16)]=texel(x,y);
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x) {
        texture1[tile(x,y,8)]=(u16)(((x&1)?0xf800:0)|((y&1)?0x07e0:0)|(((x^y)&1)?0x001f:0));
        texture2[tile(x,y,8)]=(u8)(11+23*x+3*y);
    }
    texture1[tile(3,3,8)]=0xf800;texture1[tile(4,3,8)]=0xffe0;
    texture1[tile(3,4,8)]=0x001f;texture1[tile(4,4,8)]=0xf800;
    for(unsigned screen=0;screen<2;++screen) {
        u16 width,height;u16 *output=(u16*)gfxGetFramebuffer(screen?GFX_BOTTOM:GFX_TOP,GFX_LEFT,&width,&height);
        if(width!=240 || height!=(screen?320:400))fail();
        unsigned pixels=(unsigned)width*height,depth_bytes=screen?4:2;
        request(GX_MemoryFill(targets[screen],0x102030ff,targets[screen]+pixels,GX_FILL_32BIT_DEPTH|GX_FILL_TRIGGER,
            (u32*)depths[screen],screen?0xa5ffffff:65535,(u32*)(depths[screen]+pixels*depth_bytes),
            (screen?GX_FILL_32BIT_DEPTH:GX_FILL_16BIT_DEPTH)|GX_FILL_TRIGGER));gspWaitForPSC0();
        GPUCMD_SetBuffer(commands,1024,0);
        if(!screen) { pipeline();GPUCMD_AddWrite(GPUREG_TEXUNIT0_ADDR1,osConvertVirtToPhys(texture)/8); }
        /* The second list reuses the original texture/shader state, not an upload. */
        GPUCMD_AddWrite(GPUREG_ATTRIBBUFFERS_LOC,(osConvertVirtToPhys(geometry)/16)*2);
        GPUCMD_AddWrite(GPUREG_COLORBUFFER_LOC,osConvertVirtToPhys(targets[screen])/8);
        GPUCMD_AddWrite(GPUREG_DEPTHBUFFER_LOC,osConvertVirtToPhys(depths[screen])/8);
        GPUCMD_AddWrite(GPUREG_DEPTHBUFFER_FORMAT,screen?GPU_RB_DEPTH24_STENCIL8:GPU_RB_DEPTH16);
        GPUCMD_AddWrite(GPUREG_DEPTHBUFFER_READ,screen?3:2);
        GPUCMD_AddWrite(GPUREG_DEPTHBUFFER_WRITE,screen?3:2);
        GPUCMD_AddWrite(GPUREG_FRAMEBUFFER_DIM,width|((height-1)<<12)|(1<<24));
        GPUCMD_AddWrite(GPUREG_VIEWPORT_WIDTH,0x45e000);
        GPUCMD_AddWrite(GPUREG_VIEWPORT_HEIGHT,screen?0x464000:0x469000);
        if(!screen) {
            probe_material(commands,targets[screen],depths[screen],width,height,texture1,texture2);
            probe_buffer(commands,targets[screen],depths[screen],width,height);
        }
        if(screen)prepare_stencil(commands,targets[screen],depths[screen],width,height);
        for(unsigned triangle=0;triangle<3;++triangle) {
            /* The third draw repeats the first triangle at far Z with alpha
             * disabled. LESS must reject it against both near and clear depth. */
            GPUCMD_AddWrite(GPUREG_ATTRIBBUFFER0_OFFSET,triangle==2?128:0);
            color(triangle!=0);combiner(triangle!=0);
            /* Alpha testing precedes the merger. RGB adds ONE/ONE to the
             * seeded target; alpha uses ONE/ZERO. Reset blend mode on the
             * first triangle of each retained list. */
            GPUCMD_AddWrite(GPUREG_FRAGOP_ALPHA_TEST,triangle==2?0:1u|((triangle?GPU_EQUAL:GPU_GEQUAL)<<4)|((triangle?191u:128u)<<8));
            GPUCMD_AddWrite(GPUREG_COLOR_OPERATION,triangle?0x100u:0u);
            GPUCMD_AddWrite(GPUREG_BLEND_FUNC,0x01110000u);
            GPUCMD_AddWrite(GPUREG_LOGIC_OP,GPU_LOGICOP_COPY);
            GPUCMD_AddWrite(GPUREG_DEPTH_COLOR_MASK,1u|(GPU_LESS<<4)|(15u<<8)|(1u<<12));
            if(screen) {
                if(triangle==2)stencil(GPU_EQUAL,0x50,0xf0,0xf0,
                    GPU_STENCIL_KEEP,GPU_STENCIL_INVERT,GPU_STENCIL_KEEP);
                else stencil(GPU_EQUAL,0x0c,0x0f,0x0f,
                    GPU_STENCIL_KEEP,GPU_STENCIL_KEEP,GPU_STENCIL_INCR);
            }
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
            index_buffer(screen,triangle);
            GPUCMD_AddWrite(GPUREG_DRAWELEMENTS,1);
        }
        submit(commands);
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
            unsigned index=tile(x,y,width);bool visible=expected_depth(x,y,width,height)==0;
            u32 packed=visible?0:(screen?0xffffff:65535);
            if(screen) {
                bool covered=x>=width/8 && x<width*7/8 && y>=height/8 && y<height*7/8;
                packed|=(u32)(covered?(visible?0xad:0xac):0xa5)<<24;
            }
            const u8 *stored=depths[screen]+index*depth_bytes;
            if(targets[screen][index]!=expected(x,y,width,height))fail();
            for(unsigned lane=0;lane<depth_bytes;++lane)if(stored[lane]!=(u8)(packed>>(lane*8)))fail();
        }
        request(GX_DisplayTransfer(targets[screen],GX_BUFFER_DIM(width,height),(u32*)output,GX_BUFFER_DIM(width,height),GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565)));gspWaitForPPF();
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)
            if(output[y*width+x]!=rgb565(expected(x,y,width,height)))fail();
    }
    gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();
    static const char ready[]="CTR2WUP DRAW READY";svcOutputDebugString(ready,sizeof(ready)-1);svcSleepThread(250000000);
    linearFree(commands);linearFree(geometry);linearFree(texture);linearFree(texture1);linearFree(texture2);
    linearFree(targets[0]);linearFree(targets[1]);linearFree(depths[0]);linearFree(depths[1]);gfxExit();
    static const char depth_pass[]="CTR2WUP compiled D16 depth PASS";
    svcOutputDebugString(depth_pass,sizeof(depth_pass)-1);
    static const char stencil_pass[]="CTR2WUP compiled D24S8 stencil PASS";
    svcOutputDebugString(stencil_pass,sizeof(stencil_pass)-1);
    static const char message[]="CTR2WUP compiled textured PICA PASS";svcOutputDebugString(message,sizeof(message)-1);return 0;
}
