#ifndef CTR_FS_SNAPSHOT_H
#define CTR_FS_SNAPSHOT_H
#include "ctr_fs.h"
/* Platform-selected path only. Caller creates the containing private directory.
   Missing snapshots are explicitly reported; corruption never starts a new save.
   Publication reserves .pending/.previous siblings and never requires rename
   to replace an existing destination. Missing-primary recovery uses a validated
   .previous file; corrupt primary/stale staging files are preserved. No power-loss claim. */
typedef enum { CTR_FS_SNAPSHOT_OK, CTR_FS_SNAPSHOT_MISSING, CTR_FS_SNAPSHOT_ERROR,
               CTR_FS_SNAPSHOT_RECOVERED } ctr_fs_snapshot_result;
ctr_fs_snapshot_result ctr_fs_snapshot_load(ctr_fs_volume *volume,const char *path);
bool ctr_fs_snapshot_save(void *path,const ctr_fs_volume *volume);
/* Platform/testing rename adapter: both names are trusted platform paths.
   Return the normal rename result/errno. Null selects the system implementation. */
typedef int (*ctr_fs_snapshot_rename)(void *context,const char *from,const char *to);
bool ctr_fs_snapshot_save_with_rename(const char *path,const ctr_fs_volume *volume,
                                     ctr_fs_snapshot_rename rename_file,void *context);
#endif
