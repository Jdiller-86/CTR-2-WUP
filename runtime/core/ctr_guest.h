#ifndef CTR_GUEST_H
#define CTR_GUEST_H
#include "ctr_cpu.h"

#define CTR_GUEST_PAGE 4096u
#define CTR_GUEST_MAX_BYTES (64u*1024u*1024u)
#define CTR_GUEST_STORAGE_COUNT 7u
#define CTR_GUEST_KERNEL_VERSION UINT32_C(0x023a0000)
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
typedef enum { CTR_HANDLE_EVENT, CTR_HANDLE_SESSION, CTR_HANDLE_SHARED,
               CTR_HANDLE_MUTEX, CTR_HANDLE_SEMAPHORE, CTR_HANDLE_THREAD, CTR_HANDLE_ARBITER,
               CTR_HANDLE_RESOURCE_LIMIT } ctr_guest_handle_type;
typedef enum { CTR_SERVICE_SRV, CTR_SERVICE_CFG, CTR_SERVICE_HID, CTR_SERVICE_GSP,
               CTR_SERVICE_FS, CTR_SERVICE_FILE, CTR_SERVICE_APT } ctr_guest_service;
typedef struct { uint32_t generation; unsigned object; bool active; ctr_guest_handle_type type; } ctr_guest_handle;
typedef struct { bool active, registered; unsigned references; ctr_guest_service service;
                 unsigned fs_volume, fs_file, fs_flags; bool fs_closed,fs_romfs; } ctr_guest_session;
enum { CTR_GUEST_MODEL_3DS=0, CTR_GUEST_MODEL_3DSXL=1, CTR_GUEST_MODEL_2DS=3 };
typedef struct { uint8_t region, language, model; } ctr_guest_configuration;
#define CTR_GUEST_MAX_ALLOCATIONS 16u
typedef struct { uint32_t base, size; uint8_t *storage; bool linear; unsigned pins; } ctr_guest_allocation;
typedef struct { bool active; unsigned references, depth; uint32_t owner; } ctr_guest_mutex;
typedef struct { bool active; unsigned references; uint32_t count, maximum; } ctr_guest_semaphore;
typedef struct { bool active; unsigned references; } ctr_guest_arbiter;
typedef struct {
    bool active, owned; unsigned references, allocation;
    uint8_t *storage; uint32_t size, own_permissions, other_permissions;
    uint32_t mappings[4];
} ctr_guest_shared;
typedef struct {
    bool initialized, published;
    uint32_t handles[6], held;
    unsigned index;
    uint64_t last_ns, samples;
} ctr_guest_hid;
#include "ctr_pica.h"
typedef struct {
    bool right, black, active[2];
    uint32_t info[2][7];
    uint64_t swaps[2];
    bool registered;
    unsigned relay_owner, relay_shared, relay_event;
    uint64_t next_vblank_ns, vblanks, coalesced_vblanks, dropped_interrupts, failures;
    uint64_t gx_commands,gx_dma,gx_fills,gx_transfers,gx_copies,gx_cache,gx_pica;
    uint32_t last_gx_header,last_gx_detail;
    ctr_pica_state pica;
    struct ctr_scene_state *pica_scene;
    const struct ctr_scene_backend *pica_backend;
    uint64_t pica_draws,pica_triangles,pica_fragments,pica_shader_instructions;
    uint32_t last_scene_result,last_vertex_result,last_shader_result;
    uint32_t last_scene_backend_stage;
    uint32_t last_pica_result,last_pica_offset,last_pica_register;
} ctr_guest_gsp;
#define CTR_GUEST_MAX_THREADS 8u
#define CTR_GUEST_THREAD_QUANTUM 32u
#define CTR_GUEST_CURRENT_THREAD UINT32_C(0xffff8000)
typedef struct {
    bool active, finished;
    unsigned references, stack_allocation;
    uint32_t id, r[16], cpsr, vfp[32], fpscr, tls_address;
    int priority;
    ctr_guest_state state;
    uint64_t deadline_ns;
    bool infinite_wait, wait_all, wait_multiple;
    ctr_guest_handle_type wait_types[16];
    unsigned wait_objects[16], wait_count, wait_object;
    uint32_t wait_address;
} ctr_guest_thread;
typedef struct ctr_guest {
    ctr_memory memory;
    ctr_cpu cpu;
    ctr_program program;
    uint8_t *storage[CTR_GUEST_STORAGE_COUNT];
    size_t storage_size[CTR_GUEST_STORAGE_COUNT];
    ctr_guest_state state;
    uint64_t now_ns, deadline_ns, svc_count;
    unsigned wait_object;
    bool infinite_wait;
    uint32_t fault_detail;
    ctr_cpu_result fault;
    uint32_t tls_address;
    ctr_guest_event events[32];
    ctr_guest_handle handles[64];
    ctr_guest_session sessions[16];
    /* Platform-owned virtual archives; never maps a guest path to a host path. */
    struct ctr_fs_volume *fs_volumes[2];
    struct ctr_fs_romfs *fs_romfs;
    struct { uint32_t generation; unsigned volume, owner; bool active; } fs_archives[8];
    uint32_t fs_generation;
    struct { bool available, lock, initialized, enabled, pending, notified, closing;
             unsigned mutex, events[2]; uint32_t transitions; } apt;
    ctr_guest_mutex mutexes[16];
    ctr_guest_semaphore semaphores[16];
    ctr_guest_arbiter arbiters[8];
    ctr_guest_shared shared[8];
    ctr_guest_hid hid;
    ctr_guest_gsp gsp;
    /* Guest-owned CPU snapshots/decodes; separate from copied PICA state and
     * native GPU lifetime. Cleared before the memory owner is closed/reused. */
    struct ctr_resource_cache *pica_resources;
    struct ctr_texture_cache *pica_textures;
    ctr_guest_thread threads[CTR_GUEST_MAX_THREADS];
    unsigned current_thread, quantum_left;
    uint32_t next_thread_id;
    uint64_t context_switches;
    bool threads_enabled, process_exited;
    unsigned resource_references;
    ctr_guest_handle_type wait_types[16];
    unsigned wait_objects[16], wait_count;
    uint32_t wait_address;
    bool wait_all, wait_multiple;
    ctr_guest_configuration configuration;
    ctr_guest_allocation allocations[CTR_GUEST_MAX_ALLOCATIONS];
    uint64_t memory_requests, memory_failures;
    uint32_t memory_operation, memory_result, memory_address;
    uint64_t ipc_requests, ipc_failures;
    uint32_t ipc_header, ipc_result;
    char ipc_service[9];
    char ipc_target[9];
    void (*debug)(void *context, const char *message);
    void *debug_context;
    /* Optional checked static-code engine; the scheduler retains SVC ownership. */
    ctr_cpu_result (*executor)(ctr_cpu *cpu,void *context,uint64_t budget);
    void *executor_context;
    void (*executor_close)(void *context);
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
/* Internal typed-session ownership shared with the checked IPC implementation. */
uint32_t ctr_guest_session_open(ctr_guest *guest, ctr_guest_service service);
ctr_guest_session *ctr_guest_session_for(ctr_guest *guest, uint32_t handle);
/* Internal bounded object ownership; retain mappings/waits independently of handles. */
ctr_guest_handle *ctr_guest_handle_for(ctr_guest *guest, uint32_t handle);
uint32_t ctr_guest_handle_new(ctr_guest *guest, ctr_guest_handle_type type, unsigned object);
void ctr_guest_object_retain(ctr_guest *guest, ctr_guest_handle_type type, unsigned object);
void ctr_guest_object_release(ctr_guest *guest, ctr_guest_handle_type type, unsigned object);
uint32_t ctr_guest_event_create(ctr_guest *guest, unsigned reset);
bool ctr_guest_handle_close(ctr_guest *guest, uint32_t handle);
/* Call before guest execution/service access. Only Old-family models 0,1,3. */
bool ctr_guest_configure(ctr_guest *guest, ctr_guest_configuration configuration);
/* Optional synchronous PICA renderer, set before guest execution/service access.
 * The descriptor/context remain caller-owned and stable until replaced or close.
 * NULL selects the portable software renderer. close clears without freeing it. */
void ctr_guest_set_scene_backend(ctr_guest *guest,const struct ctr_scene_backend *backend);
const char *ctr_guest_state_name(ctr_guest_state state);
#endif
