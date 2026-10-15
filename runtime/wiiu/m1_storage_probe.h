#ifndef CTR_M1_STORAGE_PROBE_H
#define CTR_M1_STORAGE_PROBE_H
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    CTR_M1_STORAGE_PASS=0, CTR_M1_STORAGE_ARGUMENT, CTR_M1_STORAGE_PATH,
    CTR_M1_STORAGE_IO, CTR_M1_STORAGE_CONTENTS, CTR_M1_STORAGE_BUSY,
    CTR_M1_STORAGE_RECOVERY
} CtrM1StorageResult;
typedef struct {
    CtrM1StorageResult result;
    int error_code;
    const char *error_stage;
    bool sentinel_written, sentinel_readback, fresh_reload;
    bool previous_recovered, pending_rejected, invalid_path_rejected, final_readback;
    uint32_t writes, readbacks, recoveries;
} CtrM1StorageEvidence;
/* The existing absolute directory must have no symlink, dot/dotdot or empty
 * components. source40 is exactly 40 lowercase hex digits. The probe owns only
 * m1-scratch-v1-{source40}/ and its fixed scratch names. A 128-byte original
 * source-bound CRC32 sentinel persists for a subsequent fresh-launch check.
 * sentinel_written means the verified sentinel was created on this or a previous
 * invocation; fresh_reload distinguishes the latter. Every read closes/reopens.
 * Other fixtures simulate rejected pending publication and backup recovery. This
 * tests closed writes/readback and software recovery, never power-loss durability.
 * A private mkdir lock excludes concurrent cooperative probes. Namespace or file
 * corruption, symlinks and unexpected existing files fail while preserving data.
 * Do not run concurrently with external modification of the scratch namespace.
 * Output is diagnostic evidence even on failure; NULL output makes no changes. */
bool ctr_m1_storage_probe(const char *directory,const char *source40,
    CtrM1StorageEvidence *output);
const char *ctr_m1_storage_result_name(CtrM1StorageResult result);
#endif
