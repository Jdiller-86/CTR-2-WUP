#ifndef CTR_FS_SNAPSHOT_H
#define CTR_FS_SNAPSHOT_H
#include "ctr_fs.h"
/* Platform-selected path only. Caller creates the containing private directory.
   Missing snapshots are explicitly reported; corruption never starts a new save.
   Publication uses an exclusive sibling file and rename. No power-loss claim. */
typedef enum { CTR_FS_SNAPSHOT_OK, CTR_FS_SNAPSHOT_MISSING, CTR_FS_SNAPSHOT_ERROR } ctr_fs_snapshot_result;
ctr_fs_snapshot_result ctr_fs_snapshot_load(ctr_fs_volume *volume,const char *path);
bool ctr_fs_snapshot_save(void *path,const ctr_fs_volume *volume);
#endif
