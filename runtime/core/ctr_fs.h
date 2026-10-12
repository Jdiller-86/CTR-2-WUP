#ifndef CTR_FS_H
#define CTR_FS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CTR_FS_MAX_FILES 16u
#define CTR_FS_PATH_BYTES 256u
#define CTR_FS_MAX_FILE_BYTES (1024u*1024u)
#define CTR_FS_MAX_VOLUME_BYTES (4u*1024u*1024u)
#define CTR_FS_NOT_FOUND UINT32_C(0xc8804478)
#define CTR_FS_EXISTS UINT32_C(0xc82044be)
#define CTR_FS_INVALID UINT32_C(0xe0e046be)
#define CTR_FS_DENIED UINT32_C(0xc8a044dc)
#define CTR_FS_BUSY UINT32_C(0xc8a044db)
#define CTR_FS_FULL UINT32_C(0xc86044d2)

typedef struct {
    char path[CTR_FS_PATH_BYTES];
    uint8_t *bytes;
    uint32_t size;
    unsigned references;
    bool active;
} ctr_fs_file;
typedef struct ctr_fs_volume {
    uint32_t archive_id;
    bool writable, dirty;
    uint64_t commits;
    unsigned mounts;
    ctr_fs_file files[CTR_FS_MAX_FILES];
    /* Commit/flush succeed only if the platform confirms persistence. */
    bool (*persist)(void *context, const struct ctr_fs_volume *volume);
    void *persist_context;
} ctr_fs_volume;
struct ctr_guest;

/* Platform-owned immutable Level-3 RomFS service view. This is file transport,
   not NCCH decryption or IVFC authentication. Reader fills the exact requested
   range; failure never partially publishes guest output. No guest host paths. */
/* Initialize a zeroed view; its reader/context remain borrowed until close. */
typedef struct ctr_fs_romfs {
    bool (*read)(void *context,uint64_t offset,void *output,uint32_t size);
    void *context;
    uint64_t size;
    unsigned mounts,references;
} ctr_fs_romfs;
bool ctr_fs_romfs_init(ctr_fs_romfs *view,uint64_t size,
                       bool (*read)(void *,uint64_t,void *,uint32_t),void *context);
bool ctr_fs_romfs_close(ctr_fs_romfs *view);
bool ctr_fs_mount_romfs(struct ctr_guest *guest,ctr_fs_romfs *view);

/* Explicit configured private volumes: savedata(4) or virtual SDMC(9).
   No default SD-card exposure, cartridge data, directories or ROMFS claims. */
bool ctr_fs_volume_init(ctr_fs_volume *volume,uint32_t archive_id,bool writable);
bool ctr_fs_volume_close(ctr_fs_volume *volume); /* Refuses live mounts/files. */
uint32_t ctr_fs_seed(ctr_fs_volume *volume,const char *path,const void *bytes,uint32_t size);
bool ctr_fs_mount(struct ctr_guest *guest,ctr_fs_volume *volume);
bool ctr_fs_request(struct ctr_guest *guest,unsigned session,const uint32_t *words,
                    uint32_t base,uint32_t *output,unsigned *count);
void ctr_fs_session_closed(struct ctr_guest *guest,unsigned session);
/* Shared checked range/physical-alias rejection for service descriptors. */
bool ctr_fs_buffer(struct ctr_guest *guest,uint32_t address,uint32_t size,
                   unsigned permissions,uint32_t command_base);
/* Portable snapshot, explicit little-endian, bounded, CRC32 corruption check.
   Import requires a fresh empty volume and validates everything before publish. */
bool ctr_fs_export(const ctr_fs_volume *volume,uint8_t **bytes,size_t *size);
bool ctr_fs_import(ctr_fs_volume *volume,const uint8_t *bytes,size_t size);
#endif
