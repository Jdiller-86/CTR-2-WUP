#include "ctr_memory.h"

#include <stdatomic.h>
#include <string.h>

static uint64_t resource_next(ctr_memory *memory) {
    if(memory->resource_clock!=UINT64_MAX)++memory->resource_clock;
    return memory->resource_clock;
}
static size_t resource_slot(uintptr_t page) {
    return (size_t)((page^(page>>10u))&(CTR_MEMORY_RESOURCE_SLOTS-1u));
}
static void resource_mark_host(ctr_memory *memory,uintptr_t first,size_t size,uint64_t version) {
    uintptr_t begin=first>>12u,last=(first+size-1u)>>12u;
    /* Large writes may conservatively dirty the entire bounded table. */
    if(last-begin+1u>=CTR_MEMORY_RESOURCE_SLOTS) {
        for(size_t i=0;i<CTR_MEMORY_RESOURCE_SLOTS;++i)memory->resource_versions[i]=version;
    } else for(uintptr_t page=begin;page<=last;++page)
        memory->resource_versions[resource_slot(page)]=version;
}
static uint64_t resource_host_version(const ctr_memory *memory,uintptr_t first,size_t size) {
    uintptr_t begin=first>>12u,last=(first+size-1u)>>12u;uint64_t version=0;
    if(last-begin+1u>=CTR_MEMORY_RESOURCE_SLOTS) {
        for(size_t i=0;i<CTR_MEMORY_RESOURCE_SLOTS;++i)
            if(memory->resource_versions[i]>version)version=memory->resource_versions[i];
    } else for(uintptr_t page=begin;page<=last;++page) {
        uint64_t current=memory->resource_versions[resource_slot(page)];
        if(current>version)version=current;
    }
    return version;
}
static void resource_mark_range(ctr_memory *memory,uint32_t address,uint32_t size,uint64_t version) {
    uint64_t cursor=address,end=cursor+size;
    while(cursor<end)for(size_t i=0;i<memory->count;++i) {
        const ctr_mapping *map=&memory->maps[i];uint64_t stop=(uint64_t)map->base+map->size;
        if(cursor<map->base || cursor>=stop)continue;
        size_t count=(size_t)((stop<end?stop:end)-cursor);
        resource_mark_host(memory,(uintptr_t)map->storage+(size_t)(cursor-map->base),count,version);
        cursor+=count;break;
    }
}

void ctr_memory_init(ctr_memory *memory) {
    memset(memory, 0, sizeof(*memory));
}

static bool watch_overlaps(const uint8_t *watch,size_t watch_size,const uint8_t *storage,size_t size) {
    uintptr_t first=(uintptr_t)storage,base=(uintptr_t)watch;
    return watch_size && first<base+watch_size && (first>=base || base-first<size);
}

static void watch_group_access(void *context,const uint8_t *storage,size_t size,bool write) {
    ctr_memory_watch_group *group=context;
    for(unsigned i=0;i<CTR_MEMORY_WATCH_SLOTS;++i) {
        const ctr_memory_watch *watch=&group->slots[i];
        if(watch->access && watch_overlaps(watch->storage,watch->size,storage,size))
            watch->access(watch->context,storage,size,write);
    }
}

bool ctr_memory_attach_watch_group(ctr_memory *memory,ctr_memory_watch_group *group) {
    if(!group)return false;
    if(memory->watch_access==watch_group_access)return memory->watch_context==group;
    if(memory->resource_mapping_version==UINT64_MAX)return false;
    memset(group,0,sizeof(*group));
    group->slots[0]=(ctr_memory_watch){memory->watch_storage,memory->watch_size,memory->watch_access,memory->watch_context};
    group->primary_start=(uintptr_t)memory->watch_storage;
    group->primary_end=group->primary_start+memory->watch_size;
    memory->watch_storage=NULL;
    group->owner=memory;
    memory->watch_size=1u;
    memory->watch_access=watch_group_access;
    memory->watch_context=group;
    memory->resource_mapping_version=resource_next(memory);
    return true;
}
bool ctr_memory_detach_watch_group(ctr_memory *memory,ctr_memory_watch_group *group) {
    if(!group || group->owner!=memory || memory->watch_access!=watch_group_access || memory->watch_context!=group)return false;
    for(unsigned i=1;i<CTR_MEMORY_WATCH_SLOTS;++i)if(group->slots[i].access)return false;
    const ctr_memory_watch *primary=&group->slots[0];
    memory->watch_storage=primary->storage;
    memory->watch_size=primary->size;
    memory->watch_access=primary->access;
    memory->watch_context=primary->context;
    memory->resource_mapping_version=resource_next(memory);
    memset(group,0,sizeof(*group));
    return true;
}

bool ctr_memory_set_watch_slot(ctr_memory *memory,unsigned index,const uint8_t *storage,size_t size,
                               ctr_memory_access_fn on_access,void *context) {
    if(index>=CTR_MEMORY_WATCH_SLOTS || memory->watch_access!=watch_group_access)return false;
    if(!on_access) { storage=NULL;size=0;context=NULL; }
    else if(!storage || !size || size>UINTPTR_MAX-(uintptr_t)storage ||
            memory->resource_mapping_version==UINT64_MAX)return false;
    ctr_memory_watch_group *group=memory->watch_context;
    ctr_memory_watch *watch=&group->slots[index];
    if(watch->storage==storage && watch->size==size && watch->access==on_access && watch->context==context)
        return true;
    *watch=(ctr_memory_watch){storage,size,on_access,context};
    if(on_access) {
        uintptr_t *start=index?&group->scanout_start:&group->primary_start;
        uintptr_t *end=index?&group->scanout_end:&group->primary_end;
        uintptr_t first=(uintptr_t)storage,last=first+size;
        /* Retain the direct-cache exclusion even when a frame is resolved.
         * Only widening it can make an existing direct CPU span unsafe. */
        if(!*end || first<*start || last>*end) {
            if(!*end || first<*start)*start=first;
            if(last>*end)*end=last;
            memory->resource_mapping_version=resource_next(memory);
        }
    }
    return true;
}

void ctr_memory_set_watch(ctr_memory *memory,const uint8_t *storage,size_t size,
                          ctr_memory_access_fn on_access,void *context) {
    if(memory->watch_access==watch_group_access) {
        (void)ctr_memory_set_watch_slot(memory,0,storage,size,on_access,context);
        return;
    }
    if(!on_access) { storage=NULL;size=0;context=NULL; }
    else if(!storage || !size || size>UINTPTR_MAX-(uintptr_t)storage ||
            memory->resource_mapping_version==UINT64_MAX)return;
    if(memory->watch_storage==storage && memory->watch_size==size &&
       memory->watch_access==on_access && memory->watch_context==context)return;
    memory->watch_storage=storage;memory->watch_size=size;
    memory->watch_access=on_access;memory->watch_context=context;
    memory->resource_mapping_version=resource_next(memory);
}

bool ctr_memory_backing_watched(const ctr_memory *memory,const uint8_t *storage,size_t size) {
    if(!memory->watch_size || !storage || !size)return false;
    if(memory->watch_access!=watch_group_access)
        return watch_overlaps(memory->watch_storage,memory->watch_size,storage,size);
    const ctr_memory_watch_group *group=memory->watch_context;
    return watch_overlaps((const uint8_t *)group->primary_start,
                          group->primary_end-group->primary_start,storage,size) ||
           watch_overlaps((const uint8_t *)group->scanout_start,
                          group->scanout_end-group->scanout_start,storage,size);
}

static bool pending_backing(const ctr_memory *memory,const uint8_t *storage,size_t size) {
    if(!ctr_memory_backing_watched(memory,storage,size))return false;
    if(memory->watch_access!=watch_group_access)return true;
    const ctr_memory_watch_group *group=memory->watch_context;
    for(unsigned i=0;i<CTR_MEMORY_WATCH_SLOTS;++i)
        if(watch_overlaps(group->slots[i].storage,group->slots[i].size,storage,size))return true;
    return false;
}
static bool ready_backing(const ctr_memory *memory,const uint8_t *storage,size_t size,bool write) {
    if(!pending_backing(memory,storage,size))return true;
    memory->watch_access(memory->watch_context,storage,size,write);
    return !pending_backing(memory,storage,size);
}

static bool clip_watch(uintptr_t watch,size_t size,uintptr_t access,unsigned width,
                       uintptr_t *start,uintptr_t *end) {
    if(!size || watch>=*end || watch+size<=*start)return true;
    if(watch+size<=access)*start=watch+size;
    else if(watch>=access+width)*end=watch;
    else return false;
    return true;
}

bool ctr_memory_unwatched_span(const ctr_memory *memory,const ctr_mapping *map,
                               uint32_t address,unsigned width,ctr_mapping *out) {
    uint32_t offset=address-map->base;
    if(!width || width>map->size || offset>map->size-width)return false;
    uintptr_t base=(uintptr_t)map->storage,start=base,end=base+map->size,access=base+offset;
    if(memory->watch_access==watch_group_access) {
        const ctr_memory_watch_group *group=memory->watch_context;
        if(!clip_watch(group->primary_start,group->primary_end-group->primary_start,
                       access,width,&start,&end) ||
           !clip_watch(group->scanout_start,group->scanout_end-group->scanout_start,
                       access,width,&start,&end))return false;
    } else if(!clip_watch((uintptr_t)memory->watch_storage,memory->watch_size,
                          access,width,&start,&end))return false;
    /* The caller's direct 32-bit access checks subtract width from size. */
    if(end-start<4u)return false;
    *out=(ctr_mapping){map->base+(uint32_t)(start-base),(uint32_t)(end-start),
                       map->permissions,map->storage+(start-base)};
    return true;
}

static bool ready_range(const ctr_memory *memory,uint32_t address,uint32_t size,bool write) {
    if(!memory->watch_size || !size)return true;
    uint64_t end=(uint64_t)address+size;
    for(size_t i=0;i<memory->count;++i) {
        const ctr_mapping *map=&memory->maps[i];uint64_t stop=(uint64_t)map->base+map->size;
        if(end<=map->base || stop<=address)continue;
        uint64_t first=address>map->base?address:map->base,last=end<stop?end:stop;
        if(!ready_backing(memory,map->storage+(size_t)(first-map->base),(size_t)(last-first),write))return false;
    }
    return true;
}

bool ctr_memory_resolve_watches(const ctr_memory *memory,uint32_t address,uint32_t size,bool write) {
    return ready_range(memory,address,size,write);
}

ctr_mem_result ctr_memory_map(ctr_memory *memory, uint32_t base, uint32_t size,
                             unsigned permissions, uint8_t *storage,
                             size_t storage_size) {
    uint64_t end = (uint64_t)base + size;
    if (size == 0u || storage == NULL || storage_size < size || end > UINT64_C(0x100000000) ||
        (uintptr_t)storage > UINTPTR_MAX - size)
        return CTR_MEM_OVERFLOW;
    if (permissions == 0u || (permissions & ~7u) != 0u)
        return CTR_MEM_PERMISSION;
    for (size_t i = 0; i < memory->count; ++i) {
        const ctr_mapping *map = &memory->maps[i];
        if ((uint64_t)base < (uint64_t)map->base + map->size && (uint64_t)map->base < end)
            return CTR_MEM_OVERLAP;
    }
    if (memory->count == CTR_MEMORY_MAX_MAPS) return CTR_MEM_CAPACITY;
    memory->maps[memory->count++] = (ctr_mapping){base, size, permissions, storage};
    ++memory->generation;
    if (permissions & CTR_MEM_EXEC) ++memory->code_generation;
    memory->resource_mapping_version=resource_next(memory);
    return CTR_MEM_OK;
}

static bool scalar_in_mapping(const ctr_mapping *map,uint32_t address,unsigned width) {
    return width<=map->size && address>=map->base && address-map->base<=map->size-width;
}

static ctr_mem_result locate(const ctr_memory *memory, uint32_t address, unsigned width,
                             unsigned permission, const ctr_mapping **found) {
    if (width != 1u && width != 2u && width != 4u) return CTR_MEM_ALIGNMENT;
    if (address > UINT32_MAX - (width - 1u)) return CTR_MEM_OVERFLOW;
    if ((address & (width - 1u)) != 0u) return CTR_MEM_ALIGNMENT;
    /* Indices are hints only. Recheck the current mapping even after remaps. */
    static atomic_uint hints[16];
    atomic_uint *hint=&hints[(address>>16)&15u];
    unsigned index=atomic_load_explicit(hint,memory_order_relaxed);
    const ctr_mapping *map=NULL;
    if(index<memory->count && scalar_in_mapping(&memory->maps[index],address,width))
        map=&memory->maps[index];
    else for(unsigned i=0;i<memory->count;++i) {
        if(scalar_in_mapping(&memory->maps[i],address,width)) {
            map=&memory->maps[i];
            atomic_store_explicit(hint,i,memory_order_relaxed);
            break;
        }
    }
    if(!map)return CTR_MEM_UNMAPPED;
    if((map->permissions&permission)!=permission)return CTR_MEM_PERMISSION;
    *found=map;
    return CTR_MEM_OK;
}

ctr_mem_result ctr_memory_read(const ctr_memory *memory, uint32_t address,
                              unsigned width, unsigned permission, uint32_t *value) {
    const ctr_mapping *map = NULL;
    ctr_mem_result result = locate(memory, address, width, permission, &map);
    if (result != CTR_MEM_OK) return result;
    size_t offset = (size_t)(address - map->base);
    if(!ready_backing(memory,map->storage+offset,width,false))return CTR_MEM_PENDING;
    uint32_t out = 0u;
    for (unsigned i = 0; i < width; ++i) out |= (uint32_t)map->storage[offset + i] << (i * 8u);
    *value = out;
    return CTR_MEM_OK;
}
ctr_mem_result ctr_memory_read_data(const ctr_memory *memory,uint32_t address,
                                   unsigned width,uint32_t *value) {
    if(width!=1 && width!=2 && width!=4)return CTR_MEM_ALIGNMENT;
    if(!(address&(width-1)))return ctr_memory_read(memory,address,width,CTR_MEM_READ,value);
    ctr_mem_result result=ctr_memory_check_range(memory,address,width,CTR_MEM_READ);
    if(result!=CTR_MEM_OK)return result;
    uint32_t out=0;
    for(unsigned i=0;i<width;++i) {
        uint32_t byte;result=ctr_memory_read(memory,address+i,1,CTR_MEM_READ,&byte);
        if(result!=CTR_MEM_OK)return result;
        out|=byte<<(8*i);
    }
    *value=out;return CTR_MEM_OK;
}
ctr_mem_result ctr_memory_write_data(ctr_memory *memory,uint32_t address,
                                    unsigned width,uint32_t value) {
    if(width!=1 && width!=2 && width!=4)return CTR_MEM_ALIGNMENT;
    if(!(address&(width-1)))return ctr_memory_write(memory,address,width,value);
    ctr_mem_result result=ctr_memory_check_range(memory,address,width,CTR_MEM_WRITE);
    if(result!=CTR_MEM_OK)return result;
    if(!ready_range(memory,address,width,true))return CTR_MEM_PENDING;
    for(unsigned i=0;i<width;++i) {
        result=ctr_memory_write(memory,address+i,1,(value>>(8*i))&255u);
        if(result!=CTR_MEM_OK)return result;
    }
    return CTR_MEM_OK;
}

ctr_mem_result ctr_memory_write(ctr_memory *memory, uint32_t address,
                               unsigned width, uint32_t value) {
    const ctr_mapping *map = NULL;
    ctr_mem_result result = locate(memory, address, width, CTR_MEM_WRITE, &map);
    if (result != CTR_MEM_OK) return result;
    size_t offset = (size_t)(address - map->base);
    if(!ready_backing(memory,map->storage+offset,width,true))return CTR_MEM_PENDING;
    for (unsigned i = 0; i < width; ++i) map->storage[offset + i] = (uint8_t)(value >> (i * 8u));
    ++memory->generation;
    uintptr_t changed = (uintptr_t)map->storage + offset;
    resource_mark_host(memory,changed,width,resource_next(memory));
    for (size_t i = 0; i < memory->count; ++i) {
        const ctr_mapping *other = &memory->maps[i];
        if ((other->permissions & CTR_MEM_EXEC) != 0u && changed < (uintptr_t)other->storage + other->size &&
            (uintptr_t)other->storage < changed + width) {
            ++memory->code_generation;
            break;
        }
    }
    return CTR_MEM_OK;
}

const char *ctr_mem_result_name(ctr_mem_result result) {
    static const char *const names[] = {"ok", "unmapped", "permission", "alignment", "overflow", "overlap", "capacity", "pending"};
    return (unsigned)result < sizeof(names) / sizeof(names[0]) ? names[result] : "unknown";
}

static bool host_alias(const ctr_memory *memory,const void *buffer,uint32_t size) {
    uintptr_t start=(uintptr_t)buffer;
    if(size>UINTPTR_MAX-start)return true;
    for(size_t i=0;i<memory->count;++i) {
        uintptr_t base=(uintptr_t)memory->maps[i].storage;
        if(start<base+memory->maps[i].size && base<start+size)return true;
    }
    return false;
}
static void copy_bytes(const ctr_memory *memory,uint32_t address,void *buffer,uint32_t size,bool write) {
    uint64_t cursor=address,end=cursor+size;uint8_t *bytes=buffer;
    while(cursor<end)for(size_t i=0;i<memory->count;++i) {
        const ctr_mapping *map=&memory->maps[i];uint64_t stop=(uint64_t)map->base+map->size;
        if(cursor<map->base || cursor>=stop)continue;
        size_t count=(size_t)((stop<end?stop:end)-cursor);
        uint8_t *backing=map->storage+(size_t)(cursor-map->base);
        if(write)memcpy(backing,bytes,count);else memcpy(bytes,backing,count);
        bytes+=count;cursor+=count;break;
    }
}
ctr_mem_result ctr_memory_read_bytes(const ctr_memory *memory,uint32_t address,void *output,uint32_t size) {
    if(size && (uint64_t)address+size<=UINT64_C(0x100000000)) {
        for(size_t i=0;i<memory->count;++i) {
            const ctr_mapping *map=&memory->maps[i];
            if(address<map->base || size>map->size || address-map->base>map->size-size)continue;
            if(!(map->permissions&CTR_MEM_READ))return CTR_MEM_PERMISSION;
            const uint8_t *source=map->storage+(address-map->base);
            /* Keep the original remapping-aware copy path for watch callbacks. */
            if(memory->watch_size && ctr_memory_backing_watched(memory,source,size))break;
            if(!output)return CTR_MEM_OVERFLOW;
            if(host_alias(memory,output,size))return CTR_MEM_OVERLAP;
            memcpy(output,source,size);
            return CTR_MEM_OK;
        }
    }
    ctr_mem_result result=ctr_memory_check_range(memory,address,size,CTR_MEM_READ);
    if(result!=CTR_MEM_OK)return result;
    if(!size)return CTR_MEM_OK;
    if(!output)return CTR_MEM_OVERFLOW;
    if(host_alias(memory,output,size))return CTR_MEM_OVERLAP;
    if(!ready_range(memory,address,size,false))return CTR_MEM_PENDING;
    copy_bytes(memory,address,output,size,false);return CTR_MEM_OK;
}
static bool write_touches_code(const ctr_memory *memory,uint32_t address,uint32_t size) {
    uint64_t end=(uint64_t)address+size;
    for(size_t i=0;i<memory->count;++i) {
        const ctr_mapping *map=&memory->maps[i];uint64_t stop=(uint64_t)map->base+map->size;
        if(address>=stop || map->base>=end)continue;
        uint64_t begin=address>map->base?address:map->base,finish=end<stop?end:stop;
        uintptr_t first=(uintptr_t)map->storage+(size_t)(begin-map->base),last=first+(size_t)(finish-begin);
        for(size_t j=0;j<memory->count;++j) {
            const ctr_mapping *other=&memory->maps[j];uintptr_t base=(uintptr_t)other->storage;
            if((other->permissions&CTR_MEM_EXEC) && first<base+other->size && base<last)return true;
        }
    }
    return false;
}
static bool write_ranges_overlap(const ctr_memory *memory,const ctr_memory_write_range *a,
                                 const ctr_memory_write_range *b) {
    if(!a->size || !b->size)return false;
    uint64_t a_end=(uint64_t)a->address+a->size,b_end=(uint64_t)b->address+b->size;
    if(a!=b && a->address<b_end && b->address<a_end)return true;
    for(size_t i=0;i<memory->count;++i) {
        const ctr_mapping *a_map=&memory->maps[i];uint64_t a_stop=(uint64_t)a_map->base+a_map->size;
        if(a->address>=a_stop || a_map->base>=a_end)continue;
        uint64_t a_begin=a->address>a_map->base?a->address:a_map->base;
        uint64_t a_finish=a_end<a_stop?a_end:a_stop;
        uintptr_t a_first=(uintptr_t)a_map->storage+(size_t)(a_begin-a_map->base);
        uintptr_t a_last=a_first+(size_t)(a_finish-a_begin);
        for(size_t j=a==b?i+1u:0u;j<memory->count;++j) {
            const ctr_mapping *b_map=&memory->maps[j];uint64_t b_stop=(uint64_t)b_map->base+b_map->size;
            if(b->address>=b_stop || b_map->base>=b_end)continue;
            uint64_t b_begin=b->address>b_map->base?b->address:b_map->base;
            uint64_t b_finish=b_end<b_stop?b_end:b_stop;
            uintptr_t b_first=(uintptr_t)b_map->storage+(size_t)(b_begin-b_map->base);
            uintptr_t b_last=b_first+(size_t)(b_finish-b_begin);
            if(a_first<b_last && b_first<a_last)return true;
        }
    }
    return false;
}
static ctr_mem_result write_ranges(ctr_memory *memory,const ctr_memory_write_range *ranges,size_t count,
                                   bool distinct_backing) {
    bool changed=false,code=false;
    for(size_t i=0;i<count;++i) {
        const ctr_memory_write_range *r=&ranges[i];
        ctr_mem_result result=ctr_memory_check_range(memory,r->address,r->size,CTR_MEM_WRITE);
        if(result!=CTR_MEM_OK)return result;
        if(!r->size)continue;
        if(!r->input)return CTR_MEM_OVERFLOW;
        if(host_alias(memory,r->input,r->size))return CTR_MEM_OVERLAP;
        if(distinct_backing && write_ranges_overlap(memory,r,r))return CTR_MEM_OVERLAP;
        for(size_t j=0;j<i;++j)if(write_ranges_overlap(memory,r,&ranges[j]))return CTR_MEM_OVERLAP;
        changed=true;
        if(write_touches_code(memory,r->address,r->size))code=true;
    }
    if(!changed)return CTR_MEM_OK;
    if(memory->watch_access)for(size_t i=0;i<count;++i)
        if(!ready_range(memory,ranges[i].address,ranges[i].size,true))return CTR_MEM_PENDING;
    for(size_t i=0;i<count;++i)if(ranges[i].size)
        copy_bytes(memory,ranges[i].address,(void*)ranges[i].input,ranges[i].size,true);
    ++memory->generation;
    if(code)++memory->code_generation;
    uint64_t version=resource_next(memory);
    for(size_t i=0;i<count;++i)if(ranges[i].size)
        resource_mark_range(memory,ranges[i].address,ranges[i].size,version);
    return CTR_MEM_OK;
}
ctr_mem_result ctr_memory_write_ranges(ctr_memory *memory,const ctr_memory_write_range *ranges,size_t count) {
    if(count>CTR_MEMORY_MAX_WRITE_RANGES)return CTR_MEM_CAPACITY;
    if(!count)return CTR_MEM_OK;
    size_t bytes=count*sizeof(*ranges);
    if(!ranges || (uintptr_t)ranges>UINTPTR_MAX-bytes)return CTR_MEM_OVERFLOW;
    if(host_alias(memory,ranges,(uint32_t)bytes))return CTR_MEM_OVERLAP;
    return write_ranges(memory,ranges,count,true);
}
ctr_mem_result ctr_memory_write_bytes(ctr_memory *memory,uint32_t address,const void *input,uint32_t size) {
    const ctr_memory_write_range range={address,input,size};
    return write_ranges(memory,&range,1,false);
}

ctr_mem_result ctr_memory_check_range(const ctr_memory *memory, uint32_t address,
                                     uint32_t size, unsigned permissions) {
    uint64_t at=address, end=at+size;
    if (end>UINT64_C(0x100000000)) return CTR_MEM_OVERFLOW;
    if (!permissions || (permissions&~7u)) return CTR_MEM_PERMISSION;
    while (at<end) {
        const ctr_mapping *found=NULL;
        for (size_t i=0;i<memory->count;++i) {
            const ctr_mapping *map=&memory->maps[i];
            if (at>=map->base && at<(uint64_t)map->base+map->size) { found=map;break; }
        }
        if (!found) return CTR_MEM_UNMAPPED;
        if ((found->permissions&permissions)!=permissions) return CTR_MEM_PERMISSION;
        uint64_t next=(uint64_t)found->base+found->size;
        at=next<end?next:end;
    }
    return CTR_MEM_OK;
}

ctr_mem_result ctr_memory_change_range(ctr_memory *memory, uint32_t address,
                                      uint32_t size, unsigned permissions, bool remove) {
    uint64_t end=(uint64_t)address+size, at=address;
    if (!size || end>UINT64_C(0x100000000)) return CTR_MEM_OVERFLOW;
    if (permissions&~7u) return CTR_MEM_PERMISSION;
    while (at<end) {
        const ctr_mapping *found=NULL;
        for (size_t i=0;i<memory->count;++i) {
            const ctr_mapping *m=&memory->maps[i];
            if (at>=m->base && at<(uint64_t)m->base+m->size) { found=m;break; }
        }
        if (!found) return CTR_MEM_UNMAPPED;
        uint64_t next=(uint64_t)found->base+found->size;
        at=next<end?next:end;
    }
    ctr_mapping next[CTR_MEMORY_MAX_MAPS];size_t count=0;
    bool changed=false, code_changed=false;
    for (size_t i=0;i<memory->count;++i) {
        ctr_mapping m=memory->maps[i], pieces[3];unsigned n=0;
        uint64_t old_end=(uint64_t)m.base+m.size;
        if (old_end<=address || m.base>=end || (!remove && m.permissions==permissions)) {
            pieces[n++]=m;
        } else {
            uint32_t begin=m.base>address?m.base:address;
            uint64_t stop=old_end<end?old_end:end;
            if (begin>m.base) pieces[n++]=(ctr_mapping){m.base,begin-m.base,m.permissions,m.storage};
            if (!remove) pieces[n++]=(ctr_mapping){begin,(uint32_t)(stop-begin),permissions,m.storage+(begin-m.base)};
            if (stop<old_end) pieces[n++]=(ctr_mapping){(uint32_t)stop,(uint32_t)(old_end-stop),m.permissions,m.storage+(stop-m.base)};
            changed=true;
            if ((m.permissions|(remove?0:permissions))&CTR_MEM_EXEC) code_changed=true;
        }
        if (count+n>CTR_MEMORY_MAX_MAPS) return CTR_MEM_CAPACITY;
        for (unsigned p=0;p<n;++p) next[count++]=pieces[p];
    }
    if (changed) {
        /* Materialize mapped backing before its owner can release it. */
        if(remove && memory->watch_access==watch_group_access) {
            if(!ready_range(memory,address,size,true))return CTR_MEM_PENDING;
            const ctr_memory_watch_group *group=memory->watch_context;
            const ctr_memory *owner=group->owner;
            if(owner->resource_clock>memory->resource_clock)memory->resource_clock=owner->resource_clock;
        }
        memset(memory->maps,0,sizeof(memory->maps));
        memcpy(memory->maps,next,count*sizeof(next[0]));memory->count=count;
        ++memory->generation;
        if (code_changed) ++memory->code_generation;
        memory->resource_mapping_version=resource_next(memory);
    }
    return CTR_MEM_OK;
}

bool ctr_memory_resource_key_capture(const ctr_memory *memory,uint32_t address,
                                      uint32_t size,unsigned permissions,
                                      ctr_memory_resource_key *key) {
    if(!key)return false;
    /* This output is owned host storage, not a destination inside its owner. */
    uintptr_t output=(uintptr_t)key;
    if(output>UINTPTR_MAX-sizeof(*key) || output%_Alignof(ctr_memory_resource_key))return false;
    if(memory) {
        uintptr_t owner=(uintptr_t)memory;
        if(output<owner+sizeof(*memory) && owner<output+sizeof(*key))return false;
        for(size_t i=0;i<memory->count;++i) {
            uintptr_t start=(uintptr_t)memory->maps[i].storage;
            if(output<start+memory->maps[i].size && start<output+sizeof(*key))return false;
        }
    }
    memset(key,0,sizeof(*key));
    if(!memory || !size || memory->resource_clock==UINT64_MAX ||
       ctr_memory_check_range(memory,address,size,permissions)!=CTR_MEM_OK)return false;
    uint64_t version=0,cursor=address,end=cursor+size;
    while(cursor<end)for(size_t i=0;i<memory->count;++i) {
        const ctr_mapping *map=&memory->maps[i];uint64_t stop=(uint64_t)map->base+map->size;
        if(cursor<map->base || cursor>=stop)continue;
        size_t count=(size_t)((stop<end?stop:end)-cursor);
        uint64_t current=resource_host_version(memory,
            (uintptr_t)map->storage+(size_t)(cursor-map->base),count);
        if(current>version)version=current;
        cursor+=count;break;
    }
    *key=(ctr_memory_resource_key){memory,address,size,permissions,memory->resource_mapping_version,version};
    return true;
}
bool ctr_memory_resource_key_current(const ctr_memory *memory,const ctr_memory_resource_key *key) {
    if(!memory || !key || (uintptr_t)key>UINTPTR_MAX-sizeof(*key) ||
       (uintptr_t)key%_Alignof(ctr_memory_resource_key) || key->owner!=memory)return false;
    ctr_memory_resource_key current;
    return ctr_memory_resource_key_capture(memory,key->address,key->size,key->permissions,&current) &&
        key->mapping_version==current.mapping_version && key->write_version==current.write_version;
}
ctr_mem_result ctr_memory_resource_changed(ctr_memory *memory,const void *storage,size_t size) {
    if(!memory)return CTR_MEM_OVERFLOW;
    if(!size)return CTR_MEM_OK;
    if(!storage || size>UINTPTR_MAX-(uintptr_t)storage)return CTR_MEM_OVERFLOW;
    resource_mark_host(memory,(uintptr_t)storage,size,resource_next(memory));
    uintptr_t first=(uintptr_t)storage,last=first+size;
    for(size_t i=0;i<memory->count;++i) {
        const ctr_mapping *map=&memory->maps[i];uintptr_t begin=(uintptr_t)map->storage;
        if((map->permissions&CTR_MEM_EXEC) && first<begin+map->size && begin<last) {
            ++memory->code_generation;break;
        }
    }
    return CTR_MEM_OK;
}
ctr_mem_result ctr_memory_backing_changed(ctr_memory *memory,const void *storage,size_t size) {
    ctr_mem_result result=ctr_memory_resource_changed(memory,storage,size);
    if(result==CTR_MEM_OK && size)++memory->generation;
    return result;
}
