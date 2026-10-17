#ifndef CTR_CAPTURE_LOG_H
#define CTR_CAPTURE_LOG_H
#include "ctr_sha256.h"
#include <stdbool.h>
#include <stdio.h>

typedef bool (*capture_log_sync)(void *);
typedef bool (*capture_log_verify)(void *,const char *,uint64_t,const uint8_t [32]);
typedef struct {
    FILE *file;
    char path[512],filename[96];
    ctr_sha256 expected;
    uint64_t bytes,verified;
    bool failed;
    int error;
    const char *stage;
    capture_log_sync sync;
    capture_log_verify verify;
    void *context;
} capture_log;

/* Every line closes, syncs and reads the exact expected bytes back through both
 * stdio and the platform's independent SD view before reopening. No missing
 * report is recreated; the first error stays latched. */
bool capture_log_open(capture_log *,const char *directory,uint64_t session,
                      capture_log_sync,capture_log_verify,void *);
bool capture_log_line(capture_log *,const char *);
bool capture_log_close(capture_log *);
/* Closed-file lifecycle checkpoints retain exact stdio verification. The
 * caller disables platform callbacks before a ProcUI/system transition. */
bool capture_log_checkpoint(capture_log *,const char *);
#endif
