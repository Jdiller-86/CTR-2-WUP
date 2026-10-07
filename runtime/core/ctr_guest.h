#ifndef CTR_GUEST_H
#define CTR_GUEST_H
#include "ctr_cpu.h"

#define CTR_GUEST_PAGE 4096u
#define CTR_GUEST_MAX_BYTES (64u*1024u*1024u)
typedef struct { uint32_t address, pages, size; } ctr_guest_segment;
typedef struct {
    const uint8_t *code;
    size_t code_size;
    ctr_guest_segment text, rodata, data;
    uint32_t bss_size, stack_size;
    const char *module_hash;
} ctr_guest_image;
typedef enum { CTR_GUEST_READY, CTR_GUEST_YIELDED, CTR_GUEST_SLEEPING,
               CTR_GUEST_WAITING, CTR_GUEST_EXITED, CTR_GUEST_FAULTED } ctr_guest_state;
typedef struct { bool active, signaled; unsigned reset, references; } ctr_guest_event;
typedef struct { uint32_t generation; unsigned object; bool active; } ctr_guest_handle;
typedef struct {
    ctr_memory memory;
    ctr_cpu cpu;
    ctr_program program;
    uint8_t *storage[5];
    size_t storage_size[5];
    ctr_guest_state state;
    uint64_t now_ns, deadline_ns, svc_count;
    unsigned wait_object;
    bool infinite_wait;
    uint32_t fault_detail;
    ctr_cpu_result fault;
    uint32_t tls_address;
    ctr_guest_event events[32];
    ctr_guest_handle handles[64];
    void (*debug)(void *context, const char *message);
    void *debug_context;
} ctr_guest;

/* Call on a new/closed object. Loader failure frees allocations and leaves it
   faulted with no usable mappings. No ROM pointers become host addresses. */
bool ctr_guest_open(ctr_guest *guest, const ctr_guest_image *image,
                    const ctr_block *blocks, size_t block_count);
void ctr_guest_close(ctr_guest *guest);
ctr_guest_state ctr_guest_run(ctr_guest *guest, uint64_t instruction_budget,
                              uint64_t now_ns, bool translated);
ctr_guest_state ctr_guest_svc(ctr_guest *guest, uint32_t number);
bool ctr_guest_signal(ctr_guest *guest, uint32_t handle);
const char *ctr_guest_state_name(ctr_guest_state state);
#endif
