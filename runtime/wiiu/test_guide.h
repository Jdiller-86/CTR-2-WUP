#ifndef CTR_TEST_GUIDE_H
#define CTR_TEST_GUIDE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    CTR_GUIDE_A=1u<<0, CTR_GUIDE_B=1u<<1, CTR_GUIDE_MINUS=1u<<2,
    CTR_GUIDE_PLUS=1u<<3, CTR_GUIDE_RIGHT=1u<<4, CTR_GUIDE_LEFT=1u<<5,
    CTR_GUIDE_UP=1u<<6, CTR_GUIDE_DOWN=1u<<7, CTR_GUIDE_R=1u<<8,
    CTR_GUIDE_L=1u<<9, CTR_GUIDE_X=1u<<10, CTR_GUIDE_Y=1u<<11,
    CTR_GUIDE_SKIP=1u<<12, CTR_GUIDE_FAIL=1u<<13
};
typedef enum { CTR_GUIDE_NATIVE, CTR_GUIDE_INTERACTIVE, CTR_GUIDE_SAVE, CTR_GUIDE_ASSETS, CTR_GUIDE_GX } ctr_guide_kind;
typedef struct {
    uint64_t milliseconds;
    uint32_t pressed,held;
    float left_x,left_y,right_x,right_y;
    bool touch_inside,touch_border,guest_exited,guest_fault,guest_pass,guest_frame_available,fresh_save_reload,guest_green_frame;
    uint64_t guest_frame_serial,input_frame_serial;
    uint16_t touch_x,touch_y;
    unsigned guest_sessions;
} ctr_guide_input;
typedef void (*ctr_guide_log)(void *, const char *);
typedef struct {
    ctr_guide_kind kind;
    unsigned step,passed,failed,skipped,expected_session;
    uint32_t observed;
    uint64_t step_started,last_time;
    uint64_t input_frame_serial,button_started;
    bool started,done,cancelled,restart_requested,restart_waiting,fresh_reload_confirmed;
    ctr_guide_log log;
    void *context;
    char outcomes[10];
    const char *evidence[10];
} ctr_test_guide;

void ctr_test_guide_init(ctr_test_guide *, ctr_guide_kind, ctr_guide_log, void *);
void ctr_test_guide_update(ctr_test_guide *, const ctr_guide_input *);
bool ctr_test_guide_take_restart(ctr_test_guide *);
void ctr_test_guide_view(const ctr_test_guide *, uint64_t milliseconds, char rows[7][72]);
void ctr_test_guide_summary(const ctr_test_guide *);
/* Named outcomes retained independently of the full trace handler. */
bool ctr_test_guide_results(const ctr_test_guide *, char *, size_t);

#endif
