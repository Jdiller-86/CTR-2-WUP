/* Original portable implementation of the published SHA-256 algorithm. */
#include "ctr_sha256.h"
#include <string.h>

static uint32_t rotate(uint32_t value,unsigned count) { return (value>>count)|(value<<(32-count)); }
static void transform(ctr_sha256 *c,const uint8_t *bytes) {
    static const uint32_t constants[64]={
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t words[64];
    for(unsigned i=0;i<16;++i)words[i]=(uint32_t)bytes[i*4]<<24|(uint32_t)bytes[i*4+1]<<16|(uint32_t)bytes[i*4+2]<<8|bytes[i*4+3];
    for(unsigned i=16;i<64;++i) {
        uint32_t x=words[i-15],y=words[i-2];
        words[i]=words[i-16]+(rotate(x,7)^rotate(x,18)^(x>>3))+words[i-7]+(rotate(y,17)^rotate(y,19)^(y>>10));
    }
    uint32_t a=c->state[0],b=c->state[1],d=c->state[3],e=c->state[4],f=c->state[5],g=c->state[6],h=c->state[7],cc=c->state[2];
    for(unsigned i=0;i<64;++i) {
        uint32_t t=h+(rotate(e,6)^rotate(e,11)^rotate(e,25))+((e&f)^((~e)&g))+constants[i]+words[i];
        uint32_t u=(rotate(a,2)^rotate(a,13)^rotate(a,22))+((a&b)^(a&cc)^(b&cc));
        h=g;g=f;f=e;e=d+t;d=cc;cc=b;b=a;a=t+u;
    }
    const uint32_t result[8]={a,b,cc,d,e,f,g,h};
    for(unsigned i=0;i<8;++i)c->state[i]+=result[i];
}
void ctr_sha256_init(ctr_sha256 *c) {
    if(!c)return;
    memset(c,0,sizeof(*c));
    const uint32_t initial[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    memcpy(c->state,initial,sizeof(initial));
}
bool ctr_sha256_update(ctr_sha256 *c,const void *data,size_t size) {
    if(!c || c->finalized || (!data && size) || c->used>=64 || c->bytes>UINT64_MAX/8 || size>UINT64_MAX/8-c->bytes)return false;
    const uint8_t *bytes=data;c->bytes+=size;
    while(size) {
        size_t count=64-c->used;if(count>size)count=size;
        memcpy(c->block+c->used,bytes,count);c->used+=(unsigned)count;bytes+=count;size-=count;
        if(c->used==64) { transform(c,c->block);c->used=0; }
    }
    return true;
}
bool ctr_sha256_final(ctr_sha256 *c,uint8_t output[32]) {
    if(!c || !output || c->finalized || c->used>=64 || c->bytes>UINT64_MAX/8)return false;
    uint64_t bits=c->bytes*8;c->block[c->used++]=0x80;
    if(c->used>56) { memset(c->block+c->used,0,64-c->used);transform(c,c->block);c->used=0; }
    memset(c->block+c->used,0,56-c->used);
    for(unsigned i=0;i<8;++i)c->block[63-i]=(uint8_t)(bits>>(i*8));
    transform(c,c->block);
    for(unsigned i=0;i<8;++i)for(unsigned b=0;b<4;++b)output[i*4+b]=(uint8_t)(c->state[i]>>(24-8*b));
    c->finalized=true;return true;
}
