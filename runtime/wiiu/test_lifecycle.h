#ifndef CTR_TEST_LIFECYCLE_H
#define CTR_TEST_LIFECYCLE_H
#include <stdbool.h>
#include <stdint.h>

typedef enum { CTR_EXIT_NONE, CTR_EXIT_HBL, CTR_EXIT_MENU } ctr_exit_action;
typedef enum { CTR_PROC_FOREGROUND, CTR_PROC_BACKGROUND, CTR_PROC_RELEASE,
    CTR_PROC_EXITING } ctr_proc_status;
typedef struct { uint64_t title_id; bool requested, finished; } ctr_test_lifecycle;
/* WUT's libwhb recognizes HBL and the three regional Mii Maker launchers.
 * Menu-launched titles must request system exit, then continue ProcUI polling. */
bool ctr_test_from_hbl(uint64_t title_id);
ctr_exit_action ctr_test_request_exit(ctr_test_lifecycle *lifecycle);
typedef struct {
    void (*cleanup_resources)(void *context);
    void (*clear_callbacks)(void *context);
    void (*request_menu)(void *context);
    ctr_proc_status (*process_messages)(void *context);
    void (*release_foreground)(void *context);
    void (*wait)(void *context);
    void (*shutdown_procui)(void *context);
    void (*shutdown_whb)(void *context);
    void (*checkpoint)(void *context,const char *message);
    /* Optional WUT-owned I/O barrier, before the system transition. After this
       callback checkpoints must use OSReport only, never reopen SD clients. */
    void (*finalize_io)(void *context);
} ctr_exit_ops;
/* Runs after rendering stops. Clean up before requesting menu transition or
 * shutting down ProcUI; remove graphics callbacks before polling again.
 * HBL relaunch is delegated to WHB only after cleanup and ProcUI shutdown.
 * Poll until EXITING for a menu request; never force-stop a background state. */
bool ctr_test_complete_exit(ctr_test_lifecycle *lifecycle,bool exit_observed,
                            const ctr_exit_ops *ops,void *context);
#endif
