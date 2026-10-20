#include "ctr_memory.h"

#include <string.h>

void ctr_memory_init(ctr_memory *memory) {
    memset(memory, 0, sizeof(*memory));
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
    return CTR_MEM_OK;
}

static ctr_mem_result locate(const ctr_memory *memory, uint32_t address, unsigned width,
                             unsigned permission, const ctr_mapping **found) {
    if (width != 1u && width != 2u && width != 4u) return CTR_MEM_ALIGNMENT;
    if ((uint64_t)address + width > UINT64_C(0x100000000)) return CTR_MEM_OVERFLOW;
    if ((address & (width - 1u)) != 0u) return CTR_MEM_ALIGNMENT;
    for (size_t i = 0; i < memory->count; ++i) {
        const ctr_mapping *map = &memory->maps[i];
        if (address >= map->base && (uint64_t)address + width <= (uint64_t)map->base + map->size) {
            if ((map->permissions & permission) != permission) return CTR_MEM_PERMISSION;
            *found = map;
            return CTR_MEM_OK;
        }
    }
    return CTR_MEM_UNMAPPED;
}

ctr_mem_result ctr_memory_read(const ctr_memory *memory, uint32_t address,
                              unsigned width, unsigned permission, uint32_t *value) {
    const ctr_mapping *map = NULL;
    ctr_mem_result result = locate(memory, address, width, permission, &map);
    if (result != CTR_MEM_OK) return result;
    size_t offset = (size_t)(address - map->base);
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
    for (unsigned i = 0; i < width; ++i) map->storage[offset + i] = (uint8_t)(value >> (i * 8u));
    ++memory->generation;
    uintptr_t changed = (uintptr_t)map->storage + offset;
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
    static const char *const names[] = {"ok", "unmapped", "permission", "alignment", "overflow", "overlap", "capacity"};
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
    ctr_mem_result result=ctr_memory_check_range(memory,address,size,CTR_MEM_READ);
    if(result!=CTR_MEM_OK)return result;
    if(!size)return CTR_MEM_OK;
    if(!output)return CTR_MEM_OVERFLOW;
    if(host_alias(memory,output,size))return CTR_MEM_OVERLAP;
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
    for(size_t i=0;i<count;++i)if(ranges[i].size)
        copy_bytes(memory,ranges[i].address,(void*)ranges[i].input,ranges[i].size,true);
    ++memory->generation;
    if(code)++memory->code_generation;
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
        memset(memory->maps,0,sizeof(memory->maps));
        memcpy(memory->maps,next,count*sizeof(next[0]));memory->count=count;
        ++memory->generation;
        if (code_changed) ++memory->code_generation;
    }
    return CTR_MEM_OK;
}
