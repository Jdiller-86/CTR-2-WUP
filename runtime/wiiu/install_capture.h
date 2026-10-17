#ifndef CTR_INSTALL_CAPTURE_H
#define CTR_INSTALL_CAPTURE_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>

typedef struct {
    const char *name;
    uint64_t size;
    uint8_t sha256[32];
} CtrInstallCaptureFile;

/* Exact inventory and bytes; no links, extra files or changed inputs. */
bool ctr_install_capture_verify(const char *directory,
                                const CtrInstallCaptureFile *files,size_t count,
                                const char **failed_file);

typedef struct {
    uint64_t title,bytes,total;
    uint32_t contents,total_contents,active;
} CtrInstallCaptureProgress;

typedef struct {
    atomic_bool complete;
    int32_t result;
    bool attempted,submitted,abort_requested,log_failed;
} CtrInstallCapture;

/* Called only by the IOS callback. Publication orders the raw result before
 * completion; all logging and UI remain on the application thread. */
void ctr_install_capture_complete(CtrInstallCapture *capture,int32_t result);

typedef struct {
    bool (*record)(void *,const char *event,int32_t result,
                   const CtrInstallCaptureProgress *progress);
    int32_t (*info)(void *);
    int32_t (*target)(void *,unsigned target);
    int32_t (*start)(void *,CtrInstallCapture *);
    int32_t (*progress)(void *,CtrInstallCaptureProgress *);
    int32_t (*abort)(void *);
    /* Pump the UI and yield. False requests cancellation, never buffer release. */
    bool (*pump)(void *);
} CtrInstallCaptureOps;

/* Caller has verified the pinned payload, title inventory, writable report and
 * explicit target selection. One attempt per instance. Never returns while an
 * accepted asynchronous request still owns the caller's buffers. */
bool ctr_install_capture_run(CtrInstallCapture *capture,
                             const CtrInstallCaptureOps *ops,void *context,
                             unsigned target);
#endif
