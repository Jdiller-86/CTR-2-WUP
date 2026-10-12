#ifndef CTR_TEST_FILESYSTEM_FSA_H
#define CTR_TEST_FILESYSTEM_FSA_H
#include <stddef.h>
#include <stdint.h>
/* Test declarations for the three public WUT calls used by report_storage.c.
 * Console builds use the installed WUT header and library, never this stub. */
typedef int32_t FSAClientHandle;
typedef int32_t FSError;
typedef struct FSAClientAttachAsyncData FSAClientAttachAsyncData;
FSAClientHandle FSAAddClient(FSAClientAttachAsyncData *data);
FSError FSAFlushVolume(FSAClientHandle client, const char *path);
FSError FSADelClient(FSAClientHandle client);
#endif
