#ifndef CTR_SHA256_H
#define CTR_SHA256_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t state[8];
    uint8_t block[64];
    uint64_t bytes;
    unsigned used;
    bool finalized;
} ctr_sha256;
void ctr_sha256_init(ctr_sha256 *context);
bool ctr_sha256_update(ctr_sha256 *context,const void *data,size_t size);
bool ctr_sha256_final(ctr_sha256 *context,uint8_t output[32]);
#endif
