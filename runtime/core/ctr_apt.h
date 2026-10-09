#ifndef CTR_APT_H
#define CTR_APT_H
#include "ctr_guest.h"
/* Explicit standalone application lifecycle, APPID_APPLICATION (0x300).
   No HOME-menu applets, shell sleep, shared font or cross-application launch. */
bool ctr_apt_configure(ctr_guest *guest);
bool ctr_apt_request(ctr_guest *guest,const uint32_t *words,uint32_t base,
                     uint32_t *output,unsigned *count);
void ctr_apt_close(ctr_guest *guest);
#endif
