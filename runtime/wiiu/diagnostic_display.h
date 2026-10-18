#ifndef CTR_DIAGNOSTIC_DISPLAY_H
#define CTR_DIAGNOSTIC_DISPLAY_H
#include <stdbool.h>
/* Shared checked display, originally accepted in Install Capture.
 * The caller owns ProcUI and must only draw while in the foreground. */
void ctr_diagnostic_display_labels(const char *title,const char *footer);
bool ctr_diagnostic_display_row(unsigned row,const char *text);
bool ctr_diagnostic_display_init(void);
void ctr_diagnostic_display_line(const char *text);
bool ctr_diagnostic_display_draw(void);
void ctr_diagnostic_display_invalidate(void);
void ctr_diagnostic_display_close(bool foreground);
#endif
