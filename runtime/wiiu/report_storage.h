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
void ctr_report_storage_close(ctr_report_storage *storage);

#endif
