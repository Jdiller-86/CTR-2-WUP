#ifndef CTR_WIIU_GX2_SCENE_H
#define CTR_WIIU_GX2_SCENE_H
#include "ctr_scene.h"
#include "ctr_gx2_shader.h"
#include "ctr_gx2_cache.h"
typedef struct GX2ContextState GX2ContextState;
typedef struct ctr_wiiu_gx2_texture_cache ctr_wiiu_gx2_texture_cache;
#define CTR_WIIU_GX2_TEXTURE_CACHE_SLOTS 6u
#define CTR_WIIU_GX2_TEXTURE_CACHE_MAX_IMAGE_BYTES (8u*1024u*1024u)
#define CTR_WIIU_GX2_TEXTURE_CACHE_MAX_BYTES (24u*1024u*1024u)
typedef struct {
    uint64_t lookups,hits,misses,evictions,bypasses,allocation_failures;
    uint64_t bytes,peak_bytes,pins;
    bool saturated;
} ctr_wiiu_gx2_texture_cache_statistics;

typedef enum {
    CTR_WIIU_GX2_SCENE_OK, CTR_WIIU_GX2_SCENE_ARGUMENT,
    CTR_WIIU_GX2_SCENE_ALLOCATION, CTR_WIIU_GX2_SCENE_LAYOUT,
    CTR_WIIU_GX2_SCENE_SHADER, CTR_WIIU_GX2_SCENE_WAIT,
    CTR_WIIU_GX2_SCENE_BUSY, CTR_WIIU_GX2_SCENE_UNSUPPORTED_READBACK
} ctr_wiiu_gx2_scene_result;

typedef struct {
    void *private_state;
    GX2ContextState *restore_context;
    ctr_wiiu_gx2_scene_result result;
    ctr_gx2_shader_result shader_result;
    uint64_t begins, draws, submissions, readbacks, shader_instructions;
    uint64_t waits, wait_failures, allocations, releases;
    uint64_t texture_uploads,texture_bytes;
    /* Immutable CPU records survive transactions and foreground release.
     * Counters are cumulative through explicit cache clears; bytes is current
     * residency. These allocations are separate from GPU allocations/releases. */
    ctr_gx2_cache *program_cache;
    ctr_gx2_cache_statistics cache_stats;
    uint64_t vertex_translations,pixel_translations;
    uint64_t vertex_translation_failures,pixel_translation_failures;
    uint64_t cache_allocation_failures;
    bool cache_disabled; /* Use the same checked compilers without lookup/store. */
    /* Select before the first transaction for Cemu-specific readback and
     * shader upload handling. */
    bool cemu_readback;
    /* Queue transformed draws in bounded, immutable GX2 upload slots.
     * Choose before opening a transaction. Use the scene as the exclusive GX2
     * owner during a batch. Resolve or abort before resuming presentation. */
    bool batch_transformed;
    uintptr_t cemu_last_vertex_program,cemu_last_pixel_program;
    /* Retired, GPU-read-only pitched RGBA images, separate from CPU records.
     * Exact decoded bytes and dimensions identify an image; sampler state is
     * rebound from the current descriptor. Pins count unit leases, including
     * duplicate bindings. Cumulative counters survive explicit clears. */
    ctr_wiiu_gx2_texture_cache *texture_cache;
    ctr_wiiu_gx2_texture_cache_statistics texture_cache_stats;
    bool texture_cache_disabled; /* Default false; use transaction-owned uploads. */
    bool quarantined;
    /* Optional borrowed CPU evidence observer. Called after a retired GPU copy
     * has populated readback, including before any later context-restore
     * failure. The surface is read-only and valid only during the callback;
     * the observer must not call GX2 or reenter this backend. */
    void (*observe_readback)(void *context,const ctr_scene_surface *readback);
    /* Same borrowed, read-only/no-GX2 contract; called after both color and
     * depth/stencil copies retire, before a possible context-restore failure. */
    void (*observe_depth_readback)(void *context,const ctr_scene_depth_surface *readback);
    void *readback_context;
    bool direct_hardware;
    bool direct_keep_depth;
    void *direct_pool;
    uint64_t direct_shader_hits,direct_shader_compiles,direct_depth_skips;
} ctr_wiiu_gx2_scene;

/* The caller supplies the native renderer's active WHB context. Synchronous
 * callbacks restore it before returning. Legacy callbacks receive original
 * shader inputs. The rounded callback receives clipped triangles.
 * Texture callbacks borrow checked decoded RGBA storage only for the call; the
 * backend owns its pitched GPU upload until all references have retired.
 * The native texture callbacks bind units 0..2 independently, including unit
 * 2's optional UV1 routing. Mip chains include each decoded level in one
 * pitched upload. Each image with its tail is bounded by 8 MiB and combined
 * storage by 24 MiB. Every enabled unit's texture and sampler bind on every draw.
 * Paired callbacks additionally own a D16 or packed D24S8 target and pitched
 * staging image. Depth/stencil bytes stay little-endian; HiZ is disabled and
 * canonical depth=-Z/W uses endpoint incoming depths in the legacy preflight.
 * The rounded callback admits independent D24S8 depth and stencil gates.
 * D24S8 controls bind explicitly for both faces, including inert disabled state.
 * TEV combiner-buffer programs initialize separate visible/pending byte state
 * per fragment and publish the delayed RGB/alpha updates after every stage.
 * Both target copies retire before readback; full pair parity remains the
 * scene executor's publication gate. Initialize only a fresh owner, or one
 * whose transaction and both caches have been cleared. */
void ctr_wiiu_gx2_scene_init(ctr_wiiu_gx2_scene *scene,GX2ContextState *restore_context);
ctr_scene_backend ctr_wiiu_gx2_scene_backend(ctr_wiiu_gx2_scene *scene);
ctr_scene_backend ctr_wiiu_gx2_scene_backend_direct(ctr_wiiu_gx2_scene *scene);
void ctr_wiiu_gx2_scene_set_depth_keep(void *context,bool keep);
/* Begin a paired target that stays on GX2 across draws and color resolves.
 * Only initialization reads the caller's depth bytes. Cemu accepts uniform
 * endpoint depth and stencil seeds through a GX2 clear. Subsequent draws
 * observe the GPU's depth/stencil state. A color resolve does not synchronize
 * any CPU depth/stencil copy. Use the paired resolve at guest visibility
 * boundaries that require both planes. Both resolves keep the targets owned
 * by this scene. End the owner with scene_abort. */
bool ctr_wiiu_gx2_scene_begin_resident(ctr_wiiu_gx2_scene *scene,const ctr_scene_surface *initial,
                                      const ctr_scene_depth_surface *depth);
bool ctr_wiiu_gx2_scene_resolve_color(ctr_wiiu_gx2_scene *scene,const ctr_scene_surface *readback);
bool ctr_wiiu_gx2_scene_resolve_targets(ctr_wiiu_gx2_scene *scene,const ctr_scene_surface *readback,
                                        const ctr_scene_depth_surface *depth);
/* Retire an active transaction before freeing every uploaded image. An idle
 * abort frees already-retired images without calling GX2, including after
 * foreground release. A failed wait preserves all ownership for retry. */
void ctr_wiiu_gx2_scene_abort(ctr_wiiu_gx2_scene *scene);
/* CPU-only: discard immutable program records without changing any GX2 owner,
 * pending reference or context. Historical counters and peak bytes remain. */
void ctr_wiiu_gx2_scene_cache_clear(ctr_wiiu_gx2_scene *scene);
/* Discard already-retired uploaded images without GX2 calls. Refuse an active
 * transaction (including quarantined storage), preserving all ownership. */
bool ctr_wiiu_gx2_scene_texture_cache_clear(ctr_wiiu_gx2_scene *scene);
/* Only after GX2/WHB graphics shutdown: release quarantined storage if a failed
 * retirement wait prevented reclamation, and clear both caches without GX2. */
void ctr_wiiu_gx2_scene_after_shutdown(ctr_wiiu_gx2_scene *scene);
const char *ctr_wiiu_gx2_scene_result_name(ctr_wiiu_gx2_scene_result result);
#endif
