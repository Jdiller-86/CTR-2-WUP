#ifndef CTR2WUP_DIAGNOSTIC_FONT_H
#define CTR2WUP_DIAGNOSTIC_FONT_H
#include <stdint.h>
/* Original 5x7 bitmap glyphs for this diagnostic. */
void ctr_draw_text(uint32_t *image, unsigned width, unsigned height,
                   unsigned x, unsigned y, const char *text, uint32_t rgba);
#endif
