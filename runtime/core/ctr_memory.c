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
    ++memory->code_generation;
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
