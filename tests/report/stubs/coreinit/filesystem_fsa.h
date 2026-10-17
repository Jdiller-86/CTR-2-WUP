#ifndef CTR_TEST_FILESYSTEM_FSA_H
#define CTR_TEST_FILESYSTEM_FSA_H
#include <stddef.h>
#include <stdint.h>
/* Test declarations for the public WUT calls used by report_storage.c.
 * Console builds use the installed WUT header and library, never this stub. */
typedef int32_t FSAClientHandle;
typedef int32_t FSError;
typedef int32_t FSAFileHandle;
enum { FS_STAT_DIRECTORY=1u<<31,FS_STAT_LINK=0x10000 };
typedef struct { uint32_t flags,size; } FSAStat;
typedef struct FSAClientAttachAsyncData FSAClientAttachAsyncData;
FSAClientHandle FSAAddClient(FSAClientAttachAsyncData *data);
FSError FSAFlushVolume(FSAClientHandle client, const char *path);
FSError FSADelClient(FSAClientHandle client);
FSError FSAGetStat(FSAClientHandle,const char *,FSAStat *);
FSError FSAGetStatFile(FSAClientHandle,FSAFileHandle,FSAStat *);
FSError FSAOpenFileEx(FSAClientHandle,const char *,const char *,uint32_t,uint32_t,uint32_t,FSAFileHandle *);
FSError FSAReadFile(FSAClientHandle,void *,uint32_t,uint32_t,FSAFileHandle,uint32_t);
FSError FSACloseFile(FSAClientHandle,FSAFileHandle);
#endif
