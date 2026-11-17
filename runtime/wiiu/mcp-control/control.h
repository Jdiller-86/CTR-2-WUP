#ifndef CTR_WIIU_CONTROL_H
#define CTR_WIIU_CONTROL_H
#include "protocol.h"

#define CTR_SD_ROOT "fs:/vol/external01/"
#define CTR_CONTROL_ROOT CTR_SD_ROOT "ctr2wup/control"
#define CTR_CONTROL_APPS "wiiu/apps/ctr2wup-control/"

bool ctr_control_ready(void);
bool ctr_control_receipt(const CtrControlRequest *request,const char *state,int32_t result);
void ctr_control_install(const CtrControlRequest *request);

#endif
