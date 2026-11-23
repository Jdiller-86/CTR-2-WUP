#ifndef CTR_DIAGNOSTIC_DISPLAY_H
#define CTR_DIAGNOSTIC_DISPLAY_H
#include <stdbool.h>
#include <stdint.h>
typedef struct GX2ContextState GX2ContextState;
typedef struct {
    const void *texture;
    uint32_t generation;
} ctr_diagnostic_scanout;
/* Shared checked display, originally accepted in Install Capture.
 * The caller owns ProcUI and must only draw while in the foreground. */
void ctr_diagnostic_display_labels(const char *title,const char *footer);
bool ctr_diagnostic_display_row(unsigned row,const char *text);
bool ctr_diagnostic_display_init(void);
GX2ContextState *ctr_diagnostic_display_context(void);
void ctr_diagnostic_display_line(const char *text);
bool ctr_diagnostic_display_draw(void);
bool ctr_diagnostic_display_guest(const uint32_t *top,const uint32_t *bottom);
#ifdef CTR_GAME_CEMU_TEST
bool ctr_diagnostic_display_scanout(const ctr_diagnostic_scanout *top_texture,const ctr_diagnostic_scanout *bottom_texture,
                                    const uint32_t *top,const uint32_t *bottom);
#endif
bool ctr_diagnostic_display_touch(uint32_t x,uint32_t y,uint16_t *guest_x,uint16_t *guest_y);
void ctr_diagnostic_display_invalidate(void);
void ctr_diagnostic_display_close(bool foreground);
#endif
