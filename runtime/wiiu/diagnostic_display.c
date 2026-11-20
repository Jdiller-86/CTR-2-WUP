#include "diagnostic_display.h"
#include "diagnostic_font.h"
#include "image_scale.h"
#include <coreinit/cache.h>
#include <coreinit/memdefaultheap.h>
#ifdef CTR_GAME_CEMU_TEST
#include <coreinit/screen.h>
#endif
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
#ifdef CTR_GAME_CEMU_TEST
static unsigned char *screen_buffers[2];
static ctr_scale_cache scanout_history[2][2];
#endif
static ctr_scale_plan plans[2];
static bool initialized,ready,dirty=true,presented;
static bool guest_mode;
static char heading[64]="CTR2WUP INSTALL CAPTURE";
static char footer[84]="MINUS: EXIT / CANCEL AND WAIT FOR MCP";

void ctr_diagnostic_display_labels(const char *title,const char *hint) {
    if(title)snprintf(heading,sizeof(heading),"%s",title);
    if(hint)snprintf(footer,sizeof(footer),"%s",hint);
    dirty=true;
}

bool ctr_diagnostic_display_row(unsigned row,const char *text) {
    if(row>=ROWS || !text)return false;
    snprintf(rows[row],sizeof(rows[row]),"%s",text);
    for(char *c=rows[row];*c;++c)if(*c=='_')*c='-';
    dirty=true;return true;
}

void ctr_diagnostic_display_line(const char *text) {
    if(!text)return;
    do {
        memmove(rows,rows+1,sizeof(rows)-sizeof(rows[0]));
        size_t n=strlen(text);if(n>COLUMNS)n=COLUMNS;
        memcpy(rows[ROWS-1],text,n);rows[ROWS-1][n]=0;text+=n;
        for(size_t i=0;i<n;++i)if(rows[ROWS-1][i]=='_')rows[ROWS-1][i]='-';
    } while(*text);
    dirty=true;
}

bool ctr_diagnostic_display_init(void) {
    if(initialized)return ready;
#ifdef CTR_GAME_CEMU_TEST
    /* Cemu 2.6 software scanout. The DRC's 854 visible pixels use a
     * 896-pixel pitch. Validate the double-buffer layout before drawing. */
    static const unsigned widths[]={1280u,854u},heights[]={720u,480u},pitches[]={1280u,896u};
    OSScreenInit();
    initialized=true;
    for(unsigned i=0;i<2u;++i) {
        GX2Surface *s=&surfaces[i];
        s->width=widths[i];s->height=heights[i];s->pitch=pitches[i];
        s->imageSize=s->pitch*s->height*4u;
        unsigned size=OSScreenGetBufferSizeEx((OSScreenID)i);
        if(size!=2u*s->imageSize)return false;
        unsigned char *base=MEMAllocFromDefaultHeapEx(size,0x100);
        if(!base)return false;
        screen_buffers[i]=base;
        memset(base,0,size);
        OSScreenSetBufferEx((OSScreenID)i,base);
        OSScreenClearBufferEx((OSScreenID)i,UINT32_C(0x010203ff));
        bool first=base[0]==1u,second=base[s->imageSize]==1u;
        if(first==second)return false;
        s->image=base+(second?s->imageSize:0u);
        CtrViewport view;
        if(!ctr_viewport_fit(&view,s->width,s->height,WIDTH,HEIGHT) ||
           !ctr_scale_plan_init(&plans[i],&view))return false;
        OSScreenEnableEx((OSScreenID)i,true);
    }
#else
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
#endif
    ready=true;dirty=true;return true;
}

static bool configure_plans(unsigned top_width,unsigned top_height,unsigned bottom_width,unsigned bottom_height) {
    const unsigned widths[]={top_width,bottom_width},heights[]={top_height,bottom_height};
    for(unsigned i=0;i<2;++i) {
        if(plans[i].view.guest_width==widths[i] && plans[i].view.guest_height==heights[i])continue;
        CtrViewport view;ctr_scale_plan next={0};
        if(!ctr_viewport_fit(&view,surfaces[i].width,surfaces[i].height,widths[i],heights[i]) ||
            !ctr_scale_plan_init(&next,&view))return false;
#ifdef CTR_GAME_CEMU_TEST
        for(unsigned half=0;half<2u;++half)ctr_scale_cache_close(&scanout_history[i][half]);
#endif
        ctr_scale_plan_close(&plans[i]);plans[i]=next;
    }
    return true;
}
static bool present(void) {
#ifdef CTR_GAME_CEMU_TEST
    for(unsigned i=0;i<2u;++i) {
        GX2Surface *s=&surfaces[i];
        OSScreenFlipBuffersEx((OSScreenID)i);
        s->image=screen_buffers[i]+(s->image==screen_buffers[i]?s->imageSize:0u);
    }
#else
    WHBGfxBeginRender();
    WHBGfxBeginRenderTV();
    GX2CopySurface(&surfaces[0],0,0,&WHBGfxGetTVColourBuffer()->surface,0,0);
    WHBGfxFinishRenderTV();
    WHBGfxBeginRenderDRC();
    GX2CopySurface(&surfaces[1],0,0,&WHBGfxGetDRCColourBuffer()->surface,0,0);
    WHBGfxFinishRenderDRC();
    WHBGfxFinishRender();
#endif
    presented=true;return true;
}
bool ctr_diagnostic_display_draw(void) {
    if(!ready)return false;
    if(guest_mode) {
        if(!configure_plans(WIDTH,HEIGHT,WIDTH,HEIGHT))return false;
        guest_mode=false;dirty=true;
    }
#ifdef CTR_GAME_CEMU_TEST
    if(!dirty && presented)return true;
#endif
    if(dirty) {
#ifndef CTR_GAME_CEMU_TEST
        if(presented)GX2DrawDone();
#endif
        for(unsigned i=0;i<WIDTH*HEIGHT;++i)pixels[i]=UINT32_C(0x102238ff);
        char title[84];snprintf(title,sizeof(title),"%s / %.12s",heading,CTR_SOURCE_COMMIT);
        ctr_draw_text(pixels,WIDTH,HEIGHT,8,10,title,UINT32_C(0x74ceffff));
        for(unsigned i=0;i<ROWS;++i)
            ctr_draw_text(pixels,WIDTH,HEIGHT,8,34+i*16,rows[i],UINT32_C(0xffffffff));
        ctr_draw_text(pixels,WIDTH,HEIGHT,8,HEIGHT-14,footer,UINT32_C(0x74ceffff));
        for(unsigned i=0;i<2;++i) {
            GX2Surface *s=&surfaces[i];
            if(!ctr_scale_rgba(&plans[i],pixels,WIDTH*HEIGHT,s->image,s->imageSize,s->pitch))return false;
            DCFlushRange(s->image,s->imageSize);
#ifndef CTR_GAME_CEMU_TEST
            GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE,s->image,s->imageSize);
#endif
        }
        dirty=false;
    }
    return present();
}
bool ctr_diagnostic_display_guest(const uint32_t *top,const uint32_t *bottom) {
    if(!ready || !top || !bottom || !configure_plans(400,240,320,240))return false;
#ifndef CTR_GAME_CEMU_TEST
    if(presented)GX2DrawDone();
#endif
    const uint32_t *images[]={top,bottom};const size_t counts[]={400u*240u,320u*240u};
    for(unsigned i=0;i<2;++i) {
        GX2Surface *s=&surfaces[i];
#ifdef CTR_GAME_CEMU_TEST
        /* Each half retains different pixels after a flip. Reuse its own
         * source history and publish only the changed output rows. */
        ctr_scale_cache *cache=&scanout_history[i][s->image!=screen_buffers[i]];
        if(cache->previous || ctr_scale_cache_init(cache,&plans[i])) {
            ctr_scale_update update;
            if(!ctr_scale_cache_update(cache,images[i],counts[i],s->image,s->imageSize,s->pitch,&update))
                return false;
            for(size_t range=0;range<cache->range_count;++range)
                DCFlushRange((uint8_t *)s->image+cache->ranges[range].offset,cache->ranges[range].bytes);
            continue;
        }
#endif
        if(!ctr_scale_rgba(&plans[i],images[i],counts[i],s->image,s->imageSize,s->pitch))return false;
        DCFlushRange(s->image,s->imageSize);
#ifndef CTR_GAME_CEMU_TEST
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE,s->image,s->imageSize);
#endif
    }
    guest_mode=true;return present();
}
bool ctr_diagnostic_display_touch(uint32_t x,uint32_t y,uint16_t *guest_x,uint16_t *guest_y) {
    CtrViewport view;
    return ready && ctr_viewport_fit(&view,surfaces[1].width,surfaces[1].height,320,240) &&
        ctr_viewport_touch(&view,x,y,1280,720,guest_x,guest_y);
}

void ctr_diagnostic_display_invalidate(void) {
    dirty=true;
#ifdef CTR_GAME_CEMU_TEST
    for(unsigned screen=0;screen<2u;++screen)
        for(unsigned half=0;half<2u;++half)ctr_scale_cache_invalidate(&scanout_history[screen][half]);
#endif
}

void ctr_diagnostic_display_close(bool foreground) {
#ifdef CTR_GAME_CEMU_TEST
    (void)foreground;
#else
    if(initialized && foreground)GX2DrawDone();
#endif
    for(unsigned i=0;i<2;++i) {
#ifdef CTR_GAME_CEMU_TEST
        for(unsigned half=0;half<2u;++half)ctr_scale_cache_close(&scanout_history[i][half]);
#endif
        ctr_scale_plan_close(&plans[i]);
#ifdef CTR_GAME_CEMU_TEST
        if(screen_buffers[i]) {
            OSScreenEnableEx((OSScreenID)i,false);
            MEMFreeToDefaultHeap(screen_buffers[i]);screen_buffers[i]=NULL;
        }
#else
        if(surfaces[i].image)MEMFreeToDefaultHeap(surfaces[i].image);
#endif
        memset(&surfaces[i],0,sizeof(surfaces[i]));
    }
#ifndef CTR_GAME_CEMU_TEST
    if(initialized)WHBGfxShutdown();
#endif
    initialized=ready=presented=guest_mode=false;
}
