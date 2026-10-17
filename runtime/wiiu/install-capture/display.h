#ifndef CTR_CAPTURE_DISPLAY_H
#define CTR_CAPTURE_DISPLAY_H
#include <stdbool.h>
/* Same WHBGfx + original bitmap font + RGBA scaling path as the native app.
 * The caller owns ProcUI and must only draw while in the foreground. */
bool capture_display_init(void);
void capture_display_line(const char *text);
bool capture_display_draw(void);
void capture_display_invalidate(void);
void capture_display_close(bool foreground);
#endif
