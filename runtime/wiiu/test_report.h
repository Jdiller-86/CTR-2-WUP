#ifndef CTR_TEST_REPORT_H
#define CTR_TEST_REPORT_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* Called after the file is synced and closed. The platform must commit directory
 * metadata too; return false and set errno on failure. It must not log recursively. */
typedef bool (*ctr_report_volume_sync)(void *context);

typedef struct {
    FILE *file;
    bool failed;
    int error_code;
    const char *error_stage;
    ctr_report_volume_sync volume_sync;
    void *sync_context;
    uint64_t commits;
    char filename[96];
    char path[512];
} ctr_test_report;

/* The caller supplies an existing directory and a zero-initialized report.
 * Each launch reserves a new file exclusively; existing reports are preserved.
 * The ID is a platform tick count, not a wall-clock date. */
bool ctr_test_report_open(ctr_test_report *report, const char *directory,
                          const char *application, const char *version, uint64_t id);
/* Flush each record so an interrupted session retains available evidence.
 * A successful flush is not a power-loss durability guarantee. */
bool ctr_test_report_line(ctr_test_report *report, const char *message);
/* Register the mounted-volume sync without taking ownership of its mount. */
void ctr_test_report_set_volume_sync(ctr_test_report *report,
                                    ctr_report_volume_sync sync, void *context);
/* File sync -> checked close -> mounted-volume sync -> reopen the existing file.
 * A missing file is never recreated. Only successful full commits are counted.
 * Filesystem acknowledgement cannot guarantee an SD controller survives power loss. */
bool ctr_test_report_commit(ctr_test_report *report);
bool ctr_test_report_close(ctr_test_report *report);
/* Append a closed-file exit checkpoint, closing it again before returning.
 * Never creates/replaces a missing report or hides an earlier write failure. */
bool ctr_test_report_checkpoint(ctr_test_report *report, const char *message);

#endif
