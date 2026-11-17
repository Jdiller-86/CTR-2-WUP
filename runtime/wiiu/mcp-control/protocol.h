#ifndef CTR_CONTROL_PROTOCOL_H
#define CTR_CONTROL_PROTOCOL_H

#include "install_capture.h"

enum { CTR_CONTROL_REBOOT=1, CTR_CONTROL_HOMEBREW, CTR_CONTROL_TITLE, CTR_CONTROL_INSTALL };
#define CTR_CONTROL_FILES 64u
#define CTR_CONTROL_PACKET_BYTES (64u+48u+2u*CTR_CONTROL_FILES*168u)
typedef struct {
    uint64_t epoch,title;
    uint32_t sequence;
    unsigned action,target,version,file_count,installed_count;
    char job[33];
    CtrInstallCaptureFile files[CTR_CONTROL_FILES],installed[CTR_CONTROL_FILES];
} CtrControlRequest;

/* Inventory names borrow the immutable packet until dispatch completes. */
bool ctr_control_decode(const uint8_t *packet,size_t size,const uint8_t public_key[32],
                        uint64_t epoch,uint32_t sequence,CtrControlRequest *request);
bool ctr_control_application(uint64_t title);

#endif
