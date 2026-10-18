#ifndef CTR_TEST_LIFECYCLE_WIIU_H
#define CTR_TEST_LIFECYCLE_WIIU_H
#include "test_lifecycle.h"
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <proc_ui/procui.h>
#include <sysapp/launch.h>
#include <whb/proc.h>

/* Live platform operations shared by Capture, the prototype and Install Check.
 * Application-owned resources/callbacks/checkpoints remain with their caller. */
static inline void ctr_procui_request_menu(void *context) { (void)context;SYSLaunchMenu(); }
static inline ctr_proc_status ctr_procui_messages(void *context) {
    (void)context;
    switch(ProcUIProcessMessages(TRUE)) {
        case PROCUI_STATUS_IN_FOREGROUND:return CTR_PROC_FOREGROUND;
        case PROCUI_STATUS_IN_BACKGROUND:return CTR_PROC_BACKGROUND;
        case PROCUI_STATUS_RELEASE_FOREGROUND:return CTR_PROC_RELEASE;
        case PROCUI_STATUS_EXITING:return CTR_PROC_EXITING;
        default:return (ctr_proc_status)-1;
    }
}
static inline void ctr_procui_release(void *context) { (void)context;ProcUIDrawDoneRelease(); }
static inline void ctr_procui_wait(void *context) { (void)context;OSSleepTicks(OSMillisecondsToTicks(5)); }
static inline void ctr_procui_shutdown(void *context) { (void)context;ProcUIShutdown(); }
static inline void ctr_procui_shutdown_whb(void *context) { (void)context;WHBProcShutdown(); }
#endif
