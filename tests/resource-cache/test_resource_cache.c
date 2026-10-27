/* Original mutation/lifetime fixtures; no emulator or commercial resources. */
#include "ctr_resource_cache.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ctr_resource_cache_statistics stats(ctr_resource_cache *cache) {
    ctr_resource_cache_statistics out={0};ctr_resource_cache_statistics_get(cache,&out);return out;
}
static ctr_resource_lease acquire(ctr_resource_cache *cache,ctr_resource_role role,
                                   uint32_t address,uint32_t size,unsigned permissions) {
    ctr_resource_lease lease={0};
    assert(ctr_resource_cache_acquire(cache,role,address,size,permissions,&lease)==CTR_MEM_OK);
    assert(lease.bytes && lease.size==size && lease.owner==cache);return lease;
}
static void roles_and_mutations(void) {
    uint8_t allocation[4u*4096u];
    uint8_t *bytes=(uint8_t *)(((uintptr_t)allocation+4095u)&~(uintptr_t)4095u);
    memset(bytes,0,3u*4096u);
    ctr_memory memory;ctr_memory_init(&memory);
    assert(ctr_memory_map(&memory,0x1000,3u*4096u,3,bytes,3u*4096u)==CTR_MEM_OK);
    assert(ctr_memory_map(&memory,0x8000,4096,3,bytes,4096)==CTR_MEM_OK);
    ctr_resource_cache *cache=ctr_resource_cache_create(&memory);assert(cache);
    for(unsigned role=0;role<CTR_RESOURCE_ROLE_COUNT;++role) {
        ctr_resource_lease first=acquire(cache,(ctr_resource_role)role,0x1020,64,1);
        const uint8_t *identity=first.bytes;ctr_resource_cache_release(&first);
        ctr_resource_lease second=acquire(cache,(ctr_resource_role)role,0x1020,64,1);
        assert(second.bytes==identity && !second.temporary);ctr_resource_cache_release(&second);
        assert(stats(cache).hits[role]==1 && stats(cache).misses[role]==1);
    }
    /* An unrelated backing page does not invalidate an unchanged resource. */
    assert(ctr_memory_write(&memory,0x3020,1,7)==CTR_MEM_OK);
    ctr_resource_lease original=acquire(cache,CTR_RESOURCE_TEXTURE,0x1020,64,1);
    assert(stats(cache).hits[CTR_RESOURCE_TEXTURE]==2);
    /* Writable aliases invalidate the source; borrowed old bytes stay immutable. */
    assert(ctr_memory_write(&memory,0x8020,1,0xa5)==CTR_MEM_OK);
    ctr_resource_lease changed=acquire(cache,CTR_RESOURCE_TEXTURE,0x1020,64,1);
    assert(changed.bytes[0]==0xa5 && original.bytes[0]==0 && changed.bytes!=original.bytes);
    assert(stats(cache).invalidations[CTR_RESOURCE_TEXTURE]==1);
    ctr_resource_cache_release(&original);ctr_resource_cache_release(&changed);
    /* Unreported direct host writes cannot manufacture a stale hit. */
    bytes[32]=0x5a;
    changed=acquire(cache,CTR_RESOURCE_TEXTURE,0x1020,64,1);
    assert(changed.bytes[0]==0x5a && stats(cache).invalidations[CTR_RESOURCE_TEXTURE]==2);
    ctr_resource_cache_release(&changed);
    /* Even an unchanged-value checked write advances the resource key. */
    assert(ctr_memory_write(&memory,0x1020,1,0x5a)==CTR_MEM_OK);
    changed=acquire(cache,CTR_RESOURCE_TEXTURE,0x1020,64,1);
    assert(changed.bytes[0]==0x5a && stats(cache).invalidations[CTR_RESOURCE_TEXTURE]==3);
    ctr_resource_cache_release(&changed);
    /* Permission changes and remaps cannot reuse an old allocation identity. */
    assert(ctr_memory_change_range(&memory,0x1000,4096,0,false)==CTR_MEM_OK);
    ctr_resource_lease failed={0};
    size_t retained=stats(cache).bytes;
    assert(ctr_resource_cache_acquire(cache,CTR_RESOURCE_TEXTURE,0x1020,64,1,&failed)==CTR_MEM_PERMISSION);
    assert(!failed.bytes && !failed.owner && stats(cache).bytes==retained);
    assert(ctr_memory_change_range(&memory,0x1000,4096,3,false)==CTR_MEM_OK);
    changed=acquire(cache,CTR_RESOURCE_TEXTURE,0x1020,64,1);ctr_resource_cache_release(&changed);
    assert(stats(cache).invalidations[CTR_RESOURCE_TEXTURE]==4);
    assert(ctr_memory_change_range(&memory,0x1000,4096,0,true)==CTR_MEM_OK);
    uint8_t replacement[4096];memset(replacement,0xcc,sizeof(replacement));
    assert(ctr_memory_map(&memory,0x1000,4096,3,replacement,sizeof(replacement))==CTR_MEM_OK);
    changed=acquire(cache,CTR_RESOURCE_TEXTURE,0x1020,64,1);assert(changed.bytes[0]==0xcc);
    ctr_resource_cache_release(&changed);
    assert(stats(cache).invalidations[CTR_RESOURCE_TEXTURE]==5);
    assert(ctr_resource_cache_clear(cache) && stats(cache).bytes==0);
    assert(ctr_resource_cache_destroy(cache));
}
static void transactions_and_targets(void) {
    uint8_t first[4096]={0},second[4096]={0};ctr_memory memory;ctr_memory_init(&memory);
    assert(ctr_memory_map(&memory,0x1000,4096,3,first,sizeof(first))==CTR_MEM_OK);
    assert(ctr_memory_map(&memory,0x2000,4096,3,second,sizeof(second))==CTR_MEM_OK);
    ctr_resource_cache *cache=ctr_resource_cache_create(&memory);assert(cache);
    ctr_resource_lease span=acquire(cache,CTR_RESOURCE_COMMAND,0x1ff0,32,1);
    assert(!memcmp(span.bytes,first+4080,16) && !memcmp(span.bytes+16,second,16));
    ctr_resource_cache_release(&span);
    span=acquire(cache,CTR_RESOURCE_COMMAND,0x1ff0,32,1);
    assert(stats(cache).hits[CTR_RESOURCE_COMMAND]==1);
    second[2]=0x7a; /* Exact checks traverse every discontiguous host fragment. */
    ctr_resource_lease updated=acquire(cache,CTR_RESOURCE_COMMAND,0x1ff0,32,1);
    assert(span.bytes[18]==0 && updated.bytes[18]==0x7a);
    ctr_resource_cache_release(&span);ctr_resource_cache_release(&updated);
    /* A late failed atomic output preserves bytes, versions and cache reuse. */
    uint8_t output[32];memset(output,0x39,sizeof(output));
    ctr_memory_write_range ranges[]={{0x1ff0,output,32},{0x4000,output,16}};
    uint64_t before=memory.generation;
    assert(ctr_memory_write_ranges(&memory,ranges,2)==CTR_MEM_UNMAPPED && memory.generation==before);
    span=acquire(cache,CTR_RESOURCE_COMMAND,0x1ff0,32,1);
    assert(stats(cache).hits[CTR_RESOURCE_COMMAND]==2);ctr_resource_cache_release(&span);
    ctr_resource_lease color=acquire(cache,CTR_RESOURCE_COLOR,0x1100,32,3);
    ctr_resource_lease depth=acquire(cache,CTR_RESOURCE_DEPTH,0x2100,32,3);
    ctr_memory_write_range targets[]={{0x1100,output,32},{0x2100,output,32}};
    assert(ctr_memory_write_ranges(&memory,targets,2)==CTR_MEM_OK);
    ctr_resource_lease next_color=acquire(cache,CTR_RESOURCE_COLOR,0x1100,32,3);
    ctr_resource_lease next_depth=acquire(cache,CTR_RESOURCE_DEPTH,0x2100,32,3);
    assert(color.bytes[0]==0 && depth.bytes[0]==0 && next_color.bytes[0]==0x39 && next_depth.bytes[0]==0x39);
    assert(!ctr_resource_cache_clear(cache) && !ctr_resource_cache_destroy(cache));
    ctr_resource_cache_release(&color);ctr_resource_cache_release(&depth);
    ctr_resource_cache_release(&next_color);ctr_resource_cache_release(&next_depth);
    assert(ctr_resource_cache_destroy(cache));
}
static void capacity_and_leases(void) {
    uint8_t bytes[4096]={0};ctr_memory memory;ctr_memory_init(&memory);
    assert(ctr_memory_map(&memory,0x1000,sizeof(bytes),3,bytes,sizeof(bytes))==CTR_MEM_OK);
    ctr_resource_cache *cache=ctr_resource_cache_create(&memory);assert(cache);
    ctr_resource_lease leases[CTR_RESOURCE_CACHE_SLOTS];
    for(unsigned i=0;i<CTR_RESOURCE_CACHE_SLOTS;++i)leases[i]=acquire(cache,CTR_RESOURCE_VERTEX,0x1000+i*16u,16,1);
    ctr_resource_lease extra=acquire(cache,CTR_RESOURCE_COMMAND,0x1800,16,1);
    assert(extra.temporary && stats(cache).pins==CTR_RESOURCE_CACHE_SLOTS+1u);
    assert(stats(cache).bypasses[CTR_RESOURCE_COMMAND]==1 && !stats(cache).evictions);
    assert(!ctr_resource_cache_clear(cache) && !ctr_resource_cache_destroy(cache));
    for(unsigned i=0;i<CTR_RESOURCE_CACHE_SLOTS;++i)assert(leases[i].bytes[0]==0);
    ctr_resource_cache_release(&extra);
    for(unsigned i=0;i<CTR_RESOURCE_CACHE_SLOTS;++i)ctr_resource_cache_release(&leases[i]);
    extra=acquire(cache,CTR_RESOURCE_COMMAND,0x1800,16,1);
    assert(!extra.temporary && stats(cache).evictions==1);ctr_resource_cache_release(&extra);
    assert(stats(cache).pins==0 && ctr_resource_cache_clear(cache) && stats(cache).bytes==0);
    assert(ctr_resource_cache_destroy(cache));
    assert(ctr_resource_cache_destroy(NULL) && !ctr_resource_cache_create(NULL));
}
static void byte_budget_and_saturation(void) {
    const uint32_t span=4u*1024u*1024u;
    uint8_t *bytes=calloc(1,span*4u);assert(bytes);
    ctr_memory memory;ctr_memory_init(&memory);
    assert(ctr_memory_map(&memory,0x10000000,span*4u,3,bytes,span*4u)==CTR_MEM_OK);
    ctr_resource_cache *cache=ctr_resource_cache_create(&memory);assert(cache);
    ctr_resource_lease leases[3];
    for(unsigned i=0;i<3;++i)leases[i]=acquire(cache,CTR_RESOURCE_TEXTURE,0x10000000+i*span,span,1);
    assert(stats(cache).bytes==CTR_RESOURCE_CACHE_MAX_BYTES);
    ctr_resource_lease fourth=acquire(cache,CTR_RESOURCE_TEXTURE,0x10000000+3u*span,span,1);
    assert(fourth.temporary && stats(cache).bytes==CTR_RESOURCE_CACHE_MAX_BYTES);
    ctr_resource_cache_release(&fourth);
    for(unsigned i=0;i<3;++i)ctr_resource_cache_release(&leases[i]);
    fourth=acquire(cache,CTR_RESOURCE_TEXTURE,0x10000000+3u*span,span,1);
    assert(!fourth.temporary && stats(cache).bytes<=CTR_RESOURCE_CACHE_MAX_BYTES);
    ctr_resource_cache_release(&fourth);
    ctr_resource_lease oversized=acquire(cache,CTR_RESOURCE_VERTEX,0x10000000,span*4u,1);
    assert(oversized.temporary && stats(cache).bytes<=CTR_RESOURCE_CACHE_MAX_BYTES);
    ctr_resource_cache_release(&oversized);
    /* Exhaustion does not reject a supported read or revive a stale key. */
    memory.resource_clock=UINT64_MAX;
    fourth=acquire(cache,CTR_RESOURCE_TEXTURE,0x10000000+3u*span,span,1);
    assert(fourth.temporary);ctr_resource_cache_release(&fourth);
    assert(ctr_resource_cache_destroy(cache));free(bytes);
}
static void insertion_order(void) {
    uint8_t bytes[4096]={0};ctr_memory memory;ctr_memory_init(&memory);
    assert(ctr_memory_map(&memory,0x1000,sizeof(bytes),3,bytes,sizeof(bytes))==CTR_MEM_OK);
    ctr_resource_cache *cache=ctr_resource_cache_create(&memory);assert(cache);
    for(unsigned i=0;i<CTR_RESOURCE_CACHE_SLOTS;++i) {
        ctr_resource_lease lease=acquire(cache,CTR_RESOURCE_VERTEX,0x1000+i*16u,16,1);
        ctr_resource_cache_release(&lease);
    }
    assert(!stats(cache).evictions);
    ctr_resource_lease extra=acquire(cache,CTR_RESOURCE_VERTEX,0x1000+CTR_RESOURCE_CACHE_SLOTS*16u,16,1);
    ctr_resource_cache_release(&extra);assert(stats(cache).evictions==1);
    /* Filling empty slots in order must evict the first insertion, while the
     * second insertion remains reusable until the next distinct insertion. */
    ctr_resource_lease retained=acquire(cache,CTR_RESOURCE_VERTEX,0x1010,16,1);
    ctr_resource_cache_release(&retained);assert(stats(cache).hits[CTR_RESOURCE_VERTEX]==1);
    uint64_t misses=stats(cache).misses[CTR_RESOURCE_VERTEX];
    ctr_resource_lease evicted=acquire(cache,CTR_RESOURCE_VERTEX,0x1000,16,1);
    ctr_resource_cache_release(&evicted);
    assert(stats(cache).misses[CTR_RESOURCE_VERTEX]==misses+1u && stats(cache).evictions==2);
    assert(ctr_resource_cache_destroy(cache));
}
static void invalid_arguments(void) {
    uint8_t bytes[4096]={0};ctr_memory memory;ctr_memory_init(&memory);
    assert(ctr_memory_map(&memory,0x1000,sizeof(bytes),3,bytes,sizeof(bytes))==CTR_MEM_OK);
    ctr_resource_cache *cache=ctr_resource_cache_create(&memory);assert(cache);
    ctr_resource_lease lease={0};
    assert(ctr_resource_cache_acquire(cache,CTR_RESOURCE_COMMAND,0x1000,0,1,&lease)==CTR_MEM_OVERFLOW);
    assert(!lease.bytes && !lease.owner);
    assert(ctr_resource_cache_acquire(cache,CTR_RESOURCE_ROLE_COUNT,0x1000,1,1,&lease)==CTR_MEM_OVERFLOW);
    assert(ctr_resource_cache_acquire(cache,CTR_RESOURCE_COMMAND,0x1000,1,2,&lease)==CTR_MEM_PERMISSION);
    assert(ctr_resource_cache_acquire(cache,CTR_RESOURCE_COMMAND,UINT32_MAX,2,1,&lease)==CTR_MEM_OVERFLOW);
    assert(ctr_resource_cache_acquire(cache,CTR_RESOURCE_COMMAND,0x1000,1,1,(ctr_resource_lease *)bytes)==CTR_MEM_OVERLAP);
    for(unsigned i=0;i<sizeof(bytes);++i)assert(!bytes[i]);
    /* Outputs into the owner must not clobber its mapping descriptors. */
    uint8_t saved[sizeof(memory)];memcpy(saved,&memory,sizeof(memory));
    assert(ctr_resource_cache_acquire(cache,CTR_RESOURCE_COMMAND,0x1000,1,1,(ctr_resource_lease *)&memory)==CTR_MEM_OVERLAP);
    assert(!memcmp(saved,&memory,sizeof(memory)));
    ctr_resource_cache_statistics_get(cache,(ctr_resource_cache_statistics *)&memory);
    assert(!memcmp(saved,&memory,sizeof(memory)));
    /* Reacquisition into a live retained or one-use lease preserves its only
     * release handle and leaves the owner's pin accounting unchanged. */
    lease=acquire(cache,CTR_RESOURCE_COMMAND,0x1000,1,1);
    const uint8_t *snapshot=lease.bytes;
    assert(ctr_resource_cache_acquire(cache,CTR_RESOURCE_VERTEX,0x1001,1,1,&lease)==CTR_MEM_OVERLAP);
    assert(lease.bytes==snapshot && lease.owner==cache && stats(cache).pins==1);
    assert(!ctr_resource_cache_clear(cache));ctr_resource_cache_release(&lease);
    memory.resource_clock=UINT64_MAX;
    lease=acquire(cache,CTR_RESOURCE_COMMAND,0x1000,1,1);assert(lease.temporary);
    snapshot=lease.bytes;
    assert(ctr_resource_cache_acquire(cache,CTR_RESOURCE_VERTEX,0x1001,1,1,&lease)==CTR_MEM_OVERLAP);
    assert(lease.bytes==snapshot && lease.owner==cache && lease.temporary && stats(cache).pins==1);
    ctr_resource_cache_release(&lease);assert(!stats(cache).pins);
    assert(ctr_resource_cache_destroy(cache));
}
int main(void) {
    roles_and_mutations();transactions_and_targets();capacity_and_leases();
    byte_budget_and_saturation();insertion_order();invalid_arguments();
    puts("immutable resource cache: roles, writes, aliases, maps, atomic targets, pins, budgets and saturation PASS");
    return 0;
}
