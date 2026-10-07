#include "ctr_cpu.h"
#include "ctr_fixture.h"

#include <string.h>

static bool fixture_memory(ctr_memory *memory, uint8_t *data) {
    ctr_memory_init(memory);
    return ctr_memory_map(memory, 0x1000u, (uint32_t)ctr_arm_fixture_code_size,
                          CTR_MEM_READ | CTR_MEM_EXEC, ctr_arm_fixture_code, ctr_arm_fixture_code_size) == CTR_MEM_OK &&
           ctr_memory_map(memory, 0x1100u, (uint32_t)ctr_thumb_fixture_code_size,
                          CTR_MEM_READ | CTR_MEM_EXEC, ctr_thumb_fixture_code, ctr_thumb_fixture_code_size) == CTR_MEM_OK &&
           ctr_memory_map(memory, 0x2000u, 16u, CTR_MEM_READ | CTR_MEM_WRITE, data, 16u) == CTR_MEM_OK;
}

int ctr_cpu_selftest(void) {
    ctr_memory interpreted_memory, translated_memory;
    uint8_t interpreted_data[16] = {0}, translated_data[16] = {0};
    if (!fixture_memory(&interpreted_memory, interpreted_data) || !fixture_memory(&translated_memory, translated_data)) return 1;
    ctr_cpu interpreted, translated;
    ctr_cpu_init(&interpreted, &interpreted_memory);
    interpreted.r[15] = 0x1000u; interpreted.r[3] = 0x2000u; interpreted.r[4] = 0x1101u;
    ctr_cpu_init(&translated, &translated_memory);
    memcpy(translated.r, interpreted.r, sizeof(translated.r));
    ctr_block blocks[32];
    size_t count = ctr_arm_fixture_block_count + ctr_thumb_fixture_block_count;
    if (count > sizeof(blocks) / sizeof(blocks[0])) return 2;
    memcpy(blocks, ctr_arm_fixture_blocks, ctr_arm_fixture_block_count * sizeof(blocks[0]));
    memcpy(blocks + ctr_arm_fixture_block_count, ctr_thumb_fixture_blocks, ctr_thumb_fixture_block_count * sizeof(blocks[0]));
    ctr_program program = {blocks, count, ctr_arm_fixture_hash, translated_memory.code_generation};
    if (ctr_cpu_run(&interpreted, 100u) != CTR_CPU_SVC) return 3;
    if (ctr_cpu_dispatch(&translated, &program, 100u, false) != CTR_CPU_SVC) return 4;
    if (memcmp(interpreted.r, translated.r, sizeof(interpreted.r)) != 0 || interpreted.cpsr != translated.cpsr ||
        interpreted.instructions != translated.instructions || memcmp(interpreted_data, translated_data, sizeof(interpreted_data)) != 0) return 5;
    if (translated.r[0] != 10u || translated.r[2] != 10u || translated.r[5] != 8u ||
        translated.r[15] != 0x1106u || translated.instructions != 40u || translated.fallback_instructions != 0u) return 6;
    uint32_t value;
    if (ctr_memory_read(&translated_memory, 0x2000u, 4u, CTR_MEM_READ, &value) != CTR_MEM_OK || value != 10u ||
        translated_data[0] != 10u || translated_data[1] != 0u) return 7;
    return 0;
}
