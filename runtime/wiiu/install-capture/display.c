#include "display.h"
#include "diagnostic_font.h"
#include "image_scale.h"
#include <coreinit/cache.h>
#include <coreinit/memdefaultheap.h>
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/surface.h>
#include <gx2/swap.h>
#include <whb/gfx.h>
#include <stdio.h>
#include <string.h>

enum { WIDTH=512,HEIGHT=288,ROWS=14,COLUMNS=82 };
static uint32_t pixels[WIDTH*HEIGHT];
static char rows[ROWS][COLUMNS+1];
static GX2Surface surfaces[2];
static ctr_scale_plan plans[2];
static bool initialized,ready,dirty=true,presented;

void capture_display_line(const char *text) {
    if(!text)return;
    do {
        memmove(rows,rows+1,sizeof(rows)-sizeof(rows[0]));
        size_t n=strlen(text);if(n>COLUMNS)n=COLUMNS;
        memcpy(rows[ROWS-1],text,n);rows[ROWS-1][n]=0;text+=n;
        for(size_t i=0;i<n;++i)if(rows[ROWS-1][i]=='_')rows[ROWS-1][i]='-';
    } while(*text);
    dirty=true;
}

bool capture_display_init(void) {
    if(initialized)return ready;
    if(!WHBGfxInit())return false;
    initialized=true;
    const GX2Surface *targets[2]={&WHBGfxGetTVColourBuffer()->surface,&WHBGfxGetDRCColourBuffer()->surface};
    for(unsigned i=0;i<2;++i) {
        GX2Surface *s=&surfaces[i];const GX2Surface *target=targets[i];
        *s=(GX2Surface){.dim=GX2_SURFACE_DIM_TEXTURE_2D,.width=target->width,.height=target->height,
            .depth=1,.mipLevels=1,.format=GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8,
            .aa=GX2_AA_MODE1X,.use=GX2_SURFACE_USE_TEXTURE,.tileMode=GX2_TILE_MODE_LINEAR_ALIGNED};
        GX2CalcSurfaceSizeAndAlignment(s);
        if(target->format!=s->format || !s->imageSize || !s->alignment)return false;
        s->image=MEMAllocFromDefaultHeapEx(s->imageSize,s->alignment);
        CtrViewport view;
        if(!s->image || !ctr_viewport_fit(&view,target->width,target->height,WIDTH,HEIGHT) ||
           !ctr_scale_plan_init(&plans[i],&view))return false;
        memset(s->image,0,s->imageSize);
    }
    ready=true;dirty=true;return true;
}

bool capture_display_draw(void) {
    if(!ready)return false;
    if(dirty) {
        if(presented)GX2DrawDone();
        for(unsigned i=0;i<WIDTH*HEIGHT;++i)pixels[i]=UINT32_C(0x102238ff);
        char title[84];snprintf(title,sizeof(title),"CTR2WUP INSTALL CAPTURE / %.12s",CTR_SOURCE_COMMIT);
        ctr_draw_text(pixels,WIDTH,HEIGHT,8,10,title,UINT32_C(0x74ceffff));
        for(unsigned i=0;i<ROWS;++i)
            ctr_draw_text(pixels,WIDTH,HEIGHT,8,34+i*16,rows[i],UINT32_C(0xffffffff));
        ctr_draw_text(pixels,WIDTH,HEIGHT,8,HEIGHT-14,"MINUS: EXIT / CANCEL AND WAIT FOR MCP",UINT32_C(0x74ceffff));
        for(unsigned i=0;i<2;++i) {
            GX2Surface *s=&surfaces[i];
            if(!ctr_scale_rgba(&plans[i],pixels,WIDTH*HEIGHT,s->image,s->imageSize,s->pitch))return false;
            DCFlushRange(s->image,s->imageSize);
            GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE,s->image,s->imageSize);
        }
        dirty=false;
    }
    WHBGfxBeginRender();
    WHBGfxBeginRenderTV();
    GX2CopySurface(&surfaces[0],0,0,&WHBGfxGetTVColourBuffer()->surface,0,0);
    WHBGfxFinishRenderTV();
    WHBGfxBeginRenderDRC();
    GX2CopySurface(&surfaces[1],0,0,&WHBGfxGetDRCColourBuffer()->surface,0,0);
    WHBGfxFinishRenderDRC();
    WHBGfxFinishRender();
    presented=true;return true;
}

void capture_display_invalidate(void) { dirty=true; }

void capture_display_close(bool foreground) {
    if(initialized && foreground)GX2DrawDone();
    for(unsigned i=0;i<2;++i) {
        ctr_scale_plan_close(&plans[i]);
        if(surfaces[i].image)MEMFreeToDefaultHeap(surfaces[i].image);
        memset(&surfaces[i],0,sizeof(surfaces[i]));
    }
    if(initialized)WHBGfxShutdown();
    initialized=ready=presented=false;
}
