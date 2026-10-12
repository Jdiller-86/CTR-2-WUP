#include "report_storage.h"
#include <coreinit/filesystem_fsa.h>
#include <errno.h>

bool ctr_report_storage_sync(void *context) {
    ctr_report_storage *storage = context;
    if (!storage) { errno = EINVAL; return false; }
    if (storage->client <= 0) {
        storage->client = FSAAddClient(NULL);
        if (storage->client <= 0) {
            storage->last_status = storage->client;
            errno = EIO;
            return false;
        }
    }
    storage->last_status = FSAFlushVolume(storage->client, "/vol/external01");
    if (storage->last_status < 0) { errno = EIO; return false; }
    return true;
}

void ctr_report_storage_close(ctr_report_storage *storage) {
    if (!storage || storage->client <= 0) return;
    storage->last_status = FSADelClient(storage->client);
    storage->client = -1;
}
