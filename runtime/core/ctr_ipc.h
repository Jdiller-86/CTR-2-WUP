#ifndef CTR_IPC_H
#define CTR_IPC_H
#include "ctr_guest.h"

#define CTR_IPC_OFFSET 0x80u
#define CTR_IPC_WORDS 64u
/* Original synchronous, single-process transport. Unsupported commands fault;
   unavailable services return a directory error instead of fabricated handles. */
void ctr_ipc_connect(ctr_guest *guest);
void ctr_ipc_request(ctr_guest *guest);
#endif
