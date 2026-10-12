#ifndef CTR_ROMFS_FILE_H
#define CTR_ROMFS_FILE_H
#include "ctr_fs.h"
#include <stdio.h>

typedef struct {
    FILE *file;
    ctr_fs_romfs view;
    uint64_t size;
} ctr_romfs_file;
/* A zeroed platform-owned provider for one explicitly configured immutable
 * prepared Level-3 blob. Whole-file SHA-256 must match the host manifest.
 * Standard C seeks are limited to LONG_MAX on the selected target; no truncation.
 * Guest paths never reach fopen. No new SD mount/client is created. */
bool ctr_romfs_file_open(ctr_romfs_file *file,const char *path,const uint8_t expected_sha256[32]);
bool ctr_romfs_file_close(ctr_romfs_file *file); /* Refuses live mounts/handles. */
#endif
