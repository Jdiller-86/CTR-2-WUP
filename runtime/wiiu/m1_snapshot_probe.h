#ifndef CTR_M1_SNAPSHOT_PROBE_H
#define CTR_M1_SNAPSHOT_PROBE_H
#include "m1_storage_probe.h"

typedef struct {
    CtrM1StorageEvidence storage;
    const char *phase;
    uint64_t producer_session, recovery_session, current_session;
    bool production_adapter, interruption_prepared;
} CtrM1SnapshotEvidence;

/* Original scratch snapshots exercise the production filesystem adapter across
 * distinct launches. PREPARED leaves an uncommitted .pending and committed
 * .previous; RECOVERED validates recovery and publishes a replacement. COMPLETE
 * rechecks the persisted completion record without claiming another recovery.
 * COMPLETE's recovery/error booleans retain that validated record. Counters
 * describe only this invocation: writes counts snapshot staging writes (not
 * metadata), readbacks counts exact file/adapter reads, recoveries counts actual
 * CTR_FS_SNAPSHOT_RECOVERED loads. COMPLETE performs no new write or recovery.
 * The namespace is source-bound and exclusively locked. Unknown/partial state,
 * links and corrupt bytes are preserved for review. This is deterministic
 * software interruption/error recovery, never a power-loss durability test.
 * directory must already exist; source40 is lowercase hex; sessions are nonzero.
 * Do not modify the owned namespace concurrently from another application. */
bool ctr_m1_snapshot_probe(const char *directory,const char *source40,
    uint64_t current_session,CtrM1SnapshotEvidence *output);
#endif
