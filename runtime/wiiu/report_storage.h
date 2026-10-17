#ifndef CTR_REPORT_STORAGE_H
#define CTR_REPORT_STORAGE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int32_t client;
    int32_t last_status;
} ctr_report_storage;

/* Initialize client to -1. WUT owns the existing SD mount/FSA initialization.
 * This helper owns only one extra FSA client and never mounts/unmounts a volume. */
bool ctr_report_storage_sync(void *context);
/* Independently read the physical SD path through FSA, checking the complete
 * expected length and SHA-256. No mount ownership or stdio cache is reused. */
bool ctr_report_storage_verify(void *context,const char *path,uint64_t bytes,
                                const uint8_t sha256[32]);
void ctr_report_storage_close(ctr_report_storage *storage);

#endif
