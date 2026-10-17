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

#define CTR_CAPTURE_TITLE_LIMIT 4096u
typedef enum {
    CTR_CAPTURE_INVENTORY_INCONCLUSIVE,
    CTR_CAPTURE_INVENTORY_ABSENT,
    CTR_CAPTURE_INVENTORY_PRESENT
} CtrInstallCaptureInventoryState;
typedef struct {
    CtrInstallCaptureInventoryState state;
    const char *reason;
    uint32_t match_index;
} CtrInstallCaptureInventory;

/* MCP entries begin with a big-endian title ID. Absence requires a successful
 * underfilled list and stable global counts within capacity; the global and
 * enumerated counts need not equal each other. Never scan outside returned. */
CtrInstallCaptureInventory ctr_install_capture_inventory(
    const void *entries,size_t bytes,size_t stride,int32_t before,
    int32_t result,uint32_t returned,int32_t after,uint64_t title);

/* Corroborating exact-ID lookup: a successful actual match always blocks;
 * nonzero API results alone never establish absence. A lookup match uses
 * UINT32_MAX as its index because its record is outside the list buffer. */
CtrInstallCaptureInventory ctr_install_capture_confirm_inventory(
    CtrInstallCaptureInventory inventory,const void *entry,size_t bytes,
    int32_t result,uint64_t title);

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

/* Require a neutral input sample before accepting a fresh selection. A launch
 * button still held when the app opens must never authorize installation. */
bool ctr_install_capture_select(bool *armed,bool valid,bool held,bool pressed);

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
