#ifndef CTR_RETAINED_LOGS_H
#define CTR_RETAINED_LOGS_H
#include <stdbool.h>
#include <stdint.h>
typedef struct { unsigned files,entries;uint64_t bytes;bool present,complete;int error; } ctr_retained_logs;
/* Copy already-dumped SD system logs into a fresh private return directory.
 * No SLC access, execution, title reinstall or native exit retry. Bounded tree;
 * exclusive destinations, no symlinks, exact binary readback before acceptance. */
ctr_retained_logs ctr_retained_logs_copy(const char *source,const char *destination);
#endif
