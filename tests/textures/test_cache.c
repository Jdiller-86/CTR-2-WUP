#include "ctr_texture_cache.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { assert(x);++checks; } while(0)
static ctr_texture_cache_statistics stats(ctr_texture_cache *cache) {
    ctr_texture_cache_statistics result={0};ctr_texture_cache_statistics_get(cache,&result);return result;
}
static void formats(void) {
    uint8_t source[1024],reference[2048];ctr_texture_cache *cache=ctr_texture_cache_create();CHECK(cache);
    for(unsigned format=0;format<14u;++format) {
        size_t size;CHECK(ctr_texture_size(format,16,8,&size)==CTR_TEXTURE_OK);
        for(size_t i=0;i<size;++i)source[i]=format>=12u?0u:(uint8_t)(i*13u+format);
        CHECK(ctr_texture_decode(source,size,format,16,8,reference,sizeof(reference))==CTR_TEXTURE_OK);
        ctr_texture_cache_lease a={0},b={0};
        CHECK(ctr_texture_cache_acquire(cache,source,size,format,16,8,&a)==CTR_TEXTURE_CACHE_OK);
        CHECK(!memcmp(a.rgba,reference,16u*8u*4u));
        CHECK(ctr_texture_cache_acquire(cache,source,size,format,16,8,&b)==CTR_TEXTURE_CACHE_OK && b.rgba==a.rgba);
        CHECK(stats(cache).pins==2u);ctr_texture_cache_release(&a);ctr_texture_cache_release(&b);
        CHECK(!a.rgba && !b.rgba && !stats(cache).pins);
    }
    CHECK(stats(cache).decodes==14u && stats(cache).hits==14u && stats(cache).entries==CTR_TEXTURE_CACHE_SLOTS);
    CHECK(stats(cache).evictions==14u-CTR_TEXTURE_CACHE_SLOTS);CHECK(ctr_texture_cache_destroy(cache));
}
static void identity_and_failures(void) {
    uint8_t source[1025]={0},reference[1024];ctr_texture_cache *cache=ctr_texture_cache_create();CHECK(cache);
    ctr_texture_cache_lease a={0},b={0};
    CHECK(ctr_texture_cache_acquire(cache,source,1024,0,16,16,&a)==CTR_TEXTURE_CACHE_OK);
    const uint8_t *first=a.rgba;ctr_texture_cache_release(&a);
    /* Unused tail bytes do not enter the exact base-level key. */
    source[1024]=199;
    CHECK(ctr_texture_cache_acquire(cache,source,sizeof(source),0,16,16,&a)==CTR_TEXTURE_CACHE_OK && a.rgba==first);
    ctr_texture_cache_release(&a);source[17]=129;
    CHECK(ctr_texture_decode(source,1024,0,16,16,reference,sizeof(reference))==CTR_TEXTURE_OK);
    CHECK(ctr_texture_cache_acquire(cache,source,1024,0,16,16,&a)==CTR_TEXTURE_CACHE_OK);
    CHECK(!memcmp(a.rgba,reference,sizeof(reference)) && stats(cache).decodes==2u);
    ctr_texture_cache_release(&a);
    /* Equal byte counts with changed dimensions/format remain distinct. */
    CHECK(ctr_texture_cache_acquire(cache,source,1024,0,8,32,&a)==CTR_TEXTURE_CACHE_OK);
    CHECK(ctr_texture_cache_acquire(cache,source,1024,2,16,32,&b)==CTR_TEXTURE_CACHE_OK);
    CHECK(stats(cache).decodes==4u);ctr_texture_cache_release(&a);ctr_texture_cache_release(&b);
    size_t bytes=stats(cache).bytes;uint64_t evictions=stats(cache).evictions;
    CHECK(ctr_texture_cache_acquire(cache,source,255,0,8,8,&a)==CTR_TEXTURE_CACHE_DECODE && !a.rgba);
    CHECK(ctr_texture_cache_acquire(cache,source,sizeof(source),14,8,8,&a)==CTR_TEXTURE_CACHE_DECODE);
    CHECK(ctr_texture_cache_acquire(cache,source,sizeof(source),0,7,8,&a)==CTR_TEXTURE_CACHE_DECODE);
    memset(source,0,sizeof(source));source[7]=0xf9;source[4]=2; /* Invalid ETC1 differential endpoint. */
    CHECK(ctr_texture_cache_acquire(cache,source,32,12,8,8,&a)==CTR_TEXTURE_CACHE_DECODE);
    CHECK(stats(cache).bytes==bytes && stats(cache).evictions==evictions && stats(cache).rejections==4u);
    CHECK(ctr_texture_cache_acquire(cache,NULL,0,0,8,8,&a)==CTR_TEXTURE_CACHE_ARGUMENT);
    CHECK(ctr_texture_cache_acquire(cache,source,sizeof(source),0,8,8,(ctr_texture_cache_lease *)source)==CTR_TEXTURE_CACHE_ARGUMENT);
    memset(source,0,sizeof(source));
    CHECK(ctr_texture_cache_acquire(cache,source,256,0,8,8,&a)==CTR_TEXTURE_CACHE_OK);
    CHECK(ctr_texture_cache_acquire(cache,a.rgba,256,0,8,8,&b)==CTR_TEXTURE_CACHE_ARGUMENT);
    CHECK(ctr_texture_cache_acquire(cache,source,256,0,8,8,&a)==CTR_TEXTURE_CACHE_ARGUMENT);
    CHECK(!ctr_texture_cache_clear(cache) && !ctr_texture_cache_destroy(cache));
    CHECK(stats(cache).pins==1u);ctr_texture_cache_release(&a);
    CHECK(ctr_texture_cache_clear(cache) && stats(cache).entries==0u && !stats(cache).bytes);
    CHECK(ctr_texture_cache_destroy(cache) && ctr_texture_cache_destroy(NULL));
    CHECK(ctr_texture_cache_acquire(NULL,source,256,0,8,8,&a)==CTR_TEXTURE_CACHE_OK && a.owned && !a.owner);
    ctr_texture_cache_release(&a);CHECK(!a.rgba && !a.owned);
}
static void pins_and_eviction(void) {
    uint8_t source[256]={0};ctr_texture_cache *cache=ctr_texture_cache_create();CHECK(cache);
    ctr_texture_cache_lease leases[CTR_TEXTURE_CACHE_SLOTS]={{0}},extra={0};
    for(unsigned i=0;i<CTR_TEXTURE_CACHE_SLOTS;++i) {
        memset(source,(int)i,sizeof(source));
        CHECK(ctr_texture_cache_acquire(cache,source,sizeof(source),0,8,8,&leases[i])==CTR_TEXTURE_CACHE_OK);
    }
    CHECK(stats(cache).pins==CTR_TEXTURE_CACHE_SLOTS && !stats(cache).evictions);
    memset(source,100,sizeof(source));
    CHECK(ctr_texture_cache_acquire(cache,source,sizeof(source),0,8,8,&extra)==CTR_TEXTURE_CACHE_BUSY && !extra.rgba);
    CHECK(stats(cache).entries==CTR_TEXTURE_CACHE_SLOTS && !stats(cache).evictions);
    ctr_texture_cache_release(&leases[1]);
    CHECK(ctr_texture_cache_acquire(cache,source,sizeof(source),0,8,8,&extra)==CTR_TEXTURE_CACHE_OK);
    CHECK(stats(cache).evictions==1u && stats(cache).pins==CTR_TEXTURE_CACHE_SLOTS);
    for(unsigned i=0;i<CTR_TEXTURE_CACHE_SLOTS;++i)if(i!=1u) {
        for(unsigned lane=0;lane<4u;++lane)CHECK(leases[i].rgba[lane]==i);
        ctr_texture_cache_release(&leases[i]);
    }
    ctr_texture_cache_release(&extra);CHECK(ctr_texture_cache_destroy(cache));
}
static void budget(void) {
    const size_t size=1024u*1024u*4u;uint8_t *source=malloc(size);CHECK(source);
    ctr_texture_cache *cache=ctr_texture_cache_create();CHECK(cache);
    ctr_texture_cache_lease leases[3]={{0}},extra={0};
    for(unsigned i=0;i<3u;++i) {
        memset(source,(int)(i+1u),size);
        CHECK(ctr_texture_cache_acquire(cache,source,size,0,1024,1024,&leases[i])==CTR_TEXTURE_CACHE_OK);
    }
    CHECK(stats(cache).bytes==CTR_TEXTURE_CACHE_MAX_BYTES && stats(cache).peak_bytes==CTR_TEXTURE_CACHE_MAX_BYTES);
    memset(source,4,size);
    CHECK(ctr_texture_cache_acquire(cache,source,size,0,1024,1024,&extra)==CTR_TEXTURE_CACHE_BUSY);
    ctr_texture_cache_release(&leases[0]);
    CHECK(ctr_texture_cache_acquire(cache,source,size,0,1024,1024,&extra)==CTR_TEXTURE_CACHE_OK);
    CHECK(stats(cache).bytes==CTR_TEXTURE_CACHE_MAX_BYTES && stats(cache).peak_bytes==CTR_TEXTURE_CACHE_MAX_BYTES);
    for(unsigned i=1;i<3u;++i) {
        CHECK(leases[i].rgba[0]==i+1u && leases[i].rgba[size-1u]==i+1u);
        ctr_texture_cache_release(&leases[i]);
    }
    ctr_texture_cache_release(&extra);CHECK(ctr_texture_cache_destroy(cache));free(source);
}
int main(void) {
    formats();identity_and_failures();pins_and_eviction();budget();
    printf("texture cache PASS %u checks\n",checks);return 0;
}
