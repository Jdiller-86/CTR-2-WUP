#ifndef CTR_TEST_REPORT_H
#define CTR_TEST_REPORT_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    FILE *file;
    bool failed;
    int error_code;
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
bool ctr_test_report_close(ctr_test_report *report);

#endif
